#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int64_t LhatInteger;

typedef enum
{
    LHAT_VALUE_NIL,
    LHAT_VALUE_BOOL,
    LHAT_VALUE_INTEGER,
    LHAT_VALUE_REAL,
    LHAT_VALUE_OBJECT
} LhatValueTag;

typedef struct
{
    int kind;
} LhatObject;

typedef struct
{
    LhatObject header;
    size_t length;
    char text[];
} LhatString;

typedef struct
{
    union
    {
        bool boolean;
        LhatInteger integer;
        double real;
        LhatObject *object;
    } as;
    LhatValueTag tag;
} LhatValue;

typedef struct LhatProgram LhatProgram;
typedef struct LhatMachine LhatMachine;
typedef void (*LhatHostFn)(LhatMachine *, void *, const LhatValue *, size_t, LhatValue *, int *);

static inline LhatValue lhat_nil(void)
{
    LhatValue v;
    v.tag = LHAT_VALUE_NIL;
    return v;
}

static inline LhatValue lhat_bool(bool b)
{
    LhatValue v;
    v.as.boolean = b;
    v.tag = LHAT_VALUE_BOOL;
    return v;
}

static inline LhatValue lhat_integer(LhatInteger i)
{
    LhatValue v;
    v.as.integer = i;
    v.tag = LHAT_VALUE_INTEGER;
    return v;
}

static inline LhatValue lhat_real(double r)
{
    LhatValue v;
    v.as.real = r;
    v.tag = LHAT_VALUE_REAL;
    return v;
}

static inline LhatObject *lhat_as_object(LhatValue v) { return v.as.object; }

static inline double lhat_number_as_real(LhatValue v) { return v.tag == LHAT_VALUE_INTEGER ? (double)v.as.integer : v.as.real; }

static inline bool lhat_number_as_whole(LhatValue v, LhatInteger *out)
{
    if (v.tag == LHAT_VALUE_INTEGER) {
        *out = v.as.integer;
        return true;
    }
    if (v.as.real == (double)(LhatInteger)v.as.real) {
        *out = (LhatInteger)v.as.real;
        return true;
    }
    return false;
}

bool lhat_register_func(LhatProgram *program, const char *module, const char *name, const char *signature, LhatHostFn call, void *context);

bool lhat_machine_make_string(LhatMachine *machine, const char *text, size_t length, LhatValue *out);

LhatProgram *mock_program(void);

void mock_run(int32_t mode);
