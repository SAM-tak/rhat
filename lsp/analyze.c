#include "analyze.h"
#include "builtins.h"
#include "position.h"
#include "sema.h"
#include "tooling.h"
#include "uri.h"
#include <ctype.h>
#include <setjmp.h>
#include <stddef.h>

typedef union Allocation Allocation;

union Allocation {
    long double align;

    struct {
        Allocation *next, *previous;
    } link;
};

// Per-analysis source coordinates and URI, shared by every exported reference.
typedef struct {
    const char *path, *text, *uri;
    size_t length, *lines;
    int *rows, line_count;
} Source;

typedef struct {
    Loc loc;
    char *message;
    List instances;
} Diagnostic;

typedef struct {
    Allocation *head;
    jmp_buf *escape;
    Program program;
    List sources, diagnostics;
    char **references;
    size_t reference_count, reference_capacity;
} Session;

static _Thread_local Session *session;

static void *allocate(size_t size)
{
    Allocation *a = calloc(1, sizeof(*a) + (size ? size : 1));
    if (!a) {
        abort();
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
    Allocation *old = (Allocation *)pointer - 1;
    Allocation *a = realloc(old, sizeof(*a) + size);
    if (!a) {
        abort();
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

static void diagnostic(Loc loc, const char *message)
{
    Diagnostic *d = NULL;
    // Inference and concrete checking may encounter the same mistake.
    for (int j = 0; j < session->diagnostics.n; j++) {
        Diagnostic *old = session->diagnostics.v[j];
        if (old->loc.line == loc.line && old->loc.col == loc.col &&
            (old->loc.file == loc.file ||
             (old->loc.file && loc.file && source_same_path(old->loc.file, loc.file))) &&
            !strcmp(old->message, message)) {
            d = old;
            break;
        }
    }
    if (!d && session->diagnostics.n < 100) {
        d = mem(sizeof(*d));
        d->loc = loc;
        d->message = str(message);
        add(&session->diagnostics, d);
    }
    Instance *in = tool_current_instance();
    if (d && in) {
        bool seen = false;
        for (int j = 0; j < d->instances.n; j++) {
            seen |= d->instances.v[j] == in;
        }
        if (!seen) {
            add(&d->instances, in);
        }
    }
    tool_abort();
}

static void abandon(void)
{
    longjmp(*session->escape, 1);
}

static const char *string(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}

static Source *source_for(Program *p, const char *path)
{
    while (session->sources.n < p->sources.n) {
        ToolSource *input = p->sources.v[session->sources.n];
        Source *source = mem(sizeof(*source));
        source->path = input->path;
        source->text = input->text;
        source->length = strlen(input->text);
        char *uri = lsp_absolute_path_to_uri(input->path);
        source->uri = str(uri ? uri : "");
        free(uri);
        int count = 1;
        for (size_t j = 0; j < source->length; j++) {
            count += input->text[j] == '\n';
        }
        source->lines = mem((size_t)count * sizeof(size_t));
        source->rows = mem((size_t)count * sizeof(int));
        source->line_count = 1;
        int row = 0;
        for (size_t j = 0; j < source->length; j++) {
            char ch = input->text[j];
            row += ch == '\r' || (ch == '\n' && (!j || input->text[j - 1] != '\r'));
            if (ch == '\n') {
                int line = source->line_count++;
                source->lines[line] = j + 1;
                source->rows[line] = row;
            }
        }
        add(&session->sources, source);
    }
    for (int j = 0; j < session->sources.n; j++) {
        Source *s = session->sources.v[j];
        if (source_same_path(s->path, path)) {
            return s;
        }
    }
    return NULL;
}

static size_t loc_offset(Source *s, Loc loc)
{
    int line = loc.line > 0 ? loc.line - 1 : 0;
    size_t at = line < s->line_count ? s->lines[line] : s->length;
    for (int col = 1; col < loc.col && s->text[at] && s->text[at] != '\n'; col++) {
        at++;
    }
    return at;
}

static LspPosition source_position(Source *s, size_t offset)
{
    int first = 0, last = s->line_count;
    while (first + 1 < last) {
        int middle = first + (last - first) / 2;
        if (s->lines[middle] <= offset) {
            first = middle;
        }
        else {
            last = middle;
        }
    }
    size_t start = s->lines[first];
    LspPosition at =
        lsp_position_at(s->text + start, s->length - start, (uint32_t)(offset - start));
    at.line += s->rows[first];
    return at;
}

static const char *short_name(const char *name)
{
    const char *last = strrchr(name, '.');
    return last ? last + 1 : name;
}

static bool word_byte(unsigned char c)
{
    return isalnum(c) || c >= 128 || c == '_' || c == '^';
}

static size_t identifier_at(Source *source, Loc loc, const char *name)
{
    const char *text = source->text;
    size_t at = loc_offset(source, loc);
    size_t length = strlen(name);
    // Exact compiler token spans also cover Unicode operators adjacent to names.
    if (loc.length > 0 && (size_t)loc.length == length && !strncmp(text + at, name, length)) {
        return at;
    }
    // Bindings have statement locations, while declarations and members carry
    // exact name tokens. Qualified type paths select their terminal component.
    for (size_t n = at; text[n] && n < at + 512; n++) {
        if (n > at && (text[n] == '\n' || text[n] == '{' || text[n] == '}')) {
            break;
        }
        // A name token may follow a hat directly, as in let^name.
        if (!strncmp(text + n, name, length) &&
            (n == 0 || n == at || !word_byte((unsigned char)text[n - 1])) &&
            (!word_byte((unsigned char)text[n + length]) ||
             unicode_operator(text + n + length, NULL))) {
            return n;
        }
    }
    return at;
}

static cJSON *location(Program *p, Loc loc, const char *name)
{
    cJSON *out = cJSON_CreateObject();
    Source *s = loc.file ? source_for(p, loc.file) : NULL;
    char *uri = s ? NULL : lsp_absolute_path_to_uri(loc.file ? loc.file : "");
    cJSON_AddStringToObject(out, "uri", s ? s->uri : uri ? uri : "");
    free(uri);
    // Diagnostics retain the compiler's exact span. Identifier relocation is
    // only for symbol records whose locations may precede their declarations.
    size_t from = s ? name ? identifier_at(s, loc, short_name(name)) : loc_offset(s, loc) : 0;
    size_t length = name ? strlen(short_name(name)) : loc.length > 0 ? (size_t)loc.length : 0;
    if (s && !name && !length && from < s->length && s->text[from] != '\r' &&
        s->text[from] != '\n') {
        // Lexer failures may precede a complete token. Select one scalar,
        // without splitting a UTF-8 sequence or moving to another token.
        length = 1;
        while (from + length < s->length &&
               ((unsigned char)s->text[from + length] & 0xc0) == 0x80) {
            length++;
        }
    }
    size_t end = s && length > s->length - from ? s->length : from + length;
    cJSON_AddNumberToObject(out, "offset", (double)from);
    cJSON_AddItemToObject(out, "range",
                          lsp_range_json(s ? source_position(s, from) : (LspPosition){0},
                                         s ? source_position(s, end) : (LspPosition){0}));
    return out;
}

static char *spec_text(Spec *s)
{
    if (!s) {
        return "?";
    }
    if (s->kind == SPEC_NAME) {
        char *out = s->name;
        for (int j = 0; j < s->type_args.n; j++) {
            out = fmt("%s%s%s", out, j ? ", " : "<", spec_text(s->type_args.v[j]));
        }
        return s->type_args.n ? fmt("%s>", out) : out;
    }
    if (s->kind == SPEC_TYPEOF) {
        return fmt("typeof^ %s",
                   s->operand && s->operand->kind == E_NAME ? s->operand->text : "(...)");
    }
    if (s->kind == SPEC_UNION) {
        return fmt("%s | %s", spec_text(s->elem), spec_text(s->other));
    }
    if (s->kind == SPEC_INTERSECTION) {
        return fmt("%s & %s", spec_text(s->elem), spec_text(s->other));
    }
    if (s->kind == SPEC_FUNCTION) {
        char *out = fmt("%s%s", s->foreign ? "externC^ " : "", s->procedure ? "p^" : "f^");
        for (int j = 0; j < s->params.n; j++) {
            out = fmt("%s%s%s", out, j ? ", " : " ", spec_text(s->params.v[j]));
        }
        return s->elem ? fmt("%s -> %s;", out, spec_text(s->elem)) : fmt("%s;", out);
    }
    if (s->kind == SPEC_DYN) {
        return fmt("dyn^ %s", spec_text(s->elem));
    }
    if (s->kind == SPEC_TUPLE) {
        char *out = str("(");
        for (int j = 0; j < s->params.n; j++) {
            out = fmt("%s%s%s", out, j ? ", " : "", spec_text(s->params.v[j]));
        }
        return fmt("%s)", out);
    }
    if (s->kind == SPEC_REF) {
        return fmt((s->elem->kind == SPEC_UNION || s->elem->kind == SPEC_INTERSECTION)
                       ? "ref^%s (%s)"
                       : "ref^%s %s",
                   s->count ? "read^" : "", spec_text(s->elem));
    }
    Spec *elem = s->elem;
    char *refs = "";
    while (elem->kind == SPEC_REF && !elem->count && elem->elem->kind != SPEC_DYN) {
        refs = fmt("%s^", *refs ? refs : "ref");
        elem = elem->elem;
    }
    char *kind = s->kind == SPEC_SLICE     ? ".."
                 : s->kind == SPEC_GROW    ? (s->count == ARRAY_CAPPED ? "max^"
                                              : s->count               ? "flex^"
                                                                       : "grow^")
                 : s->kind == SPEC_BOUNDED ? fmt("max^%d", s->count)
                 : s->count                ? fmt("%d", s->count)
                                           : "";
    bool wrap =
        elem->kind == SPEC_UNION || elem->kind == SPEC_INTERSECTION || elem->kind == SPEC_REF;
    return fmt(wrap ? "(%s)[%s%s%s]" : "%s[%s%s%s]", spec_text(elem), refs,
               *refs && *kind ? " " : "", kind);
}

// One callable shape; a concrete instance supplies the types left to call sites.
static char *signature_arm(Function *f, Instance *in)
{
    char *out = fmt("%s%s", f->templ ? "template^" : "", f->procedure ? "p^" : "f^");
    for (int j = 0; j < f->params.n; j++) {
        Param *a = f->params.v[j];
        Type *t = a->self && f->owner ? f->owner->type
                  : a->inferred       ? a->inferred
                  : in                ? in->params.v[j]
                                      : NULL;
        out = fmt("%s%s%s:%s", out, j ? ", " : "", a->name,
                  a->spec && !strcmp(a->name, "...") ? spec_text(a->spec->elem)
                  : a->spec                          ? spec_text(a->spec)
                  : t                                ? type_name(t)
                                                     : "?");
    }
    Type *result = in ? in->result : f->required_result;
    for (int j = 0; !in && !result && j < f->instances.n; j++) {
        result = ((Instance *)f->instances.v[j])->result;
    }
    if (!f->result && result && result->kind == T_VOID) {
        return out;
    }
    return fmt("%s -> %s", out,
               f->result ? spec_text(f->result)
               : result  ? type_name(result)
                         : "?");
}

// Parameters left to call sites list each distinct specialization as an intersection.
static char *signature(Function *f, const char *name)
{
    List arms = {0};
    for (int j = 0; inferred_parameter(f) && j < f->instances.n; j++) {
        Instance *in = f->instances.v[j];
        if (in->invalid || in->params.n != f->params.n + f->captures.n) {
            continue;
        }
        char *arm = signature_arm(f, in);
        bool seen = false;
        for (int k = 0; k < arms.n; k++) {
            seen |= !strcmp(arms.v[k], arm);
        }
        if (!seen) {
            add(&arms, arm);
        }
    }
    if (arms.n < 2) {
        return fmt("%s : %s", short_name(name),
                   arms.n ? (char *)arms.v[0] : signature_arm(f, NULL));
    }
    char *out = fmt("%s : %s;", short_name(name), (char *)arms.v[0]);
    for (int j = 1; j < arms.n; j++) {
        out = fmt("%s & %s;", out, (char *)arms.v[j]);
    }
    return out;
}

static size_t reference_hash(const char *key)
{
    size_t hash = 2166136261u;
    for (; *key; key++) {
        hash = (hash ^ (unsigned char)*key) * 16777619u;
    }
    return hash;
}

static bool first_reference(char *key)
{
    if (session->reference_count * 2 >= session->reference_capacity) {
        size_t old_capacity = session->reference_capacity;
        char **old = session->references;
        size_t capacity = old_capacity ? old_capacity * 2 : 1024;
        session->references = mem(capacity * sizeof(char *));
        session->reference_capacity = capacity;
        for (size_t j = 0; j < old_capacity; j++) {
            if (old[j]) {
                size_t at = reference_hash(old[j]) & (capacity - 1);
                while (session->references[at]) {
                    at = (at + 1) & (capacity - 1);
                }
                session->references[at] = old[j];
            }
        }
    }
    size_t at = reference_hash(key) & (session->reference_capacity - 1);
    while (session->references[at]) {
        if (!strcmp(session->references[at], key)) {
            return false;
        }
        at = (at + 1) & (session->reference_capacity - 1);
    }
    session->references[at] = key;
    session->reference_count++;
    return true;
}

// Later overload arms have no written name: navigation points at their keyword.
static bool implicit_error_method(Function *f)
{
    return f && f->owner && f->owner->group && f->declaration.line == f->owner->loc.line &&
           f->declaration.col == f->owner->loc.col;
}

static const char *declaration_spelling(Program *p, ToolRef *r)
{
    Function *f = r->function;
    if (implicit_error_method(f)) {
        return f->owner->name;
    }
    if (f && r->declaration.line == f->loc.line && r->declaration.col == f->loc.col) {
        Source *s = source_for(p, r->declaration.file);
        if (s) {
            const char *at = s->text + loc_offset(s, r->declaration);
            const char *keywords[] = {"template^", "f^", "p^"};
            for (size_t j = 0; j < sizeof(keywords) / sizeof(*keywords); j++) {
                if (!strncmp(at, keywords[j], strlen(keywords[j]))) {
                    return keywords[j];
                }
            }
        }
    }
    return r->name;
}

// A byte of a name word, hats included, as the server's lexer reads it.
static bool name_byte(unsigned char c)
{
    return isalnum(c) || c >= 128 || c == '_' || c == '^';
}

static void emit_ref(Program *p, cJSON *array, ToolRef *r)
{
    if (!r->declaration.file || !r->name || *short_name(r->name) == '<' ||
        (unsigned char)*short_name(r->name) < 32) {
        return;
    }
    // Keep implicit methods for completion, without giving them a source token range.
    bool implicit = r->definition && implicit_error_method(r->function);
    // Name resolution can record a context binding before its type is inferred.
    for (int j = 0; !r->type && !r->function && j < p->state_fields.n; j++) {
        Field *f = p->state_fields.v[j];
        if (!strcmp(r->name, f->name) && r->declaration.line == f->loc.line &&
            r->declaration.col == f->loc.col &&
            source_same_path(r->declaration.file, f->loc.file)) {
            r->type = f->type;
        }
    }
    const char *decl_name = declaration_spelling(p, r);
    const char *use_name = r->definition ? decl_name : r->name;
    Source *source = source_for(p, r->use.file);
    if (source &&
        ((r->kind == TOOL_PARAMETER && !strcmp(r->name, "self^")) || !strcmp(r->name, "it^")) &&
        strncmp(source->text + loc_offset(source, r->use), r->name, strlen(r->name))) {
        // Generated error and match bodies reuse source locations for implicit receivers.
        return;
    }
    Loc at_use = r->use;
    bool error_leaf = r->kind == 23 && r->owner && r->owner->group;
    if (error_leaf && !r->definition && source) {
        // Error.Leaf contains two nominal names, each with its own target.
        // Synthesized union alternatives point at the bare leaf declaration,
        // so only add the group when it is actually written before a dot.
        Def *group = r->owner->group;
        const char *name = short_name(group->name);
        size_t start = identifier_at(source, r->use, name);
        const char *at = source->text + start;
        if (!strncmp(at, name, strlen(name))) {
            at += strlen(name);
            while (isspace((unsigned char)*at)) {
                at++;
            }
            if (*at == '.') {
                at++;
                while (isspace((unsigned char)*at)) {
                    at++;
                }
                const char *leaf = short_name(r->name);
                if (!strncmp(at, leaf, strlen(leaf)) &&
                    !name_byte((unsigned char)at[strlen(leaf)])) {
                    ToolRef use = {r->use, group->loc, group->name, group->type,
                                   NULL,   group,      23,          false};
                    emit_ref(p, array, &use);
                }
            }
        }
    }
    if (r->kind == 23 && !r->definition && source &&
        !strncmp(source->text + loc_offset(source, r->use), "Self^", 5)) {
        use_name = "Self^";
    }
    else if (r->kind == 23 && !r->definition && source && !error_leaf) {
        // A module path or import alias before a type stays a namespace; the
        // type is the segment after the longest such prefix.
        const char *start = source->text + loc_offset(source, r->use), *last = start;
        for (int j = 0; j < p->modules.n; j++) {
            Module *m = p->modules.v[j];
            List prefixes = {0};
            add(&prefixes, m->name);
            for (int k = 0; k < m->imports.n; k++) {
                add(&prefixes, ((Import *)m->imports.v[k])->alias);
            }
            for (int k = 0; k < prefixes.n; k++) {
                const char *prefix = prefixes.v[k];
                size_t n = prefix ? strlen(prefix) : 0;
                if (n && !strncmp(start, prefix, n) && start[n] == '.' && start + n + 1 > last) {
                    last = start + n + 1;
                }
            }
        }
        if (last != start) {
            size_t n = 0;
            while (name_byte((unsigned char)last[n]) && last[n] != '^') {
                n++;
            }
            char *segment = mem(n + 1);
            memcpy(segment, last, n);
            use_name = segment;
            at_use.col += (int)(last - start);
        }
    }
    if (r->function && source) {
        const char *at = source->text + loc_offset(source, r->use);
        if (r->function->operator_member && strncmp(at, "op^", 3)) {
            size_t length = 1;
            unicode_operator(at, &length);
            if (!strncmp(at, "<=>", 3)) {
                length = 3;
            }
            else if (strchr("=!<>", at[0]) && at[1] == '=') {
                length = 2;
            }
            else {
                const char *op = r->function->name + 3;
                if (!strncmp(at, op, strlen(op))) {
                    length = strlen(op);
                }
            }
            char *spelling = mem(length + 1);
            memcpy(spelling, at, length);
            use_name = spelling;
        }
        if (!strncmp(at, "this^", 5)) {
            size_t count = 5;
            while (at[count] == '^') {
                count++;
            }
            char *name = mem(count + 1);
            memcpy(name, at, count);
            use_name = name;
        }
    }
    cJSON *use = location(p, at_use, use_name);
    char *key = fmt("%s:%d:%d:%s", r->declaration.file, r->declaration.line, r->declaration.col,
                    short_name(r->name));
    // Keep one checked view per source occurrence and declaration. Distinct
    // nominal declarations retain separate records at a shared occurrence.
    if (!first_reference(fmt("%zu:%s:%s:%u:%d", strlen(key), key, string(use, "uri"),
                             (unsigned)cJSON_GetObjectItem(use, "offset")->valuedouble,
                             r->definition))) {
        cJSON_Delete(use);
        return;
    }
    cJSON *decl = location(p, r->declaration, decl_name);
    cJSON *out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "id", key);
    cJSON_AddStringToObject(out, "name", short_name(r->name));
    cJSON_AddNumberToObject(out, "length", implicit ? 0 : (double)strlen(short_name(use_name)));
    cJSON_AddStringToObject(out, "qualified", r->name);
    cJSON_AddNumberToObject(out, "kind", r->kind);
    cJSON_AddBoolToObject(out, "declaration", r->definition);
    cJSON_AddStringToObject(out, "type", r->type ? type_name(r->type) : "");
    cJSON_AddStringToObject(out, "owner", r->owner ? r->owner->name : "");
    cJSON *receivers = cJSON_AddArrayToObject(out, "receivers");
    if (r->owner && r->definition) {
        for (int j = 0; j < p->defs.n; j++) {
            Def *d = p->defs.v[j];
            bool contains = r->kind == 13 && derives(d, r->owner);
            List *members = r->function ? &d->methods : &d->fields;
            for (int k = 0; k < members->n; k++) {
                Loc loc = r->function ? ((Function *)members->v[k])->declaration
                                      : ((Field *)members->v[k])->loc;
                if (loc.file && r->declaration.file &&
                    source_same_path(loc.file, r->declaration.file) &&
                    loc.line == r->declaration.line && loc.col == r->declaration.col) {
                    contains = true;
                }
            }
            if (contains) {
                cJSON_AddItemToArray(receivers, cJSON_CreateString(d->name));
            }
        }
    }
    cJSON_AddBoolToObject(out, "private", r->function ? r->function->priv : false);
    // Static members and type constants belong to the def^, not its values.
    cJSON_AddBoolToObject(out, "static",
                          r->function ? r->function->static_member
                                      : r->kind == 14 || (r->owner && r->kind == 13));
    bool public = r->function               ? r->function->public
                  : r->type && r->type->def ? r->type->def->public
                                            : false;
    cJSON_AddBoolToObject(out, "public", public);
    cJSON_AddItemToObject(out, "use", use);
    cJSON_AddItemToObject(out, "target", decl);
    const char *detail = r->detail                     ? r->detail
                         : r->function                 ? signature(r->function, r->function->name)
                         : capturing_callable(r->type) ? signature(r->type->function, use_name)
                                                       : fmt("%s: %s", short_name(use_name),
                                                             r->type ? type_name(r->type) : "?");
    cJSON_AddStringToObject(out, "detail", detail);
    if (callable_type(r->type)) {
        cJSON *signatures = cJSON_AddArrayToObject(out, "callableSignatures");
        List arms = r->type->arms;
        if (r->type->kind == T_FUNCTION) {
            add(&arms, r->type);
        }
        for (int j = 0; j < arms.n; j++) {
            Type *arm = arms.v[j];
            cJSON *sig = cJSON_CreateObject();
            cJSON_AddStringToObject(sig, "label",
                                    capturing_callable(arm) ? signature(arm->function, use_name)
                                                            : type_name(arm));
            cJSON *params = cJSON_AddArrayToObject(sig, "parameters");
            for (int k = 0; k < arm->params.n; k++) {
                Param *param = arm->function ? arm->function->params.v[k] : NULL;
                cJSON_AddItemToArray(params,
                                     cJSON_CreateString(param ? param->name : fmt("arg%d", k + 1)));
            }
            cJSON_AddItemToArray(signatures, sig);
        }
    }
    if (r->function) {
        cJSON *params = cJSON_AddArrayToObject(out, "parameters");
        for (int j = 0; j < r->function->params.n; j++) {
            Param *a = r->function->params.v[j];
            if (!a->self) {
                cJSON_AddItemToArray(params, cJSON_CreateString(a->name));
            }
        }
    }
    cJSON_AddItemToArray(array, out);
}

static void related_diagnostic(cJSON *diagnostic, cJSON *where, const char *message)
{
    cJSON *related = cJSON_GetObjectItem(diagnostic, "relatedInformation");
    if (!related) {
        related = cJSON_AddArrayToObject(diagnostic, "relatedInformation");
    }
    cJSON *item = cJSON_CreateObject();
    cJSON_AddItemToObject(item, "location", where);
    cJSON_AddStringToObject(item, "message", message);
    cJSON_AddItemToArray(related, item);
}

static void export_diagnostic(Program *p, cJSON *errors, Diagnostic *d)
{
    cJSON *primary = location(p, d->loc, NULL);
    cJSON_AddStringToObject(primary, "message", d->message);
    cJSON_AddItemToArray(errors, primary);
    List seen = {0};
    for (int j = 0; j < d->instances.n; j++) {
        List trace = tool_instantiation_trace(d->instances.v[j]);
        for (int k = 0; k < trace.n; k++) {
            ToolInstantiation *use = trace.v[k];
            bool duplicate = false;
            for (int n = 0; n < seen.n; n++) {
                duplicate |= seen.v[n] == use;
            }
            if (duplicate) {
                continue;
            }
            add(&seen, use);
            char *message =
                fmt("in instantiation of '%s': %s", short_name(use->callee->fn->name), d->message);
            cJSON *call = location(p, use->loc, NULL);
            cJSON_AddStringToObject(call, "message", message);
            related_diagnostic(call, location(p, d->loc, NULL), d->message);
            related_diagnostic(primary, location(p, use->loc, NULL), message);
            cJSON_AddItemToArray(errors, call);
        }
        mem_free(trace.v);
    }
    mem_free(seen.v);
}

// Index only facts independent of specialization, without checking a deferred body.
typedef struct {
    const char *name, *type;
    Loc loc;
    int kind;
} GenericLocal;

static void generic_body(Program *p, Module *module, cJSON *refs, List *names, List *body);
static void generic_function(Program *p, Module *module, cJSON *refs, List *names, Function *f);

static GenericLocal *generic_local(List *names, const char *path)
{
    size_t length = strcspn(path, ".");
    for (int j = names->n - 1; j >= 0; j--) {
        GenericLocal *local = names->v[j];
        if (strlen(local->name) == length && !strncmp(local->name, path, length)) {
            return local;
        }
    }
    return NULL;
}

static void generic_binding(Program *p, cJSON *refs, List *names, const char *name, Loc loc,
                            const char *type, int kind)
{
    GenericLocal *local = mem(sizeof(*local));
    *local = (GenericLocal){name, type, loc, kind};
    add(names, local);
    if (loc.file) {
        ToolRef ref = {
            loc, loc, name, NULL, NULL, NULL, kind, true, fmt("%s: %s", name, type ? type : "?")};
        emit_ref(p, refs, &ref);
    }
}

static const LspBuiltin *generic_builtin(Expr *e)
{
    if (e && e->kind == E_NAME) {
        for (size_t j = 0; j < sizeof(lsp_builtins) / sizeof(*lsp_builtins); j++) {
            if (!strcmp(e->text, lsp_builtins[j].name)) {
                return &lsp_builtins[j];
            }
        }
    }
    return NULL;
}

static bool generic_target(Module *module, const char *path, Loc loc, ToolRef *ref)
{
    Module *target = module;
    const char *name = path;
    bool imported = false;
    if (module->name && !strncmp(path, module->name, strlen(module->name)) &&
        path[strlen(module->name)] == '.') {
        name = path + strlen(module->name) + 1;
    }
    for (int j = 0; j < module->imports.n; j++) {
        Import *import = module->imports.v[j];
        const char *prefix = import->alias ? import->alias : import->name;
        size_t n = strlen(prefix);
        if (!strncmp(path, prefix, n) && path[n] == '.') {
            target = import->target;
            name = path + n + 1;
            imported = true;
            break;
        }
    }
    if (!target) {
        return false;
    }
    const char *qualified = target->name ? fmt("%s.%s", target->name, name) : name;
    for (int j = 0; j < target->defs.n; j++) {
        Def *d = target->defs.v[j];
        if (imported && !d->public) {
            continue;
        }
        if (!strcmp(d->name, qualified)) {
            *ref = (ToolRef){loc, d->loc, d->name, d->type, NULL, d, 23, false};
            return true;
        }
        size_t n = strlen(d->name);
        if (!strncmp(qualified, d->name, n) && qualified[n] == '.') {
            Function *found = NULL;
            int count = 0;
            for (int k = 0; k < d->own_methods.n; k++) {
                Function *f = d->own_methods.v[k];
                if (f->static_member && (!imported || !f->priv) &&
                    !strcmp(short_name(f->name), qualified + n + 1)) {
                    found = f;
                    count++;
                }
            }
            if (count == 1) {
                *ref = (ToolRef){
                    loc,         found->declaration.file ? found->declaration : found->loc,
                    found->name, NULL,
                    found,       d,
                    6,           false};
                return true;
            }
        }
    }
    for (int j = 0; j < target->aliases.n; j++) {
        TypeAlias *a = target->aliases.v[j];
        if ((!imported || a->public) && !strcmp(a->name, name)) {
            *ref = (ToolRef){loc, a->loc, qualified, a->spec->type, NULL, NULL, 23, false};
            return true;
        }
    }
    Function *found = NULL;
    int count = 0;
    for (int j = 0; j < target->functions.n; j++) {
        Function *f = target->functions.v[j];
        if ((!imported || f->public) && !strcmp(short_name(f->name), name)) {
            found = f;
            count++;
        }
    }
    // Overloads need concrete argument types; checked references take precedence.
    if (count != 1) {
        return false;
    }
    *ref = (ToolRef){loc,         found->declaration.file ? found->declaration : found->loc,
                     found->name, NULL,
                     found,       NULL,
                     12,          false};
    return true;
}

static void generic_spec(Program *p, Module *module, cJSON *refs, List *names, Spec *s)
{
    if (!s || s->invalid) {
        return;
    }
    ToolRef ref;
    if (s->self_type) {
        Def *d = s->self_type;
        ref = (ToolRef){s->loc, d->loc, d->name, d->type, NULL, d, 23, false};
        emit_ref(p, refs, &ref);
    }
    else if (s->kind == SPEC_NAME && s->name && !s->parameter_owner &&
             !generic_local(names, s->name) && generic_target(module, s->name, s->loc, &ref) &&
             ref.kind == 23) {
        emit_ref(p, refs, &ref);
    }
    generic_spec(p, module, refs, names, s->elem);
    generic_spec(p, module, refs, names, s->other);
    for (int j = 0; j < s->params.n; j++) {
        generic_spec(p, module, refs, names, s->params.v[j]);
    }
    for (int j = 0; j < s->type_args.n; j++) {
        generic_spec(p, module, refs, names, s->type_args.v[j]);
    }
}

static char *generic_path(Expr *e)
{
    if (!e) {
        return NULL;
    }
    if (e->kind == E_NAME) {
        return e->text;
    }
    if (e->kind == E_TYPE && e->type_expr && e->type_expr->kind == SPEC_NAME) {
        return e->type_expr->self_type ? e->type_expr->self_type->name : e->type_expr->name;
    }
    if ((e->kind == E_MEMBER || e->kind == E_STATIC) && !e->optional) {
        char *base = generic_path(e->a);
        return base ? fmt("%s.%s", base, e->text) : NULL;
    }
    return NULL;
}

static const char *generic_result(Module *module, List *names, Expr *e)
{
    if (!e) {
        return NULL;
    }
    if (e->kind == E_CALL) {
        const LspBuiltin *builtin = generic_builtin(e->a);
        if (builtin) {
            return builtin->result;
        }
        char *path = generic_path(e->a);
        ToolRef ref;
        if (path && !generic_local(names, path) && generic_target(module, path, e->loc, &ref) &&
            ref.function && ref.function->result && !ref.function->owner) {
            return spec_text(ref.function->result);
        }
    }
    if (e->kind == E_NAME) {
        GenericLocal *local = generic_local(names, e->text);
        return local ? local->type : NULL;
    }
    return NULL;
}

static void generic_expr(Program *p, Module *module, cJSON *refs, List *names, Expr *e)
{
    if (!e || e->invalid) {
        return;
    }
    char *path = generic_path(e);
    const LspBuiltin *builtin = generic_builtin(e);
    if (builtin) {
        ToolRef ref = {e->loc, e->loc, builtin->name,     NULL, NULL, NULL,
                       12,     false,  builtin->signature};
        int count = cJSON_GetArraySize(refs);
        emit_ref(p, refs, &ref);
        if (cJSON_GetArraySize(refs) > count) {
            cJSON *symbol = cJSON_GetArrayItem(refs, count);
            cJSON_AddBoolToObject(symbol, "builtin", true);
            cJSON *params = cJSON_AddArrayToObject(symbol, "parameters");
            for (int j = 0; j < 3 && builtin->parameters[j]; j++) {
                cJSON_AddItemToArray(params, cJSON_CreateString(builtin->parameters[j]));
            }
        }
    }
    else if (path) {
        GenericLocal *local = generic_local(names, path);
        ToolRef ref;
        Loc use = e->name_loc.file ? e->name_loc : e->loc;
        if (local && e->kind == E_NAME && local->loc.file) {
            ref = (ToolRef){use,
                            local->loc,
                            local->name,
                            NULL,
                            NULL,
                            NULL,
                            local->kind,
                            false,
                            fmt("%s: %s", local->name, local->type ? local->type : "?")};
            emit_ref(p, refs, &ref);
        }
        else if (!local && generic_target(module, path, use, &ref)) {
            emit_ref(p, refs, &ref);
        }
    }
    generic_spec(p, module, refs, names, e->type_expr);
    generic_spec(p, module, refs, names, e->test_spec);
    generic_spec(p, module, refs, names, e->array_literal);
    int mark = names->n;
    if (e->kind == E_FUNCTION && e->function) {
        generic_function(p, module, refs, names, e->function);
    }
    else {
        generic_body(p, module, refs, names, &e->bindings);
        generic_expr(p, module, refs, names, e->a);
        if (e->kind == E_CATCH) {
            generic_binding(p, refs, names, "it^", (Loc){0}, NULL, TOOL_PARAMETER);
        }
        generic_expr(p, module, refs, names, e->b);
        generic_expr(p, module, refs, names, e->c);
        for (int j = 0; j < e->args.n; j++) {
            generic_expr(p, module, refs, names, e->args.v[j]);
        }
    }
    names->n = mark;
}

static void generic_body(Program *p, Module *module, cJSON *refs, List *names, List *body)
{
    for (int j = 0; j < body->n; j++) {
        Stmt *stmt = body->v[j];
        int mark = names->n;
        generic_body(p, module, refs, names, &stmt->prolog);
        if (stmt->kind == S_BIND_MANY) {
            for (int k = 0; k < stmt->body.n; k++) {
                Stmt *binding = stmt->body.v[k];
                generic_expr(p, module, refs, names, binding->a);
            }
            names->n = mark;
            for (int k = 0; k < stmt->body.n; k++) {
                Stmt *binding = stmt->body.v[k];
                generic_spec(p, module, refs, names, binding->spec);
                generic_binding(p, refs, names, binding->name, binding->loc,
                                binding->spec ? spec_text(binding->spec) : NULL,
                                binding->mut ? 13 : 14);
            }
            continue;
        }
        generic_body(p, module, refs, names, &stmt->pre);
        generic_expr(p, module, refs, names, stmt->a);
        generic_expr(p, module, refs, names, stmt->b);
        generic_spec(p, module, refs, names, stmt->spec);
        for (int k = 0; k < stmt->targets.n; k++) {
            generic_expr(p, module, refs, names, stmt->targets.v[k]);
        }
        for (int k = 0; k < stmt->values.n; k++) {
            generic_expr(p, module, refs, names, stmt->values.v[k]);
        }
        generic_body(p, module, refs, names, &stmt->first);
        if (stmt->kind == S_INLINE) {
            generic_binding(p, refs, names, stmt->name, (Loc){0}, NULL, 13);
        }
        int inner = names->n;
        generic_body(p, module, refs, names, &stmt->body);
        names->n = inner;
        generic_body(p, module, refs, names, &stmt->update);
        names->n = inner;
        generic_body(p, module, refs, names, &stmt->last);
        names->n = inner;
        generic_body(p, module, refs, names, &stmt->epilog);
        names->n = mark;
        generic_body(p, module, refs, names, &stmt->other);
        for (int k = 0; k < stmt->handlers.n; k++) {
            names->n = mark;
            generic_binding(p, refs, names, "it^", (Loc){0}, NULL, TOOL_PARAMETER);
            generic_body(p, module, refs, names, &((CatchArm *)stmt->handlers.v[k])->body);
        }
        names->n = mark;
        if (stmt->kind == S_BIND || stmt->kind == S_TYPE) {
            generic_binding(p, refs, names, stmt->name, stmt->loc,
                            stmt->spec ? spec_text(stmt->spec)
                                       : generic_result(module, names, stmt->a),
                            stmt->mut ? 13 : 14);
        }
    }
}

static void generic_function(Program *p, Module *module, cJSON *refs, List *names, Function *f)
{
    int mark = names->n;
    generic_spec(p, module, refs, names, f->result);
    for (int j = 0; j < f->params.n; j++) {
        Param *param = f->params.v[j];
        generic_spec(p, module, refs, names, param->spec);
        generic_binding(p, refs, names, param->name, param->loc,
                        param->spec ? spec_text(param->spec) : NULL, TOOL_PARAMETER);
    }
    generic_body(p, module, refs, names, &f->body);
    names->n = mark;
}

static cJSON *export_index(Session *s)
{
    Program *p = &s->program;
    cJSON *out = cJSON_CreateObject();
    cJSON_AddStringToObject(
        out, "rootModule",
        p->modules.n && ((Module *)p->modules.v[0])->name ? ((Module *)p->modules.v[0])->name : "");
    cJSON *sources = cJSON_AddArrayToObject(out, "sources");
    for (int j = 0; j < p->sources.n; j++) {
        ToolSource *source = p->sources.v[j];
        cJSON *v = cJSON_CreateObject();
        char *uri = lsp_absolute_path_to_uri(source->path);
        cJSON_AddStringToObject(v, "uri", uri);
        free(uri);
        cJSON_AddStringToObject(v, "text", source->text);
        cJSON_AddItemToArray(sources, v);
    }
    cJSON *imports = cJSON_AddArrayToObject(out, "imports");
    for (int j = 0; j < p->modules.n; j++) {
        Module *m = p->modules.v[j];
        for (int k = 0; k < m->imports.n; k++) {
            Import *i = m->imports.v[k];
            cJSON *item = cJSON_CreateObject();
            char *uri = lsp_absolute_path_to_uri(m->path);
            cJSON_AddStringToObject(item, "uri", uri);
            free(uri);
            cJSON_AddStringToObject(item, "alias", i->alias ? i->alias : i->name);
            cJSON_AddStringToObject(item, "module", i->name);
            cJSON_AddItemToArray(imports, item);
        }
    }
    cJSON *refs = cJSON_AddArrayToObject(out, "symbols");
    for (int j = 0; j < p->defs.n; j++) {
        Def *d = p->defs.v[j];
        ToolRef r = {d->loc, d->loc, d->generic ? d->generic->name : d->name, d->type, NULL, NULL,
                     23,     true};
        emit_ref(p, refs, &r);
        for (int k = 0; k < d->own_fields.n; k++) {
            Field *f = d->own_fields.v[k];
            r = (ToolRef){f->loc, f->loc, f->name, f->type, NULL, d, 8, true};
            emit_ref(p, refs, &r);
            cJSON *last = cJSON_GetArrayItem(refs, cJSON_GetArraySize(refs) - 1);
            cJSON_ReplaceItemInObjectCaseSensitive(last, "private", cJSON_CreateBool(f->priv));
        }
        for (int k = 0; k < d->own_methods.n; k++) {
            Function *f = d->own_methods.v[k];
            Loc loc = f->declaration.file ? f->declaration : f->loc;
            r = (ToolRef){loc, loc, f->name, NULL, f, d, 6, true};
            emit_ref(p, refs, &r);
        }
        for (int k = 0; k < d->state_fields.n; k++) {
            Field *f = d->state_fields.v[k];
            r = (ToolRef){f->loc, f->loc, f->name, f->type, NULL, d, 13, true};
            int count = cJSON_GetArraySize(refs);
            emit_ref(p, refs, &r);
            if (cJSON_GetArraySize(refs) > count) {
                cJSON *last = cJSON_GetArrayItem(refs, count);
                cJSON_ReplaceItemInObjectCaseSensitive(last, "private", cJSON_CreateBool(f->priv));
            }
        }
        for (int k = 0; k < d->constants.n; k++) {
            Constant *c = d->constants.v[k];
            Type *type = NULL;
            for (int n = 0; n < p->instances.n; n++) {
                Instance *in = p->instances.v[n];
                if (in->fn->constant_value && in->fn->owner == d &&
                    in->fn->loc.line == c->binding->loc.line &&
                    in->fn->loc.col == c->binding->loc.col) {
                    type = in->result;
                    break;
                }
            }
            r = (ToolRef){c->binding->loc, c->binding->loc, c->name, type, NULL, d, 14, true};
            int count = cJSON_GetArraySize(refs);
            emit_ref(p, refs, &r);
            if (cJSON_GetArraySize(refs) > count) {
                cJSON *last = cJSON_GetArrayItem(refs, count);
                cJSON_ReplaceItemInObjectCaseSensitive(last, "private",
                                                       cJSON_CreateBool(!c->public));
            }
            for (int n = 0; n < p->resolutions.n; n++) {
                ToolRef *use = p->resolutions.v[n];
                if (use->kind == 14 && use->owner == d && !strcmp(use->name, c->name)) {
                    use->type = type;
                }
            }
        }
    }
    for (int j = 0; j < p->modules.n; j++) {
        Module *m = p->modules.v[j];
        for (int k = 0; k < m->aliases.n; k++) {
            TypeAlias *a = m->aliases.v[k];
            const char *name = m->name ? fmt("%s.%s", m->name, a->name) : a->name;
            ToolRef r = {a->loc, a->loc, name, a->spec->type, NULL, NULL, 23, true};
            emit_ref(p, refs, &r);
            cJSON *last = cJSON_GetArrayItem(refs, cJSON_GetArraySize(refs) - 1);
            if (last) {
                cJSON_ReplaceItemInObjectCaseSensitive(last, "public", cJSON_CreateBool(a->public));
            }
            for (int n = 0; n < p->resolutions.n; n++) {
                ToolRef *use = p->resolutions.v[n];
                if (use->kind == 23 && !strcmp(use->name, name)) {
                    use->type = a->spec->type;
                }
            }
        }
        for (int k = 0; k < m->defs.n; k++) {
            Def *d = m->defs.v[k];
            if (d->type_params.n) {
                ToolRef r = {d->loc, d->loc, d->name, NULL, NULL, NULL, 23, true};
                int count = cJSON_GetArraySize(refs);
                emit_ref(p, refs, &r);
                if (cJSON_GetArraySize(refs) > count) {
                    cJSON *last = cJSON_GetArrayItem(refs, count);
                    cJSON_ReplaceItemInObjectCaseSensitive(last, "public",
                                                           cJSON_CreateBool(d->public));
                }
            }
        }
        for (int k = 0; k < m->constants.n; k++) {
            Constant *c = m->constants.v[k];
            Type *type = NULL;
            for (int n = 0; n < p->instances.n; n++) {
                Instance *in = p->instances.v[n];
                if (in->fn->module == m && strstr(in->fn->name, "<constant:") &&
                    in->fn->loc.line == c->binding->loc.line &&
                    in->fn->loc.col == c->binding->loc.col) {
                    type = in->result;
                    break;
                }
            }
            const char *name = m->name ? fmt("%s.%s", m->name, c->name) : c->name;
            ToolRef r = {c->binding->loc, c->binding->loc, name, type, NULL, NULL, 14, true};
            emit_ref(p, refs, &r);
            cJSON *last = cJSON_GetArrayItem(refs, cJSON_GetArraySize(refs) - 1);
            if (last) {
                cJSON_ReplaceItemInObjectCaseSensitive(last, "public", cJSON_CreateBool(c->public));
            }
            for (int n = 0; n < p->resolutions.n; n++) {
                ToolRef *use = p->resolutions.v[n];
                if (use->kind == 14 && !strcmp(use->name, name)) {
                    use->type = type;
                }
            }
        }
    }
    for (int j = 0; j < p->functions.n; j++) {
        Function *f = p->functions.v[j];
        if (f->constant_value) {
            continue;
        }
        Loc loc = f->declaration.file ? f->declaration : f->loc;
        ToolRef r = {loc, loc, f->name, NULL, f, NULL, 12, true};
        emit_ref(p, refs, &r);
    }
    for (int j = 0; j < p->resolutions.n; j++) {
        ToolRef *ref = p->resolutions.v[j];
        if (ref->kind == 23 && !ref->type && ref->owner) {
            ref->type = ref->owner->type;
        }
        emit_ref(p, refs, ref);
    }
    for (int j = 0; j < p->modules.n; j++) {
        Module *module = p->modules.v[j];
        List module_names = {0};
        for (int k = 0; k < module->functions.n; k++) {
            generic_function(p, module, refs, &module_names, module->functions.v[k]);
        }
        if (module->entry) {
            generic_function(p, module, refs, &module_names, module->entry);
        }
        for (int k = 0; k < module->defs.n; k++) {
            Def *d = module->defs.v[k];
            List names = {0};
            for (int n = 0; n < d->type_params.n; n++) {
                generic_binding(p, refs, &names, d->type_params.v[n], (Loc){0}, NULL, 23);
            }
            for (int n = 0; n < d->own_methods.n; n++) {
                generic_binding(p, refs, &names, ((Function *)d->own_methods.v[n])->name, (Loc){0},
                                NULL, 6);
            }
            for (int n = 0; n < d->own_fields.n; n++) {
                generic_binding(p, refs, &names, ((Field *)d->own_fields.v[n])->name, (Loc){0},
                                NULL, 8);
            }
            for (int n = 0; n < d->constants.n; n++) {
                generic_binding(p, refs, &names, ((Constant *)d->constants.v[n])->name, (Loc){0},
                                NULL, 14);
            }
            for (int n = 0; n < d->state_fields.n; n++) {
                generic_binding(p, refs, &names, ((Field *)d->state_fields.v[n])->name, (Loc){0},
                                NULL, 13);
            }
            for (int n = 0; n < d->own_methods.n; n++) {
                Function *f = d->own_methods.v[n];
                generic_function(p, module, refs, &names, f);
            }
        }
    }
    cJSON *errors = cJSON_AddArrayToObject(out, "diagnostics");
    for (int j = 0; j < s->diagnostics.n; j++) {
        export_diagnostic(p, errors, s->diagnostics.v[j]);
    }
    return out;
}

static cJSON *check_once(const char *path, const cJSON *documents, const cJSON *module_paths,
                         const char *stdlib, bool library)
{
    Session *s = calloc(1, sizeof(*s));
    if (!s) {
        return NULL;
    }
    s->escape = calloc(1, sizeof(*s->escape));
    if (!s->escape) {
        free(s);
        return NULL;
    }
    session = s;
    CompilerHooks hooks = {allocate, resize, release, diagnostic, abandon};
    compiler_hooks = &hooks;
    Program *p = &s->program;
    p->tooling = true;
    p->library_root = library;
    p->stdlib_dir = stdlib;
    if (!setjmp(*s->escape)) {
        cJSON *doc;
        cJSON_ArrayForEach(doc, documents) {
            ToolSource *source = mem(sizeof(*source));
            *source = (ToolSource){str(string(doc, "path")), str(string(doc, "text"))};
            add(&p->overlays, source);
        }
        cJSON_ArrayForEach(doc, module_paths) {
            if (cJSON_IsString(doc)) {
                add(&p->module_paths, str(doc->valuestring));
            }
        }
        parse(p, path);
        analyze(p);
    }
    cJSON *index = export_index(s);
    while (s->head) {
        release(s->head + 1);
    }
    compiler_hooks = NULL;
    session = NULL;
    free(s->escape);
    free(s);
    return index;
}

cJSON *rhat_lsp_analyze(const char *path, const cJSON *documents, const cJSON *module_paths,
                        const char *stdlib)
{
    cJSON *entry = check_once(path, documents, module_paths, stdlib, false);
    if (entry && *string(entry, "rootModule") &&
        cJSON_GetArraySize(cJSON_GetObjectItem(entry, "diagnostics"))) {
        cJSON *library = check_once(path, documents, module_paths, stdlib, true);
        bool public_constant = false;
        cJSON *error;
        cJSON_ArrayForEach(error, cJSON_GetObjectItem(entry, "diagnostics")) {
            public_constant |=
                strstr(string(error, "message"), "public^ executable bindings") != NULL;
        }
        if (library &&
            (public_constant || !cJSON_GetArraySize(cJSON_GetObjectItem(library, "diagnostics")))) {
            cJSON_Delete(entry);
            return library;
        }
        cJSON_Delete(library);
    }
    return entry;
}
