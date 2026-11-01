#include "tooling.h"
#include <ctype.h>

_Thread_local CompilerHooks *compiler_hooks;
static _Thread_local ToolRecovery *recovery;

void tool_enter(ToolRecovery *next)
{
    next->previous = recovery;
    recovery = next;
}

void tool_leave(ToolRecovery *current)
{
    recovery = current->previous;
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

bool source_same_path(const char *a, const char *b)
{
    if (a == b || path_equal(a, b)) {
        return true;
    }
#ifdef _WIN32
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
    *r = (ToolRef){use, declaration, name, type, function, owner, kind, definition};
    add(&p->resolutions, r);
}
