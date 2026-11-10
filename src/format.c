// Adapted from L^'s token-preserving formatter, using R^'s own parser.
#if !defined(_WIN32)
#define _XOPEN_SOURCE 700
#endif
#include "format.h"
#include "tooling.h"
#include <setjmp.h>

void rhat_format_defaults(RhatFormatOptions *options)
{
    options->indent_with_tabs = false;
    options->indent_size = 4;
    options->tab_width = 4;
    options->max_line_length = 100;
    options->end_of_line = RHAT_EOL_KEEP;
    options->final_newline = true;
    options->brace_next_line = false;
    options->space_after_hat = false;
}

// ---------------------------------------------------------------------------
// Growable arrays
// ---------------------------------------------------------------------------

static bool grow(void **items, size_t *capacity, size_t count, size_t size)
{
    if (count < *capacity) {
        return true;
    }
    size_t wanted = *capacity < 16 ? 16 : *capacity * 2;
    void *grown = realloc(*items, wanted * size);
    if (grown == NULL) {
        return false;
    }
    *items = grown;
    *capacity = wanted;
    return true;
}

typedef struct {
    char *bytes;
    size_t length;
    size_t capacity;
    bool failed;
} Buffer;

static void put(Buffer *b, const char *bytes, size_t length)
{
    while (!b->failed && b->length + length + 1 > b->capacity) {
        b->failed = !grow((void **)&b->bytes, &b->capacity, b->capacity + 1, 1);
    }
    if (!b->failed) {
        memcpy(b->bytes + b->length, bytes, length);
        b->length += length;
        b->bytes[b->length] = '\0';
    }
}

static void put_repeated(Buffer *b, char c, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        put(b, &c, 1);
    }
}

// Each request owns its parser allocations, including failed parses.
typedef union Allocation Allocation;

union Allocation {
    long double align;

    struct {
        Allocation *next, *previous;
    } link;
};

typedef struct {
    Allocation *head;
    jmp_buf *escape;
    bool oom;
} Session;

static _Thread_local Session *session;

static void *allocate(size_t size)
{
    Allocation *a = calloc(1, sizeof(*a) + size);
    if (!a) {
        session->oom = true;
        longjmp(*session->escape, 1);
    }
    a->link.next = session->head;
    if (session->head) {
        session->head->link.previous = a;
    }
    session->head = a;
    return a + 1;
}

static void release(void *pointer)
{
    if (!pointer) {
        return;
    }
    Allocation *a = (Allocation *)pointer - 1;
    if (a->link.previous) {
        a->link.previous->link.next = a->link.next;
    }
    else {
        session->head = a->link.next;
    }
    if (a->link.next) {
        a->link.next->link.previous = a->link.previous;
    }
    free(a);
}

static void *resize(void *pointer, size_t size)
{
    if (!pointer) {
        return allocate(size);
    }
    Allocation *a = realloc((Allocation *)pointer - 1, sizeof(*a) + size);
    if (!a) {
        session->oom = true;
        longjmp(*session->escape, 1);
    }
    if (a->link.previous) {
        a->link.previous->link.next = a;
    }
    else {
        session->head = a;
    }
    if (a->link.next) {
        a->link.next->link.previous = a;
    }
    return a + 1;
}

static void syntax_error(Loc loc, const char *message)
{
    (void)loc;
    (void)message;
    longjmp(*session->escape, 1);
}

static void abandon(void)
{
    longjmp(*session->escape, 1);
}

typedef struct {
    const char *text;
    size_t length;
    FormatSyntax syntax;
} Reading;

static RhatFormatStatus reading_init(Reading *r, const char *text, size_t length)
{
    memset(r, 0, sizeof(*r));
    r->text = text;
    r->length = length;
    jmp_buf escape;
    jmp_buf *previous = session->escape;
    session->escape = &escape;
    if (setjmp(escape)) {
        session->escape = previous;
        return session->oom ? RHAT_FORMAT_OUT_OF_MEMORY : RHAT_FORMAT_SYNTAX_ERROR;
    }
    Program *p = mem(sizeof(*p));
    Module *m = mem(sizeof(*m));
    m->root = true;
    p->format = &r->syntax;
    ToolSource *source = mem(sizeof(*source));
    source->path = "<format>";
    source->text = mem(length + 1);
    memcpy(source->text, text, length);
    r->text = source->text;
    add(&p->overlays, source);
    parse_file(p, source->path, m);
    session->escape = previous;
    return RHAT_FORMAT_OK;
}

typedef enum { UNIT_TOKEN, UNIT_INTERP, UNIT_LINE_COMMENT, UNIT_BLOCK_COMMENT } UnitKind;

typedef struct {
    UnitKind kind;
    FormatToken token; // UNIT_TOKEN only
    uint32_t start, end;
    uint32_t breaks;     // line breaks before it: 0, 1 or 2 (a blank line)
    uint32_t written;    // spaces the input had before it on the same line
    uint32_t space;      // spaces to write before it when it is not first
    int32_t match;       // the other bracket of a pair, or -1
    int32_t brace;       // the innermost '{' it stands inside, or -1
    int32_t label_owner; // [lsp.format.labels]: where its construct starts, or -1
    bool block_open;     // the '{' a block's body opens with
    // Worked out by layout().
    size_t line_start; // the unit its line begins with
    uint32_t depth;    // line starts only
} Unit;

typedef struct {
    size_t open, close;
    bool indents; // false: a row of statements, which is not nested in anything
} Region;

typedef struct {
    const RhatFormatOptions *options;
    const char *text;
    Unit *units;
    size_t unit_count, unit_capacity;
    Region *regions;
    size_t region_count, region_capacity;
    size_t *stack;   // the regions open at a unit, innermost last
    uint32_t *bases; // beside each: the depth its contents are one deeper than
    bool failed;
} Format;

static bool unit_is_op(const Unit *u, const char *op)
{
    return u->kind == UNIT_TOKEN && u->token.kind == FORMAT_OP && !strcmp(u->token.text, op);
}

static bool unit_is_comment(const Unit *u)
{
    return u->kind == UNIT_LINE_COMMENT || u->kind == UNIT_BLOCK_COMMENT;
}

static bool unit_opens(const Unit *u)
{
    return unit_is_op(u, "(") || unit_is_op(u, "[") || unit_is_op(u, "{") || unit_is_op(u, "?(") ||
           unit_is_op(u, "?[");
}

static bool unit_closes(const Unit *u)
{
    return unit_is_op(u, ")") || unit_is_op(u, "]") || unit_is_op(u, "}") || unit_is_op(u, ";");
}

static Unit *add_unit(Format *f, UnitKind kind, uint32_t start, uint32_t end, uint32_t *breaks,
                      uint32_t *written)
{
    if (!grow((void **)&f->units, &f->unit_capacity, f->unit_count, sizeof *f->units)) {
        f->failed = true;
        return NULL;
    }
    Unit *u = &f->units[f->unit_count++];
    memset(u, 0, sizeof *u);
    u->kind = kind;
    u->start = start;
    u->end = end;
    u->breaks = *breaks > 2 ? 2 : *breaks;
    u->written = *written;
    u->match = -1;
    u->label_owner = -1;
    *breaks = 0;
    *written = 0;
    return u;
}

// What lies between two tokens is blanks and comments ([lex.comment]); the
// comments become units, and the blanks are counted for the unit after them.
static void scan_gap(Format *f, uint32_t from, uint32_t to, uint32_t *breaks, uint32_t *written)
{
    const char *t = f->text;
    uint32_t at = from;
    while (at < to && !f->failed) {
        if (t[at] == '\n' || t[at] == '\r') {
            (*breaks)++;
            *written = 0;
            if (t[at] == '\r' && at + 1 < to && t[at + 1] == '\n') {
                at++;
            }
            at++;
        }
        else if (t[at] != '#') {
            (*written)++;
            at++;
        }
        else if (at + 1 < to && t[at + 1] == '[') {
            uint32_t start = at;
            int depth = 0;
            do {
                if (t[at] == '#' && t[at + 1] == '[') {
                    depth++;
                    at += 2;
                }
                else if (t[at] == ']' && t[at + 1] == '#') {
                    depth--;
                    at += 2;
                }
                else {
                    at++;
                }
            } while (depth > 0 && at < to);
            add_unit(f, UNIT_BLOCK_COMMENT, start, at, breaks, written);
        }
        else {
            uint32_t start = at;
            while (at < to && t[at] != '\n' && t[at] != '\r') {
                at++;
            }
            add_unit(f, UNIT_LINE_COMMENT, start, at, breaks, written);
        }
    }
}

static void cut_units(Format *f, const Reading *r)
{
    uint32_t breaks = 0, written = 0, at = 0;
    for (int j = 0; j < r->syntax.tokens.n && !f->failed; j++) {
        FormatToken *t = r->syntax.tokens.v[j];
        scan_gap(f, at, t->offset, &breaks, &written);
        at = t->offset + t->length;
        Unit *u = add_unit(f, UNIT_TOKEN, t->offset, at, &breaks, &written);
        if (u) {
            u->token = *t;
        }
    }
    scan_gap(f, at, (uint32_t)r->length, &breaks, &written);
    if (f->unit_count) {
        f->units[0].breaks = 0;
    }
}

static void match_brackets(Format *f)
{
    size_t *stack = f->stack;
    size_t depth = 0;
    int32_t brace = -1;
    for (size_t i = 0; i < f->unit_count; i++) {
        Unit *u = &f->units[i];
        u->brace = brace;
        if (unit_is_op(u, "{")) {
            brace = (int32_t)i;
        }
        else if (unit_is_op(u, "}") && brace >= 0) {
            brace = f->units[brace].brace;
            u->brace = brace;
        }
        if (unit_opens(u)) {
            stack[depth++] = i;
        }
        else if (unit_closes(u) && !unit_is_op(u, ";") && depth > 0) {
            size_t open = stack[--depth];
            f->units[open].match = (int32_t)i;
            u->match = (int32_t)open;
        }
    }
}

// The unit an offset falls in.
static size_t unit_at(const Format *f, uint32_t offset)
{
    size_t low = 0, high = f->unit_count;
    while (high - low > 1) {
        size_t middle = (low + high) / 2;
        if (f->units[middle].start <= offset) {
            low = middle;
        }
        else {
            high = middle;
        }
    }
    return low;
}

static void add_region(Format *f, size_t open, size_t close, bool indents)
{
    if (open >= close) {
        return;
    }
    if (!grow((void **)&f->regions, &f->region_capacity, f->region_count, sizeof *f->regions)) {
        f->failed = true;
        return;
    }
    f->regions[f->region_count++] = (Region){open, close, indents};
}

static bool label(const Unit *u)
{
    return u->kind == UNIT_TOKEN && u->token.clause;
}

static void syntax_regions(Format *f, const Reading *r)
{
    if (!f->unit_count) {
        return;
    }
    for (int j = 0; j < r->syntax.spans.n; j++) {
        FormatSpan *s = r->syntax.spans.v[j];
        size_t a = unit_at(f, s->start), b = unit_at(f, s->end - 1);
        add_region(f, a, b, !s->row);
        if (s->block && a < b && unit_is_op(&f->units[a], "{")) {
            f->units[a].block_open = true;
        }
        if (s->kind >= 2000 && s->kind < 3000) {
            const char *head = f->units[a].token.text;
            bool body = s->kind != 2000 + S_EXPR &&
                        (!strcmp(head, "if^") || !strcmp(head, "for^") || !strcmp(head, "repeat^"));
            for (size_t k = a; body && k <= b; k++) {
                Unit *u = &f->units[k];
                if (unit_is_op(u, "{")) {
                    u->block_open = true;
                    break;
                }
                if (unit_opens(u) && u->match > (int32_t)k) {
                    k = (size_t)u->match;
                }
            }
        }
    }
    // Clause labels sit at the head of the construct whose braces contain them.
    for (size_t i = 0; i < f->unit_count; i++) {
        Unit *u = &f->units[i];
        if (!label(u)) {
            continue;
        }
        int32_t brace = u->brace;
        if (brace >= 0 && f->units[brace].match > (int32_t)i) {
            size_t owner = (size_t)brace;
            for (int j = 0; j < r->syntax.spans.n; j++) {
                FormatSpan *s = r->syntax.spans.v[j];
                size_t a = unit_at(f, s->start), b = unit_at(f, s->end - 1);
                if (a <= (size_t)brace && b == (size_t)f->units[brace].match && a < owner) {
                    owner = a;
                }
            }
            u->label_owner = (int32_t)owner;
        }
        size_t limit = brace >= 0 ? (size_t)f->units[brace].match : f->unit_count;
        for (int j = 0; j < r->syntax.spans.n; j++) {
            FormatSpan *s = r->syntax.spans.v[j];
            if (s->kind == 100 + E_IF || s->kind == 100 + E_SCOPE) {
                size_t a = unit_at(f, s->start), b = unit_at(f, s->end - 1);
                // The innermost colon-form expression owns its labels.
                if (a < i && b >= i && (brace < 0 || a > (size_t)brace) &&
                    (b < limit || (b == limit && (int32_t)a > u->label_owner))) {
                    limit = b;
                    u->label_owner = (int32_t)a;
                }
            }
        }
        size_t end = i + 1;
        while (end < limit) {
            Unit *v = &f->units[end];
            if (v->brace == u->brace && (label(v) || unit_is_op(v, "}"))) {
                break;
            }
            if (unit_opens(v) && v->match > (int32_t)end) {
                end = (size_t)v->match;
            }
            end++;
        }
        if (end > i + 1) {
            add_region(f, i, end - 1, true);
        }
    }
}

static int compare_regions(const void *a, const void *b)
{
    const Region *x = (const Region *)a, *y = (const Region *)b;
    if (x->open != y->open) {
        return x->open < y->open ? -1 : 1;
    }
    if (x->close != y->close) {
        return x->close > y->close ? -1 : 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Spacing
// ---------------------------------------------------------------------------

static bool ends_operand(const Unit *u)
{
    return u->kind == UNIT_TOKEN &&
           (u->token.kind == FORMAT_WORD || u->token.kind == FORMAT_LITERAL || unit_is_op(u, ")") ||
            unit_is_op(u, "]") || unit_is_op(u, "}") || unit_is_op(u, "?") || unit_is_op(u, "..."));
}

static uint32_t spacing(const Format *f, size_t i)
{
    const Unit *a = &f->units[i - 1];
    const Unit *b = &f->units[i];
    uint32_t kept = b->written != 0 || b->breaks != 0; // a line break was some

    if (b->kind == UNIT_LINE_COMMENT) {
        return b->written != 0 ? b->written : 1;
    }
    if (unit_is_comment(a) || unit_is_comment(b)) {
        return kept;
    }
    if (b->token.nested_array) {
        return unit_is_op(a, "[") || (a->kind == UNIT_TOKEN && !strcmp(a->token.text, "read^")) ? 0
                                                                                                : 1;
    }
    if (a->kind == UNIT_TOKEN && a->token.kind == FORMAT_OP &&
        a->token.operator_role == FORMAT_OPERATOR_PREFIX) {
        return 0;
    }
    // [lsp.format.hat]: the hat is drawn faint, and a word after it reads as
    // the next word without a space.
    if (a->kind == UNIT_TOKEN && a->token.hat && !f->options->space_after_hat) {
        return 0;
    }
    if (a->kind == UNIT_TOKEN && a->token.kind == FORMAT_SCOPE) {
        return 0;
    }
    if (unit_is_op(b, ",") || unit_is_op(b, ";") || unit_is_op(b, ")") || unit_is_op(b, "]") ||
        unit_is_op(b, ".") || unit_is_op(b, "?.") || unit_is_op(b, "?(") || unit_is_op(b, "?[") ||
        unit_is_op(b, "...") || unit_is_op(b, "?")) {
        return 0;
    }
    if (unit_is_op(a, "(") || unit_is_op(a, "[") || unit_is_op(a, ".") || unit_is_op(a, "?.") ||
        unit_is_op(a, "?(") || unit_is_op(a, "?[") || unit_is_op(a, "!")) {
        return 0;
    }
    // Inside braces on one line the writer chooses `{ a }` or `{a}`.
    if (unit_is_op(a, "{")) {
        return unit_is_op(b, "}") ? 0 : kept;
    }
    if (unit_is_op(b, "}")) {
        return kept;
    }
    bool after_hat = a->kind == UNIT_TOKEN && a->token.hat;
    // An expression has no '[' that begins a value, so one after '>' is an
    // array of the type whose arguments the '>' closed: `Task<number^>[]`.
    if (unit_is_op(b, "[") && unit_is_op(a, ">")) {
        return 0;
    }
    if (unit_is_op(b, "(") || unit_is_op(b, "[")) {
        return after_hat ? kept : !ends_operand(a);
    }
    if (unit_is_op(b, "{") &&
        ((a->kind == UNIT_TOKEN && a->token.kind == FORMAT_WORD && !a->token.hat) ||
         unit_is_op(a, ")") || unit_is_op(a, "]"))) {
        return kept; // a constructor's braces may sit against its name
    }
    if ((a->kind == UNIT_TOKEN && a->token.kind == FORMAT_OP &&
         a->token.operator_role == FORMAT_OPERATOR_BINARY) ||
        (b->kind == UNIT_TOKEN && b->token.kind == FORMAT_OP &&
         b->token.operator_role == FORMAT_OPERATOR_BINARY)) {
        return 1;
    }
    if (unit_is_op(a, ":") || unit_is_op(b, ":") || unit_is_op(a, "..") || unit_is_op(b, "..") ||
        unit_is_op(a, "...") || unit_is_op(a, "<") || unit_is_op(a, ">") || unit_is_op(b, "<") ||
        unit_is_op(b, ">") || unit_is_op(a, "|") || unit_is_op(b, "|") || unit_is_op(a, "&") ||
        unit_is_op(b, "&")) {
        return kept;
    }
    return 1;
}

static void layout(Format *f)
{
    size_t next = 0;  // the next region to push
    size_t depth = 0; // regions on the stack
    size_t line = 0;
    bool begins = true; // whether this line begins a construct
    for (size_t i = 0; i < f->unit_count; i++) {
        Unit *u = &f->units[i];
        while (depth > 0 && f->regions[f->stack[depth - 1]].close < i) {
            depth--;
        }
        if (i == 0 || u->breaks != 0) {
            line = i;
            size_t inner = depth; // the innermost region that indents
            while (inner > 0 && !f->regions[f->stack[inner - 1]].indents) {
                inner--;
            }
            uint32_t outer = inner > 0 ? f->bases[inner - 1] : 0;
            if (unit_closes(u) && depth > 0 && f->regions[f->stack[depth - 1]].close == i) {
                u->depth = f->bases[depth - 1];
            }
            else if (u->label_owner >= 0) {
                const Unit *owner = &f->units[u->label_owner];
                u->depth = f->units[owner->line_start].depth;
            }
            else if (u->block_open || inner == 0) {
                u->depth = outer;
            }
            else {
                u->depth = outer + 1;
            }
            begins = next < f->region_count && f->regions[next].open == i;
        }
        u->line_start = line;
        // A region on a line that begins a construct counts from that line.
        // On one that carries on a construct begun above -- the rest of a
        // long type, say -- it counts from where that began, so a body
        // opened at the end of a long head is one deeper than the head's
        // first line, not than its last.
        while (next < f->region_count && f->regions[next].open == i) {
            f->bases[depth] = begins || depth == 0 ? f->units[line].depth : f->bases[depth - 1];
            f->stack[depth++] = next++;
        }
    }
    // Comment lines written above a label are about it, and stand with it.
    size_t label = SIZE_MAX;
    for (size_t i = f->unit_count; i > 0; i--) {
        Unit *u = &f->units[i - 1];
        if (u->line_start != i - 1) {
            continue;
        }
        if (!unit_is_comment(u)) {
            label = u->label_owner >= 0 ? i - 1 : SIZE_MAX;
        }
        else if (label != SIZE_MAX) {
            u->depth = f->units[label].depth;
        }
    }
}

// Columns a unit takes up to its first line break, and whether it has one.
static size_t unit_width(const Format *f, const Unit *u, bool *broken)
{
    size_t width = 0;
    *broken = false;
    for (uint32_t at = u->start; at < u->end; at++) {
        unsigned char c = (unsigned char)f->text[at];
        if (c == '\n') {
            *broken = true;
            break;
        }
        width += (c & 0xC0) != 0x80;
    }
    return width;
}

static size_t indent_width(const Format *f, uint32_t depth)
{
    const RhatFormatOptions *o = f->options;
    return (size_t)depth * (o->indent_with_tabs ? o->tab_width : o->indent_size);
}

// [lsp.format.length]: a line that is too long is broken inside its widest
// bracket pair -- after the opener, after each comma at the pair's own level,
// and before the closer. Nothing is broken before a '(' or '[', which would
// stop it being a call or an index ([lex.ambig.call-line]). Whether anything
// was broken.
static bool break_long_line(Format *f)
{
    size_t limit = f->options->max_line_length;
    for (size_t start = 0; start < f->unit_count;) {
        size_t end = start + 1;
        while (end < f->unit_count && f->units[end].breaks == 0) {
            end++;
        }
        size_t width = indent_width(f, f->units[start].depth);
        for (size_t i = start; i < end; i++) {
            bool broken;
            width += (i > start ? f->units[i].space : 0) + unit_width(f, &f->units[i], &broken);
            if (broken) {
                break;
            }
        }
        if (width > limit) {
            size_t parameter_break = SIZE_MAX, first_parameter_break = SIZE_MAX;
            size_t columns = indent_width(f, f->units[start].depth);
            for (size_t i = start; i + 1 < end; i++) {
                bool broken;
                columns +=
                    (i > start ? f->units[i].space : 0) + unit_width(f, &f->units[i], &broken);
                if (broken) {
                    break;
                }
                if (f->units[i].token.parameter_separator && !unit_is_comment(&f->units[i + 1])) {
                    if (first_parameter_break == SIZE_MAX) {
                        first_parameter_break = i + 1;
                    }
                    if (columns <= limit) {
                        parameter_break = i + 1;
                    }
                }
            }
            // Keep as many parameters as fit; a single oversized parameter
            // still ends at its comma before considering its inner brackets.
            if (parameter_break == SIZE_MAX) {
                parameter_break = first_parameter_break;
            }
            if (parameter_break != SIZE_MAX) {
                f->units[parameter_break].breaks = 1;
                return true;
            }
            size_t best = SIZE_MAX, best_span = 0;
            for (size_t i = start; i < end; i++) {
                const Unit *u = &f->units[i];
                if (unit_opens(u) && u->match > (int32_t)(i + 1) && (size_t)u->match < end &&
                    (best == SIZE_MAX ||
                     (f->units[best].token.array_type && !u->token.array_type) ||
                     (f->units[best].token.array_type == u->token.array_type &&
                      (size_t)u->match - i > best_span))) {
                    best = i;
                    best_span = (size_t)u->match - i;
                }
            }
            if (best != SIZE_MAX) {
                size_t close = (size_t)f->units[best].match;
                f->units[best + 1].breaks = 1;
                for (size_t i = best + 1; i < close; i++) {
                    if (f->units[i].match > (int32_t)i && unit_opens(&f->units[i])) {
                        i = (size_t)f->units[i].match;
                    }
                    else if (unit_is_op(&f->units[i], ",") && !unit_is_comment(&f->units[i + 1])) {
                        f->units[i + 1].breaks = 1;
                    }
                }
                f->units[close].breaks = 1;
                return true;
            }
        }
        start = end;
    }
    return false;
}

// [lsp.format.braces]: a block's '{' goes where the brace style puts it. A
// block on one line stays on it.
static void place_braces(Format *f)
{
    bool next_line = f->options->brace_next_line;
    for (size_t i = 1; i < f->unit_count; i++) {
        Unit *u = &f->units[i];
        const Unit *previous = &f->units[i - 1];
        if (u->block_open) {
            bool spread = false;
            for (int32_t k = (int32_t)i + 1; k <= u->match; k++) {
                spread = spread || f->units[k].breaks != 0;
            }
            if (next_line && spread) {
                u->breaks = u->breaks != 0 ? u->breaks : 1;
            }
            else if (!next_line && !unit_is_comment(previous)) {
                u->breaks = 0;
            }
        }
    }
}

static RhatEndOfLine end_of_line_of(const char *text, size_t length);

static void render(const Format *f, Buffer *out)
{
    const RhatFormatOptions *o = f->options;
    RhatEndOfLine eol =
        o->end_of_line == RHAT_EOL_KEEP ? end_of_line_of(f->text, strlen(f->text)) : o->end_of_line;
    const char *newline = eol == RHAT_EOL_CRLF ? "\r\n" : eol == RHAT_EOL_CR ? "\r" : "\n";
    for (size_t i = 0; i < f->unit_count; i++) {
        const Unit *u = &f->units[i];
        if (i && u->breaks) {
            for (uint32_t n = 0; n < u->breaks; n++) {
                put(out, newline, strlen(newline));
            }
        }
        if (u->line_start == i) {
            if (o->indent_with_tabs) {
                put_repeated(out, '\t', u->depth);
            }
            else {
                put_repeated(out, ' ', (size_t)u->depth * o->indent_size);
            }
        }
        else {
            put_repeated(out, ' ', u->space);
        }
        // Literal and comment bytes, including their own newlines, are untouched.
        put(out, f->text + u->start, u->end - u->start);
    }
    if (o->final_newline && f->unit_count) {
        put(out, newline, strlen(newline));
    }
}

static bool same_reading(const Reading *a, const Reading *b)
{
    if (a->syntax.tokens.n != b->syntax.tokens.n || a->syntax.spans.n != b->syntax.spans.n) {
        return false;
    }
    for (int j = 0; j < a->syntax.tokens.n; j++) {
        FormatToken *x = a->syntax.tokens.v[j], *y = b->syntax.tokens.v[j];
        if (x->kind != y->kind || x->clause != y->clause || x->operator_role != y->operator_role ||
            x->array_type != y->array_type || x->nested_array != y->nested_array ||
            x->parameter_separator != y->parameter_separator || x->length != y->length ||
            memcmp(a->text + x->offset, b->text + y->offset, x->length)) {
            return false;
        }
    }
    for (int j = 0; j < a->syntax.spans.n; j++) {
        FormatSpan *x = a->syntax.spans.v[j], *y = b->syntax.spans.v[j];
        if (x->kind != y->kind || x->first != y->first || x->last != y->last ||
            x->block != y->block || x->row != y->row) {
            return false;
        }
    }
    return true;
}

static RhatEndOfLine end_of_line_of(const char *text, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        if (text[i] == '\r') {
            return i + 1 < length && text[i + 1] == '\n' ? RHAT_EOL_CRLF : RHAT_EOL_CR;
        }
        if (text[i] == '\n') {
            return RHAT_EOL_LF;
        }
    }
    return RHAT_EOL_LF;
}

static RhatFormatStatus format_impl(const char *text, size_t length,
                                    const RhatFormatOptions *options, char **out,
                                    size_t *out_length)
{
    Reading input;
    RhatFormatStatus status = reading_init(&input, text, length);
    if (status != RHAT_FORMAT_OK) {
        return status;
    }
    Format f = {.options = options, .text = input.text};
    cut_units(&f, &input);
    f.stack = malloc((f.unit_count + 1) * sizeof(*f.stack));
    Buffer formatted = {0};
    if (!f.failed && f.stack) {
        match_brackets(&f);
        for (size_t i = 0; i < f.unit_count; i++) {
            if (f.units[i].match > (int32_t)i) {
                add_region(&f, i, (size_t)f.units[i].match, true);
            }
        }
        syntax_regions(&f, &input);
    }
    free(f.stack);
    f.stack = malloc((f.region_count + 1) * sizeof(*f.stack));
    f.bases = malloc((f.region_count + 1) * sizeof(*f.bases));
    if (f.failed || !f.stack || !f.bases) {
        status = RHAT_FORMAT_OUT_OF_MEMORY;
    }
    if (status == RHAT_FORMAT_OK) {
        qsort(f.regions, f.region_count, sizeof(*f.regions), compare_regions);
        for (size_t i = 1; i < f.unit_count; i++) {
            f.units[i].space = spacing(&f, i);
        }
        place_braces(&f);
        layout(&f);
        while (options->max_line_length && break_long_line(&f)) {
            layout(&f);
        }
        // Wrapping can turn an inline block into a multiline body.
        place_braces(&f);
        layout(&f);
        render(&f, &formatted);
        if (formatted.failed) {
            status = RHAT_FORMAT_OUT_OF_MEMORY;
        }
    }
    free(f.units);
    free(f.regions);
    free(f.stack);
    free(f.bases);
    if (status == RHAT_FORMAT_OK) {
        Reading output;
        status = reading_init(&output, formatted.bytes ? formatted.bytes : "", formatted.length);
        if (status == RHAT_FORMAT_SYNTAX_ERROR ||
            (status == RHAT_FORMAT_OK && !same_reading(&input, &output))) {
            status = RHAT_FORMAT_CHANGED_TOKENS;
        }
    }
    if (status == RHAT_FORMAT_OK) {
        if (!formatted.bytes) {
            formatted.bytes = calloc(1, 1);
        }
        if (!formatted.bytes) {
            return RHAT_FORMAT_OUT_OF_MEMORY;
        }
        *out = formatted.bytes;
        *out_length = formatted.length;
    }
    else {
        free(formatted.bytes);
    }
    return status;
}

RhatFormatStatus rhat_format(const char *text, size_t length, const RhatFormatOptions *options,
                             char **out, size_t *out_length)
{
    *out = NULL;
    *out_length = 0;
    if (length > 16 * 1024 * 1024 || memchr(text, 0, length)) {
        return RHAT_FORMAT_SYNTAX_ERROR;
    }
    Session s = {0}, *previous = session;
    session = &s;
    CompilerHooks hooks = {allocate, resize, release, syntax_error, abandon}, *old = compiler_hooks;
    compiler_hooks = &hooks;
    RhatFormatStatus status = format_impl(text, length, options, out, out_length);
    while (s.head) {
        release(s.head + 1);
    }
    compiler_hooks = old;
    session = previous;
    return status;
}

static bool glob_match(const char *p, const char *pe, const char *s, const char *se);

// '{a,b}' and '{1..3}', at p, which is the '{'. Answers through *matched;
// false when the braces do not close and the '{' is an ordinary character.
static bool glob_braces(const char *p, const char *pe, const char *s, const char *se, bool *matched)
{
    const char *close = NULL;
    bool comma = false;
    int depth = 0;
    for (const char *q = p; q < pe; q++) {
        if (*q == '\\' && q + 1 < pe) {
            q++;
            continue;
        }
        if (*q == '{') {
            depth++;
        }
        if (*q == '}' && --depth == 0) {
            close = q;
            break;
        }
        if (*q == ',' && depth == 1) {
            comma = true;
        }
    }
    if (close == NULL) {
        return false;
    }
    const char *rest = close + 1;
    if (!comma) {
        long low, high;
        int used = 0;
        size_t inner = (size_t)(close - p - 1);
        char range[64];
        if (inner >= sizeof range) {
            return false;
        }
        memcpy(range, p + 1, inner);
        range[inner] = '\0';
        if (sscanf(range, "%ld..%ld%n", &low, &high, &used) != 2 || (size_t)used != inner) {
            return false;
        }
        const char *end = s < se && *s == '-' ? s + 1 : s;
        const char *digits = end;
        long value = 0;
        while (end < se && *end >= '0' && *end <= '9') {
            value = value * 10 + (*end++ - '0');
        }
        value = digits > s ? -value : value;
        *matched = end > digits && value >= (low < high ? low : high) &&
                   value <= (low < high ? high : low) && glob_match(rest, pe, end, se);
        return true;
    }
    // Each alternative, followed by what comes after the braces.
    *matched = false;
    const char *from = p + 1;
    depth = 0;
    for (const char *q = p + 1; q <= close && !*matched; q++) {
        if (*q == '\\' && q + 1 < close) {
            q++;
            continue;
        }
        if (*q == '{') {
            depth++;
        }
        if (*q == '}' && q != close) {
            depth--;
        }
        if ((*q == ',' && depth == 0) || q == close) {
            size_t alternative = (size_t)(q - from), tail = (size_t)(pe - rest);
            char *joined = (char *)malloc(alternative + tail + 1);
            if (joined == NULL) {
                return true;
            }
            memcpy(joined, from, alternative);
            memcpy(joined + alternative, rest, tail);
            *matched = glob_match(joined, joined + alternative + tail, s, se);
            free(joined);
            from = q + 1;
        }
    }
    return true;
}

static bool glob_match(const char *p, const char *pe, const char *s, const char *se)
{
    while (p < pe) {
        char c = *p;
        if (c == '*') {
            bool deep = p + 1 < pe && p[1] == '*';
            p += deep ? 2 : 1;
            for (const char *t = s;; t++) {
                if (glob_match(p, pe, t, se)) {
                    return true;
                }
                if (t == se || (!deep && *t == '/')) {
                    return false;
                }
            }
        }
        if (c == '{') {
            bool matched;
            if (glob_braces(p, pe, s, se, &matched)) {
                return matched;
            }
        }
        if (c == '[') {
            const char *q = p + 1;
            bool negate = q < pe && *q == '!';
            q += negate;
            const char *close = q;
            while (close < pe && (*close != ']' || close == q)) {
                close++;
            }
            if (close < pe) {
                if (s == se || *s == '/') {
                    return false;
                }
                bool in = false;
                for (const char *k = q; k < close; k++) {
                    if (k + 2 < close && k[1] == '-') {
                        in = in || (*s >= k[0] && *s <= k[2]);
                        k += 2;
                    }
                    else {
                        in = in || *s == *k;
                    }
                }
                if (in == negate) {
                    return false;
                }
                p = close + 1;
                s++;
                continue;
            }
        }
        if (c == '?') {
            if (s == se || *s == '/') {
                return false;
            }
            p++;
            s++;
            continue;
        }
        if (c == '\\' && p + 1 < pe) {
            c = *++p;
        }
        if (s == se || *s != c) {
            return false;
        }
        p++;
        s++;
    }
    return s == se;
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    char *end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) {
        end--;
    }
    *end = '\0';
    return s;
}

static void lower(char *s)
{
    for (; *s; s++) {
        if (*s >= 'A' && *s <= 'Z') {
            *s = (char)(*s - 'A' + 'a');
        }
    }
}

static void apply_property(RhatFormatOptions *o, const char *key, const char *value)
{
    RhatFormatOptions defaults;
    rhat_format_defaults(&defaults);
    bool unset = strcmp(value, "unset") == 0;
    unsigned long number = strtoul(value, NULL, 10);
    bool counted = number > 0 && number < 1000;
    if (strcmp(key, "indent_style") == 0) {
        o->indent_with_tabs = unset ? defaults.indent_with_tabs : strcmp(value, "tab") == 0;
    }
    else if (strcmp(key, "indent_size") == 0) {
        if (unset) {
            o->indent_size = defaults.indent_size;
        }
        else if (strcmp(value, "tab") == 0) {
            o->indent_size = o->tab_width;
        }
        else if (counted) {
            o->indent_size = (uint32_t)number;
        }
    }
    else if (strcmp(key, "tab_width") == 0) {
        if (unset) {
            o->tab_width = defaults.tab_width;
        }
        else if (counted) {
            o->tab_width = (uint32_t)number;
        }
    }
    else if (strcmp(key, "max_line_length") == 0) {
        if (unset) {
            o->max_line_length = defaults.max_line_length;
        }
        else if (strcmp(value, "off") == 0) {
            o->max_line_length = 0;
        }
        else if (counted) {
            o->max_line_length = (uint32_t)number;
        }
    }
    else if (strcmp(key, "end_of_line") == 0) {
        o->end_of_line = strcmp(value, "lf") == 0     ? RHAT_EOL_LF
                         : strcmp(value, "crlf") == 0 ? RHAT_EOL_CRLF
                         : strcmp(value, "cr") == 0   ? RHAT_EOL_CR
                                                      : defaults.end_of_line;
    }
    else if (strcmp(key, "insert_final_newline") == 0) {
        o->final_newline = unset ? defaults.final_newline : strcmp(value, "false") != 0;
    }
    else if (strcmp(key, "rhat_brace_style") == 0) {
        o->brace_next_line = strcmp(value, "next_line") == 0;
    }
    else if (strcmp(key, "rhat_space_after_hat") == 0) {
        o->space_after_hat = strcmp(value, "true") == 0;
    }
}

// One .editorconfig in `directory` (with its trailing '/'), applied to the
// file `relative` names under it. Whether it said root = true.
static bool apply_editorconfig(const char *directory, const char *relative, RhatFormatOptions *o)
{
    size_t length = strlen(directory);
    char *path = (char *)malloc(length + sizeof ".editorconfig");
    if (path == NULL) {
        return false;
    }
    memcpy(path, directory, length);
    memcpy(path + length, ".editorconfig", sizeof ".editorconfig");
    FILE *file = fopen(path, "rb");
    free(path);
    if (file == NULL) {
        return false;
    }

    const char *base = strrchr(relative, '/');
    base = base != NULL ? base + 1 : relative;
    bool root = false, preamble = true, applies = false;
    char line[1024];
    while (fgets(line, sizeof line, file) != NULL) {
        line[strcspn(line, "\n")] = '\0';
        char *text = trim(line);
        if (*text == '\0' || *text == '#' || *text == ';') {
            continue;
        }
        if (*text == '[') {
            char *close = strrchr(text, ']');
            if (close == NULL) {
                continue;
            }
            *close = '\0';
            const char *glob = text + 1;
            preamble = false;
            // A glob without a '/' names a file in any directory below.
            const char *against = strchr(glob, '/') != NULL ? relative : base;
            if (*glob == '/') {
                glob++;
            }
            applies = glob_match(glob, glob + strlen(glob), against, against + strlen(against));
            continue;
        }
        char *equals = strchr(text, '=');
        if (equals == NULL) {
            continue;
        }
        *equals = '\0';
        char *key = trim(text), *value = trim(equals + 1);
        lower(key);
        lower(value);
        if (preamble) {
            root = root || (strcmp(key, "root") == 0 && strcmp(value, "true") == 0);
        }
        else if (applies) {
            apply_property(o, key, value);
        }
    }
    fclose(file);
    return root;
}

void rhat_format_read_editorconfig(const char *path, RhatFormatOptions *options)
{
    size_t length = strlen(path);
    char *full = (char *)malloc(2 * (length + 1));
    size_t *slashes = (size_t *)malloc((length + 1) * sizeof *slashes);
    if (full == NULL || slashes == NULL) {
        free(full);
        free(slashes);
        return;
    }
    char *directory = full + length + 1;
    size_t count = 0;
    for (size_t i = 0; i <= length; i++) {
        full[i] = path[i] == '\\' ? '/' : path[i];
        if (full[i] == '/') {
            slashes[count++] = i;
        }
    }
    // Nearest first, to find the root; then laid on from the root down, so
    // the nearer file has the last word.
    size_t first = 0;
    for (size_t pass = 0; pass < 2; pass++) {
        for (size_t step = 0; step < count; step++) {
            size_t k = pass == 0 ? count - 1 - step : step;
            if (pass == 1 && k < first) {
                continue;
            }
            memcpy(directory, full, slashes[k] + 1);
            directory[slashes[k] + 1] = '\0';
            RhatFormatOptions scratch = *options;
            bool root = apply_editorconfig(directory, full + slashes[k] + 1,
                                           pass == 0 ? &scratch : options);
            if (pass == 0 && root) {
                first = k;
                break;
            }
        }
    }
    free(full);
    free(slashes);
}

int rhat_format_file(const char *path, bool check)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "%s: cannot read source\n", path);
        return 1;
    }
    bool ok = fseek(file, 0, SEEK_END) == 0;
    long size = ok ? ftell(file) : -1;
    if (size < 0 || size > 16 * 1024 * 1024 || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        fprintf(stderr, "%s: cannot read source size\n", path);
        return 1;
    }
    char *source = calloc((size_t)size + 1, 1);
    ok = source && fread(source, 1, (size_t)size, file) == (size_t)size && !ferror(file);
    fclose(file);
    if (!ok) {
        free(source);
        fprintf(stderr, "%s: cannot read source\n", path);
        return 1;
    }
    RhatFormatOptions options;
    rhat_format_defaults(&options);
#if defined(_WIN32)
    char *full = _fullpath(NULL, path, 0);
#else
    char *full = realpath(path, NULL);
#endif
    rhat_format_read_editorconfig(full ? full : path, &options);
    free(full);
    char *formatted = NULL;
    size_t length = 0;
    RhatFormatStatus status = rhat_format(source, (size_t)size, &options, &formatted, &length);
    bool changed =
        status == RHAT_FORMAT_OK && (length != (size_t)size || memcmp(source, formatted, length));
    free(source);
    if (status != RHAT_FORMAT_OK) {
        fprintf(stderr, "%s: %s\n", path,
                status == RHAT_FORMAT_SYNTAX_ERROR ? "cannot format invalid syntax"
                : status == RHAT_FORMAT_CHANGED_TOKENS
                    ? "formatting would change the syntax; source was preserved"
                    : "formatter ran out of memory");
        free(formatted);
        return 1;
    }
    if (check && changed) {
        fprintf(stderr, "%s: formatting differs\n", path);
    }
    else if (changed) {
        file = fopen(path, "wb");
        ok = file && fwrite(formatted, 1, length, file) == length;
        if (file && fclose(file)) {
            ok = false;
        }
        if (!ok) {
            fprintf(stderr, "%s: cannot write formatted source\n", path);
        }
    }
    free(formatted);
    return !ok || (check && changed) ? 1 : 0;
}
