#include "tooling.h"
#include <ctype.h>

_Thread_local CompilerHooks *compiler_hooks;

void mem_free(void *pointer)
{
    if (compiler_hooks) {
        compiler_hooks->release(pointer);
    }
    else {
        free(pointer);
    }
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
