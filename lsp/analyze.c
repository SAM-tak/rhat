#include "analyze.h"
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

typedef struct {
    Allocation *head;
    jmp_buf *escape;
    Program program;
    Loc error;
    char message[4096];
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
    session->error = loc;
    snprintf(session->message, sizeof(session->message), "%s", message);
    longjmp(*session->escape, 1);
}

static const char *string(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}

static ToolSource *source_for(Program *p, const char *path)
{
    for (int j = 0; j < p->sources.n; j++) {
        ToolSource *s = p->sources.v[j];
        if (source_same_path(s->path, path)) {
            return s;
        }
    }
    return NULL;
}

static size_t loc_offset(const char *text, Loc loc)
{
    size_t at = 0;
    for (int line = 1; line < loc.line && text[at]; at++) {
        if (text[at] == '\n') {
            line++;
        }
    }
    for (int col = 1; col < loc.col && text[at] && text[at] != '\n'; col++) {
        at++;
    }
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

static size_t identifier_at(const char *text, Loc loc, const char *name)
{
    size_t at = loc_offset(text, loc);
    size_t length = strlen(name);
    // Bindings have statement locations, while declarations and members carry
    // exact name tokens. Qualified type paths select their terminal component.
    for (size_t n = at; text[n] && n < at + 512; n++) {
        if (n > at && (text[n] == '\n' || text[n] == '{' || text[n] == '}')) {
            break;
        }
        // A name token may follow a hat directly, as in let^name.
        if (!strncmp(text + n, name, length) &&
            (n == 0 || n == at || !word_byte((unsigned char)text[n - 1])) &&
            !word_byte((unsigned char)text[n + length])) {
            return n;
        }
    }
    return at;
}

static cJSON *location(Program *p, Loc loc, const char *name)
{
    cJSON *out = cJSON_CreateObject();
    ToolSource *s = loc.file ? source_for(p, loc.file) : NULL;
    char *uri = lsp_absolute_path_to_uri(loc.file ? loc.file : "");
    cJSON_AddStringToObject(out, "uri", uri ? uri : "");
    free(uri);
    size_t from = s ? identifier_at(s->text, loc, short_name(name)) : 0;
    size_t length = s ? strlen(s->text) : 0;
    cJSON_AddNumberToObject(out, "offset", (double)from);
    cJSON_AddItemToObject(
        out, "range",
        lsp_range_json(lsp_position_at(s ? s->text : "", length, (uint32_t)from),
                       lsp_position_at(s ? s->text : "", length,
                                       (uint32_t)(from + strlen(short_name(name))))));
    return out;
}

static char *spec_text(Spec *s)
{
    if (!s) {
        return "?";
    }
    if (s->kind == SPEC_NAME) {
        return s->name;
    }
    if (s->kind == SPEC_UNION) {
        return fmt("%s | %s", spec_text(s->elem), spec_text(s->other));
    }
    if (s->kind == SPEC_INTERSECTION) {
        return fmt("%s & %s", spec_text(s->elem), spec_text(s->other));
    }
    if (s->kind == SPEC_FUNCTION) {
        char *out = str(s->procedure ? "p^" : "f^");
        for (int j = 0; j < s->params.n; j++) {
            out = fmt("%s%s%s", out, j ? ", " : " ", spec_text(s->params.v[j]));
        }
        return fmt("%s -> %s;", out, s->elem ? spec_text(s->elem) : "void^");
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
        return fmt((s->elem->kind == SPEC_UNION || s->elem->kind == SPEC_INTERSECTION) ? "ref^ (%s)"
                                                                                       : "ref^ %s",
                   spec_text(s->elem));
    }
    bool wrap = s->elem->kind == SPEC_REF || s->elem->kind == SPEC_UNION ||
                s->elem->kind == SPEC_INTERSECTION;
    return fmt(wrap ? "%s(%s)[%s]" : "%s%s[%s]", s->kind == SPEC_GROW ? "grow^ " : "",
               spec_text(s->elem),
               s->kind == SPEC_GROW    ? ""
               : s->kind == SPEC_SLICE ? ":"
                                       : fmt("%d", s->count));
}

static char *signature(Program *p, Function *f)
{
    char *out = fmt("%s%s %s(", f->templ ? "template^" : "", f->procedure ? "p^" : "f^",
                    short_name(f->name));
    for (int j = 0; j < f->params.n; j++) {
        Param *a = f->params.v[j];
        Type *t = a->self && f->owner ? f->owner->type : a->inferred;
        out = fmt("%s%s%s:%s", out, j ? ", " : "", a->name,
                  a->spec && !strcmp(a->name, "...") ? spec_text(a->spec->elem)
                  : a->spec                          ? spec_text(a->spec)
                  : t                                ? type_name(t)
                                                     : "?");
    }
    Type *result = f->required_result;
    if (!result) {
        for (int j = 0; j < p->instances.n; j++) {
            Instance *in = p->instances.v[j];
            if (in->fn == f && in->result) {
                result = in->result;
                break;
            }
        }
    }
    return fmt("%s) -> %s", out,
               f->result ? spec_text(f->result)
               : result  ? type_name(result)
                         : "?");
}

static void emit_ref(Program *p, cJSON *array, ToolRef *r)
{
    if (!r->declaration.file || !r->name || *short_name(r->name) == '<') {
        return;
    }
    const char *use_name = r->name;
    ToolSource *source = source_for(p, r->use.file);
    if (r->function && source) {
        const char *at = source->text + loc_offset(source->text, r->use);
        if (r->function->operator_member && strncmp(at, "op^", 3)) {
            size_t length = 1;
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
    cJSON *use = location(p, r->use, use_name);
    cJSON *decl = location(p, r->declaration, r->name);
    char *key = fmt("%s:%d:%d:%s", r->declaration.file, r->declaration.line, r->declaration.col,
                    short_name(r->name));
    // Keep one checked view per source occurrence and declaration. Distinct
    // nominal declarations retain separate records at a shared occurrence.
    cJSON *prior;
    cJSON_ArrayForEach(prior, array) {
        if (!strcmp(string(prior, "id"), key) &&
            !strcmp(string(cJSON_GetObjectItem(prior, "use"), "uri"), string(use, "uri")) &&
            cJSON_GetObjectItem(cJSON_GetObjectItem(prior, "use"), "offset")->valuedouble ==
                cJSON_GetObjectItem(use, "offset")->valuedouble &&
            cJSON_IsTrue(cJSON_GetObjectItem(prior, "declaration")) == r->definition) {
            cJSON_Delete(use);
            cJSON_Delete(decl);
            return;
        }
    }
    cJSON *out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "id", key);
    cJSON_AddStringToObject(out, "name", short_name(r->name));
    cJSON_AddNumberToObject(out, "length", (double)strlen(short_name(use_name)));
    cJSON_AddStringToObject(out, "qualified", r->name);
    cJSON_AddNumberToObject(out, "kind", r->kind);
    cJSON_AddBoolToObject(out, "declaration", r->definition);
    cJSON_AddStringToObject(out, "type", r->type ? type_name(r->type) : "");
    cJSON_AddStringToObject(out, "owner", r->owner ? r->owner->name : "");
    cJSON *receivers = cJSON_AddArrayToObject(out, "receivers");
    if (r->owner && r->definition) {
        for (int j = 0; j < p->defs.n; j++) {
            Def *d = p->defs.v[j];
            bool contains = false;
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
    bool public = r->function               ? r->function->public
                  : r->type && r->type->def ? r->type->def->public
                                            : false;
    cJSON_AddBoolToObject(out, "public", public);
    cJSON_AddItemToObject(out, "use", use);
    cJSON_AddItemToObject(out, "target", decl);
    char *detail = r->function
                       ? signature(p, r->function)
                       : fmt("%s: %s", short_name(r->name), r->type ? type_name(r->type) : "?");
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
            cJSON_AddStringToObject(sig, "label", type_name(arm));
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
        ToolRef r = {d->loc, d->loc, d->name, d->type, NULL, NULL, 23, true};
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
    }
    for (int j = 0; j < p->modules.n; j++) {
        Module *m = p->modules.v[j];
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
        Loc loc = f->declaration.file ? f->declaration : f->loc;
        ToolRef r = {loc, loc, f->name, NULL, f, NULL, 12, true};
        emit_ref(p, refs, &r);
    }
    for (int j = 0; j < p->resolutions.n; j++) {
        emit_ref(p, refs, p->resolutions.v[j]);
    }
    cJSON *errors = cJSON_AddArrayToObject(out, "diagnostics");
    if (*s->message) {
        cJSON *e = location(p, s->error, "");
        cJSON_AddStringToObject(e, "message", s->message);
        cJSON_AddItemToArray(errors, e);
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
    CompilerHooks hooks = {allocate, resize, release, diagnostic};
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
        cJSON *error = cJSON_GetArrayItem(cJSON_GetObjectItem(entry, "diagnostics"), 0);
        bool public_constant =
            strstr(string(error, "message"), "public^ executable bindings") != NULL;
        if (library &&
            (public_constant || !cJSON_GetArraySize(cJSON_GetObjectItem(library, "diagnostics")))) {
            cJSON_Delete(entry);
            return library;
        }
        cJSON_Delete(library);
    }
    return entry;
}
