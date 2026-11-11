#include "tooling.h"
#include <ctype.h>

_Thread_local CompilerHooks *compiler_hooks;
static _Thread_local ToolRecovery *recovery;

void tool_enter(ToolRecovery *next)
{
    next->previous = recovery;
    next->instance = recovery ? recovery->instance : NULL;
    recovery = next;
}

void tool_leave(ToolRecovery *current)
{
    recovery = current->previous;
}

Instance *tool_current_instance(void)
{
    return recovery ? recovery->instance : NULL;
}

void tool_instance_use(Instance *in, Loc loc)
{
    Instance *caller = tool_current_instance();
    if (!caller || !loc.file) {
        return;
    }
    for (int j = 0; j < in->callers.n; j++) {
        ToolInstantiation *use = in->callers.v[j];
        if (use->caller == caller && use->loc.line == loc.line && use->loc.col == loc.col &&
            source_same_path(use->loc.file, loc.file)) {
            return;
        }
    }
    ToolInstantiation *use = mem(sizeof(*use));
    *use = (ToolInstantiation){caller, in, loc};
    add(&in->callers, use);
}

List tool_instantiation_trace(Instance *in)
{
    List trace = {0}, pending = {0};
    if (!in || !in->fn->templ) {
        return trace;
    }
    add(&pending, in);
    for (int j = 0; j < pending.n; j++) {
        Instance *next = pending.v[j];
        for (int k = 0; k < next->callers.n; k++) {
            ToolInstantiation *use = next->callers.v[k];
            add(&trace, use);
            bool seen = false;
            for (int n = 0; n < pending.n; n++) {
                seen |= pending.v[n] == use->caller;
            }
            if (!seen) {
                add(&pending, use->caller);
            }
        }
    }
    mem_free(pending.v);
    return trace;
}

_Noreturn void tool_abort(void)
{
    if (recovery) {
        longjmp(recovery->escape, 1);
    }
    compiler_hooks->abandon();
    abort();
}

struct ToolState {
    void *target;
    size_t size, count;
    const size_t *lists;
    unsigned char data[];
};

ToolState *tool_save(void *target, size_t size, const size_t *lists, size_t count)
{
    size_t bytes = size;
    for (size_t j = 0; j < count; j++) {
        List *list = (List *)((char *)target + lists[j]);
        bytes += (size_t)list->n * sizeof(void *);
    }
    ToolState *state = mem(sizeof(*state) + bytes);
    *state = (ToolState){target, size, count, lists};
    memcpy(state->data, target, size);
    unsigned char *at = state->data + size;
    for (size_t j = 0; j < count; j++) {
        List *list = (List *)((char *)target + lists[j]);
        size_t length = (size_t)list->n * sizeof(void *);
        if (length) {
            memcpy(at, list->v, length);
            at += length;
        }
    }
    return state;
}

ToolState *tool_save_objects(List *objects, size_t size)
{
    ToolState *state = mem(sizeof(*state) + (size + sizeof(void *)) * (size_t)objects->n);
    *state = (ToolState){NULL, size, (size_t)objects->n, NULL};
    unsigned char *at = state->data;
    for (int j = 0; j < objects->n; j++) {
        memcpy(at, &objects->v[j], sizeof(void *));
        at += sizeof(void *);
        memcpy(at, objects->v[j], size);
        at += size;
    }
    return state;
}

void tool_restore(ToolState *state)
{
    if (!state->target) {
        unsigned char *at = state->data;
        for (size_t j = 0; j < state->count; j++) {
            void *target;
            memcpy(&target, at, sizeof(void *));
            at += sizeof(void *);
            memcpy(target, at, state->size);
            at += state->size;
        }
        return;
    }
    // List offsets are ordered. Copy scalar ranges around their live buffers.
    size_t begin = 0;
    unsigned char *at = state->data + state->size;
    for (size_t j = 0; j < state->count; j++) {
        size_t offset = state->lists[j];
        memcpy((char *)state->target + begin, state->data + begin, offset - begin);
        List *list = (List *)((char *)state->target + offset);
        List old;
        memcpy(&old, state->data + offset, sizeof(old));
        if (list->cap < old.n) {
            list->cap = old.n;
            list->v = compiler_hooks->resize(list->v, (size_t)old.n * sizeof(void *));
        }
        list->n = old.n;
        size_t length = (size_t)old.n * sizeof(void *);
        if (length) {
            memcpy(list->v, at, length);
            at += length;
        }
        begin = offset + sizeof(List);
    }
    memcpy((char *)state->target + begin, state->data + begin, state->size - begin);
}

void mem_free(void *pointer)
{
    if (compiler_hooks) {
        compiler_hooks->release(pointer);
    }
    // CLI allocations belong to process-lifetime chunks.
}

static bool path_equal(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x == '\\') {
            x = '/';
        }
        if (y == '\\') {
            y = '/';
        }
#ifdef _WIN32
        x = (unsigned char)tolower(x);
        y = (unsigned char)tolower(y);
#endif
        if (x != y) {
            return false;
        }
    }
    return *a == *b;
}

#ifdef _WIN32
// Full paths without dot or empty components cannot become equal by resolving the cwd.
static bool normalized_absolute_path(const char *path)
{
    if (!path || !isalpha((unsigned char)path[0]) || path[1] != ':' ||
        (path[2] != '/' && path[2] != '\\')) {
        return false;
    }
    const char *part = path + 3;
    for (const char *at = part;; at++) {
        if (*at && *at != '/' && *at != '\\') {
            continue;
        }
        size_t length = (size_t)(at - part);
        if ((!length && *at) || (length == 1 && part[0] == '.') ||
            (length == 2 && part[0] == '.' && part[1] == '.') ||
            (length && (at[-1] == '.' || at[-1] == ' '))) {
            return false;
        }
        if (!*at) {
            return true;
        }
        part = at + 1;
    }
}
#endif

bool source_same_path(const char *a, const char *b)
{
    if (a == b || path_equal(a, b)) {
        return true;
    }
#ifdef _WIN32
    if (normalized_absolute_path(a) && normalized_absolute_path(b)) {
        return false;
    }
    char *x = _fullpath(NULL, a, 0), *y = _fullpath(NULL, b, 0);
    bool result = path_equal(x ? x : a, y ? y : b);
    free(x);
    free(y);
    return result;
#else
    return path_equal(a, b);
#endif
}

const char *source_override(Program *p, const char *path)
{
    for (int j = 0; j < p->overlays.n; j++) {
        ToolSource *s = p->overlays.v[j];
        if (source_same_path(s->path, path)) {
            return s->text;
        }
    }
    return NULL;
}

void tool_record(Program *p, Loc use, Loc declaration, const char *name, Type *type,
                 Function *function, Def *owner, int kind, bool definition)
{
    if (!p->tooling || !use.file || !declaration.file || !name || (unsigned char)*name < 32) {
        return;
    }
    ToolRef *r = mem(sizeof(*r));
    *r = (ToolRef){use, declaration, name, type, function, owner, kind, definition, NULL};
    add(&p->resolutions, r);
}

void tool_record_try(Program *p, Loc use, Loc destination, Type *errors, const char *detail)
{
    int before = p->resolutions.n;
    tool_record(p, use, destination, "try^", errors, NULL, NULL, TOOL_TRY, false);
    if (p->resolutions.n != before) {
        ((ToolRef *)p->resolutions.v[before])->detail = detail;
    }
}
