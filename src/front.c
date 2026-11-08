#include "rhat.h"
#include "tooling.h"
#include "format.h"
#include <ctype.h>
#include <errno.h>

void *mem(size_t n)
{
    if (compiler_hooks) {
        return compiler_hooks->allocate(n);
    }
    // CLI compiler objects live until process exit. Zeroed chunks avoid a heap
    // allocation for every token, expression, inference node and instruction.
    static _Thread_local unsigned char *next;
    static _Thread_local size_t remaining;
    size_t align = _Alignof(long double);
    if (n > SIZE_MAX - align) {
        fputs("out of memory\n", stderr);
        exit(1);
    }
    size_t size = ((n ? n : 1) + align - 1) / align * align;
    if (size > remaining) {
        remaining = size > 65536 ? size : 65536;
        next = calloc(1, remaining);
        if (!next) {
            fputs("out of memory\n", stderr);
            exit(1);
        }
    }
    void *p = next;
    next += size;
    remaining -= size;
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
    List trace = tool_instantiation_trace(tool_current_instance());
    for (int j = 0; j < trace.n; j++) {
        ToolInstantiation *use = trace.v[j];
        fprintf(stderr, "%s:%d:%d: note: in instantiation of '%s' requested here\n", use->loc.file,
                use->loc.line, use->loc.col, use->callee->fn->name);
    }
    exit(1);
}

typedef struct {
    char *text;
    Loc loc;
    int kind;
    size_t length;
    uint32_t offset;
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
    TOK_EOF,
    TOK_INVALID
};

typedef struct {
    Program *p;
    Module *module;
    Function *enclosing;
    Def *type_owner;
    List tokens;
    int at, serial, scope_depth;
    List type_names, error_names; // Declaration names indexed once after lexing.
    bool header;
    bool text_cast; // A char^(...) or string^(...) cast needs std.text for its error.
    bool enum_cast; // An enum^ declaration's Mode(n) cast needs std.sys for its error.
} Parser;

static void format_span(Parser *p, int first, int kind, bool block, bool row)
{
    if (!p->p->format || p->at <= first) {
        return;
    }
    Token *a = p->tokens.v[first], *b = p->tokens.v[p->at - 1];
    FormatSpan *s = mem(sizeof(*s));
    *s = (FormatSpan){first, p->at, kind, a->offset, b->offset + (uint32_t)b->loc.length,
                      block, row};
    add(&p->p->format->spans, s);
}

static void format_tokens(Parser *p)
{
    if (!p->p->format) {
        return;
    }
    for (int j = 0; j < p->tokens.n; j++) {
        Token *t = p->tokens.v[j];
        if (t->kind == TOK_EOF) {
            break;
        }
        FormatToken *out = mem(sizeof(*out));
        out->text = t->text;
        out->offset = t->offset;
        out->length = (uint32_t)t->loc.length;
        out->kind = t->kind == TOK_WORD                           ? FORMAT_WORD
                    : t->kind == TOK_SCOPE                        ? FORMAT_SCOPE
                    : t->kind >= TOK_INT && t->kind <= TOK_QUOTED ? FORMAT_LITERAL
                                                                  : FORMAT_OP;
        out->hat =
            t->kind == TOK_WORD && out->length && p->p->source[t->offset + out->length - 1] == '^';
        if (t->kind == TOK_INTERP) {
            int depth = 1;
            while (++j < p->tokens.n) {
                Token *end = p->tokens.v[j];
                depth += end->kind == TOK_INTERP;
                depth -= end->kind == TOK_INTERP_END;
                if (!depth) {
                    out->length = end->offset + (uint32_t)end->loc.length - out->offset;
                    break;
                }
            }
            out->kind = FORMAT_LITERAL;
        }
        add(&p->p->format->tokens, out);
    }
}

static FormatToken *format_token(Parser *p, int at)
{
    if (!p->p->format) {
        return NULL;
    }
    Token *token = p->tokens.v[at];
    List *tokens = &p->p->format->tokens;
    int low = 0, high = tokens->n;
    while (low < high) {
        int middle = low + (high - low) / 2;
        FormatToken *t = tokens->v[middle];
        if (t->offset < token->offset) {
            low = middle + 1;
        }
        else {
            high = middle;
        }
    }
    if (low < tokens->n) {
        FormatToken *t = tokens->v[low];
        if (t->offset == token->offset) {
            return t;
        }
    }
    return NULL;
}

static void format_clause(Parser *p, int at)
{
    FormatToken *t = format_token(p, at);
    if (t && t->kind == FORMAT_WORD) {
        t->clause = true;
    }
}

static void format_operator(Parser *p, int at, int role)
{
    FormatToken *t = format_token(p, at);
    if (t) {
        t->operator_role = role;
    }
}

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

// Balance braces while skipping a damaged unit. A new declaration on a later
// line can also recover from a missing closing delimiter at the same indentation.
static void synchronize(Parser *p, int start, int failed, bool outer)
{
    Token *first = p->tokens.v[start];
    int braces = 0;
    p->at = start;
    while (tok(p)->kind != TOK_EOF) {
        Token *t = tok(p);
        bool starter = is(p, "let^") || is(p, "var^") || is(p, "return^") || is(p, "def^") ||
                       is(p, "struct^") || is(p, "import^") || is(p, "public^") ||
                       is(p, "exportC^") || is(p, "exportL^") || is(p, "if^") || is(p, "for^") ||
                       is(p, "while^") || is(p, "print^") || is(p, "op^") || is(p, "private^") ||
                       is(p, "abstract^") || is(p, "override^") || is(p, "overload^") ||
                       is(p, "inline^");
        if (t->kind == TOK_WORD && p->at + 2 < p->tokens.n) {
            Token *equal = p->tokens.v[p->at + 1], *value = p->tokens.v[p->at + 2];
            starter |= (!strcmp(equal->text, "=") || !strcmp(equal->text, ":=")) &&
                       (!strcmp(value->text, "f^") || !strcmp(value->text, "p^"));
        }
        if (p->at > start && p->at >= failed &&
            ((starter && !braces) ||
             (t->loc.line > first->loc.line && t->loc.col <= first->loc.col && starter))) {
            return;
        }
        if (is(p, "}")) {
            if (!braces) {
                if (outer) {
                    next(p);
                }
                return;
            }
            braces--;
            next(p);
            if (!braces && p->at >= failed) {
                return;
            }
        }
        else if (take(p, ";") && !braces && p->at >= failed) {
            return;
        }
        else {
            braces += is(p, "{");
            next(p);
        }
    }
}

static void need(Parser *p, const char *s)
{
    if (!take(p, s)) {
        if (tok(p)->kind == TOK_INVALID) {
            tool_abort();
        }
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
    if (e->kind == E_TYPE) {
        return true;
    }
    char *name = expression_path(e);
    if (!name) {
        return false;
    }
    for (int j = 0; j < p->type_names.n; j++) {
        if (!strcmp(p->type_names.v[j], name)) {
            return true;
        }
    }
    for (int j = 0; j < p->error_names.n; j++) {
        const char *error = p->error_names.v[j];
        size_t n = strlen(error);
        if (!strncmp(name, error, n) && name[n] == '.') {
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
static const char *interp_text(const char *s, Token *t, char *out)
{
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
    t->text = slice(out, n);
    t->length = n;
    return s;
}

typedef struct {
    const char *s;
    char *scratch;
    int line, col, level, nest[33];
    bool text;
} Lexer;

const char *unicode_operator(const char *text, size_t *length)
{
    static const char *const operators[][2] = {{"≤", "<="},  {"≦", "<="}, {"≥", ">="},
                                               {"≧", ">="},  {"≠", "!="}, {"×", "cross^"},
                                               {"⋅", "dot^"}};
    if ((unsigned char)*text >= 128) {
        for (size_t j = 0; j < sizeof(operators) / sizeof(*operators); j++) {
            size_t width = strlen(operators[j][0]);
            if (!strncmp(text, operators[j][0], width)) {
                if (length) {
                    *length = width;
                }
                return operators[j][1];
            }
        }
    }
    return NULL;
}

static void lex_step(Parser *p, Lexer *l)
{
    if (l->text || (l->level && !l->nest[l->level] && (*l->s == '}' || *l->s == ':'))) {
        Token *t = mem(sizeof(*t));
        t->loc = (Loc){p->p->path, l->line, l->col};
        const char *start = l->s;
        if (l->text && (*l->s == '"' || *l->s == '{')) {
            bool hole = *l->s++ == '{';
            t->kind = hole ? TOK_HOLE : TOK_INTERP_END;
            t->text = hole ? "\001hole" : "\001end";
            l->level -= !hole;
            l->nest[l->level] = hole ? 0 : l->nest[l->level];
            l->text = false;
        }
        else if (l->text) {
            l->s = interp_text(l->s, t, l->scratch);
        }
        else if (*l->s == '}') {
            l->s++;
            t->kind = TOK_HOLE_END;
            t->text = "\001hole_end";
            l->text = true;
        }
        else {
            // A format is raw text up to the hole's }.
            const char *format = ++l->s;
            while (*l->s && *l->s != '}' && *l->s != '\n') {
                l->s++;
            }
            if (*l->s != '}') {
                fail(t->loc, "unterminated format in an interpolated string");
            }
            t->kind = TOK_FORMAT;
            t->text = slice(format, (size_t)(l->s - format));
        }
        t->loc.length = (int)(l->s - start);
        t->offset = (uint32_t)(start - p->p->source);
        for (; start < l->s; start++) {
            l->line += *start == '\n';
            l->col = *start == '\n' ? 1 : l->col + 1;
        }
        add(&p->tokens, t);
        return;
    }
    if (isspace((unsigned char)*l->s)) {
        if (*l->s++ == '\n') {
            l->line++;
            l->col = 1;
        }
        else {
            l->col++;
        }
        return;
    }
    if (l->s[0] == '#' && l->s[1] == '[') {
        l->s += 2;
        l->col += 2;
        int depth = 1;
        while (*l->s && depth) {
            if (l->s[0] == '#' && l->s[1] == '[') {
                depth++;
                l->s += 2;
                l->col += 2;
            }
            else if (l->s[0] == ']' && l->s[1] == '#') {
                depth--;
                l->s += 2;
                l->col += 2;
            }
            else if (*l->s++ == '\n') {
                l->line++;
                l->col = 1;
            }
            else {
                l->col++;
            }
        }
        if (depth) {
            fail((Loc){p->p->path, l->line, l->col}, "unterminated comment");
        }
        return;
    }
    if (*l->s == '#') {
        while (*l->s && *l->s != '\n') {
            l->s++;
            l->col++;
        }
        return;
    }
    Token *t = mem(sizeof(*t));
    t->loc = (Loc){p->p->path, l->line, l->col};
    const char *start = l->s;
    size_t unicode_length;
    const char *unicode = unicode_operator(l->s, &unicode_length);
    if (unicode) {
        l->s += unicode_length;
        t->text = (char *)unicode;
        t->kind = '=';
    }
    else if (l->s[0] == '$' && l->s[1] == '"') {
        l->s += 2;
        t->kind = TOK_INTERP;
        t->text = "\001interp";
        if (++l->level > 32) {
            fail(t->loc, "interpolated strings nest deeper than 32");
        }
        l->text = true;
    }
    else if (l->s[0] == '$' && l->s[1] == '\'') {
        fail(t->loc, "interpolation is written $\"...\"; $'...' does not interpolate");
    }
    else if (*l->s == '$') {
        l->s++;
        if (*l->s == '^') {
            while (*l->s == '^') {
                l->s++;
            }
        }
        else {
            while (*l->s == '$') {
                l->s++;
            }
        }
        if (!(isalpha((unsigned char)*l->s) || *l->s == '_' || *l->s == '`' ||
              (unsigned char)*l->s >= 128)) {
            fail(t->loc, "scope prefix must be adjacent to an identifier");
        }
        t->kind = TOK_SCOPE;
    }
    else if (*l->s == '`') {
        l->s++;
        char *out = l->scratch;
        size_t n = 0;
        bool closed = false;
        while (*l->s) {
            if (*l->s == '\n' || *l->s == '\r') {
                fail(t->loc, "newline in quoted identifier");
            }
            if (*l->s == '`') {
                l->s++;
                if (*l->s != '`') {
                    closed = true;
                    break;
                }
            }
            if ((unsigned char)*l->s < 32 || (unsigned char)*l->s == 127) {
                fail(t->loc, "control character in quoted identifier");
            }
            out[n++] = *l->s++;
        }
        if (!closed || !n) {
            fail(t->loc, "empty or unterminated quoted identifier");
        }
        t->text = slice(out, n);
        t->kind = TOK_QUOTED;
    }
    else if (isalpha((unsigned char)*l->s) || *l->s == '_' || (unsigned char)*l->s >= 128) {
        while (isalnum((unsigned char)*l->s) || *l->s == '_' || (unsigned char)*l->s >= 128) {
            if (unicode_operator(l->s, NULL)) {
                break;
            }
            l->s++;
        }
        while (*l->s == '^') {
            l->s++;
        }
        t->kind = TOK_WORD;
    }
    else if (isdigit((unsigned char)*l->s)) {
        t->kind = TOK_INT;
        if (l->s[0] == '0' && strchr("xXoObB", l->s[1]) && l->s[1]) {
            int base = l->s[1] == 'x' || l->s[1] == 'X'   ? 16
                       : l->s[1] == 'o' || l->s[1] == 'O' ? 8
                                                          : 2;
            l->s += 2;
            const char *begin = l->s;
            l->s = digits(l->s, base, t->loc);
            t->text = decimal_base(begin, l->s, base);
        }
        else {
            l->s = digits(l->s, 10, t->loc);
            if (*l->s == '.' && isdigit((unsigned char)l->s[1])) {
                t->kind = TOK_FLOAT;
                l->s = digits(l->s + 1, 10, t->loc);
            }
            if (*l->s == 'e' || *l->s == 'E') {
                t->kind = TOK_FLOAT;
                l->s++;
                if (*l->s == '+' || *l->s == '-') {
                    l->s++;
                }
                l->s = digits(l->s, 10, t->loc);
            }
            t->text = mem((size_t)(l->s - start) + 1);
            size_t n = 0;
            for (const char *q = start; q < l->s; q++) {
                if (*q != '_') {
                    t->text[n++] = *q;
                }
            }
        }
        if (isalnum((unsigned char)*l->s) || *l->s == '_') {
            fail(t->loc, "invalid numeric literal");
        }
    }
    else if (*l->s == '"' || *l->s == '\'') {
        char quote = *l->s++;
        bool line_string = quote == '"' && l->s[0] == '"' && l->s[1] == '"';
        if (line_string) {
            l->s += 2;
        }
        char *out = l->scratch;
        size_t n = 0;
        bool closed = line_string;
        while (*l->s && (!line_string || (*l->s != '\n' && *l->s != '\r'))) {
            if (!line_string && *l->s == quote) {
                l->s++;
                if (quote != '\'' || *l->s != '\'') {
                    closed = true;
                    break;
                }
                out[n++] = *l->s++;
                continue;
            }
            if (*l->s == '\r' && l->s[1] == '\n') {
                out[n++] = '\n';
                l->s += 2;
                continue;
            }
            if (!line_string && quote == '"' && *l->s == '\\') {
                l->s = string_escape(l->s + 1, out, &n, t->loc);
            }
            else {
                out[n++] = *l->s++;
            }
        }
        if (!closed) {
            fail(t->loc, "unterminated string");
        }
        utf8_text(out, n, t->loc);
        t->kind = TOK_STRING;
        t->text = slice(out, n);
        t->length = n;
    }
    else if (*l->s == '?') {
        const char *ops[] = {"?:=", "?+=", "?-=", "?*=", "?/=", "?%=", "?.", "?[", "?(", "??"};
        t->kind = '?';
        l->s++;
        for (size_t j = 0; j < sizeof(ops) / sizeof(*ops); j++) {
            size_t n = strlen(ops[j]);
            if (!strncmp(start, ops[j], n)) {
                l->s = start + n;
                break;
            }
        }
    }
    else {
        t->kind = (unsigned char)*l->s++;
        if (!strncmp(start, "<=>", 3)) {
            l->s = start + 3;
        }
        else if ((start[0] == '*' && *l->s == '*') || (start[0] == '/' && *l->s == '/')) {
            l->s++;
        }
        else if ((strchr("+-*/%", start[0]) && *l->s == '=') || (start[0] == '=' && *l->s == '/') ||
                 (start[0] == ':' && (*l->s == '=' || *l->s == ':')) ||
                 (start[0] == '-' && *l->s == '>') || (strchr("=!<>", start[0]) && *l->s == '=') ||
                 (start[0] == '&' && *l->s == '&') || (start[0] == '|' && *l->s == '|') ||
                 (start[0] == '.' && *l->s == '.')) {
            l->s++;
        }
    }
    if (l->s - start == 2 && !strncmp(start, "..", 2) && *l->s == '.') {
        l->s++; // The variadic marker ... is matched before a range.
    }
    if (l->s - start == 2 && *l->s == '=' &&
        (!strncmp(start, "**", 2) || !strncmp(start, "//", 2) || !strncmp(start, "::", 2))) {
        l->s++;
    }
    t->loc.length = (int)(l->s - start);
    t->offset = (uint32_t)(start - p->p->source);
    if (!t->text) {
        t->text = slice(start, (size_t)(l->s - start));
    }
    if (t->kind != TOK_STRING && t->kind != TOK_QUOTED && !strcmp(t->text, "=/")) {
        t->text = "!=";
    }
    if (t->kind != TOK_STRING && t->kind != TOK_QUOTED &&
        (!strcmp(t->text, "==") || !strcmp(t->text, "||"))) {
        fail(t->loc, "use '=', 'and^', or 'or^'; C-style comparison/logical "
                     "operators are not supported");
    }
    for (const char *q = start; q < l->s; q++) {
        if (*q == '\n') {
            l->line++;
            l->col = 1;
        }
        else {
            l->col++;
        }
    }
    if (l->level && t->kind != TOK_STRING && t->kind != TOK_QUOTED && l->s - start <= 2) {
        // Brackets inside a hole keep its } and : from closing it.
        l->nest[l->level] += strchr("([{", l->s[-1])                        ? 1
                             : strchr(")]}", l->s[-1]) && l->s - start == 1 ? -1
                                                                            : 0;
    }
    add(&p->tokens, t);
}

static void lex_checked(Parser *p, Lexer *l)
{
    const char *start = l->s;
    Loc loc = {p->p->path, l->line, l->col};
    ToolRecovery recovery;
    tool_enter(&recovery);
    if (setjmp(recovery.escape)) {
        l->s = start;
        // A broken quoted token ends at its physical line. An open block
        // comment has no unambiguous code after it, so it consumes the rest.
        while (*l->s && (*l->s != '\n' || (start[0] == '#' && start[1] == '['))) {
            l->s++;
        }
        l->line = loc.line;
        l->col = loc.col;
        for (const char *at = start; at < l->s; at++) {
            l->line += *at == '\n';
            l->col = *at == '\n' ? 1 : l->col + 1;
        }
        l->level = 0;
        l->text = false;
        Token *bad = mem(sizeof(*bad));
        *bad = (Token){.text = "<invalid>", .loc = loc, .kind = TOK_INVALID};
        add(&p->tokens, bad);
    }
    else {
        lex_step(p, l);
    }
    tool_leave(&recovery);
}

static void lex(Parser *p)
{
    Lexer l = {.s = p->p->source, .line = 1, .col = 1};
    l.scratch = mem(strlen(l.s) + 1);
    while (*l.s) {
        if (p->p->tooling) {
            lex_checked(p, &l);
        }
        else {
            lex_step(p, &l);
        }
    }
    if (l.level) {
        TOOL_CHECK(p->p,
                   fail((Loc){p->p->path, l.line, l.col}, "unterminated interpolated string"););
    }
    Token *end = mem(sizeof(*end));
    end->kind = TOK_EOF;
    end->text = "<eof>";
    end->loc = (Loc){p->p->path, l.line, l.col};
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

static int dynamic_depth(const char *text)
{
    if (strncmp(text, "dynamic^", 8)) {
        return 0;
    }
    size_t hats = strspn(text + 7, "^");
    return text[7 + hats] ? 0 : (int)hats;
}

// A dynamic reference is the ordinary ref^(dyn^ T) representation; additional
// hats wrap references around it. read^ qualifies the innermost reference.
static Spec *reference_spec(Spec *elem, Loc loc, int refs, bool read, bool dynamic)
{
    if (dynamic) {
        Spec *dyn = mem(sizeof(*dyn));
        *dyn = (Spec){.kind = SPEC_DYN, .loc = loc, .elem = elem};
        elem = dyn;
    }
    while (refs--) {
        Spec *ref = mem(sizeof(*ref));
        *ref = (Spec){.kind = SPEC_REF, .loc = loc, .elem = elem, .count = read};
        read = false;
        elem = ref;
    }
    return elem;
}

static Spec *spec(Parser *p);
static Spec *result_spec(Parser *p);

static bool type_parameter(Parser *p, const char *name)
{
    for (int j = 0; p->type_owner && j < p->type_owner->type_params.n; j++) {
        if (!strcmp(p->type_owner->type_params.v[j], name)) {
            return true;
        }
    }
    return false;
}

static void type_close(Parser *p)
{
    // In a type context the first character of >= closes the argument list.
    if (is(p, ">=")) {
        Token *t = tok(p);
        t->text = "=";
        t->loc.col++;
        t->loc.length = 1;
        return;
    }
    need(p, ">");
}

static bool type_arguments_ahead(Parser *p)
{
    if (!is(p, "<") || p->at + 1 >= p->tokens.n) {
        return false;
    }
    Token *first = p->tokens.v[p->at + 1];
    if (first->kind != TOK_WORD && strcmp(first->text, "(")) {
        return false;
    }
    int angles = 0, parens = 0, brackets = 0;
    for (int j = p->at; j < p->tokens.n; j++) {
        Token *t = p->tokens.v[j];
        if (t->kind == TOK_EOF || !strcmp(t->text, "{") || !strcmp(t->text, "}")) {
            return false;
        }
        if (!strcmp(t->text, "<")) {
            angles++;
        }
        else if (!strcmp(t->text, ">") || !strcmp(t->text, ">=")) {
            if (!--angles) {
                return true;
            }
        }
        else if (!strcmp(t->text, "(")) {
            parens++;
        }
        else if (!strcmp(t->text, ")")) {
            if (!parens--) {
                return false;
            }
        }
        else if (!strcmp(t->text, "[")) {
            brackets++;
        }
        else if (!strcmp(t->text, "]")) {
            if (!brackets--) {
                return false;
            }
        }
    }
    return false;
}

static void type_arguments(Parser *p, Spec *s)
{
    if (type_arguments_ahead(p) && take(p, "<")) {
        if (is(p, ">")) {
            fail(tok(p)->loc, "type argument list must not be empty");
        }
        do {
            add(&s->type_args, spec(p));
        } while (take(p, ","));
        type_close(p);
    }
}

static int self_type_depth(const char *text)
{
    if (strncmp(text, "Self", 4) || text[4] != '^') {
        return 0;
    }
    size_t count = strspn(text + 4, "^");
    return text[4 + count] ? 0 : (int)count;
}

static Expr *expr(Parser *p, int min);

// A slice that f^ takes is read^ as written: it can change nothing through it anyway.
static void read_view(Spec *s)
{
    if (s->kind == SPEC_SLICE) {
        s->count = 1;
    }
    else if (s->kind == SPEC_UNION) {
        read_view(s->elem);
        read_view(s->other);
    }
}

// Parse an array type's bracket contents, leaving the closing ']' unread.
static Spec *array_header(Parser *p, Spec *elem, int *references)
{
    int refs = reference_depth(tok(p)->text);
    bool dynamic = dynamic_depth(tok(p)->text) != 0;
    if (dynamic) {
        refs = dynamic_depth(tok(p)->text);
    }
    *references = refs;
    if (refs) {
        Token *token = next(p);
        bool read = take(p, "read^");
        dynamic = dynamic || take(p, "dyn^");
        elem = reference_spec(elem, token->loc, refs, read, dynamic);
    }
    Spec *w = mem(sizeof(*w));
    w->loc = elem->loc;
    w->elem = elem;
    if (is(p, "]")) {
        w->kind = SPEC_ARRAY;
    }
    else if (take(p, "..")) {
        w->kind = SPEC_SLICE;
    }
    else if (is(p, "grow^") || is(p, "flex^")) {
        w->kind = SPEC_GROW;
        w->count = !strcmp(next(p)->text, "flex^");
    }
    else {
        bool bounded = take(p, "max^");
        if (bounded && is(p, "]")) {
            w->kind = SPEC_GROW;
            w->count = ARRAY_CAPPED;
            return w;
        }
        w->kind = bounded ? SPEC_BOUNDED : SPEC_ARRAY;
        Token *n = tok(p);
        if (n->kind != TOK_INT || p->at + 1 >= p->tokens.n ||
            strcmp(((Token *)p->tokens.v[p->at + 1])->text, "]")) {
            // A constant expression such as WIDTH * DEPTH; modules.c folds it.
            bool header = p->header;
            p->header = false;
            w->length = expr(p, 0);
            p->header = header;
            return w;
        }
        next(p);
        errno = 0;
        long k = strtol(n->text, NULL, 10);
        if (errno || k <= 0 || k > 1000000) {
            fail(n->loc, "array length out of range (1..1000000)");
        }
        w->kind = bounded ? SPEC_BOUNDED : SPEC_ARRAY;
        w->count = (int)k;
    }
    return w;
}

// Whether the '[' at token k opens an array type in an expression: a kind or
// reference word, an empty pair, or a length followed by '{' or another one.
static bool array_type_at(Parser *p, int k)
{
    if (k + 1 >= p->tokens.n) {
        return false;
    }
    Token *next = p->tokens.v[k + 1];
    if (!strcmp(next->text, "]") || !strcmp(next->text, "grow^") || !strcmp(next->text, "flex^") ||
        !strcmp(next->text, "max^") || reference_depth(next->text) || dynamic_depth(next->text)) {
        return true;
    }
    if (next->kind != TOK_INT || k + 3 >= p->tokens.n ||
        strcmp(((Token *)p->tokens.v[k + 2])->text, "]")) {
        return false;
    }
    const char *after = ((Token *)p->tokens.v[k + 3])->text;
    return (!strcmp(after, "{") && !p->header) || (!strcmp(after, "[") && array_type_at(p, k + 3));
}

// The array type suffixes after s; refs counts the last bracket's ref^ depth.
static Spec *array_spec(Parser *p, Spec *s, int *refs)
{
    while (take(p, "[")) {
        int opening_at = p->at - 1;
        s = array_header(p, s, refs);
        FormatToken *opening = format_token(p, opening_at);
        if (opening) {
            opening->array_type = true;
        }
        need(p, "]");
    }
    return s;
}

// typeof^(e) delimits its operand, so typeof^(x).signature^ asks about x.
static Spec *typeof_spec(Parser *p, Loc loc)
{
    Spec *s = mem(sizeof(*s));
    *s = (Spec){.kind = SPEC_TYPEOF, .loc = loc};
    if (take(p, "(")) {
        bool header = p->header;
        p->header = false;
        s->operand = expr(p, 0);
        p->header = header;
        need(p, ")");
    }
    else {
        s->operand = expr(p, 9);
    }
    return s;
}

// One nominal or parenthesized type, before any array suffix. dynamic^ binds
// here, whereas ref^ continues to consume the complete following type.
static Spec *spec_atom(Parser *p)
{
    Spec *s = mem(sizeof(*s));
    s->loc = tok(p)->loc;
    if (take(p, "typeof^")) {
        return typeof_spec(p, s->loc);
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
        int depth = tok(p)->kind == TOK_WORD ? self_type_depth(tok(p)->text) : 0;
        if (depth) {
            s->name = next(p)->text;
            if (depth != 1 || !p->type_owner) {
                fail(s->loc, "'%s' is outside the enclosing type definition", s->name);
            }
            s->self_type = p->type_owner;
        }
        else {
            s->name = qualified_word(p);
            if (type_parameter(p, s->name)) {
                s->parameter_owner = p->type_owner;
            }
        }
        type_arguments(p, s);
    }
    return s;
}

static Spec *spec_part(Parser *p)
{
    Spec *s = mem(sizeof(*s));
    s->loc = tok(p)->loc;
    if (take(p, "typeof^")) {
        int none = 0;
        return array_spec(p, typeof_spec(p, s->loc), &none);
    }
    if (take(p, "read^")) {
        s = spec_part(p);
        if (s->kind != SPEC_SLICE) {
            fail(s->loc,
                 "read^ here needs a slice, as in read^ T[..]; a reference is ref^ read^ T");
        }
        s->count = 1;
        return s;
    }
    int refs = reference_depth(tok(p)->text);
    int dynamics = dynamic_depth(tok(p)->text);
    if (dynamics) {
        next(p);
        bool read = take(p, "read^");
        s = reference_spec(spec_atom(p), s->loc, dynamics, read, true);
        return array_spec(p, s, &refs);
    }
    if (refs) {
        next(p);
        bool read = take(p, "read^");
        return reference_spec(spec_part(p), s->loc, refs, read, false);
    }
    if (take(p, "f^") || take(p, "p^")) {
        s->kind = SPEC_FUNCTION;
        s->procedure = !strcmp(((Token *)p->tokens.v[p->at - 1])->text, "p^");
        if (!is(p, "->") && !is(p, ";")) {
            do {
                Spec *param = spec(p);
                if (!s->procedure) {
                    read_view(param);
                }
                add(&s->params, param);
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
    return array_spec(p, spec_atom(p), &refs);
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

// A numeric subscript before an array-kind bracket denotes an element array type.
static Spec *array_element_spec(Parser *p, Expr *e)
{
    if (e->kind == E_TYPE) {
        return e->type_expr;
    }
    Spec *s = mem(sizeof(*s));
    s->loc = e->loc;
    if (e->kind == E_INDEX && !e->optional && e->b->kind == E_INT) {
        s->kind = SPEC_ARRAY;
        s->elem = array_element_spec(p, e->a);
        errno = 0;
        long count = strtol(e->b->text, NULL, 10);
        if (errno || count <= 0 || count > 1000000) {
            fail(e->loc, "array length out of range (1..1000000)");
        }
        s->count = (int)count;
        return s;
    }
    s->kind = SPEC_NAME;
    s->name = expression_path(e);
    if (!s->name) {
        fail(e->loc, "an array constructor needs an element type");
    }
    int depth = self_type_depth(s->name);
    if (depth) {
        if (depth != 1 || !p->type_owner) {
            fail(e->loc, "'%s' is outside the enclosing type definition", s->name);
        }
        s->self_type = p->type_owner;
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

// Fields after { up to }, with a type annotation, an initializer, or both.
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
        if (take(p, ":")) {
            f->spec = spec(p);
        }
        if (take(p, "=") || take(p, ":=")) {
            f->init = expr(p, 0);
        }
        if (!f->spec && !f->init) {
            fail(f->loc, "field '%s' requires a type annotation or an initializer", f->name);
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
        int first = p->at;
        if (named) {
            add(&e->names, word(p));
            need(p, "=");
        }
        add(&e->args, expr(p, 0));
        if (named) {
            // A named initializer anchors nested values to its field's line.
            format_span(p, first, 3002, false, false);
        }
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

// The contents of an array literal after its '{': elements, or the only
// element v repeat^ n, a copied source..., or a max^ capacity.
static void array_entries(Parser *p, Expr *e)
{
    bool header = p->header;
    p->header = false;
    if (take(p, "max^")) {
        e->text = "reserve";
        add(&e->args, expr(p, 0));
    }
    else if (!is(p, "}")) {
        add(&e->args, expr(p, 0));
        if (take(p, "repeat^")) {
            e->text = "repeat";
            add(&e->args, expr(p, 0));
        }
        else if (take(p, "...")) {
            e->text = "copy";
        }
        else {
            while (take(p, ",") && !is(p, "}")) {
                add(&e->args, expr(p, 0));
            }
        }
    }
    need(p, "}");
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
    format_clause(p, p->at);
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

// Import module name when needed, unless module is it or imports it already.
static void hidden_import(Module *module, bool needed, const char *name, const char *path)
{
    needed = needed && !(module->name && !strcmp(module->name, name));
    for (int j = 0; needed && j < module->imports.n; j++) {
        needed = strcmp(((Import *)module->imports.v[j])->name, name);
    }
    if (needed) {
        Import *i = mem(sizeof(*i));
        i->name = (char *)name;
        i->alias = fmt("\001%s", name);
        i->loc = (Loc){path, 1, 1};
        add(&module->imports, i);
    }
}

static Expr *primary(Parser *p)
{
    int first = p->at;
    Token *t = next(p);
    if (t->kind == TOK_INVALID) {
        tool_abort();
    }
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
    else if (!strcmp(t->text, "[")) {
        fail(t->loc, "array literals are written {a, b} or T[...]{a, b}");
    }
    else if (!strcmp(t->text, "{")) {
        // {name = value} and {} are records; positional {a, b} is an array.
        bool named = is(p, "}") || (tok(p)->kind == TOK_WORD && p->at + 1 < p->tokens.n &&
                                    !strcmp(((Token *)p->tokens.v[p->at + 1])->text, "="));
        e = node(named ? E_RECORD : E_ARRAY, t->loc);
        if (named) {
            entries(p, e, "}", true);
        }
        else {
            array_entries(p, e);
        }
    }
    else if (reference_depth(t->text) || !strcmp(t->text, "-") || !strcmp(t->text, "!") ||
             !strcmp(t->text, "*") || !strcmp(t->text, "**") || !strcmp(t->text, "bitnot^") ||
             !strcmp(t->text, "try^")) {
        format_operator(p, first, FORMAT_OPERATOR_PREFIX);
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
    else if (dynamic_depth(t->text)) {
        p->at--;
        e = node(E_TYPE, t->loc);
        e->type_expr = spec_part(p);
        Spec *s = e->type_expr;
        if (s->kind == SPEC_ARRAY || s->kind == SPEC_SLICE || s->kind == SPEC_GROW ||
            s->kind == SPEC_BOUNDED) {
            if (take(p, "{")) {
                e->kind = E_ARRAY;
                e->type_expr = NULL;
                e->array_literal = s;
                e->array_refs = s->elem->kind == SPEC_REF;
                array_entries(p, e);
            }
            else if (is(p, "(")) {
                e->kind = E_ARRAY;
                e->type_expr = NULL;
                e->test_spec = s;
            }
        }
    }
    else if (!strcmp(t->text, "typeof^")) {
        e = node(E_TYPE, t->loc);
        e->type_expr = typeof_spec(p, t->loc);
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
        // def^.name reaches a member of the enclosing def^, as Self^.name does.
        int depth = !strcmp(t->text, "def^") && is(p, ".") ? 1 : self_type_depth(t->text);
        if (depth || type_parameter(p, t->text)) {
            if (depth && (depth != 1 || !p->type_owner)) {
                fail(t->loc, "'%s' is outside the enclosing type definition", t->text);
            }
            e->kind = E_TYPE;
            e->type_expr = mem(sizeof(Spec));
            *e->type_expr = (Spec){.kind = SPEC_NAME,
                                   .loc = t->loc,
                                   .name = t->text,
                                   .self_type = depth ? p->type_owner : NULL,
                                   .parameter_owner = depth ? NULL : p->type_owner};
        }
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
        if (type_arguments_ahead(p) && type_constructor(p, e)) {
            Spec *s = array_element_spec(p, e);
            type_arguments(p, s);
            e = node(E_TYPE, e->loc);
            e->type_expr = s;
        }
        else if (take(p, ".") || take(p, "?.")) {
            Expr *n = node(E_MEMBER, e->loc);
            n->optional = !strcmp(((Token *)p->tokens.v[p->at - 1])->text, "?.");
            n->a = e;
            n->name_loc = tok(p)->loc;
            n->text = word(p);
            // tostring() and write_text(out) answer with std.text.Builder; trim() and split(t) live
            // in std.text.
            p->text_cast |= !strcmp(n->text, "tostring") || !strcmp(n->text, "write_text") ||
                            !strcmp(n->text, "trim") || !strcmp(n->text, "split");
            e = n;
        }
        else if (is(p, "(") && ((Token *)p->tokens.v[p->at - 1])->loc.line == tok(p)->loc.line) {
            next(p);
            Expr *n = node(E_CALL, e->loc);
            n->a = e;
            entries(p, n, ")", false);
            if (e->kind == E_ARRAY && e->test_spec) {
                Spec *s = e->test_spec;
                bool capped = s->kind == SPEC_GROW && s->count == ARRAY_CAPPED;
                if (capped && (e->text || n->args.n != 1)) {
                    fail(e->loc, "T[max^] construction expects a capacity");
                }
                if (!capped && (e->text || s->kind != SPEC_ARRAY || s->count || n->args.n != 1)) {
                    fail(e->loc, "T[] construction expects a source; write T[]{value repeat^ n} "
                                 "to repeat a value");
                }
                e->text = capped ? "reserve" : "copy";
                e->args = n->args;
                continue;
            }
            e = n;
        }
        else if (is(p, "[") && array_type_at(p, p->at)) {
            Spec *base = array_element_spec(p, e);
            Expr *array = node(E_ARRAY, e->loc);
            int refs;
            Spec *s = array_spec(p, base, &refs);
            if (take(p, "{")) {
                array->array_literal = s;
                array->array_refs = refs;
                array_entries(p, array);
            }
            else if (is(p, "(")) {
                array->test_spec = s;
            }
            else {
                fail(array->loc, "an array type here needs {elements} or (arguments)");
            }
            e = array;
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
            if (e->kind == E_TYPE) {
                n->type_expr = e->type_expr;
            }
            else {
                n->text = expression_path(e);
            }
            entries(p, n, "}", true);
            e = n;
        }
        else {
            break;
        }
    }
    format_span(p, first, 100 + e->kind, false, false);
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
    if (!strcmp(s, "::")) {
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
        format_operator(p, p->at, FORMAT_OPERATOR_BINARY);
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
                format_operator(p, p->at, FORMAT_OPERATOR_BINARY);
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
            binary->b = expr(p, pr + (!strcmp(t->text, "::") || !strcmp(t->text, "**") ? 0 : 1));
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

// An expression, at the top level also a range a .. b or a to^ b. A
// header's to^ belongs to its loop or arm instead. An omitted end must
// stand next to a delimiter, so that a range never runs on to the next
// line's expression.
static Expr *expr(Parser *p, int min)
{
    bool open_start = !min && !p->header && is(p, "..");
    if (open_start && !range_delimiter(p, -1, true)) {
        fail(tok(p)->loc, "a range without its start goes right after (, [ or ,; write (.. n)");
    }
    Expr *e = open_start ? NULL : operation(p, min);
    if (min || p->header || !(is(p, "..") || is(p, "to^"))) {
        return e;
    }
    Token *t = next(p);
    Expr *r = node(E_RANGE, e ? e->loc : t->loc);
    r->text = t->text;
    r->a = e;
    if (!range_delimiter(p, 0, false)) {
        if (tok(p)->loc.line != t->loc.line) {
            fail(t->loc, "a range without its end goes right before ), ] or ,; write (n ..)");
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

// Whether word can only begin a statement, never a literal's first element.
static bool statement_word(const char *word)
{
    const char *words[] = {"let^",   "var^",  "return^", "if^",  "for^",   "repeat^",
                           "break^", "next^", "do^",     "def^", "print^", "inline^"};
    for (size_t j = 0; j < sizeof(words) / sizeof(*words); j++) {
        if (!strcmp(word, words[j])) {
            return true;
        }
    }
    return false;
}

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

static Stmt *binding(char *name, Expr *value, bool mut);
static Stmt *with_statement(Parser *p, Loc loc);

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
        format_clause(p, p->at);
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
                         "::=", "?:=", "?+=", "?-=", "?*=", "?/=", "?%="};
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

// | continues only a type, so meeting one after the value means the value was the union's first
// alternative: read the whole again as a type.
static Expr *binding_value(Parser *p)
{
    int start = p->at;
    Expr *e = expr(p, 0);
    if (!is(p, "|")) {
        return e;
    }
    p->at = start;
    e = node(E_TYPE, tok(p)->loc);
    e->type_expr = spec(p);
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
        format_clause(p, p->at - 1);
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
        format_clause(p, p->at - 1);
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
        format_clause(p, p->at - 1);
        Expr *e = node(E_IF, tok(p)->loc);
        e->a = match_condition(p, name);
        e->b = expr(p, 0);
        *arm = e;
        arm = &e->c;
        first = first ? first : e;
    }
    if (take(p, "other^") || take(p, "el^") || take(p, "else^")) {
        format_clause(p, p->at - 1);
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
            format_clause(p, p->at);
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

// A loop header ends at {, so its range is read here rather than by expr.
static Expr *loop_range(Parser *p)
{
    Expr *e = expr(p, 0);
    if (!is(p, "..") && !is(p, "to^")) {
        return e;
    }
    Token *t = next(p);
    if (is(p, "{")) {
        fail(t->loc, "a range without its end goes in parentheses; write (n ..)");
    }
    Expr *r = node(E_RANGE, e->loc);
    r->text = t->text;
    r->a = e;
    r->b = operation(p, 0);
    return r;
}

// for^ names in^, with one or more names.
static bool for_in_ahead(Parser *p)
{
    for (int at = p->at; at + 1 < p->tokens.n; at += 2) {
        Token *name = p->tokens.v[at], *after = p->tokens.v[at + 1];
        if (name->kind != TOK_WORD) {
            return false;
        }
        if (strcmp(after->text, ",")) {
            return !strcmp(after->text, "in^");
        }
    }
    return false;
}

static Expr *for_in_part(char *op, char *source, Expr *value, char *other, Loc loc)
{
    Expr *e = node(E_LOOP_PART, loc);
    e->text = op;
    e->a = value ? value : named(source, loc);
    e->b = other ? named(other, loc) : NULL;
    return e;
}

static Stmt *for_in_stop(Expr *condition, Loc loc)
{
    Stmt *stop = mem(sizeof(*stop)), *out = mem(sizeof(*out));
    stop->kind = S_BREAK;
    stop->loc = loc;
    stop->depth = 1;
    out->kind = S_IF;
    out->loc = loc;
    out->a = condition;
    add(&out->body, stop);
    return out;
}

// for^ names in^ value, whatever value turns out to be. A cursor answers nil^ when it runs out;
// a range or an array is walked by a counter. Each turn takes an item, stops if there is none and
// binds the names, so what differs by type is left to E_LOOP_PART.
static void for_in_statement(Parser *p, Stmt *scope, Stmt *loop, List *names, Expr *value, Loc loc)
{
    char *source = fmt("\001source%d", ++p->serial), *counter = fmt("\001counter%d", p->serial);
    char *item = fmt("\001item%d", p->serial);
    add(&scope->body, binding(source, for_in_part("iter", NULL, value, NULL, value->loc), true));
    add(&scope->body, binding(counter, for_in_part("start", source, NULL, NULL, loc), true));
    add(&loop->pre, binding(item, for_in_part("next", source, NULL, counter, loc), false));
    Expr *none = node(E_FITS, loc);
    none->a = named(item, loc);
    none->test_spec = mem(sizeof(Spec));
    *none->test_spec = (Spec){.kind = SPEC_NAME, .loc = loc, .name = "nil^"};
    add(&loop->pre, for_in_stop(none, loc));
    add(&loop->pre, for_in_stop(for_in_part("done", source, NULL, item, loc), loc));
    if (names->n == 1) {
        add(&loop->pre, binding(names->v[0], for_in_part("item", source, NULL, item, loc), false));
    }
    else {
        // let^ k, v = item: the same shape a written destructuring parses to.
        List values = {0};
        add(&values, named(item, loc));
        Stmt *many = mem(sizeof(*many));
        many->kind = S_BIND_MANY;
        many->loc = loc;
        add(&many->prolog, results(p, &values, names->n, loc));
        for (int j = 0; j < names->n; j++) {
            Stmt *part = binding(names->v[j], values.v[j], false);
            part->loc = loc;
            add(&many->body, part);
        }
        add(&loop->pre, many);
    }
    add(&loop->update,
        assignment(named(counter, loc), contextual(number("1", loc), named(counter, loc)), "+"));
    loop->a = node(E_BOOL, loc);
    loop->a->text = "true^";
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
    else if (!repeat && for_in_ahead(p)) {
        List names = {0};
        do {
            add(&names, take(p, "_^") ? "_^" : decl_word(p));
        } while (take(p, ","));
        need(p, "in^");
        Expr *value = loop_range(p);
        if (is(p, "step^") || is(p, "downto^")) {
            fail(tok(p)->loc, "for^ in^ walks a range^ upward by one; use from^ for %s",
                 tok(p)->text);
        }
        if (value->kind != E_RANGE) {
            for_in_statement(p, scope, loop, &names, value, loc);
            p->header = header;
            loop_body(p, loop);
            if (loop->last.n) {
                // The names are bound per turn, so last^ could not see them.
                fail(loc,
                     "last^ needs a range written in the header; for^ in^ over a value has none");
            }
            add(&scope->body, loop);
            return scope;
        }
        if (names.n != 1) {
            fail(loc, "a range^ gives one value at a time, not %d", names.n);
        }
        char *focus = names.v[0];
        // A range written here keeps the plain counting shape the optimizer knows.
        char *range = fmt("\001range%d", ++p->serial);
        counter = fmt("\001counter%d", p->serial);
        add(&scope->body, binding(range, value, false));
        Expr *start = node(E_MEMBER, value->loc), *contains = node(E_MEMBER, loc);
        start->a = contains->a = named(range, loc);
        start->text = "\001start"; // Unlike .start, rejects anything but a range^.
        contains->text = "contains";
        add(&scope->body, binding(counter, start, true));
        add(&scope->body, binding(focus, named(counter, loc), false));
        add(&loop->focuses, focus);
        loop->a = node(E_CALL, loc);
        loop->a->a = contains;
        add(&loop->a->args, named(counter, loc));
        Stmt *refresh = assignment(named(focus, loc), named(counter, loc), NULL);
        refresh->internal = true;
        add(&loop->pre, refresh);
        add(&loop->update, assignment(named(counter, loc),
                                      contextual(number("1", loc), named(counter, loc)), "+"));
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
            if (is(p, "in^")) {
                fail(tok(p)->loc,
                     "for^ in^ counts in the range's type; use from^ to choose another type");
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

static Stmt *stmt_unchecked(Parser *p)
{
    Token *t = tok(p);
    if (is(p, "catch^")) {
        fail(t->loc, "catch^ clause must end an enclosing block; use do^{...}");
    }
    if (branch_word(p)) {
        fail(t->loc, "%s belongs inside the preceding if^ braces", t->text);
    }
    if (is(p, "overload^")) {
        fail(t->loc, "overload^ declares top-level functions only");
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
            add(&values, binding_value(p));
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
            if (!s->mut && (s->a->kind == E_TYPE || type_constructor(p, s->a) ||
                            (s->a->kind == E_NAME && numeric_kind(s->a->text) >= 0))) {
                add(&p->type_names, s->name);
            }
        }
        else {
            s->kind = S_BIND_MANY;
            s->body = bindings;
        }
    }
    else if (take(p, "inline^")) {
        need(p, "for^");
        s->kind = S_INLINE;
        s->name = take(p, "it^") ? "it^" : take(p, "_^") ? "_^" : decl_word(p);
        need(p, "in^");
        bool header = p->header;
        p->header = true;
        s->a = expr(p, 0);
        p->header = header;
        block(p, &s->body);
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
    else if (is(p, "{") && p->at + 1 < p->tokens.n &&
             statement_word(((Token *)p->tokens.v[p->at + 1])->text)) {
        // Otherwise a leading '{' is a record or array literal.
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

static Stmt *stmt(Parser *p)
{
    if (!p->p->tooling) {
        if (!p->p->format) {
            return stmt_unchecked(p);
        }
        int first = p->at;
        Stmt *out = stmt_unchecked(p);
        Token *head = p->tokens.v[first];
        bool row = !strcmp(head->text, "for^") && first + 1 < p->at &&
                   (!strcmp(((Token *)p->tokens.v[first + 1])->text, "let^") ||
                    !strcmp(((Token *)p->tokens.v[first + 1])->text, "var^"));
        format_span(p, first, 2000 + out->kind, false, row);
        return out;
    }
    int start = p->at, depth = p->scope_depth;
    bool header = p->header;
    Function *enclosing = p->enclosing;
    Def *type_owner = p->type_owner;
    ToolRecovery recovery;
    tool_enter(&recovery);
    if (setjmp(recovery.escape)) {
        synchronize(p, start, p->at, depth == 0);
        p->scope_depth = depth;
        p->header = header;
        p->enclosing = enclosing;
        p->type_owner = type_owner;
        Token *first = p->tokens.v[start];
        Stmt *bad = mem(sizeof(*bad));
        bad->kind = S_EXPR;
        bad->loc = first->loc;
        bad->a = node(E_NAME, first->loc);
        bad->a->invalid = true;
        bad->a->text = "<invalid>";
        if ((!strcmp(first->text, "let^") || !strcmp(first->text, "var^")) &&
            start + 1 < p->tokens.n) {
            Token *name = p->tokens.v[start + 1];
            if (name->kind == TOK_WORD && !strchr(name->text, '^')) {
                bad->kind = S_BIND;
                bad->name = name->text;
                bad->loc = name->loc;
            }
        }
        tool_leave(&recovery);
        return bad;
    }
    Stmt *out = stmt_unchecked(p);
    tool_leave(&recovery);
    return out;
}

static void block(Parser *p, List *out)
{
    int first = p->at;
    need(p, "{");
    p->scope_depth++;
    while (!is(p, "}") && !catch_head(p)) {
        if (tok(p)->kind == TOK_EOF) {
            TOOL_CHECK(p->p, fail(tok(p)->loc, "unterminated block"););
            break;
        }
        if (take(p, ";")) {
            continue;
        }
        add(out, stmt(p));
    }
    catches(p, out, false);
    if (tok(p)->kind != TOK_EOF || !p->p->tooling) {
        need(p, "}");
    }
    p->scope_depth--;
    format_span(p, first, 4000, true, false);
}

static Function *function(Parser *p, char *name, Def *owner, bool abstr)
{
    int first = p->at;
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
                if (!f->procedure) {
                    read_view(a->spec);
                }
            }
            if (!strcmp(a->name, "...")) {
                if (!a->spec) {
                    if (!templ) {
                        fail(a->loc, "variadic parameter needs an element type, as in ...:T; "
                                     "untyped ... requires template^");
                    }
                    f->heterogeneous = true;
                }
                else {
                    Spec *slice = mem(sizeof(*slice));
                    slice->kind = SPEC_SLICE;
                    slice->count = 1; // The elements are a temporary or someone else's.
                    slice->loc = a->spec->loc;
                    slice->elem = a->spec;
                    a->spec = slice;
                }
                f->variadic = true;
            }
            if (!a->self && !a->spec && abstr && !templ) {
                fail(a->loc, "bodiless declaration parameter requires a type annotation");
            }
            add(&f->params, a);
            if (is(p, ",")) {
                FormatToken *separator = format_token(p, p->at);
                if (separator) {
                    separator->parameter_separator = true;
                }
            }
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
    format_span(p, first, 1000, false, false);
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

static void require_new_function_name(Module *module, const char *name, Loc loc)
{
    for (int j = 0; j < module->functions.n; j++) {
        Function *previous = module->functions.v[j];
        if (!previous->overload && !strcmp(previous->name, name)) {
            fail(loc, "function '%s' is already declared; add overloads with & or overload^", name);
        }
    }
}

static void definition(Parser *p, bool structure)
{
    Def *d = mem(sizeof(*d));
    d->loc = tok(p)->loc;
    d->name = decl_word(p);
    if (take(p, "<")) {
        do {
            Loc at = tok(p)->loc;
            char *name = decl_word(p);
            for (int j = 0; j < d->type_params.n; j++) {
                if (!strcmp(name, d->type_params.v[j])) {
                    fail(at, "duplicate type parameter '%s'", name);
                }
            }
            add(&d->type_params, name);
        } while (take(p, ","));
        type_close(p);
    }
    d->id = p->p->defs.n;
    add(&p->p->defs, d);
    if (structure) {
        need(p, "{");
        Def *outer = p->type_owner;
        p->type_owner = d;
        fields(p, d);
        p->type_owner = outer;
        return;
    }
    if (take(p, "=")) {
        Def *outer = p->type_owner;
        p->type_owner = d;
        do {
            add(&d->union_specs, spec_part(p));
        } while (take(p, "|"));
        p->type_owner = outer;
        take(p, ";");
        return;
    }
    if (take(p, ":")) {
        Def *outer = p->type_owner;
        p->type_owner = d;
        do {
            add(&d->base_specs, spec_part(p));
        } while (take(p, ","));
        p->type_owner = outer;
    }
    def_body(p, d);
}

// A def^ body from its {: fields in self^{ ... } and methods outside.
static void def_member(Parser *p, Def *d)
{
    if (tok(p)->kind == TOK_EOF) {
        fail(tok(p)->loc, "unterminated def^");
    }
    if (take(p, ",") || take(p, ";")) {
        return;
    }
    if (take(p, "self^")) {
        need(p, "{");
        fields(p, d);
        return;
    }
    bool priv = false, abstr = false, over = false, overload = false;
    while (is(p, "private^") || is(p, "abstract^") || is(p, "override^") || is(p, "overload^")) {
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
    if (take(p, "var^")) {
        Field *f = mem(sizeof(*f));
        f->loc = tok(p)->loc;
        f->owner = f->initializer_owner = d;
        f->priv = priv;
        f->name = decl_word(p);
        if (abstr || over || overload) {
            fail(f->loc, "static variables cannot be abstract^, override^ or overload^");
        }
        if (take(p, ":")) {
            f->spec = spec(p);
        }
        if (take(p, "=") || take(p, ":=")) {
            f->init = expr(p, 0);
        }
        if (!f->spec && !f->init) {
            fail(f->loc, "static variable '%s' requires a type annotation or an initializer",
                 f->name);
        }
        add(&d->state_fields, f);
        return;
    }
    Loc declaration = tok(p)->loc;
    char *name;
    if (take(p, "op^")) {
        Token *op = next(p);
        const char *allowed[] = {"+",  "-",  "*",      "/",    "%",   "//",
                                 "**", "::", "cross^", "dot^", "<=>", "="};
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
    if (!abstr && !is(p, "f^") && !is(p, "p^") && !is(p, "template^") && !is(p, "mutable^")) {
        if (over || overload || !strncmp(name, "op^", 3)) {
            fail(declaration, "type constants cannot override or overload methods");
        }
        Constant *constant = mem(sizeof(*constant));
        Stmt *binding = mem(sizeof(*binding));
        binding->kind = S_BIND;
        binding->loc = declaration;
        binding->name = name;
        binding->a = expr(p, 0);
        constant->name = name;
        constant->binding = binding;
        constant->owner = d;
        constant->public = !priv;
        add(&d->constants, constant);
        return;
    }
    Function *f = function(p, name, d, abstr);
    f->declaration = declaration;
    f->priv = priv;
    f->override = over;
    f->overload = overload;
    f->static_member = !f->operator_member && (!f->params.n || !((Param *)f->params.v[0])->self);
    if (f->static_member && (abstr || over)) {
        fail(f->loc, "static member '%s' cannot be abstract^ or override^", name);
    }
    if (f->operator_member) {
        int self_count = 0;
        for (int k = 0; k < f->params.n; k++) {
            self_count += ((Param *)f->params.v[k])->self;
        }
        bool unary = !strcmp(name, "op^-") && f->params.n == 1;
        if (f->procedure || f->mutable_self || self_count != 1 || (!unary && f->params.n != 2)) {
            fail(f->loc, "operator requires a pure f^ with self^ and one operand (or unary '-')");
        }
    }
    add(&d->own_methods, f);
}

static void def_member_checked(Parser *p, Def *d)
{
    if (!p->p->tooling) {
        int first = p->at;
        def_member(p, d);
        // A member's name anchors its body even when f^ starts mid-line.
        format_span(p, first, 3001, false, false);
        return;
    }
    int start = p->at, depth = p->scope_depth;
    bool header = p->header, fields = is(p, "self^");
    Function *enclosing = p->enclosing;
    Def *type_owner = p->type_owner;
    ToolRecovery recovery;
    tool_enter(&recovery);
    if (setjmp(recovery.escape)) {
        synchronize(p, start, p->at, false);
        p->scope_depth = depth;
        p->header = header;
        p->enclosing = enclosing;
        p->type_owner = type_owner;
        d->invalid |= fields;
    }
    else {
        def_member(p, d);
    }
    tool_leave(&recovery);
}

// An editor can still resolve the remaining methods after a damaged member.
static void def_body(Parser *p, Def *d)
{
    need(p, "{");
    Def *outer = p->type_owner;
    p->type_owner = d;
    while (!take(p, "}")) {
        if (p->p->tooling && (is(p, "let^") || is(p, "def^") || is(p, "struct^") ||
                              is(p, "import^") || is(p, "public^"))) {
            TOOL_CHECK(p->p, fail(tok(p)->loc, "expected '}' before a top-level declaration"););
            break;
        }
        if (tok(p)->kind == TOK_EOF) {
            TOOL_CHECK(p->p, fail(tok(p)->loc, "unterminated def^"););
            break;
        }
        def_member_checked(p, d);
    }
    p->type_owner = outer;
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

static void declaration(Parser *q)
{
    Program *p = q->p;
    Module *module = q->module;
    if (take(q, ";")) {
        return;
    }
    bool public = take(q, "public^");
    // exportC^ and exportL^ mark a function entered from C or from L^;
    // exportL^ also marks a def^ L^ holds by value, or with ref^ by
    // reference, and an errordef^ L^ receives as its own errors.
    Loc export_at = tok(q)->loc;
    bool exported = false, lhat_export = false;
    for (;;) {
        if (take(q, "exportC^")) {
            exported = true;
        }
        else if (take(q, "exportL^")) {
            lhat_export = true;
        }
        else {
            break;
        }
    }
    bool lhat_ref = lhat_export && take(q, "ref^");
    bool lhat_def = lhat_export && !exported &&
                    (is(q, "def^") || is(q, "struct^") || (is(q, "errordef^") && !lhat_ref) ||
                     (is(q, "externC^") && q->at + 1 < q->tokens.n &&
                      (!strcmp(((Token *)q->tokens.v[q->at + 1])->text, "def^") ||
                       !strcmp(((Token *)q->tokens.v[q->at + 1])->text, "struct^"))));
    if ((exported || lhat_export) && !lhat_def && (lhat_ref || !is(q, "let^"))) {
        fail(export_at, "exportC^ and exportL^ apply only to a let^ function declaration"
                        " (or exportL^ to a def^ or errordef^)");
    }
    if (take(q, "import^") || (is(q, "let^") && q->at + 3 < q->tokens.n &&
                               !strcmp(((Token *)q->tokens.v[q->at + 2])->text, "=") &&
                               !strcmp(((Token *)q->tokens.v[q->at + 3])->text, "import^"))) {
        if (public) {
            fail(tok(q)->loc, "imports cannot be public^");
        }
        Import *i = mem(sizeof(*i));
        i->loc = tok(q)->loc;
        if (take(q, "let^")) {
            i->alias = decl_word(q);
            need(q, "=");
            need(q, "import^");
        }
        i->name = qualified_word(q);
        add(&module->imports, i);
        take(q, ";");
        return;
    }
    if (take(q, "enum^")) {
        q->enum_cast = true; // Mode(n) fails with std.sys's error.
        enum_declaration(q, p, module, public);
        return;
    }
    if (take(q, "errordef^")) {
        Def *group = mem(sizeof(*group));
        group->loc = tok(q)->loc;
        group->name = decl_word(q);
        group->module = module;
        group->public = public;
        group->lhat_error = lhat_def;
        group->id = p->defs.n;
        add(&p->defs, group);
        add(&module->defs, group);
        need(q, "{");
        do {
            if (is(q, "}")) {
                break;
            }
            Def *arm = mem(sizeof(*arm));
            arm->loc = tok(q)->loc;
            arm->name = fmt("%s.%s", group->name, decl_word(q));
            arm->module = module;
            arm->public = public;
            arm->group = group;
            error_variant(q, arm);
            arm->id = p->defs.n;
            add(&p->defs, arm);
            add(&module->defs, arm);
            Spec *alternative = mem(sizeof(*alternative));
            alternative->kind = SPEC_NAME;
            alternative->loc = arm->loc;
            alternative->name = arm->name;
            add(&group->union_specs, alternative);
        } while (take(q, ","));
        need(q, "}");
        if (!group->union_specs.n) {
            fail(group->loc, "errordef^ requires a variant");
        }
        return;
    }
    if (is(q, "includeC^")) {
        Loc at = next(q)->loc;
        Token *header = next(q);
        if (header->kind != TOK_STRING || public) {
            fail(at, "includeC^ takes a header name string and cannot be public^");
        }
        add(&module->c_headers, header->text);
        take(q, ";");
        return;
    }
    if (take(q, "externC^")) {
        bool structure = take(q, "struct^");
        if (!structure && !take(q, "def^")) {
            Loc declaration = tok(q)->loc;
            char *name = decl_word(q);
            require_new_function_name(module, name, declaration);
            Function *f = external_function(q, name, name);
            f->declaration = declaration;
            f->module = module;
            f->public = public;
            add(&module->functions, f);
            add(&p->functions, f);
            return;
        }
        // A def^ laid out as the C type of the same name, or opaque.
        Loc loc = tok(q)->loc;
        int start = q->at;
        char *name = decl_word(q);
        Def *d = NULL;
        if (!structure && take(q, ";")) {
            d = mem(sizeof(*d));
            *d = (Def){.loc = loc, .name = name, .id = p->defs.n, .opaque = true};
            add(&p->defs, d);
        }
        else {
            q->at = start;
            definition(q, structure);
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
        return;
    }
    bool structure = take(q, "struct^");
    if (structure || take(q, "def^")) {
        definition(q, structure);
        Def *d = p->defs.v[p->defs.n - 1];
        d->module = module;
        d->public = public;
        d->lhat_value = lhat_def && !lhat_ref;
        d->lhat_data = lhat_ref;
        add(&module->defs, d);
        return;
    }
    // overload^ adds arms to the name its one let^ declares, under its visibility.
    bool overload = is(q, "overload^");
    if (overload && public) {
        fail(export_at, "overload^ arms take the visibility of their let^ declaration");
    }
    bool binding = false;
    if ((overload || is(q, "let^")) && q->at + 3 < q->tokens.n) {
        Token *equal = q->tokens.v[q->at + 2];
        Token *initializer = q->tokens.v[q->at + 3];
        binding =
            !strcmp(equal->text, "=") && initializer->kind == TOK_WORD &&
            (!strcmp(initializer->text, "f^") || !strcmp(initializer->text, "p^") ||
             !strcmp(initializer->text, "externC^") || !strcmp(initializer->text, "externLLVM^") ||
             !strcmp(initializer->text, "template^"));
    }
    if ((exported || lhat_export) && (!binding || q->at + 3 >= q->tokens.n ||
                                      strstr(((Token *)q->tokens.v[q->at + 3])->text, "extern"))) {
        fail(export_at, "exportC^ and exportL^ apply only to a let^ function declaration");
    }
    if (overload && !binding) {
        fail(tok(q)->loc, "overload^ takes NAME = followed by a function");
    }
    if (!binding) {
        Stmt *s = stmt(q);
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
                c->name = discard ? fmt("\001constant_discard%d", ++q->serial) : value->name;
                c->binding = value;
                c->public = public && !discard;
                add(&module->constants, c);
            }
        }
        else {
            s->public_binding = public;
            add(&p->entry->body, s);
        }
        return;
    }
    int binding_start = q->at;
    next(q);
    Loc declaration = tok(q)->loc;
    char *name = decl_word(q);
    need(q, "=");
    bool first_arm = true;
    do {
        Function *parsed_function = NULL;
        if (take(q, "externC^")) {
            parsed_function = external_function(q, name, word(q));
        }
        else if (take(q, "externLLVM^")) {
            parsed_function = external_function(q, name, qualified_word(q));
            parsed_function->intrinsic = true;
        }
        else {
            parsed_function = function(q, name, NULL, false);
            // A called literal initializes a value; it does not declare a named
            // function.
            if (first_arm && (is(q, "(") || is(q, ".") || is(q, "[") ||
                              (tok(q)->kind != TOK_STRING && prec(tok(q)->text) >= 0))) {
                if (overload) {
                    fail(tok(q)->loc, "overload^ takes a function, not a value");
                }
                q->at = binding_start;
                add(&p->entry->body, stmt(q));
                break;
            }
        }
        if (first_arm && !overload) {
            require_new_function_name(module, name, declaration);
        }
        parsed_function->overload = overload;
        parsed_function->declaration = first_arm ? declaration : parsed_function->loc;
        parsed_function->module = module;
        parsed_function->public = public;
        parsed_function->exported = exported;
        parsed_function->lhat_export = lhat_export;
        add(&module->functions, parsed_function);
        add(&p->functions, parsed_function);
        first_arm = false;
    } while (take(q, "&"));
    take(q, ";");
}

static void declaration_checked(Parser *q)
{
    if (!q->p->tooling) {
        int first = q->at;
        declaration(q);
        format_span(q, first, 3000, false, false);
        return;
    }
    int start = q->at, defs = q->p->defs.n, depth = q->scope_depth;
    Function *enclosing = q->enclosing;
    Def *type_owner = q->type_owner;
    ToolRecovery recovery;
    tool_enter(&recovery);
    if (setjmp(recovery.escape)) {
        synchronize(q, start, q->at, true);
        q->scope_depth = depth;
        q->header = false;
        q->enclosing = enclosing;
        q->type_owner = type_owner;
        // Keep the name of a damaged definition so its uses do not cascade.
        for (int j = defs; j < q->p->defs.n; j++) {
            Def *d = q->p->defs.v[j];
            d->invalid = true;
            if (!d->module) {
                d->module = q->module;
                add(&q->module->defs, d);
            }
        }
    }
    else {
        declaration(q);
    }
    tool_leave(&recovery);
}

void parse_file(Program *p, const char *path, Module *module)
{
    module->program = p;
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
    format_tokens(&q);
    for (int j = 0; j + 1 < q.tokens.n; j++) {
        Token *a = q.tokens.v[j], *b = q.tokens.v[j + 1];
        if (!strcmp(a->text, "def^") || !strcmp(a->text, "struct^")) {
            add(&q.type_names, b->text);
        }
        else if (!strcmp(a->text, "errordef^")) {
            add(&q.error_names, b->text);
        }
    }
    p->entry = mem(sizeof(*p->entry));
    p->entry->loc = (Loc){path, 1, 1};
    p->entry->name = "<top-level>";
    p->entry->procedure = true;
    module->entry = p->entry;
    if (module->root) {
        // The entry file is the body of p^ ...:string^ over the command arguments.
        Spec *text = mem(sizeof(*text)), *slice = mem(sizeof(*slice));
        *text = (Spec){.kind = SPEC_NAME, .loc = p->entry->loc, .name = "string^"};
        *slice = (Spec){.kind = SPEC_SLICE, .count = 1, .loc = p->entry->loc, .elem = text};
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
        declaration_checked(&q);
    }
    // An overload^ arm, wherever written, joins the let^ of its name.
    for (int j = 0; j < module->functions.n; j++) {
        Function *f = module->functions.v[j], *base = NULL;
        for (int k = 0; f->overload && !base && k < module->functions.n; k++) {
            Function *g = module->functions.v[k];
            base = !g->overload && !strcmp(g->name, f->name) ? g : NULL;
        }
        if (base) {
            f->public = base->public;
            f->exported = base->exported;
            f->lhat_export = base->lhat_export;
        }
        else if (f->overload) {
            TOOL_CHECK(p, fail(f->loc, "overload^ has no existing function '%s'", f->name););
        }
    }

    // Casts fail with std.text's or std.sys's errors, imported out of sight.
    hidden_import(module, q.text_cast, "std.text", path);
    hidden_import(module, q.enum_cast, "std.sys", path);
    // Disposable is written bare everywhere, unless a module declares its own.
    bool own = false;
    for (int j = 0; j < module->defs.n; j++) {
        own |= !strcmp(((Def *)module->defs.v[j])->name, "Disposable");
    }
    for (int j = 0; !own && j < module->defs.n; j++) {
        List *bases = &((Def *)module->defs.v[j])->base_specs;
        for (int k = 0; k < bases->n; k++) {
            Spec *base = bases->v[k];
            if (base->kind == SPEC_NAME && base->name && !strcmp(base->name, "Disposable")) {
                base->name = "\001std.disposable.Disposable";
                hidden_import(module, true, "std.disposable", path);
            }
        }
    }
}
