#include "rhat.h"
#include "tooling.h"
#include <ctype.h>
#include <errno.h>

void *mem(size_t n)
{
    void *p = compiler_hooks ? compiler_hooks->allocate(n) : calloc(1, n ? n : 1);
    if (!p) {
        fputs("out of memory\n", stderr);
        exit(1);
    }
    return p;
}

char *str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = mem(n);
    memcpy(p, s, n);
    return p;
}

char *fmt(const char *s, ...)
{
    va_list a, b;
    va_start(a, s);
    va_copy(b, a);
    int n = vsnprintf(NULL, 0, s, a);
    va_end(a);
    if (n < 0) {
        exit(1);
    }
    char *p = mem((size_t)n + 1);
    vsnprintf(p, (size_t)n + 1, s, b);
    va_end(b);
    return p;
}

void add(List *a, void *v)
{
    if (a->n == a->cap) {
        a->cap = a->cap ? a->cap * 2 : 8;
        void **p = compiler_hooks ? compiler_hooks->resize(a->v, (size_t)a->cap * sizeof(void *))
                                  : realloc(a->v, (size_t)a->cap * sizeof(void *));
        if (!p) {
            exit(1);
        }
        a->v = p;
    }
    a->v[a->n++] = v;
}

_Noreturn void fail(Loc l, const char *s, ...)
{
    if (compiler_hooks) {
        char message[4096];
        va_list args;
        va_start(args, s);
        vsnprintf(message, sizeof(message), s, args);
        va_end(args);
        compiler_hooks->error(l, message);
    }
    fprintf(stderr, "%s:%d:%d: error: ", l.file ? l.file : "rhat", l.line, l.col);
    va_list a;
    va_start(a, s);
    vfprintf(stderr, s, a);
    va_end(a);
    fputc('\n', stderr);
    exit(1);
}

typedef struct {
    char *text;
    Loc loc;
    int kind;
    size_t length;
} Token;

// The width of the UTF-8 encoded scalar value at s within n bytes, with
// its value in *code, or 0 when the bytes there encode none.
static size_t utf8_scalar(const unsigned char *s, size_t n, unsigned long *code)
{
    size_t width = !n ? 0 : s[0] < 0x80 ? 1 : s[0] >= 0xF0 ? 4 : s[0] >= 0xE0 ? 3 : 2;
    if (!width || width > n || (s[0] >= 0x80 && (s[0] < 0xC2 || s[0] > 0xF4))) {
        return 0;
    }
    unsigned long c = width == 1 ? s[0] : s[0] & (0x7Fu >> width);
    for (size_t j = 1; j < width; j++) {
        if ((s[j] & 0xC0) != 0x80) {
            return 0;
        }
        c = c << 6 | (s[j] & 0x3F);
    }
    unsigned long least = width == 2 ? 0x80 : width == 3 ? 0x800 : width == 4 ? 0x10000 : 0;
    *code = c;
    return c < least || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF) ? 0 : width;
}

enum {
    TOK_WORD = 256,
    TOK_INT,
    TOK_FLOAT,
    TOK_STRING,
    TOK_QUOTED,
    TOK_SCOPE,
    TOK_INTERP,     // $" opening an interpolated string.
    TOK_HOLE,       // { opening one of its holes.
    TOK_FORMAT,     // The raw format after a hole's first top-level :.
    TOK_HOLE_END,   // } closing a hole.
    TOK_INTERP_END, // " closing the string.
    TOK_EOF
};

typedef struct {
    Program *p;
    Module *module;
    Function *enclosing;
    List tokens;
    int at, serial, scope_depth;
    bool header;
    bool text_cast; // A char^(...) or string^(...) cast needs std.text for its error.
} Parser;

static Token *tok(Parser *p)
{
    return p->tokens.v[p->at];
}

static bool is(Parser *p, const char *s)
{
    return tok(p)->kind != TOK_STRING && tok(p)->kind != TOK_QUOTED && !strcmp(tok(p)->text, s);
}

static bool take(Parser *p, const char *s)
{
    if (is(p, s)) {
        p->at++;
        return true;
    }
    return false;
}

static Token *next(Parser *p)
{
    Token *t = tok(p);
    if (t->kind != TOK_EOF) {
        p->at++;
    }
    return t;
}

static void need(Parser *p, const char *s)
{
    if (!take(p, s)) {
        fail(tok(p)->loc, "expected '%s', found '%s'", s, tok(p)->text);
    }
}

static char *word(Parser *p)
{
    Token *t = next(p);
    if (t->kind != TOK_WORD && t->kind != TOK_QUOTED) {
        fail(t->loc, "expected a name");
    }
    return t->text;
}

static char *qualified_word(Parser *p)
{
    char *name = word(p);
    while (take(p, ".")) {
        name = fmt("%s.%s", name, word(p));
    }
    return name;
}

static char *expression_path(Expr *e)
{
    if (e->kind == E_NAME) {
        return e->text;
    }
    if (e->kind == E_MEMBER) {
        char *base = expression_path(e->a);
        if (base) {
            return fmt("%s.%s", base, e->text);
        }
    }
    return NULL;
}

static bool type_constructor(Parser *p, Expr *e)
{
    char *name = expression_path(e);
    if (!name) {
        return false;
    }
    for (int j = 0; j + 1 < p->tokens.n; j++) {
        Token *a = p->tokens.v[j], *b = p->tokens.v[j + 1];
        if (!strcmp(a->text, "def^") && !strcmp(b->text, name)) {
            return true;
        }
        size_t n = strlen(b->text);
        if (!strcmp(a->text, "errordef^") && !strncmp(name, b->text, n) && name[n] == '.') {
            return true;
        }
    }
    for (int j = 0; j < p->module->imports.n; j++) {
        Import *i = p->module->imports.v[j];
        const char *prefix = i->alias ? i->alias : i->name;
        size_t n = strlen(prefix);
        if (!strncmp(name, prefix, n) && name[n] == '.') {
            return true;
        }
    }
    return false;
}

static char *decl_word(Parser *p)
{
    Loc loc = tok(p)->loc;
    char *name = word(p);
    for (int k = T_BOOL; k < T_STRUCT; k++) {
        if (!strcmp(name, builtin_name(k))) {
            fail(loc, "built-in type name '%s' cannot be redeclared", name);
        }
    }
    if (strchr(name, '^')) {
        fail(loc, "hat identifiers cannot be declared by users");
    }
    return name;
}

static char *slice(const char *s, size_t n)
{
    char *r = mem(n + 1);
    memcpy(r, s, n);
    return r;
}

static int digit_value(unsigned char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static const char *digits(const char *s, int base, Loc loc)
{
    bool previous = false, any = false;
    while (*s) {
        int d = digit_value((unsigned char)*s);
        if (d >= 0 && d < base) {
            previous = any = true;
            s++;
        }
        else if (*s == '_') {
            int next = digit_value((unsigned char)s[1]);
            if (!previous || next < 0 || next >= base) {
                fail(loc, "numeric separator must be between digits");
            }
            previous = false;
            s++;
        }
        else {
            break;
        }
    }
    if (!any) {
        fail(loc, "expected a digit in numeric literal");
    }
    return s;
}

static char *decimal_base(const char *start, const char *end, int base)
{
    size_t n = (size_t)(end - start);
    char *out = mem(n * 2 + 2);
    out[0] = '0';
    size_t count = 1;
    for (const char *p = start; p < end; p++) {
        if (*p == '_') {
            continue;
        }
        int carry = digit_value((unsigned char)*p);
        for (size_t j = 0; j < count; j++) {
            int v = (out[j] - '0') * base + carry;
            out[j] = (char)('0' + v % 10);
            carry = v / 10;
        }
        while (carry) {
            out[count++] = (char)('0' + carry % 10);
            carry /= 10;
        }
    }
    while (count > 1 && out[count - 1] == '0') {
        count--;
    }
    for (size_t j = 0; j < count / 2; j++) {
        char c = out[j];
        out[j] = out[count - 1 - j];
        out[count - 1 - j] = c;
    }
    return out;
}

static void unicode_escape(char *out, size_t *n, unsigned value, Loc loc)
{
    if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
        fail(loc, "invalid Unicode scalar value");
    }
    if (value < 0x80) {
        out[(*n)++] = (char)value;
    }
    else if (value < 0x800) {
        out[(*n)++] = (char)(0xc0 | (value >> 6));
        out[(*n)++] = (char)(0x80 | (value & 63));
    }
    else if (value < 0x10000) {
        out[(*n)++] = (char)(0xe0 | (value >> 12));
        out[(*n)++] = (char)(0x80 | ((value >> 6) & 63));
        out[(*n)++] = (char)(0x80 | (value & 63));
    }
    else {
        out[(*n)++] = (char)(0xf0 | (value >> 18));
        out[(*n)++] = (char)(0x80 | ((value >> 12) & 63));
        out[(*n)++] = (char)(0x80 | ((value >> 6) & 63));
        out[(*n)++] = (char)(0x80 | (value & 63));
    }
}

// Decode the escape after a backslash at s into out, answering where the
// string goes on.
static const char *string_escape(const char *s, char *out, size_t *n, Loc loc)
{
    char c = *s++;
    switch (c) {
    case 'n':
        out[(*n)++] = '\n';
        break;
    case 'r':
        out[(*n)++] = '\r';
        break;
    case 't':
        out[(*n)++] = '\t';
        break;
    case '0':
        out[(*n)++] = 0;
        break;
    case '\\':
    case '"':
        out[(*n)++] = c;
        break;
    case '\n':
        break;
    case '\r':
        if (*s == '\n') {
            s++;
        }
        break;
    case 'x': {
        int a = digit_value((unsigned char)s[0]);
        if (a < 0 || !s[0]) {
            fail(loc, "hex escape requires two digits");
        }
        int b = digit_value((unsigned char)s[1]);
        if (b < 0) {
            fail(loc, "hex escape requires two digits");
        }
        if (a > 7) {
            fail(loc, "\\x escapes stop at \\x7F to keep strings UTF-8; write \\u{...}");
        }
        out[(*n)++] = (char)((a << 4) | b);
        s += 2;
        break;
    }
    case 'u': {
        if (*s++ != '{') {
            fail(loc, "Unicode escape requires braces");
        }
        unsigned cp = 0;
        int count = 0;
        while (*s && *s != '}') {
            int d = digit_value((unsigned char)*s++);
            if (d < 0 || ++count > 6) {
                fail(loc, "invalid Unicode escape");
            }
            cp = cp * 16 + (unsigned)d;
        }
        if (*s != '}' || !count) {
            fail(loc, "invalid Unicode escape");
        }
        s++;
        unicode_escape(out, n, cp, loc);
        break;
    }
    default:
        fail(loc, "unsupported string escape");
    }
    return s;
}

// Fail unless the n bytes at out are UTF-8.
static void utf8_text(const char *out, size_t n, Loc loc)
{
    unsigned long code;
    for (size_t j = 0, w; j < n; j += w) {
        if (!(w = utf8_scalar((const unsigned char *)out + j, n - j, &code))) {
            fail(loc, "string literal is not valid UTF-8");
        }
    }
}

// The text of an interpolated string up to its next hole or its end, with
// {{ and }} for braces.
static const char *interp_text(const char *s, Token *t)
{
    char *out = mem(strlen(s) + 1);
    size_t n = 0;
    while (*s && *s != '"' && !(*s == '{' && s[1] != '{')) {
        if ((*s == '{' || *s == '}') && s[1] == *s) {
            out[n++] = *s;
            s += 2;
        }
        else if (*s == '\\') {
            s = string_escape(s + 1, out, &n, t->loc);
        }
        else if (*s == '\r' && s[1] == '\n') {
            out[n++] = '\n';
            s += 2;
        }
        else {
            out[n++] = *s++;
        }
    }
    if (!*s) {
        fail(t->loc, "unterminated interpolated string");
    }
    utf8_text(out, n, t->loc);
    t->kind = TOK_STRING;
    t->text = out;
    t->length = n;
    return s;
}

static void lex(Parser *p)
{
    const char *s = p->p->source;
    int line = 1, col = 1;
    // Open interpolated strings, the bracket depth inside each one's hole,
    // and whether the innermost is in its text rather than a hole.
    int level = 0, nest[33] = {0};
    bool text = false;
    while (*s) {
        if (text || (level && !nest[level] && (*s == '}' || *s == ':'))) {
            Token *t = mem(sizeof(*t));
            t->loc = (Loc){p->p->path, line, col};
            const char *start = s;
            if (text && (*s == '"' || *s == '{')) {
                bool hole = *s++ == '{';
                t->kind = hole ? TOK_HOLE : TOK_INTERP_END;
                t->text = hole ? "\001hole" : "\001end";
                level -= !hole;
                nest[level] = hole ? 0 : nest[level];
                text = false;
            }
            else if (text) {
                s = interp_text(s, t);
            }
            else if (*s == '}') {
                s++;
                t->kind = TOK_HOLE_END;
                t->text = "\001hole_end";
                text = true;
            }
            else {
                // A format is raw text up to the hole's }.
                const char *format = ++s;
                while (*s && *s != '}' && *s != '\n') {
                    s++;
                }
                if (*s != '}') {
                    fail(t->loc, "unterminated format in an interpolated string");
                }
                t->kind = TOK_FORMAT;
                t->text = slice(format, (size_t)(s - format));
            }
            for (; start < s; start++) {
                line += *start == '\n';
                col = *start == '\n' ? 1 : col + 1;
            }
            add(&p->tokens, t);
            continue;
        }
        if (isspace((unsigned char)*s)) {
            if (*s++ == '\n') {
                line++;
                col = 1;
            }
            else {
                col++;
            }
            continue;
        }
        if (s[0] == '#' && s[1] == '[') {
            s += 2;
            col += 2;
            int depth = 1;
            while (*s && depth) {
                if (s[0] == '#' && s[1] == '[') {
                    depth++;
                    s += 2;
                    col += 2;
                }
                else if (s[0] == ']' && s[1] == '#') {
                    depth--;
                    s += 2;
                    col += 2;
                }
                else if (*s++ == '\n') {
                    line++;
                    col = 1;
                }
                else {
                    col++;
                }
            }
            if (depth) {
                fail((Loc){p->p->path, line, col}, "unterminated comment");
            }
            continue;
        }
        if (*s == '#') {
            while (*s && *s != '\n') {
                s++;
                col++;
            }
            continue;
        }
        Token *t = mem(sizeof(*t));
        t->loc = (Loc){p->p->path, line, col};
        const char *start = s;
        const char *operators[] = {"≤", "≦", "≥", "≧", "≠"};
        int unicode = -1;
        for (int j = 0; j < 5; j++) {
            if (!strncmp(s, operators[j], strlen(operators[j]))) {
                unicode = j;
            }
        }
        if (unicode >= 0) {
            s += strlen(operators[unicode]);
            t->text = unicode < 2 ? "<=" : unicode < 4 ? ">=" : "!=";
            t->kind = '=';
        }
        else if (s[0] == '$' && s[1] == '"') {
            s += 2;
            t->kind = TOK_INTERP;
            t->text = "\001interp";
            if (++level > 32) {
                fail(t->loc, "interpolated strings nest deeper than 32");
            }
            text = true;
        }
        else if (s[0] == '$' && s[1] == '\'') {
            fail(t->loc, "interpolation is written $\"...\"; $'...' does not interpolate");
        }
        else if (*s == '$') {
            s++;
            if (*s == '^') {
                while (*s == '^') {
                    s++;
                }
            }
            else {
                while (*s == '$') {
                    s++;
                }
            }
            if (!(isalpha((unsigned char)*s) || *s == '_' || *s == '`' ||
                  (unsigned char)*s >= 128)) {
                fail(t->loc, "scope prefix must be adjacent to an identifier");
            }
            t->kind = TOK_SCOPE;
        }
        else if (*s == '`') {
            s++;
            char *out = mem(strlen(s) + 1);
            size_t n = 0;
            bool closed = false;
            while (*s) {
                if (*s == '\n' || *s == '\r') {
                    fail(t->loc, "newline in quoted identifier");
                }
                if (*s == '`') {
                    s++;
                    if (*s != '`') {
                        closed = true;
                        break;
                    }
                }
                if ((unsigned char)*s < 32 || (unsigned char)*s == 127) {
                    fail(t->loc, "control character in quoted identifier");
                }
                out[n++] = *s++;
            }
            if (!closed || !n) {
                fail(t->loc, "empty or unterminated quoted identifier");
            }
            t->text = out;
            t->kind = TOK_QUOTED;
        }
        else if (isalpha((unsigned char)*s) || *s == '_' || (unsigned char)*s >= 128) {
            while (isalnum((unsigned char)*s) || *s == '_' || (unsigned char)*s >= 128) {
                bool symbol = false;
                for (int j = 0; j < 5; j++) {
                    if (!strncmp(s, operators[j], strlen(operators[j]))) {
                        symbol = true;
                    }
                }
                if (symbol) {
                    break;
                }
                s++;
            }
            while (*s == '^') {
                s++;
            }
            t->kind = TOK_WORD;
        }
        else if (isdigit((unsigned char)*s)) {
            t->kind = TOK_INT;
            if (s[0] == '0' && strchr("xXoObB", s[1]) && s[1]) {
                int base = s[1] == 'x' || s[1] == 'X' ? 16 : s[1] == 'o' || s[1] == 'O' ? 8 : 2;
                s += 2;
                const char *begin = s;
                s = digits(s, base, t->loc);
                t->text = decimal_base(begin, s, base);
            }
            else {
                s = digits(s, 10, t->loc);
                if (*s == '.' && isdigit((unsigned char)s[1])) {
                    t->kind = TOK_FLOAT;
                    s = digits(s + 1, 10, t->loc);
                }
                if (*s == 'e' || *s == 'E') {
                    t->kind = TOK_FLOAT;
                    s++;
                    if (*s == '+' || *s == '-') {
                        s++;
                    }
                    s = digits(s, 10, t->loc);
                }
                t->text = mem((size_t)(s - start) + 1);
                size_t n = 0;
                for (const char *q = start; q < s; q++) {
                    if (*q != '_') {
                        t->text[n++] = *q;
                    }
                }
            }
            if (isalnum((unsigned char)*s) || *s == '_') {
                fail(t->loc, "invalid numeric literal");
            }
        }
        else if (*s == '"' || *s == '\'') {
            char quote = *s++;
            bool line_string = quote == '"' && s[0] == '"' && s[1] == '"';
            if (line_string) {
                s += 2;
            }
            char *out = mem(strlen(s) + 1);
            size_t n = 0;
            bool closed = line_string;
            while (*s && (!line_string || (*s != '\n' && *s != '\r'))) {
                if (!line_string && *s == quote) {
                    s++;
                    if (quote != '\'' || *s != '\'') {
                        closed = true;
                        break;
                    }
                    out[n++] = *s++;
                    continue;
                }
                if (*s == '\r' && s[1] == '\n') {
                    out[n++] = '\n';
                    s += 2;
                    continue;
                }
                if (!line_string && quote == '"' && *s == '\\') {
                    s = string_escape(s + 1, out, &n, t->loc);
                }
                else {
                    out[n++] = *s++;
                }
            }
            if (!closed) {
                fail(t->loc, "unterminated string");
            }
            utf8_text(out, n, t->loc);
            t->kind = TOK_STRING;
            t->text = out;
            t->length = n;
        }
        else if (*s == '?') {
            const char *ops[] = {"?:=", "?+=", "?-=", "?*=", "?/=", "?%=", "?.", "?[", "?(", "??"};
            t->kind = '?';
            s++;
            for (size_t j = 0; j < sizeof(ops) / sizeof(*ops); j++) {
                size_t n = strlen(ops[j]);
                if (!strncmp(start, ops[j], n)) {
                    s = start + n;
                    break;
                }
            }
        }
        else {
            t->kind = (unsigned char)*s++;
            if (!strncmp(start, "<=>", 3)) {
                s = start + 3;
            }
            else if ((start[0] == '*' && *s == '*') || (start[0] == '/' && *s == '/')) {
                s++;
            }
            else if ((strchr("+-*/%", start[0]) && *s == '=') || (start[0] == '=' && *s == '/') ||
                     (start[0] == ':' && (*s == '=' || *s == ':')) ||
                     (start[0] == '-' && *s == '>') || (strchr("=!<>", start[0]) && *s == '=') ||
                     (start[0] == '&' && *s == '&') || (start[0] == '|' && *s == '|') ||
                     (start[0] == '.' && *s == '.')) {
                s++;
            }
        }
        if (s - start == 2 && !strncmp(start, "..", 2) && *s == '.') {
            s++; // The variadic marker ... is matched before concatenation.
        }
        if (s - start == 2 && *s == '=' &&
            (!strncmp(start, "**", 2) || !strncmp(start, "//", 2) || !strncmp(start, "..", 2))) {
            s++;
        }
        if (!t->text) {
            t->text = slice(start, (size_t)(s - start));
        }
        if (t->kind != TOK_STRING && t->kind != TOK_QUOTED && !strcmp(t->text, "=/")) {
            t->text = "!=";
        }
        if (t->kind != TOK_STRING && t->kind != TOK_QUOTED &&
            (!strcmp(t->text, "==") || !strcmp(t->text, "||"))) {
            fail(t->loc, "use '=', 'and^', or 'or^'; C-style comparison/logical "
                         "operators are not supported");
        }
        for (const char *q = start; q < s; q++) {
            if (*q == '\n') {
                line++;
                col = 1;
            }
            else {
                col++;
            }
        }
        if (level && t->kind != TOK_STRING && t->kind != TOK_QUOTED && s - start <= 2) {
            // Brackets inside a hole keep its } and : from closing it.
            nest[level] += strchr("([{", s[-1])                     ? 1
                           : strchr(")]}", s[-1]) && s - start == 1 ? -1
                                                                    : 0;
        }
        add(&p->tokens, t);
    }
    if (level) {
        fail((Loc){p->p->path, line, col}, "unterminated interpolated string");
    }
    Token *end = mem(sizeof(*end));
    end->kind = TOK_EOF;
    end->text = "<eof>";
    end->loc = (Loc){p->p->path, line, col};
    add(&p->tokens, end);
}

// Count the reference prefixes represented by a single hatted word.
static int reference_depth(const char *text)
{
    if (strncmp(text, "ref^", 4)) {
        return 0;
    }
    int end = 3;
    while (text[end] == '^') {
        end++;
    }
    return text[end] ? 0 : end - 3;
}

static Spec *spec(Parser *p);
static Spec *result_spec(Parser *p);

static Spec *spec_part(Parser *p)
{
    Spec *s = mem(sizeof(*s));
    s->loc = tok(p)->loc;
    int refs = reference_depth(tok(p)->text);
    if (refs) {
        next(p);
        s->kind = SPEC_REF;
        s->elem = spec_part(p);
        while (--refs) {
            Spec *outer = mem(sizeof(*outer));
            outer->kind = SPEC_REF;
            outer->loc = s->loc;
            outer->elem = s;
            s = outer;
        }
        return s;
    }
    if (take(p, "f^") || take(p, "p^")) {
        s->kind = SPEC_FUNCTION;
        s->procedure = !strcmp(((Token *)p->tokens.v[p->at - 1])->text, "p^");
        if (!is(p, "->") && !is(p, ";")) {
            do {
                add(&s->params, spec(p));
            } while (take(p, ","));
        }
        if (take(p, "->")) {
            s->elem = result_spec(p);
        }
        need(p, ";");
        return s;
    }
    if (take(p, "externC^")) {
        s = spec_part(p);
        if (s->kind != SPEC_FUNCTION) {
            fail(s->loc, "externC^ in a type needs a signature, as in externC^ f^ i32^ -> i32^;");
        }
        s->foreign = true;
        return s;
    }
    if (take(p, "dyn^")) {
        s->kind = SPEC_DYN;
        s->elem = spec_part(p);
        return s;
    }
    if (is(p, "grow^") || is(p, "flex^")) {
        // A flex^ array also returns storage when it shrinks; count marks it.
        const char *kind = next(p)->text;
        s = spec_part(p);
        if (s->kind != SPEC_ARRAY || s->count) {
            fail(s->loc, "%s needs an array type ending in [], as in %s T[]", kind, kind);
        }
        s->kind = SPEC_GROW;
        s->count = !strcmp(kind, "flex^");
        return s;
    }
    if (take(p, "(")) {
        s = spec(p);
        if (is(p, ",")) {
            fail(tok(p)->loc, "multiple results (A, B) are written only after ->");
        }
        need(p, ")");
    }
    else {
        s->kind = SPEC_NAME;
        s->name = qualified_word(p);
    }
    while (take(p, "[")) {
        Spec *w = mem(sizeof(*w));
        w->loc = s->loc;
        w->elem = s;
        if (is(p, "]")) {
            w->kind = SPEC_ARRAY; // Unsized until grow^ claims it.
        }
        else if (take(p, ":")) {
            w->kind = SPEC_SLICE;
        }
        else {
            Token *n = next(p);
            if (n->kind != TOK_INT) {
                fail(n->loc, "fixed array length must be an integer constant");
            }
            errno = 0;
            long k = strtol(n->text, NULL, 10);
            if (errno || k <= 0 || k > 1000000) {
                fail(n->loc, "array length out of range (1..1000000)");
            }
            w->kind = SPEC_ARRAY;
            w->count = (int)k;
        }
        need(p, "]");
        s = w;
    }
    return s;
}

static Spec *spec_intersection(Parser *p)
{
    Spec *s = spec_part(p);
    while (take(p, "&")) {
        Spec *both = mem(sizeof(*both));
        both->kind = SPEC_INTERSECTION;
        both->loc = s->loc;
        both->elem = s;
        both->other = spec_part(p);
        s = both;
    }
    return s;
}

// s and the alternatives after it.
static Spec *union_tail(Parser *p, Spec *s)
{
    while (take(p, "|")) {
        Spec *sum = mem(sizeof(*sum));
        sum->kind = SPEC_UNION;
        sum->loc = s->loc;
        sum->elem = s;
        sum->other = spec_intersection(p);
        s = sum;
    }
    return s;
}

static Spec *spec(Parser *p)
{
    return union_tail(p, spec_intersection(p));
}

// The result after ->, where (A, B) with a comma at its top is multiple
// results, which | E joins as a whole, and any other parenthesis groups a
// type.
static Spec *result_spec(Parser *p)
{
    int at = p->at;
    Loc loc = tok(p)->loc;
    if (take(p, "(")) {
        Spec *first = spec(p);
        if (take(p, ",")) {
            Spec *s = mem(sizeof(*s));
            s->kind = SPEC_TUPLE;
            s->loc = loc;
            add(&s->params, first);
            do {
                add(&s->params, spec(p));
            } while (take(p, ","));
            need(p, ")");
            return union_tail(p, s);
        }
        p->at = at;
    }
    return spec(p);
}

static Expr *expr(Parser *p, int min);
static Expr *operation(Parser *p, int min);
static bool catch_head(Parser *p);
static Function *function(Parser *p, char *name, Def *owner, bool abstr);

static Expr *node(int k, Loc l)
{
    Expr *e = mem(sizeof(*e));
    e->kind = k;
    e->loc = l;
    return e;
}

// return^ a, b: a record of the multiple result type, fields 0, 1, ...
static Expr *tuple(List *values, Loc loc)
{
    Expr *e = node(E_RECORD, loc);
    e->tuple = true;
    for (int j = 0; j < values->n; j++) {
        add(&e->names, fmt("%d", j));
        add(&e->args, values->v[j]);
    }
    return e;
}

// Bind the multiple results in the only value once, under a hidden name,
// and replace the value by its n parts.
static Stmt *results(Parser *p, List *values, int n, Loc loc)
{
    Stmt *source = mem(sizeof(*source));
    source->kind = S_BIND;
    source->loc = loc;
    source->name = fmt("\001results%d", ++p->serial);
    source->a = values->v[0];
    source->depth = n; // The number of names its results split into.
    values->n = 0;
    for (int j = 0; j < n; j++) {
        Expr *part = node(E_MEMBER, loc);
        part->a = node(E_NAME, loc);
        part->a->text = source->name;
        part->name_loc = loc;
        part->text = fmt("%d", j);
        add(values, part);
    }
    return source;
}

// Fields after { up to }, as name:T or name:T = default.
static void fields(Parser *p, Def *d)
{
    while (!take(p, "}")) {
        if (take(p, ",") || take(p, ";")) {
            continue;
        }
        Field *f = mem(sizeof(*f));
        f->loc = tok(p)->loc;
        f->owner = f->initializer_owner = d;
        while (is(p, "private^") || is(p, "abstract^")) {
            if (take(p, "private^")) {
                f->priv = true;
            }
            else {
                next(p);
                f->abstract = true;
            }
        }
        f->name = decl_word(p);
        need(p, ":");
        f->spec = spec(p);
        if (take(p, "=") || take(p, ":=")) {
            f->init = expr(p, 0);
        }
        if (f->abstract && f->init) {
            fail(f->loc, "abstract field cannot have an initializer");
        }
        add(&d->own_fields, f);
    }
}

static void def_body(Parser *p, Def *d);

// An error variant starts with its hidden site, the static record of where
// it is built: source, line, column and message. Its body, as a def^'s,
// adds fields and methods; message() gives the recorded message unless the
// variant says otherwise.
static void error_variant(Parser *p, Def *d)
{
    Field *site = mem(sizeof(*site));
    Spec *text = mem(sizeof(*text)), *where = mem(sizeof(*where)), *line = mem(sizeof(*line));
    text->kind = line->kind = SPEC_NAME;
    text->name = "string^";
    line->name = "u32^";
    where->kind = SPEC_TUPLE;
    add(&where->params, text);
    add(&where->params, line);
    add(&where->params, line);
    add(&where->params, text);
    site->loc = d->loc;
    site->name = "\001site";
    site->owner = site->initializer_owner = d;
    site->spec = mem(sizeof(Spec));
    site->spec->kind = SPEC_REF;
    site->spec->elem = where;
    add(&d->own_fields, site);
    if (is(p, "{")) {
        def_body(p, d);
    }
    for (int j = 1; j < d->own_fields.n; j++) {
        Field *f = d->own_fields.v[j];
        if (!strcmp(f->name, "source") || !strcmp(f->name, "line") || !strcmp(f->name, "column") ||
            !strcmp(f->name, "message")) {
            fail(f->loc, "'%s' is a member every error has; give the field another name", f->name);
        }
    }
    for (int j = 0; j < d->own_methods.n; j++) {
        if (!strcmp(((Function *)d->own_methods.v[j])->name, "message")) {
            return;
        }
    }
    Function *m = mem(sizeof(*m));
    m->loc = m->declaration = d->loc;
    m->lexical = p->scope_depth + 2;
    m->name = "message";
    m->owner = d;
    m->result = text;
    Param *self = mem(sizeof(*self));
    self->loc = d->loc;
    self->name = "self^";
    self->self = true;
    add(&m->params, self);
    Stmt *answer = mem(sizeof(*answer));
    answer->kind = S_RETURN;
    answer->loc = d->loc;
    answer->a = node(E_MEMBER, d->loc);
    answer->a->name_loc = d->loc;
    answer->a->text = "\001text";
    answer->a->a = node(E_SELF, d->loc);
    answer->a->a->text = "self^";
    add(&m->body, answer);
    add(&d->own_methods, m);
}

static void entries(Parser *p, Expr *e, const char *end, bool named)
{
    bool header = p->header;
    p->header = false;
    if (take(p, end)) {
        p->header = header;
        return;
    }
    do {
        if (named) {
            add(&e->names, word(p));
            need(p, "=");
        }
        add(&e->args, expr(p, 0));
        if (!strcmp(end, ")") && take(p, "...")) {
            ((Expr *)e->args.v[e->args.n - 1])->spread = true;
        }
        if (is(p, end)) {
            break;
        }
    } while (take(p, ","));
    need(p, end);
    p->header = header;
}

static bool branch_word(Parser *p)
{
    const char *words[] = {"elseif^", "elsif^", "elif^", "ei^", "else^", "el^"};
    for (size_t j = 0; j < sizeof(words) / sizeof(*words); j++) {
        if (is(p, words[j])) {
            return true;
        }
    }
    return false;
}

static bool branch(Parser *p)
{
    if (!branch_word(p)) {
        return false;
    }
    Token *word = next(p);
    bool default_allowed = !strcmp(word->text, "el^");
    if (!strcmp(word->text, "else^")) {
        default_allowed = !take(p, "if^");
        if (default_allowed && !is(p, ":")) {
            fail(tok(p)->loc, "else^ requires ':' or if^");
        }
    }
    if (!default_allowed && is(p, ":")) {
        fail(tok(p)->loc, "conditional branch requires a condition");
    }
    return true;
}

static Expr *if_tail(Parser *p, Expr *condition, Loc loc)
{
    Expr *e = node(E_IF, loc);
    e->a = condition;
    e->b = expr(p, 0);
    if (!branch(p)) {
        fail(tok(p)->loc, "if^ expression requires a final el^: branch");
    }
    if (take(p, ":")) {
        e->c = expr(p, 0);
        need(p, ";");
    }
    else {
        bool header = p->header;
        p->header = true;
        Expr *next_condition = expr(p, 0);
        p->header = header;
        need(p, ":");
        e->c = if_tail(p, next_condition, next_condition->loc);
    }
    return e;
}

static Expr *for_expression(Parser *p, Loc loc);
static Expr *named(char *name, Loc loc);

static Expr *primary(Parser *p)
{
    Token *t = next(p);
    Expr *e = NULL;
    p->text_cast |= t->kind == TOK_WORD &&
                    (!strcmp(t->text, "char^") || !strcmp(t->text, "string^")) && is(p, "(");
    // T.parse(s) for a number type T answers with std.text's errors.
    p->text_cast |= t->kind == TOK_WORD && numeric_kind(t->text) >= 0 && is(p, ".") &&
                    p->at + 1 < p->tokens.n &&
                    !strcmp(((Token *)p->tokens.v[p->at + 1])->text, "parse");
    if (t->kind == TOK_WORD && (!strcmp(t->text, "externC^") || !strcmp(t->text, "externLLVM^"))) {
        fail(t->loc, "%s declarations are only allowed at the top level", t->text);
    }
    if (t->kind != TOK_STRING && t->kind != TOK_QUOTED && !strcmp(t->text, "...")) {
        e = node(E_NAME, t->loc); // The collected variadic arguments.
        e->text = "...";
    }
    else if (t->kind == TOK_WORD && !strcmp(t->text, "print^")) {
        // print^ value, or print^(a, b) for several values.
        e = node(E_CALL, t->loc);
        e->a = named("print^", t->loc);
        if (take(p, "(")) {
            entries(p, e, ")", false);
        }
        else {
            add(&e->args, expr(p, 0));
        }
    }
    else if (t->kind == TOK_WORD && !strcmp(t->text, "char^") && tok(p)->kind == TOK_STRING) {
        // char^"x": exactly one Unicode scalar value, checked here.
        Token *text = next(p);
        unsigned long code = 0;
        if (!text->length ||
            utf8_scalar((const unsigned char *)text->text, text->length, &code) != text->length) {
            fail(text->loc, "char^\"...\" holds exactly one Unicode scalar value");
        }
        e = node(E_CHAR, t->loc);
        e->text = fmt("%lu", code);
    }
    else if (t->kind == TOK_INTERP) {
        // $"...{x}...{y:format}...": text and holes in order, each with its
        // format, "" for none.
        e = node(E_INTERP, t->loc);
        p->text_cast = true;
        bool header = p->header;
        p->header = false;
        while (tok(p)->kind != TOK_INTERP_END) {
            Token *part = next(p);
            if (part->kind == TOK_STRING) {
                Expr *x = node(E_STRING, part->loc);
                x->text = part->text;
                x->length = part->length;
                add(&e->args, x);
                add(&e->names, "");
                continue;
            }
            add(&e->args, expr(p, 0));
            add(&e->names, tok(p)->kind == TOK_FORMAT ? next(p)->text : "");
            if (next(p)->kind != TOK_HOLE_END) {
                fail(
                    p->at ? ((Token *)p->tokens.v[p->at - 1])->loc : t->loc,
                    "an interpolation hole holds one expression; put one with : at its top in ( )");
            }
        }
        next(p);
        p->header = header;
    }
    else if (t->kind == TOK_WORD && !strcmp(t->text, "panic^")) {
        e = node(E_PANIC, t->loc);
        e->a = expr(p, 0);
    }
    else if (t->kind == TOK_SCOPE) {
        e = node(E_NAME, t->loc);
        e->text = word(p);
        e->scope_kind = t->text[1] == '^' ? 2 : 1;
        e->scope_count = (int)strlen(t->text) - 1;
    }
    else if (t->kind == TOK_QUOTED) {
        e = node(E_NAME, t->loc);
        e->text = t->text;
    }
    else if (t->kind == TOK_INT || t->kind == TOK_FLOAT || t->kind == TOK_STRING) {
        e = node(t->kind == TOK_INT ? E_INT : t->kind == TOK_FLOAT ? E_FLOAT : E_STRING, t->loc);
        e->text = t->text;
        e->length = t->length;
    }
    else if (!strcmp(t->text, "true^") || !strcmp(t->text, "false^")) {
        e = node(E_BOOL, t->loc);
        e->text = t->text;
    }
    else if (!strcmp(t->text, "(")) {
        bool header = p->header;
        p->header = false;
        e = expr(p, 0);
        if (is(p, ",")) {
            // (a, b): multiple results in place of others, as after catch^ or ??.
            List values = {0};
            add(&values, e);
            while (take(p, ",")) {
                add(&values, expr(p, 0));
            }
            e = tuple(&values, t->loc);
        }
        p->header = header;
        need(p, ")");
    }
    else if (!strcmp(t->text, "[") || !strcmp(t->text, "grow^") || !strcmp(t->text, "flex^")) {
        e = node(E_ARRAY, t->loc);
        if (strcmp(t->text, "[")) {
            e->text = t->text;
        }
        if (strcmp(t->text, "[") && !take(p, "[")) {
            p->at--; // grow^ T[] or flex^ T[] names an empty array of that type.
            e->test_spec = spec_part(p);
        }
        else {
            entries(p, e, "]", false);
        }
    }
    else if (!strcmp(t->text, "{")) {
        e = node(E_RECORD, t->loc);
        entries(p, e, "}", true);
    }
    else if (reference_depth(t->text) || !strcmp(t->text, "-") || !strcmp(t->text, "!") ||
             !strcmp(t->text, "*") || !strcmp(t->text, "**") || !strcmp(t->text, "bitnot^") ||
             !strcmp(t->text, "try^")) {
        e = node(!strcmp(t->text, "try^") ? E_TRY : E_UNARY, t->loc);
        int refs = reference_depth(t->text);
        e->text = refs ? "ref^" : !strcmp(t->text, "**") ? "*" : t->text;
        e->a = expr(p, 9);
        if (!strcmp(t->text, "**")) {
            Expr *inner = node(E_UNARY, t->loc);
            inner->text = "*";
            inner->a = e->a;
            e->a = inner;
        }
        while (refs > 1) {
            Expr *outer = node(E_UNARY, t->loc);
            outer->text = "ref^";
            outer->a = e;
            e = outer;
            refs--;
        }
    }
    else if (!strcmp(t->text, "for^")) {
        e = for_expression(p, t->loc);
    }
    else if (!strcmp(t->text, "if^")) {
        bool header = p->header;
        p->header = true;
        Expr *condition = expr(p, 0);
        p->header = header;
        need(p, ":");
        e = if_tail(p, condition, t->loc);
    }
    else if (!strcmp(t->text, "f^") || !strcmp(t->text, "p^") || !strcmp(t->text, "template^")) {
        p->at--;
        e = node(E_FUNCTION, t->loc);
        e->function = function(p, fmt("<anonymous@%d:%d>", t->loc.line, t->loc.col), NULL, false);
    }
    else if (!strcmp(t->text, "error^")) {
        fail(t->loc, "error^ construction has been removed; write Type{...}");
    }
    else if (!strcmp(t->text, "return^")) {
        fail(t->loc, "expected an expression; return^ belongs in a catch^ clause body");
    }
    else if (t->kind == TOK_WORD) {
        e = node(!strcmp(t->text, "self^") ? E_SELF : E_NAME, t->loc);
        e->text = t->text;
        if (!strncmp(e->text, "it^^", 4)) {
            e->scope_kind = 3;
            e->scope_count = (int)strlen(e->text) - 3;
            e->text = "it^";
        }
    }
    else {
        fail(t->loc, "expected an expression, found '%s'", t->text);
    }
    for (;;) {
        if (take(p, ".") || take(p, "?.")) {
            Expr *n = node(E_MEMBER, e->loc);
            n->optional = !strcmp(((Token *)p->tokens.v[p->at - 1])->text, "?.");
            n->a = e;
            n->name_loc = tok(p)->loc;
            n->text = word(p);
            // tostring() and write_text(out) answer with std.text.Builder.
            p->text_cast |= !strcmp(n->text, "tostring") || !strcmp(n->text, "write_text");
            e = n;
        }
        else if (is(p, "(") && ((Token *)p->tokens.v[p->at - 1])->loc.line == tok(p)->loc.line) {
            next(p);
            Expr *n = node(E_CALL, e->loc);
            n->a = e;
            entries(p, n, ")", false);
            e = n;
        }
        else if (take(p, "[") || take(p, "?[")) {
            Expr *n = node(E_INDEX, e->loc);
            n->optional = !strcmp(((Token *)p->tokens.v[p->at - 1])->text, "?[");
            n->a = e;
            bool header = p->header;
            p->header = false;
            n->b = expr(p, 0);
            p->header = header;
            if (n->b->kind == E_RANGE) {
                // A range written in place slices with its bounds.
                n->kind = E_SLICE;
                n->text = n->b->text;
                n->c = n->b->b;
                n->b = n->b->a;
            }
            need(p, "]");
            e = n;
        }
        else if (take(p, "?")) {
            Expr *n = node(E_PRESENT, e->loc);
            n->a = e;
            e = n;
        }
        else if (is(p, "?(")) {
            fail(tok(p)->loc, "optional callable values are not supported; use an "
                              "optional receiver?.method(...)");
        }
        else if (!p->header && type_constructor(p, e) && take(p, "{")) {
            Expr *n = node(E_RECORD, e->loc);
            n->text = expression_path(e);
            entries(p, n, "}", true);
            e = n;
        }
        else {
            break;
        }
    }
    return e;
}

static int prec(const char *s)
{
    if (!strcmp(s, "??") || !strcmp(s, "catch^")) {
        return 8;
    }
    if (!strcmp(s, "or^")) {
        return 1;
    }
    if (!strcmp(s, "and^")) {
        return 2;
    }
    if (!strcmp(s, "=") || !strcmp(s, "==") || !strcmp(s, "!=") || !strcmp(s, "<") ||
        !strcmp(s, ">") || !strcmp(s, "<=") || !strcmp(s, ">=") || !strcmp(s, "fits^")) {
        return 3;
    }
    if (!strcmp(s, "<=>")) {
        return 4;
    }
    if (!strcmp(s, "..")) {
        return 5;
    }
    if (!strcmp(s, "+") || !strcmp(s, "-")) {
        return 6;
    }
    if (!strcmp(s, "**")) {
        return 10;
    }
    if (!strcmp(s, "*") || !strcmp(s, "/") || !strcmp(s, "%") || !strcmp(s, "//") ||
        !strcmp(s, "cross^") || !strcmp(s, "dot^") || !strcmp(s, "bitand^") ||
        !strcmp(s, "bitor^") || !strcmp(s, "bitxor^") || !strcmp(s, "bitshift^")) {
        return 7;
    }
    return -1;
}

static Expr *operation(Parser *p, int min)
{
    Expr *e = primary(p);
    for (;;) {
        Token *t = tok(p);
        int pr = t->kind == TOK_STRING || t->kind == TOK_QUOTED ? -1 : prec(t->text);
        if (pr < min) {
            break;
        }
        if (!p->header && catch_head(p)) {
            break;
        }
        next(p);
        if (pr == 3) {
            Expr *chain = node(E_CHAIN, t->loc);
            add(&chain->args, e);
            for (;;) {
                add(&chain->names, t->text);
                if (!strcmp(t->text, "fits^")) {
                    Expr *test = node(E_FITS, t->loc);
                    test->test_spec = spec(p);
                    add(&chain->args, test);
                }
                else {
                    add(&chain->args, expr(p, pr + 1));
                }
                t = tok(p);
                if (t->kind == TOK_STRING || t->kind == TOK_QUOTED || prec(t->text) != 3) {
                    break;
                }
                next(p);
            }
            if (chain->names.n == 1) {
                Expr *single = !strcmp((char *)chain->names.v[0], "fits^")
                                   ? chain->args.v[1]
                                   : node(E_BIN, chain->loc);
                single->text = chain->names.v[0];
                single->a = chain->args.v[0];
                if (single->kind != E_FITS) {
                    single->b = chain->args.v[1];
                }
                e = single;
            }
            else {
                e = chain;
            }
        }
        else {
            Expr *binary = node(!strcmp(t->text, "??")       ? E_COALESCE
                                : !strcmp(t->text, "catch^") ? E_CATCH
                                                             : E_BIN,
                                t->loc);
            binary->text = t->text;
            binary->a = e;
            binary->b = expr(p, pr + ((!strcmp(t->text, "..") || !strcmp(t->text, "**")) ? 0 : 1));
            e = binary;
        }
    }
    return e;
}

// Whether the token at offset k from the current one is ( [ ?[ , or ) ] ,.
static bool range_delimiter(Parser *p, int k, bool opening)
{
    if (p->at + k < 0 || p->at + k >= p->tokens.n) {
        return false;
    }
    const char *s = ((Token *)p->tokens.v[p->at + k])->text;
    return opening ? !strcmp(s, "(") || !strcmp(s, "[") || !strcmp(s, "?[") || !strcmp(s, ",")
                   : !strcmp(s, ")") || !strcmp(s, "]") || !strcmp(s, ",");
}

// An expression, at the top level also a range a :: b or a to^ b. A
// header's to^ belongs to its loop or arm instead. An omitted end must
// stand next to a delimiter, so that a range never runs on to the next
// line's expression.
static Expr *expr(Parser *p, int min)
{
    bool open_start = !min && !p->header && is(p, "::");
    if (open_start && !range_delimiter(p, -1, true)) {
        fail(tok(p)->loc, "a range without its start goes right after (, [ or ,; write (:: n)");
    }
    Expr *e = open_start ? NULL : operation(p, min);
    if (min || p->header || !(is(p, "::") || is(p, "to^"))) {
        return e;
    }
    Token *t = next(p);
    Expr *r = node(E_RANGE, e ? e->loc : t->loc);
    r->text = t->text;
    r->a = e;
    if (!range_delimiter(p, 0, false)) {
        if (tok(p)->loc.line != t->loc.line) {
            fail(t->loc, "a range without its end goes right before ), ] or ,; write (n ::)");
        }
        r->b = operation(p, 0);
    }
    if (!strcmp(r->text, "to^") && (!r->a || !r->b)) {
        fail(r->loc, "an inclusive to^ range needs both bounds");
    }
    return r;
}

static void block(Parser *p, List *out);
static Stmt *stmt(Parser *p);
static int loop_section(Parser *p);

// A clause head is syntactically a nominal type (possibly a union), then ':'.
static bool catch_type(Parser *p)
{
    if (reference_depth(tok(p)->text)) {
        next(p);
        return catch_type(p);
    }
    if (take(p, "(")) {
        if (!catch_type(p)) {
            return false;
        }
        while (take(p, "|")) {
            if (!catch_type(p)) {
                return false;
            }
        }
        return take(p, ")");
    }
    if (tok(p)->kind != TOK_WORD && tok(p)->kind != TOK_QUOTED) {
        return false;
    }
    next(p);
    while (take(p, ".")) {
        if (tok(p)->kind != TOK_WORD && tok(p)->kind != TOK_QUOTED) {
            return false;
        }
        next(p);
    }
    return true;
}

static bool catch_head(Parser *p)
{
    if (!is(p, "catch^")) {
        return false;
    }
    Parser q = *p;
    next(&q);
    if (take(&q, ":")) {
        return true;
    }
    if (!catch_type(&q)) {
        return false;
    }
    while (take(&q, "|")) {
        if (!catch_type(&q)) {
            return false;
        }
    }
    return is(&q, ":");
}

static void catches(Parser *p, List *body, bool loop)
{
    if (!catch_head(p)) {
        return;
    }
    Stmt *s = mem(sizeof(*s));
    s->kind = S_HANDLE;
    s->loc = tok(p)->loc;
    s->body = *body;
    s->loop_handler = loop;
    bool final = false;
    while (catch_head(p)) {
        if (final) {
            fail(tok(p)->loc, "catch^: must be the final catch arm");
        }
        CatchArm *arm = mem(sizeof(*arm));
        arm->loc = next(p)->loc;
        if (!is(p, ":")) {
            arm->spec = spec(p);
        }
        final = !arm->spec;
        need(p, ":");
        while (!is(p, "}") && !catch_head(p) && (!loop || loop_section(p) < 0)) {
            if (tok(p)->kind == TOK_EOF) {
                fail(tok(p)->loc, "unterminated catch^ arm");
            }
            if (!take(p, ";")) {
                add(&arm->body, stmt(p));
            }
        }
        add(&s->handlers, arm);
    }
    *body = (List){0};
    add(body, s);
}

static void clause_body(Parser *p, List *out)
{
    while (!is(p, "}") && !branch_word(p) && !catch_head(p)) {
        if (tok(p)->kind == TOK_EOF) {
            fail(tok(p)->loc, "unterminated if^");
        }
        if (take(p, ";")) {
            continue;
        }
        add(out, stmt(p));
    }
}

static Stmt *if_statement(Parser *p, Expr *condition, Loc loc)
{
    Stmt *s = mem(sizeof(*s));
    s->kind = S_IF;
    s->loc = loc;
    s->a = condition;
    clause_body(p, &s->body);
    if (branch(p)) {
        if (take(p, ":")) {
            clause_body(p, &s->other);
        }
        else {
            bool header = p->header;
            p->header = true;
            Expr *next_condition = expr(p, 0);
            p->header = header;
            need(p, ":");
            Stmt *nested = if_statement(p, next_condition, next_condition->loc);
            nested->internal = true;
            add(&s->other, nested);
        }
    }
    return s;
}

static const char *assignment_operator(Parser *p)
{
    const char *ops[] = {":=",  "+=",  "-=",  "*=",  "/=",  "%=",  "//=", "**=",
                         "..=", "?:=", "?+=", "?-=", "?*=", "?/=", "?%="};
    for (size_t j = 0; j < sizeof(ops) / sizeof(*ops); j++) {
        if (is(p, ops[j])) {
            return ops[j];
        }
    }
    return NULL;
}

static Expr *named(char *name, Loc loc)
{
    Expr *e = node(E_NAME, loc);
    e->text = name;
    return e;
}

static Expr *number(char *text, Loc loc)
{
    Expr *e = node(E_INT, loc);
    e->text = text;
    return e;
}

static Expr *binary(char *op, Expr *a, Expr *b)
{
    Expr *e = node(E_BIN, a->loc);
    e->text = op;
    e->a = a;
    e->b = b;
    return e;
}

static Expr *contextual(Expr *value, Expr *hint)
{
    Expr *e = node(E_EXPECT, value->loc);
    e->a = value;
    e->b = hint;
    return e;
}

static Stmt *binding(char *name, Expr *value, bool mut)
{
    Stmt *s = mem(sizeof(*s));
    s->kind = S_BIND;
    s->loc = value->loc;
    s->name = name;
    s->a = value;
    s->mut = mut;
    return s;
}

static Stmt *assignment(Expr *target, Expr *value, char *op)
{
    Stmt *s = mem(sizeof(*s));
    s->kind = S_ASSIGN;
    s->loc = target->loc;
    s->a = target;
    s->b = value;
    s->op = op;
    return s;
}

static Stmt *match_binding(char *name, Expr *value)
{
    Stmt *s = binding(name, value, false);
    s->flow_alias = true;
    return s;
}

static bool match_clause(Parser *p)
{
    return is(p, "when^") || is(p, "other^") || is(p, "el^") || is(p, "else^");
}

static Expr *match_condition(Parser *p, char *subject)
{
    Expr *condition = NULL;
    do {
        if (take(p, "fits^")) {
            Expr *test = node(E_FITS, tok(p)->loc);
            test->a = named("it^", test->loc);
            test->test_spec = spec(p);
            condition = condition ? binary("or^", condition, test) : test;
            continue;
        }
        // An arm's low to^ high is its own form, not a range^.
        Expr *low = operation(p, 0), *test = NULL;
        if (take(p, "to^")) {
            Expr *high = operation(p, 0);
            test = binary("and^", binary(">=", named(subject, low->loc), low),
                          binary("<=", named(subject, high->loc), high));
        }
        else {
            test = binary("=", named(subject, low->loc), low);
        }
        condition = condition ? binary("or^", condition, test) : test;
    } while (take(p, ","));
    need(p, ":");
    return condition;
}

static void match_body(Parser *p, List *out)
{
    while (!is(p, "}") && !match_clause(p)) {
        if (tok(p)->kind == TOK_EOF) {
            fail(tok(p)->loc, "unterminated value match");
        }
        if (!take(p, ";")) {
            add(out, stmt(p));
        }
    }
}

static Stmt *match_statement(Parser *p, Stmt *scope, Expr *subject)
{
    char *name = fmt("\001match%d", ++p->serial);
    add(&scope->body, match_binding(name, subject));
    add(&scope->body, match_binding("it^", named(name, subject->loc)));
    need(p, "{");
    if (!match_clause(p)) {
        fail(tok(p)->loc, "for^ requires a driving clause or when^/other^ arms");
    }
    p->scope_depth++;
    List *arm = &scope->body;
    while (take(p, "when^")) {
        Stmt *s = mem(sizeof(*s));
        s->kind = S_IF;
        s->loc = tok(p)->loc;
        s->internal = true;
        s->a = match_condition(p, name);
        match_body(p, &s->body);
        add(arm, s);
        arm = &s->other;
    }
    if (take(p, "other^") || take(p, "el^") || take(p, "else^")) {
        need(p, ":");
        match_body(p, arm);
    }
    need(p, "}");
    p->scope_depth--;
    return scope;
}

static Expr *match_expression(Parser *p, Expr *scope, Expr *subject)
{
    char *name = fmt("\001match%d", ++p->serial);
    add(&scope->bindings, match_binding(name, subject));
    add(&scope->bindings, match_binding("it^", named(name, subject->loc)));
    need(p, ":");
    Expr **arm = &scope->a, *first = NULL;
    while (take(p, "when^")) {
        Expr *e = node(E_IF, tok(p)->loc);
        e->a = match_condition(p, name);
        e->b = expr(p, 0);
        *arm = e;
        arm = &e->c;
        first = first ? first : e;
    }
    if (take(p, "other^") || take(p, "el^") || take(p, "else^")) {
        need(p, ":");
        *arm = expr(p, 0);
    }
    else if (first) {
        // The checker proves that the arms cover the subject, so this is never reached.
        Expr *miss = node(E_PANIC, tok(p)->loc);
        miss->a = node(E_STRING, miss->loc);
        miss->a->text = "match fell through";
        miss->a->length = strlen(miss->a->text);
        *arm = miss;
        first->match = named(name, subject->loc);
    }
    else {
        fail(tok(p)->loc, "value match expression requires other^: or arms covering the subject");
    }
    need(p, ";");
    return scope;
}

static Expr *for_expression(Parser *p, Loc loc)
{
    Expr *scope = node(E_SCOPE, loc);
    bool header = p->header;
    p->header = true;
    Expr *subject = NULL;
    if (is(p, "let^") || is(p, "var^")) {
        do {
            Stmt *b = stmt(p);
            if (b->kind != S_BIND) {
                fail(b->loc, "a focus introduces one binding");
            }
            add(&scope->bindings, b);
            subject = named(b->name, b->loc);
        } while (take(p, "for^"));
    }
    if (take(p, "if^")) {
        Expr *condition = expr(p, 0);
        need(p, ":");
        p->header = header;
        scope->a = if_tail(p, condition, loc);
        return scope;
    }
    if (take(p, "do^")) {
        need(p, ":");
        p->header = header;
        scope->a = expr(p, 0);
        need(p, ";");
        return scope;
    }
    if (!subject) {
        subject = expr(p, 0);
    }
    p->header = header;
    return match_expression(p, scope, subject);
}

static int loop_section(Parser *p)
{
    const char *names[] = {"prolog^", "pre^", "first^", "main^", "last^", "epilog^"};
    for (int j = 0; j < 6; j++) {
        if (is(p, names[j])) {
            return j;
        }
    }
    if (is(p, "prologue^")) {
        return 0;
    }
    if (is(p, "premain^")) {
        return 1;
    }
    if (is(p, "epilogue^")) {
        return 5;
    }
    return -1;
}

static void loop_body(Parser *p, Stmt *loop)
{
    need(p, "{");
    p->scope_depth++;
    List *parts[] = {&loop->prolog, &loop->pre,  &loop->first,
                     &loop->body,   &loop->last, &loop->epilog};
    int section = -1;
    bool explicit = false, body = false;
    while (!is(p, "}")) {
        if (tok(p)->kind == TOK_EOF) {
            fail(tok(p)->loc, "unterminated loop");
        }
        if (take(p, ";")) {
            continue;
        }
        int next_section = loop_section(p);
        if (catch_head(p)) {
            if (section >= 0 && section != 3) {
                fail(tok(p)->loc, "loop catch^ arms must follow main^");
            }
            section = 3;
            body = true;
            catches(p, &loop->body, true);
            continue;
        }
        if (next_section >= 0) {
            if (next_section <= section) {
                fail(tok(p)->loc, "loop sections must follow prolog^, pre^, first^, "
                                  "main^, last^, epilog^ order");
            }
            explicit = true;
            section = next_section;
            next(p);
            need(p, ":");
            body |= section >= 1 && section <= 4;
        }
        else {
            if (section < 0) {
                section = 3;
                body = true;
            }
            Stmt *s = stmt(p);
            if (section == 2) {
                if (s->kind == S_BIND && !strcmp(s->name, "_^")) {
                    s->name = fmt("\001first_discard%d", ++p->serial);
                }
                if (s->kind == S_BIND_MANY) {
                    for (int j = 0; j < s->body.n; j++) {
                        Stmt *b = s->body.v[j];
                        if (!strcmp(b->name, "_^")) {
                            b->name = fmt("\001first_discard%d", ++p->serial);
                        }
                    }
                }
            }
            add(parts[section], s);
        }
    }
    need(p, "}");
    p->scope_depth--;
    if (explicit && !body) {
        fail(loop->loc, "loop requires pre^, first^, main^, or last^");
    }
}

static Stmt *scoped_statement(Stmt *scope, Expr *value)
{
    Expr *e = node(E_SCOPE, scope->loc);
    e->bindings = scope->body;
    e->a = value;
    Stmt *s = mem(sizeof(*s));
    s->kind = S_EXPR;
    s->loc = scope->loc;
    s->a = e;
    return s;
}

static Stmt *matched_statement(Parser *p, Stmt *scope, Expr *subject)
{
    Expr *e = node(E_SCOPE, scope->loc);
    e->bindings = scope->body;
    match_expression(p, e, subject);
    Stmt *s = mem(sizeof(*s));
    s->kind = S_EXPR;
    s->loc = scope->loc;
    s->a = e;
    return s;
}

static Stmt *loop_statement(Parser *p, bool repeat, Loc loc)
{
    Stmt *scope = mem(sizeof(*scope));
    scope->kind = S_BLOCK;
    scope->loc = loc;
    Stmt *loop = mem(sizeof(*loop));
    loop->kind = S_WHILE;
    loop->loc = loc;
    bool header = p->header;
    p->header = true;
    char *counter = NULL, *limit = NULL;
    if (!repeat && (is(p, "let^") || is(p, "var^"))) {
        do {
            Stmt *focus = stmt(p);
            add(&scope->body, focus);
            if (focus->kind == S_BIND) {
                add(&loop->focuses, focus->name);
            }
        } while (take(p, "for^"));
        if (take(p, "if^")) {
            Expr *condition = expr(p, 0);
            p->header = header;
            if (take(p, ":")) {
                return scoped_statement(scope, if_tail(p, condition, loc));
            }
            need(p, "{");
            p->scope_depth++;
            Stmt *branch = if_statement(p, condition, loc);
            branch->internal = true;
            need(p, "}");
            p->scope_depth--;
            add(&scope->body, branch);
            return scope;
        }
        if (take(p, "do^")) {
            need(p, ":");
            p->header = header;
            Expr *value = expr(p, 0);
            need(p, ";");
            return scoped_statement(scope, value);
        }
        if (is(p, ":")) {
            Stmt *focus = scope->body.v[scope->body.n - 1];
            p->header = header;
            return matched_statement(p, scope, named(focus->name, loc));
        }
        if (is(p, "{")) {
            Stmt *focus = scope->body.v[scope->body.n - 1];
            p->header = header;
            return match_statement(p, scope, named(focus->name, loc));
        }
    }
    else if (!repeat && tok(p)->kind == TOK_WORD &&
             (p->at + 1 < p->tokens.n && !strcmp(((Token *)p->tokens.v[p->at + 1])->text, ":="))) {
        Stmt *focus = stmt(p);
        add(&scope->body, focus);
        add(&loop->focuses, focus->a->text);
    }
    else if (!is(p, "{") && !is(p, "while^") && !is(p, "until^")) {
        char *focus = "it^";
        Spec *annotation = NULL;
        Expr *start = NULL;
        if (!repeat && tok(p)->kind == TOK_WORD && p->at + 1 < p->tokens.n &&
            (!strcmp(((Token *)p->tokens.v[p->at + 1])->text, "from^") ||
             (!strcmp(((Token *)p->tokens.v[p->at + 1])->text, ":") && p->at + 2 < p->tokens.n &&
              strcmp(((Token *)p->tokens.v[p->at + 2])->text, "when^") &&
              strcmp(((Token *)p->tokens.v[p->at + 2])->text, "other^") &&
              strcmp(((Token *)p->tokens.v[p->at + 2])->text, "el^") &&
              strcmp(((Token *)p->tokens.v[p->at + 2])->text, "else^")))) {
            focus = decl_word(p);
            if (take(p, ":")) {
                annotation = spec(p);
            }
            need(p, "from^");
            start = expr(p, 0);
        }
        else {
            start = expr(p, 0);
        }
        if (!repeat && is(p, "{")) {
            p->header = header;
            return match_statement(p, scope, start);
        }
        if (!repeat && is(p, ":")) {
            p->header = header;
            return matched_statement(p, scope, start);
        }
        counter = fmt("\001counter%d", ++p->serial);
        limit = fmt("\001limit%d", p->serial);
        if (repeat) {
            add(&scope->body, binding(limit, start, false));
            ((Stmt *)scope->body.v[scope->body.n - 1])->integer = true;
            add(&scope->body,
                binding(counter, contextual(number("0", loc), named(limit, loc)), true));
            loop->a = binary("<", named(counter, loc), named(limit, loc));
            add(&loop->update, assignment(named(counter, loc),
                                          contextual(number("1", loc), named(counter, loc)), "+"));
        }
        else {
            bool down = take(p, "downto^");
            if (!down) {
                need(p, "to^");
            }
            Expr *end = expr(p, 0);
            Stmt *initial =
                binding(counter,
                        !annotation && (start->kind == E_INT || start->kind == E_FLOAT ||
                                        (start->kind == E_UNARY && !strcmp(start->text, "-") &&
                                         (start->a->kind == E_INT || start->a->kind == E_FLOAT)))
                            ? contextual(start, end)
                            : start,
                        true);
            initial->spec = annotation;
            initial->integer = true;
            add(&scope->body, initial);
            add(&scope->body, binding(limit, contextual(end, named(counter, loc)), false));
            Expr *step = take(p, "step^") ? expr(p, 0) : number("1", loc);
            char *stride = fmt("\001step%d", p->serial), *active = fmt("\001active%d", p->serial);
            add(&scope->body, binding(stride, contextual(step, named(counter, loc)), false));
            Expr *positive =
                binary(">", named(stride, loc), contextual(number("0", loc), named(counter, loc)));
            Expr *bounds = binary(down ? ">=" : "<=", named(counter, loc), named(limit, loc));
            if ((step->kind == E_INT && !strcmp(step->text, "0")) ||
                (step->kind == E_UNARY && !strcmp(step->text, "-") && step->a->kind == E_INT)) {
                fail(step->loc, "step^ must be positive");
            }
            Expr *check = node(E_CALL, loc);
            check->a = named("assert^", loc);
            add(&check->args, positive);
            Stmt *validate = mem(sizeof(*validate));
            validate->kind = S_EXPR;
            validate->loc = loc;
            validate->a = check;
            add(&scope->body, validate);
            add(&scope->body, binding(active, bounds, true));
            loop->a = named(active, loc);
            add(&scope->body, binding(focus, named(counter, loc), false));
            add(&loop->focuses, focus);
            Stmt *refresh = assignment(named(focus, loc), named(counter, loc), NULL);
            refresh->internal = true;
            add(&loop->pre, refresh);
            Expr *a = node(E_CALL, loc), *b = node(E_CALL, loc), *c = node(E_CALL, loc);
            a->a = named("u128^", loc);
            b->a = named("u128^", loc);
            c->a = named("u128^", loc);
            add(&a->args, named(down ? counter : limit, loc));
            add(&b->args, named(down ? limit : counter, loc));
            add(&c->args, named(stride, loc));
            Stmt *advance = mem(sizeof(*advance));
            advance->kind = S_IF;
            advance->loc = loc;
            advance->a = binary(">", c, binary("-", a, b));
            Expr *no = node(E_BOOL, loc);
            no->text = "false^";
            add(&advance->body, assignment(named(active, loc), no, NULL));
            add(&advance->other,
                assignment(named(counter, loc), named(stride, loc), down ? "-" : "+"));
            add(&loop->update, advance);
        }
    }
    if (counter && (is(p, "while^") || is(p, "until^"))) {
        fail(loc, "counted loop cannot take another driving clause");
    }
    if (!repeat && !scope->body.n && !counter) {
        fail(loc, "for^ requires a focus; use repeat^ for a focusless loop");
    }
    if (!repeat && (is(p, "while^") || is(p, "until^")) && scope->body.n) {
        Stmt *focus = scope->body.v[scope->body.n - 1];
        if (focus->kind == S_BIND && !focus->mut) {
            fail(focus->loc, "while^/until^ focus requires var^");
        }
    }
    if (take(p, "while^") || take(p, "until^")) {
        Token *keyword = p->tokens.v[p->at - 1];
        loop->a = expr(p, 0);
        if (!strcmp(keyword->text, "until^")) {
            Expr *not= node(E_UNARY, loc);
            not->text = "!";
            not->a = loop->a;
            loop->a = not;
        }
    }
    if (!loop->a) {
        if (!repeat) {
            fail(loc, "for^ requires a driving clause");
        }
        loop->a = node(E_BOOL, loc);
        loop->a->text = "true^";
    }
    if (take(p, "next^")) {
        if (repeat || counter) {
            fail(loc, "repeat^ and counted loops do not accept an explicit update clause");
        }
        do {
            Stmt *update = stmt(p);
            if (update->kind != S_ASSIGN &&
                !(update->kind == S_EXPR && update->a->kind == E_CALL)) {
                fail(update->loc, "next^ update requires an assignment or call");
            }
            add(&loop->update, update);
        } while (take(p, ","));
    }
    p->header = header;
    loop_body(p, loop);
    add(&scope->body, loop);
    return scope;
}

static Stmt *stmt(Parser *p)
{
    Token *t = tok(p);
    if (is(p, "catch^")) {
        fail(t->loc, "catch^ clause must end an enclosing block; use do^{...}");
    }
    if (branch_word(p)) {
        fail(t->loc, "%s belongs inside the preceding if^ braces", t->text);
    }
    Stmt *s = mem(sizeof(*s));
    s->loc = t->loc;
    if (take(p, "let^") || take(p, "var^")) {
        s->kind = S_BIND;
        s->mut = !strcmp(t->text, "var^");
        List bindings = {0}, values = {0};
        do {
            Stmt *binding = mem(sizeof(*binding));
            binding->kind = S_BIND;
            binding->loc = tok(p)->loc;
            binding->name = take(p, "_^") ? "_^" : decl_word(p);
            binding->mut = s->mut;
            if (take(p, ":")) {
                binding->spec = spec(p);
            }
            add(&bindings, binding);
        } while (!p->header && take(p, ","));
        if (!(take(p, "=") || (s->mut && take(p, ":=")))) {
            need(p, "=");
        }
        do {
            add(&values, expr(p, 0));
        } while (!p->header && take(p, ","));
        if (values.n == 1 && bindings.n > 1) {
            // let^ a, b = f(): the source binds first, then each name its part.
            add(&s->prolog, results(p, &values, bindings.n, s->loc));
            ((Stmt *)s->prolog.v[0])->mut = s->mut;
        }
        if (bindings.n != values.n) {
            fail(s->loc, "the number of bindings and values does not match");
        }
        for (int j = 0; j < bindings.n; j++) {
            ((Stmt *)bindings.v[j])->a = values.v[j];
        }
        if (bindings.n == 1) {
            *s = *(Stmt *)bindings.v[0];
        }
        else {
            s->kind = S_BIND_MANY;
            s->body = bindings;
        }
    }
    else if (take(p, "for^") || take(p, "repeat^")) {
        s = loop_statement(p, !strcmp(t->text, "repeat^"), t->loc);
    }
    else if (t->kind == TOK_WORD &&
             (!strncmp(t->text, "break^", 6) || !strncmp(t->text, "next^", 5) ||
              !strncmp(t->text, "skip^", 5) || !strncmp(t->text, "continue^", 9))) {
        next(p);
        s->kind = !strncmp(t->text, "break^", 6) ? S_BREAK : S_NEXT;
        s->depth = 0;
        for (const char *q = strchr(t->text, '^'); q && *q == '^'; q++) {
            s->depth++;
        }
        if (take(p, "[")) {
            if (s->depth > 1) {
                fail(t->loc, "jump depth must use hats or brackets, not both");
            }
            Token *depth = next(p);
            if (depth->kind != TOK_INT) {
                fail(depth->loc, "jump depth must be a positive integer literal");
            }
            errno = 0;
            long count = strtol(depth->text, NULL, 10);
            if (errno || count <= 0 || count > 65536) {
                fail(depth->loc, "jump depth must be positive and at most 65536");
            }
            s->depth = (int)count;
            need(p, "]");
        }
        if (s->depth < 1) {
            fail(t->loc, "jump depth must be positive");
        }
    }
    else if (take(p, "return^")) {
        s->kind = S_RETURN;
        if (tok(p)->kind != TOK_EOF && !is(p, "}") && !is(p, ";") && !branch_word(p) &&
            !match_clause(p) && !catch_head(p) && loop_section(p) < 0 &&
            tok(p)->loc.line == t->loc.line) {
            s->a = expr(p, 0);
            if (is(p, ",")) {
                List values = {0};
                add(&values, s->a);
                while (take(p, ",")) {
                    add(&values, expr(p, 0));
                }
                s->a = tuple(&values, s->a->loc);
            }
        }
    }
    else if (take(p, "while^")) {
        fail(t->loc, "while^ is a driving clause; write repeat^ while^ or for^ ... while^");
    }
    else if (take(p, "if^")) {
        bool header = p->header;
        p->header = true;
        s->a = expr(p, 0);
        p->header = header;
        if (take(p, ":")) {
            s->kind = S_EXPR;
            s->a = if_tail(p, s->a, t->loc);
            return s;
        }
        need(p, "{");
        p->scope_depth++;
        s = if_statement(p, s->a, t->loc);
        if (catch_head(p)) {
            List body = {0};
            add(&body, s);
            catches(p, &body, false);
            s = body.v[0];
            s->scoped_handlers = true;
            s->a = ((Stmt *)s->body.v[0])->a;
        }
        need(p, "}");
        p->scope_depth--;
    }
    else if (take(p, "do^")) {
        s->kind = S_BLOCK;
        block(p, &s->body);
    }
    else if (is(p, "{") &&
             !(p->at + 1 < p->tokens.n && !strcmp(((Token *)p->tokens.v[p->at + 1])->text, "}")) &&
             !(p->at + 2 < p->tokens.n && !strcmp(((Token *)p->tokens.v[p->at + 2])->text, "="))) {
        fail(t->loc, "standalone blocks require do^{...}");
    }
    else {
        s->a = expr(p, 0);
        add(&s->targets, s->a);
        while ((!p->header || s->a->kind != E_CALL) && take(p, ",")) {
            add(&s->targets, expr(p, 0));
        }
        const char *op = assignment_operator(p);
        if (op) {
            next(p);
            s->kind = S_ASSIGN;
            if (*op == '?') {
                s->optional = true;
                op++;
            }
            if (strcmp(op, ":=")) {
                s->op = slice(op, strlen(op) - 1);
            }
            do {
                add(&s->values, expr(p, 0));
            } while ((!p->header || s->values.n < s->targets.n) && take(p, ","));
            Stmt *source = NULL;
            if (s->targets.n > 1 && s->values.n == 1 && !s->op && !s->optional) {
                source = results(p, &s->values, s->targets.n, s->loc);
            }
            if (s->targets.n != s->values.n) {
                fail(s->loc, "the number of targets and values does not match");
            }
            s->b = s->values.v[0];
            if (source) {
                // a, b := f(): a block binds the results and assigns their parts.
                Stmt *assign = mem(sizeof(*assign));
                *assign = *s;
                s->kind = S_BLOCK;
                s->a = s->b = NULL;
                s->targets = s->values = (List){0};
                add(&s->body, source);
                add(&s->body, assign);
            }
        }
        else if (s->targets.n > 1) {
            // f^{ a, b }: multiple results of a single expression body.
            s->kind = S_EXPR;
            s->a = tuple(&s->targets, s->a->loc);
            s->targets = (List){0};
        }
        else {
            s->kind = S_EXPR;
        }
    }
    take(p, ";");
    return s;
}

static void block(Parser *p, List *out)
{
    need(p, "{");
    p->scope_depth++;
    while (!is(p, "}") && !catch_head(p)) {
        if (tok(p)->kind == TOK_EOF) {
            fail(tok(p)->loc, "unterminated block");
        }
        if (take(p, ";")) {
            continue;
        }
        add(out, stmt(p));
    }
    catches(p, out, false);
    need(p, "}");
    p->scope_depth--;
}

static Function *function(Parser *p, char *name, Def *owner, bool abstr)
{
    Function *f = mem(sizeof(*f));
    f->loc = tok(p)->loc;
    f->enclosing = p->enclosing;
    f->lexical = p->scope_depth + 2;
    f->name = name;
    f->owner = owner;
    f->operator_member = owner && !strncmp(name, "op^", 3);
    bool templ = take(p, "template^");
    f->templ = templ;
    f->abstract = abstr;
    if (take(p, "p^")) {
        f->procedure = true;
    }
    else {
        need(p, "f^");
    }
    if (take(p, "mutable^")) {
        f->mutable_self = true;
    }
    if (!is(p, "{") && !is(p, "->") && !is(p, ";")) {
        do {
            Param *a = mem(sizeof(*a));
            a->loc = tok(p)->loc;
            a->name = take(p, "self^") ? "self^"
                      : take(p, "_^")  ? "_^"
                      : take(p, "...") ? "..."
                                       : decl_word(p);
            if (f->variadic) {
                fail(a->loc, "the variadic parameter ... must be last");
            }
            a->self = !strcmp(a->name, "self^");
            if (a->self && (!owner || (f->params.n && !f->operator_member))) {
                fail(a->loc, "self^ must be the first parameter of a method");
            }
            if (a->self && f->params.n) {
                f->right_receiver = true;
            }
            if (take(p, ":")) {
                a->spec = spec(p);
            }
            if (!strcmp(a->name, "...")) {
                if (!a->spec) {
                    fail(a->loc, "variadic parameter needs an element type, as in ...:T");
                }
                Spec *slice = mem(sizeof(*slice));
                slice->kind = SPEC_SLICE;
                slice->loc = a->spec->loc;
                slice->elem = a->spec;
                a->spec = slice;
                f->variadic = true;
            }
            if (!a->self && !a->spec && abstr && !templ) {
                fail(a->loc, "bodiless declaration parameter requires a type annotation");
            }
            add(&f->params, a);
        } while (take(p, ","));
    }
    if (f->mutable_self &&
        (!owner || !f->params.n || !((Param *)f->params.v[0])->self || f->procedure)) {
        fail(f->loc, "mutable^self^ requires an f^ method receiver");
    }
    if (take(p, "->")) {
        f->result = result_spec(p);
    }
    if (abstr) {
        need(p, ";");
    }
    else {
        Function *enclosing = p->enclosing;
        p->enclosing = f;
        block(p, &f->body);
        p->enclosing = enclosing;
        if (!f->procedure && f->body.n == 1) {
            Stmt *only = f->body.v[0];
            if (only->kind == S_EXPR) {
                only->kind = S_RETURN;
            }
            if (only->kind == S_HANDLE) {
                if (only->body.n == 1 && ((Stmt *)only->body.v[0])->kind == S_EXPR) {
                    ((Stmt *)only->body.v[0])->kind = S_RETURN;
                }
                for (int j = 0; j < only->handlers.n; j++) {
                    CatchArm *arm = only->handlers.v[j];
                    if (arm->body.n == 1 && ((Stmt *)arm->body.v[0])->kind == S_EXPR) {
                        ((Stmt *)arm->body.v[0])->kind = S_RETURN;
                    }
                }
            }
        }
    }
    return f;
}

// Parse the signature after externC^ SYMBOL, bound to name.
static Function *external_function(Parser *p, char *name, char *symbol)
{
    need(p, ":");
    Function *f = function(p, name, NULL, true);
    f->abstract = false;
    f->external = symbol;
    return f;
}

static void definition(Parser *p)
{
    Def *d = mem(sizeof(*d));
    d->loc = tok(p)->loc;
    d->name = decl_word(p);
    d->id = p->p->defs.n;
    add(&p->p->defs, d);
    if (take(p, "=")) {
        do {
            add(&d->union_specs, spec_part(p));
        } while (take(p, "|"));
        take(p, ";");
        return;
    }
    if (take(p, ":")) {
        do {
            add(&d->base_names, qualified_word(p));
        } while (take(p, ","));
    }
    def_body(p, d);
}

// A def^ body from its {: fields in self^{ ... } and methods outside.
static void def_body(Parser *p, Def *d)
{
    need(p, "{");
    while (!take(p, "}")) {
        if (tok(p)->kind == TOK_EOF) {
            fail(tok(p)->loc, "unterminated def^");
        }
        if (take(p, ",") || take(p, ";")) {
            continue;
        }
        if (take(p, "self^")) {
            need(p, "{");
            fields(p, d);
            continue;
        }
        bool priv = false, abstr = false, over = false, overload = false;
        while (is(p, "private^") || is(p, "abstract^") || is(p, "override^") ||
               is(p, "overload^")) {
            if (take(p, "private^")) {
                priv = true;
            }
            else if (take(p, "abstract^")) {
                abstr = true;
            }
            else if (take(p, "overload^")) {
                overload = true;
            }
            else {
                next(p);
                over = true;
            }
        }
        if (over && overload) {
            fail(tok(p)->loc, "override^ and overload^ cannot be combined");
        }
        Loc declaration = tok(p)->loc;
        char *name;
        if (take(p, "op^")) {
            Token *op = next(p);
            const char *allowed[] = {"+",  "-",  "*",      "/",    "%",   "//",
                                     "**", "..", "cross^", "dot^", "<=>", "="};
            bool valid = false;
            for (size_t k = 0; k < sizeof(allowed) / sizeof(*allowed); k++) {
                valid |= !strcmp(op->text, allowed[k]);
            }
            if (!valid) {
                fail(op->loc, "invalid operator definition '%s'", op->text);
            }
            name = fmt("op^%s", op->text);
        }
        else {
            name = decl_word(p);
        }
        if (abstr) {
            need(p, ":");
        }
        else if (!take(p, ":=")) {
            need(p, "=");
        }
        Function *f = function(p, name, d, abstr);
        f->declaration = declaration;
        f->priv = priv;
        f->override = over;
        f->overload = overload;
        if (f->operator_member) {
            int self_count = 0;
            for (int k = 0; k < f->params.n; k++) {
                self_count += ((Param *)f->params.v[k])->self;
            }
            bool unary = !strcmp(name, "op^-") && f->params.n == 1;
            if (f->procedure || f->mutable_self || self_count != 1 ||
                (!unary && f->params.n != 2)) {
                fail(f->loc,
                     "operator requires a pure f^ with self^ and one operand (or unary '-')");
            }
        }
        add(&d->own_methods, f);
    }
}

// An integer literal's value: decimal, 0x, 0o or 0b, with _ separators.
static bool literal_value(const char *text, long long *out)
{
    int base = 10;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'o' || text[1] == 'b')) {
        base = text[1] == 'x' ? 16 : text[1] == 'o' ? 8 : 2;
        text += 2;
    }
    char digits[72];
    size_t n = 0;
    for (; *text; text++) {
        if (*text != '_') {
            if (n + 1 >= sizeof(digits)) {
                return false;
            }
            digits[n++] = *text;
        }
    }
    digits[n] = 0;
    char *end;
    errno = 0;
    *out = strtoll(digits, &end, base);
    return n && !*end && !errno;
}

// enum^ Name : u8^ { A, B = 10 }: a def^ of integer constants. Each member is a
// module constant named Name.Member that carries the enum as its type.
static void enum_declaration(Parser *q, Program *p, Module *module, bool public)
{
    Def *d = mem(sizeof(*d));
    d->loc = tok(q)->loc;
    d->name = decl_word(q);
    d->module = module;
    d->public = public;
    d->id = p->defs.n;
    if (take(q, ":")) {
        d->backing = spec(q);
    }
    else {
        d->backing = mem(sizeof(Spec));
        *d->backing = (Spec){.kind = SPEC_NAME, .loc = d->loc, .name = "i32^"};
    }
    add(&p->defs, d);
    add(&module->defs, d);
    Spec *type = mem(sizeof(*type));
    *type = (Spec){.kind = SPEC_NAME, .loc = d->loc, .name = d->name};
    need(q, "{");
    long long following = 0;
    do {
        if (is(q, "}")) {
            break;
        }
        Loc at = tok(q)->loc;
        char *name = decl_word(q);
        if (take(q, "=")) {
            bool negative = take(q, "-");
            Token *literal = next(q);
            long long value = 0;
            if (literal->kind != TOK_INT || !literal_value(literal->text, &value)) {
                fail(literal->loc, "an enum^ value is an integer literal");
            }
            following = negative ? -value : value;
        }
        EnumMember *member = mem(sizeof(*member));
        member->name = name;
        member->value = fmt("%lld", following++);
        for (int j = 0; j < d->members.n; j++) {
            EnumMember *other = d->members.v[j];
            if (!strcmp(other->name, name) || !strcmp(other->value, member->value)) {
                fail(at, "enum^ '%s' repeats the %s '%s'", d->name,
                     strcmp(other->name, name) ? "value" : "member",
                     strcmp(other->name, name) ? member->value : name);
            }
        }
        add(&d->members, member);
        Stmt *binding = mem(sizeof(*binding));
        binding->kind = S_BIND;
        binding->loc = at;
        binding->name = fmt("%s.%s", d->name, name);
        binding->spec = type;
        binding->a = number(member->value, at);
        binding->a->constant_spec = type;
        Constant *constant = mem(sizeof(*constant));
        constant->name = binding->name;
        constant->binding = binding;
        constant->public = public;
        add(&module->constants, constant);
    } while (take(q, ","));
    need(q, "}");
    if (!d->members.n) {
        fail(d->loc, "enum^ requires a member");
    }
}

void parse_file(Program *p, const char *path, Module *module)
{
    p->path = path;
    const char *overlay = source_override(p, path);
    if (overlay) {
        if (strlen(overlay) > 16 * 1024 * 1024) {
            fail((Loc){path, 1, 1}, "source too large");
        }
        p->source = str(overlay);
    }
    else {
        FILE *f = fopen(path, "rb");
        if (!f) {
            fail((Loc){path, 1, 1}, "cannot open source");
        }
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        if (n < 0 || n > 16 * 1024 * 1024) {
            fclose(f);
            fail((Loc){path, 1, 1}, "source too large");
        }
        rewind(f);
        p->source = mem((size_t)n + 1);
        bool okay = fread(p->source, 1, (size_t)n, f) == (size_t)n;
        fclose(f);
        if (!okay) {
            fail((Loc){path, 1, 1}, "cannot read source");
        }
    }
    if (p->tooling) {
        ToolSource *source = mem(sizeof(*source));
        *source = (ToolSource){path, p->source};
        add(&p->sources, source);
    }
    Parser q = {0};
    q.p = p;
    q.module = module;
    lex(&q);
    p->entry = mem(sizeof(*p->entry));
    p->entry->loc = (Loc){path, 1, 1};
    p->entry->name = "<top-level>";
    p->entry->procedure = true;
    module->entry = p->entry;
    if (module->root) {
        // The entry file is the body of p^ ...:string^ over the command arguments.
        Spec *text = mem(sizeof(*text)), *slice = mem(sizeof(*slice));
        *text = (Spec){.kind = SPEC_NAME, .loc = p->entry->loc, .name = "string^"};
        *slice = (Spec){.kind = SPEC_SLICE, .loc = p->entry->loc, .elem = text};
        Param *arguments = mem(sizeof(*arguments));
        *arguments = (Param){.loc = p->entry->loc, .name = "...", .spec = slice};
        add(&p->entry->params, arguments);
        p->entry->variadic = true;
    }
    if (take(&q, "module^")) {
        module->name = qualified_word(&q);
        take(&q, ";");
    }
    while (tok(&q)->kind != TOK_EOF) {
        if (take(&q, ";")) {
            continue;
        }
        bool public = take(&q, "public^");
        // exportC^ and exportL^ mark a function entered from C or from L^;
        // exportL^ also marks a def^ L^ holds by value, or with ref^ by
        // reference, and an errordef^ L^ receives as its own errors.
        Loc export_at = tok(&q)->loc;
        bool exported = false, lhat_export = false;
        for (;;) {
            if (take(&q, "exportC^")) {
                exported = true;
            }
            else if (take(&q, "exportL^")) {
                lhat_export = true;
            }
            else {
                break;
            }
        }
        bool lhat_ref = lhat_export && take(&q, "ref^");
        bool lhat_def = lhat_export && !exported &&
                        (is(&q, "def^") || (is(&q, "errordef^") && !lhat_ref) ||
                         (is(&q, "externC^") && q.at + 1 < q.tokens.n &&
                          !strcmp(((Token *)q.tokens.v[q.at + 1])->text, "def^")));
        if ((exported || lhat_export) && !lhat_def && (lhat_ref || !is(&q, "let^"))) {
            fail(export_at, "exportC^ and exportL^ apply only to a let^ function declaration"
                            " (or exportL^ to a def^ or errordef^)");
        }
        if (take(&q, "import^") || (is(&q, "let^") && q.at + 3 < q.tokens.n &&
                                    !strcmp(((Token *)q.tokens.v[q.at + 2])->text, "=") &&
                                    !strcmp(((Token *)q.tokens.v[q.at + 3])->text, "import^"))) {
            if (public) {
                fail(tok(&q)->loc, "imports cannot be public^");
            }
            Import *i = mem(sizeof(*i));
            i->loc = tok(&q)->loc;
            if (take(&q, "let^")) {
                i->alias = decl_word(&q);
                need(&q, "=");
                need(&q, "import^");
            }
            i->name = qualified_word(&q);
            add(&module->imports, i);
            take(&q, ";");
            continue;
        }
        if (take(&q, "enum^")) {
            enum_declaration(&q, p, module, public);
            continue;
        }
        if (take(&q, "errordef^")) {
            Def *group = mem(sizeof(*group));
            group->loc = tok(&q)->loc;
            group->name = decl_word(&q);
            group->module = module;
            group->public = public;
            group->lhat_error = lhat_def;
            group->id = p->defs.n;
            add(&p->defs, group);
            add(&module->defs, group);
            need(&q, "{");
            do {
                if (is(&q, "}")) {
                    break;
                }
                Def *arm = mem(sizeof(*arm));
                arm->loc = tok(&q)->loc;
                arm->name = fmt("%s.%s", group->name, decl_word(&q));
                arm->module = module;
                arm->public = public;
                arm->group = group;
                error_variant(&q, arm);
                arm->id = p->defs.n;
                add(&p->defs, arm);
                add(&module->defs, arm);
                Spec *alternative = mem(sizeof(*alternative));
                alternative->kind = SPEC_NAME;
                alternative->loc = arm->loc;
                alternative->name = arm->name;
                add(&group->union_specs, alternative);
            } while (take(&q, ","));
            need(&q, "}");
            if (!group->union_specs.n) {
                fail(group->loc, "errordef^ requires a variant");
            }
            continue;
        }
        if (is(&q, "includeC^")) {
            Loc at = next(&q)->loc;
            Token *header = next(&q);
            if (header->kind != TOK_STRING || public) {
                fail(at, "includeC^ takes a header name string and cannot be public^");
            }
            add(&module->c_headers, header->text);
            take(&q, ";");
            continue;
        }
        if (take(&q, "externC^")) {
            if (!take(&q, "def^")) {
                Loc declaration = tok(&q)->loc;
                char *name = decl_word(&q);
                Function *f = external_function(&q, name, name);
                f->declaration = declaration;
                f->module = module;
                f->public = public;
                add(&module->functions, f);
                add(&p->functions, f);
                continue;
            }
            // A def^ laid out as the C type of the same name, or opaque.
            Loc loc = tok(&q)->loc;
            int start = q.at;
            char *name = decl_word(&q);
            Def *d = NULL;
            if (take(&q, ";")) {
                d = mem(sizeof(*d));
                *d = (Def){.loc = loc, .name = name, .id = p->defs.n, .opaque = true};
                add(&p->defs, d);
            }
            else {
                q.at = start;
                definition(&q);
                d = p->defs.v[p->defs.n - 1];
            }
            // With methods only, it is still opaque.
            d->opaque |= !d->own_fields.n;
            d->c_name = name;
            d->module = module;
            d->public = public;
            d->lhat_value = lhat_def && !lhat_ref;
            d->lhat_data = lhat_ref;
            add(&module->defs, d);
            continue;
        }
        if (take(&q, "def^")) {
            definition(&q);
            Def *d = p->defs.v[p->defs.n - 1];
            d->module = module;
            d->public = public;
            d->lhat_value = lhat_def && !lhat_ref;
            d->lhat_data = lhat_ref;
            add(&module->defs, d);
            continue;
        }
        bool binding = false;
        if (is(&q, "let^") && q.at + 3 < q.tokens.n) {
            Token *equal = q.tokens.v[q.at + 2];
            Token *initializer = q.tokens.v[q.at + 3];
            binding = !strcmp(equal->text, "=") && initializer->kind == TOK_WORD &&
                      (!strcmp(initializer->text, "f^") || !strcmp(initializer->text, "p^") ||
                       !strcmp(initializer->text, "externC^") ||
                       !strcmp(initializer->text, "externLLVM^") ||
                       !strcmp(initializer->text, "template^"));
        }
        if ((exported || lhat_export) &&
            (!binding || q.at + 3 >= q.tokens.n ||
             strstr(((Token *)q.tokens.v[q.at + 3])->text, "extern"))) {
            fail(export_at, "exportC^ and exportL^ apply only to a let^ function declaration");
        }
        if (!binding) {
            Stmt *s = stmt(&q);
            if (!module->root) {
                if ((s->kind != S_BIND && s->kind != S_BIND_MANY) || s->mut) {
                    fail(s->loc, "library top level permits only declarations and "
                                 "immutable constants");
                }
                List bindings = {0};
                if (s->kind == S_BIND) {
                    add(&bindings, s);
                }
                else {
                    bindings = s->body;
                }
                for (int j = 0; j < bindings.n; j++) {
                    Stmt *value = bindings.v[j];
                    Constant *c = mem(sizeof(*c));
                    bool discard = !strcmp(value->name, "_^");
                    c->name = discard ? fmt("\001constant_discard%d", ++q.serial) : value->name;
                    c->binding = value;
                    c->public = public && !discard;
                    add(&module->constants, c);
                }
            }
            else {
                if (public) {
                    fail(s->loc, "public^ executable bindings are not supported in the "
                                 "entry file");
                }
                add(&p->entry->body, s);
            }
            continue;
        }
        int binding_start = q.at;
        need(&q, "let^");
        Loc declaration = tok(&q)->loc;
        char *name = decl_word(&q);
        need(&q, "=");
        Function *parsed_function = NULL;
        if (take(&q, "externC^")) {
            parsed_function = external_function(&q, name, word(&q));
        }
        else if (take(&q, "externLLVM^")) {
            parsed_function = external_function(&q, name, qualified_word(&q));
            parsed_function->intrinsic = true;
        }
        else {
            parsed_function = function(&q, name, NULL, false);
            // A called literal initializes a value; it does not declare a named
            // function.
            if (is(&q, "(") || is(&q, ".") || is(&q, "[") ||
                (tok(&q)->kind != TOK_STRING && prec(tok(&q)->text) >= 0)) {
                q.at = binding_start;
                add(&p->entry->body, stmt(&q));
                continue;
            }
        }
        parsed_function->declaration = declaration;
        parsed_function->module = module;
        parsed_function->public = public;
        parsed_function->exported = exported;
        parsed_function->lhat_export = lhat_export;
        add(&module->functions, parsed_function);
        add(&p->functions, parsed_function);
        take(&q, ";");
    }
    // A char^(...) or string^(...) cast fails with std.text's error, imported out of sight.
    bool text = q.text_cast && !(module->name && !strcmp(module->name, "std.text"));
    for (int j = 0; text && j < module->imports.n; j++) {
        text = strcmp(((Import *)module->imports.v[j])->name, "std.text");
    }
    if (text) {
        Import *i = mem(sizeof(*i));
        i->name = "std.text";
        i->alias = "\001std.text";
        i->loc = (Loc){path, 1, 1};
        add(&module->imports, i);
    }
}
