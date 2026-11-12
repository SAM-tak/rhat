#ifndef RHAT_H
#define RHAT_H
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    void **v;
    int n, cap;
} List;

void *mem(size_t n);
char *str(const char *s);
char *fmt(const char *s, ...);
void add(List *a, void *v);

typedef struct {
    const char *file;
    int line, col;
    int length; // Source spelling in UTF-8 bytes; zero for a point location.
} Loc;

_Noreturn void fail(Loc l, const char *s, ...);
typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Def Def;
typedef struct Function Function;
typedef struct Type Type;
typedef struct Spec Spec;
typedef struct Instance Instance;
typedef struct Field Field;
typedef struct Module Module;
typedef struct Program Program;

// A compile-time name for a type, preserving the target's nominal identity.
typedef struct {
    char *name;
    Loc loc;
    Spec *spec;
    Module *module;
    bool public;
    int state;
} TypeAlias;

typedef struct {
    char *name, *alias;
    Loc loc;
    Module *target;
    bool invalid; // Import failure already reported by the editor checker.
} Import;

typedef struct {
    char *name;
    Stmt *binding;
    Def *owner; // Non-NULL for a constant declared in a def^.
    bool public;
    int state;
} Constant;

struct Module {
    Program *program;
    char *name, *path;
    List imports, defs, functions, constants, aliases;
    List c_headers; // includeC^ headers its externC^ shims read.
    Function *entry;
    bool root;
    int state;
};

struct Spec {
    Def *parameter_owner; // Lexical owner of a generic type parameter.
    List type_args;       // Explicit arguments to a generic def^.
    Def *self_type;       // Lexical definition denoted by Self^; unaffected by inheritance.
    bool invalid;         // An editor diagnostic already explains this unavailable type.
    Type *type;           // Canonical type, cached after module name resolution.
    bool resolved;
    Loc loc;
    char *name;
    Spec *elem, *other; // Binary type operands; elem holds a callable result.
    int kind, count;
    // SPEC_REF uses count 1 for read^ and 0 for an ordinary reference.
    Expr *length;  // A constant expression for count, evaluated during name resolution.
    Expr *operand; // SPEC_TYPEOF: the unevaluated expression whose static type this names.
    List *pins; // SPEC_TYPEOF of a local type binding: Instance, Type pairs fixed at the binding.
    List read_from; // Parameter names whose read capability this result reference inherits.
    List params;    // Parameter specifications of a callable signature.
    bool procedure; // A p^ signature carries the procedure effect.
    bool foreign;   // externC^ before a signature: a C function pointer.
};

enum {
    SPEC_NAME,
    SPEC_REF,
    SPEC_ARRAY,
    SPEC_GROW,
    SPEC_SLICE,
    SPEC_BOUNDED,      // T[..N]
    SPEC_UNION,        // An inline set of alternative types.
    SPEC_FUNCTION,     // A callable signature ending in a semicolon.
    SPEC_INTERSECTION, // A value satisfying every callable signature.
    SPEC_DYN,          // dyn^ D, valid only as ref^ dyn^ D.
    SPEC_TUPLE,        // -> (A, B): multiple results, params holding each.
    SPEC_TYPEOF        // typeof^ e: the static type of operand in each specialization.
};

enum {
    E_INT,
    E_FLOAT,
    E_STRING,
    E_BOOL,
    E_NAME,
    E_SELF,
    E_BIN,
    E_UNARY,
    E_CALL,
    E_MEMBER,
    E_INDEX,
    E_RECORD,
    E_ARRAY,
    E_FUNCTION,
    E_CAPTURE,
    E_IF,
    E_SAVED,     // A compiler-owned value snapshot, never parsed from source.
    E_SCOPE,     // A lexical focus scope producing one value.
    E_EXPECT,    // Infer a contextual numeric type without evaluating the hint.
    E_PRESENT,   // Postfix nil presence test; it does not unwrap a value.
    E_FITS,      // Type conformance predicate; its right operand is a Spec.
    E_TRY,       // Unwrap success or transfer an explicitly returned error.
    E_CATCH,     // Replace an error value with a lazily evaluated expression.
    E_COALESCE,  // Lazy nil replacement, below unary and above binary operators.
    E_CHAIN,     // Comparisons share operands and short-circuit left to right.
    E_SLICE,     // A range index: text is ".." or "to^"; b/c are optional bounds.
    E_PACK,      // Compiler-only: trailing arguments of a variadic call, or the
                 // single forwarded sequence in a.
    E_PANIC,     // panic^ a: end the process; it produces no value.
    E_RANGE,     // a .. b or a to^ b (text): a range^ value; a or b may be omitted.
    E_CHAR,      // char^"x": the Unicode scalar value in text, as decimal digits.
    E_INTERP,    // $"...": text and holes in args, each hole's format in names.
    E_TYPE,      // Compile-time type expression, eliminated by module resolution.
    E_STATIC,    // Type.text naming a static member: a is the E_TYPE; only called.
    E_LOOP_PART, // Compiler-only: a piece of for^ in^ (text) whose form follows the type of a.
    E_STATE,     // A mutable type member in the current execution context.
    E_LAYOUT,    // sizeof^ / alignof^ query the layout of type_expr.
    E_BYTESOF,   // bytesof^ a borrows the raw representation as read-only bytes.
    E_DEFAULT,   // Default construction of a static member with an explicit type.
    E_YIELD,     // yield^ a: suspend the enclosing coroutine, handing it a (or nothing).
    E_AWAIT      // await^ a: run coroutine frame a to its end through this one; its result.
};

struct Expr {
    int index_count;     // Comma arity on the first index; -1 for internal dimension accesses.
    bool parenthesized;  // Written grouping separates nested array constructor type suffixes.
    int capture_index;   // E_CAPTURE's index in the current function's lexical captures.
    Field *state_field;  // E_STATE's declaration, resolved before type checking.
    Spec *type_expr;     // Type application, Self^, or an explicit constructor type.
    Def *constant_owner; // A type constant retains its declaration's access rights.
    bool invalid;        // A recovered syntax/name-resolution error, never executable.
    bool top_binding;    // ref^ of a top-level let^ for its hidden parameter: read only.
    bool untracked;      // A compiler-generated name: editors see no reference to it.
    int repeat_count;    // repeat^: -1 for contextual max^, 1..1000000 for constants, else 0.
    Loc name_loc;        // Exact member spelling, distinct from receiver diagnostics.
    Spec *test_spec;     // The type operand of fits^, or the type of a T[max^](capacity)
                         // construction; never evaluated as a value.
    Spec *constant_spec;
    Spec *array_literal;  // The written type of a T[...]{...} literal.
    int array_refs;       // Explicit ref^ depth, enabling stable-place borrowing.
    bool optional;        // This postfix link starts a nil-propagating guard.
    bool tuple;           // return^ a, b: a record of the multiple result type.
    bool spread;          // A trailing ... forwards this sequence to a variadic
                          // parameter.
    void *compiler_value; // Internal full Value, retaining borrow provenance.
    Expr *forms[3];       // E_LOOP_PART: its form for a range^, an array and a cursor.
    int kind;
    Loc loc;
    char *text;
    Expr *a, *b, *c;
    Function *function;
    List args, names;
    int saved_slot;              // Scalar slot referenced by compiler-generated E_SAVED.
    int scope_kind, scope_count; // Explicit lexical scope or enclosing it^ selection.
    size_t length;               // Byte length of a string literal, including embedded NULs.
    List bindings;               // Lexical declarations preceding a scoped expression.
    Expr *match; // The first arm of a match with no other^: its subject, which the arms must cover.
};

// Statements include parallel declarations, loop jumps, and reserved first^
// initialization.
enum {
    S_BIND,
    S_ASSIGN,
    S_EXPR,
    S_RETURN,
    S_IF,
    S_WHILE,
    S_BLOCK,
    S_BIND_MANY,
    S_BREAK,
    S_NEXT,
    S_INIT,
    S_INIT_MANY,
    S_HANDLE, // A physical block body with ordered error handling arms.
    S_INLINE, // Compile-time expansion over a fixed array or heterogeneous arguments.
    S_TYPE    // let^ t = a type inside a body: emits nothing; spec is the type it names.
};

// One ordered, typed or default arm of a physical block's catch^ clause.
typedef struct {
    Loc loc;    // Source location of the catch^ arm.
    Spec *spec; // Error type filter, or NULL for the final default arm.
    List body;  // Statements evaluated with the caught error as it^.
    bool used;  // Some specialization can reach this arm.
} CatchArm;

struct Stmt {
    Field *state_field;   // Entry-owned storage published through the task context.
    bool public_binding;  // A public entry binding must resolve to a type alias.
    List handlers;        // Ordered CatchArm pointers for S_HANDLE.
    bool loop_handler;    // A loop catches main^ only, with outer names visible.
    bool scoped_handlers; // If arms share the if^ body's lexical scope depth.
    int kind;
    Loc loc;
    char *name;
    bool mut;
    Spec *spec;
    Expr *a, *b;
    char *op;             // The arithmetic operator of a compound assignment.
    List targets, values; // Parallel writes evaluate all addresses/values first.
    List body, other;
    List update;                           // Loop updates also run after a targeted next^.
    List prolog, pre, first, last, epilog; // Ordered loop sections.
    List focuses;    // Header bindings whose last successful values are retained.
    bool integer;    // A compiler-generated counted-loop integer requirement.
    bool optional;   // Nil-guarded assignment evaluates only present pairs.
    bool flow_alias; // A compiler-generated match snapshot shares type facts.
    bool internal;   // A compiler-generated write to a read-only focus.
    int depth;       // A jump counts enclosing loops, starting at one.
};

typedef struct {
    Loc loc;
    char *name;
    Spec *spec;
    bool self;
    Stmt *capture;        // Original binding metadata for a compiler-generated reference.
    Expr *capture_source; // Lexical place captured by a local function.
    Type *inferred;       // Body-derived constraint, independent of call sites.
} Param;

struct Function {
    List captures;            // Hidden reference parameters, after the written parameters.
    Function *closure_origin; // Unspecialized lexical declaration, for capture-shape caching.
    Type *environment;        // Inline record of captured references, or NULL.
    bool resolved;            // Module references in this body have been resolved.
    bool constant_value;      // Synthetic validation body of a type constant.
    bool invalid;             // Its signature or parameter constraints could not be checked.
    Loc declaration;          // Name token for editor navigation.
    Function *enclosing;      // Lexical enclosing declaration; captures hold activation data.
    Module *module;
    bool public;
    Loc loc;
    int lexical; // Source lexical depth; noncapturing literals keep scope
                 // numbering.
    char *name;
    Def *owner;
    List params, body;
    List instances; // Concrete call shapes belonging to this declaration.
    Spec *result;
    Type *required_result;
    char *external; // C symbol of an externC^ declaration, which has no body.
    bool intrinsic; // externLLVM^ binds an LLVM intrinsic instead of a C symbol.
    bool procedure, templ, abstract, priv, override, mutable_self;
    bool overload, operator_member, right_receiver; // Explicit member roles.
    bool static_member; // A def^ member without self^, called as Type.name(...).
    bool exported;      // exportC^: callable from C under its own name.
    bool lhat_export;   // exportL^: registered as an L^ host function.
    bool variadic;      // The last parameter "..." collects trailing arguments as a slice or pack.
    bool heterogeneous; // An untyped template^ ... parameter carries a fixed argument pack.
    bool yields;        // The body suspends with yield^: a call makes a coroutine frame.
    List uses;          // Top-level bindings of the entry file the body reaches (Stmt *).
};

struct Field {
    Stmt *binding;       // Original entry binding when accessed through the task context.
    bool state_checking; // Guard recursive static-member type inference.
    Loc loc;
    char *name;
    Spec *spec;
    Type *type;
    Expr *init;
    Def *owner, *initializer_owner;
    bool abstract, priv;
    int index;
};

// One member of an enum^: its name and its value, in decimal.
typedef struct {
    char *name, *value;
} EnumMember;

struct Def {
    List state_fields; // Mutable type members, independent of instance storage.
    List type_params, type_args, instances; // Parameters, concrete arguments and cached defs.
    Def *generic;                           // The template of a concrete instantiation.
    List base_specs, constants;             // Composition types and type-level constants.
    bool resolved;                          // Module references have been resolved.
    bool invalid; // Its composition or storage layout could not be checked.
    Module *module;
    bool public;
    Def *group; // An error variant's errordef^, else NULL.
    Loc loc;
    char *name;
    List base_names, bases, own_fields, own_methods;
    List union_specs, alternatives; // Declared union alternatives and resolved types.
    List fields, methods;
    int state, id;
    bool abstract;
    bool disposable; // Composes std.sys.Disposable: scopes call its dispose.
    Type *type;
    char *c_name;    // externC^ def^: the C type of this name it lays out as.
    bool opaque;     // externC^ def^ Name; with no body, usable only as ref^.
    bool lhat_value; // exportL^ def^: an L^ host value type, its methods members.
    bool lhat_data;  // exportL^ ref^ def^: an L^ host data type over ref^ to it.
    bool lhat_error; // exportL^ errordef^: an L^ error kind; L^ sees no fields.
    // One of std.sys's coroutine types: R, Y and T, NULL where it has none, how many it takes
    // (1 for Coroutine<T>, 2 without R, 3 with it; 0 for other defs) and whether it is a Pure one.
    Type *coroutine_args[3];
    int coroutine_params;
    bool coroutine_pure;
    bool tuple;    // The anonymous struct of multiple results, fields 0, 1, ...
    bool pack;     // Compiler-only heterogeneous arguments, with numbered fields.
    Spec *backing; // enum^: the integer type its values lay out as; NULL for other defs.
    List members;  // enum^: EnumMember, in declaration order.
};

enum {
    T_VOID,
    T_BOOL,
    T_U8,
    T_U16,
    T_U32,
    T_U64,
    T_U128,
    T_I8,
    T_I16,
    T_I32,
    T_I64,
    T_I128,
    T_F8,
    T_F16,
    T_F32,
    T_F64,
    T_STRING,
    T_NIL,
    T_CHAR, // char^: a Unicode scalar value, laid out as u32^.
    T_STRUCT,
    T_REF,
    T_ARRAY,
    T_GROW, // An owning runtime-sized array; count selects ARRAY_GROW/FLEX/SIZED/CAPPED.
    T_SLICE,
    T_UNION,
    T_NEVER,        // Compiler-only type for an expression with no normal value path.
    T_FUNCTION,     // A callable signature, optionally tied to a static declaration.
    T_INTERSECTION, // A canonical set of callable signatures.
    T_DYN,          // ref^ dyn^ D: data reference and method table of a type composing
                    // elem's def.
    T_CFUNC,        // externC^ f^ ...;: a C function pointer with elem's signature.
    T_ENUM,         // enum^: a closed set of named integer constants laid out as elem.
    T_BOUNDED,      // T[max^N]: up to count elems inline after an i64 length; owns nothing.
    T_CHANNEL,      // channel_handle^<T>: an opaque, noncopyable channel hold for elem.
    T_FRAME         // The frame a call to a yielding instance makes: its coroutine's state.
};

// Owning array length policies; ARRAY_SIZED fixes length after construction,
// ARRAY_CAPPED fixes capacity: its stack field names a private RhStack whose
// limit ends the reserved elements.
enum { ARRAY_GROW, ARRAY_FLEX, ARRAY_SIZED, ARRAY_CAPPED };

struct Type {
    bool invalid; // Storage layout failed during editor analysis.
    char *symbol;
    int kind, id, count;
    int regions; // Statically reserved data stacks owned by this value.
    // T_REF and T_DYN use count 1 for read^; the representation is unchanged.
    Type *elem;
    List wrappers; // Canonical ref/array/etc. types whose element is this type.
    Def *def;
    size_t size, align;
    size_t payload_align, payload_bytes, payload_offset;
    bool refs, owned;
    bool disposable;     // Needs disposal: a Disposable, channel handle, or aggregate holding one.
    bool structural;     // Anonymous union identity is its canonical alternative set.
    List params, arms;   // Callable parameters or intersection members.
    Type *result;        // The result of a callable signature.
    Function *function;  // A noncapturing declaration known at compile time.
    bool procedure;      // The callable may have procedure effects.
    Instance *coroutine; // T_FRAME: the yielding instance whose state this holds.
};

struct Program {
    List state_fields;           // Mutable type members and entry bindings in the task context.
    struct Root *state_root;     // Borrow provenance shared by the hidden context references.
    struct FormatSyntax *format; // Optional parser trace for the source formatter.
    bool library_root;           // Check a standalone module using library rules.
    bool tooling;                // Retain source and checked resolutions for editor clients.
    List sources, overlays, resolutions; // ToolSource and ToolRef records.
    List tool_spans; // ToolSpan declaration extents retained for editor outlines.
    List modules, module_paths;
    Module *emitting;
    const char *stdlib_dir;
    List defs, functions, types, instances, strings;
    Type *builtins[T_NEVER + 1]; // Primitive types indexed by kind.
    List catch_sites;            // CatchSite records: each arm in each specialization lowered.
    List dyn_tables;             // DynTable records, one per converted concrete type and
                                 // dyn^ def.
    Function *entry;
    List transfer_types;    // Validated types needing internal thread transfer descriptors.
    int inferred_instances; // Instance count at the last return-inference fixed point.
    int stack_count;
    bool callbacks; // Some exportC^ function may be entered from C.
    bool crt_dll;   // --crt-dll: build and link against the C runtime DLL.
    char *source;
    const char *path;
};

// Whether a spec contains typeof^ and so differs between specializations.
bool dependent_spec(Spec *s);

void parse(Program *p, const char *path);
void parse_file(Program *p, const char *path, Module *module);
void resolve_modules(Program *p);
// Clone a generic definition, substituting its lexical type parameters and Self^.
Def *specialize_definition(Def *source, List *arguments, const char *name);
void analyze(Program *p);
Type *type_spec(Program *p, Spec *s);
// Resolve the fixed type of a mutable member from its annotation or initializer.
Type *state_field_type(Program *p, Field *field);
Type *type_builtin(Program *p, int kind);
Type *type_wrap(Program *p, int kind, Type *elem, int count);
// The frame type of a yielding instance: what each call of it makes.
Type *frame_type(Program *p, Instance *in);
// Whether a call of f is a p^ call. A yielding function's call only makes its frame, so it is
// an f^ whatever its body is; the body's kind goes to the frame's start and resume instead.
bool procedure_call(const Function *f);
// Whether a coroutine frame offers d, one of std.sys's coroutine types: the same T, and for the
// ones that advance, the same Y, an R exactly when the frame receives one, and an f^ body for
// a Pure one.
bool frame_conforms(Type *frame, Def *d);
// Whether an inline array owns independently stored rows; its element layout must be known.
bool owning_rows(Type *t);

// A literal's array type: its written T[...]{...} type, else the context or
// a fixed array of its first element's type.
Type *array_literal_type(Program *p, Expr *e, Type *expected, Type *first);
// Format outer-to-inner array policies, returning the remaining element spec in base.
char *array_spec_shape(Spec *spec, Spec **base);
// Source spelling of a reference or slice read capability, including conditional origins.
char *spec_read(Spec *spec);
char *type_name(Type *t);
const char *builtin_name(int kind);
bool kind_integer(int kind);
// The number type named name, such as i32^ or f64^, or -1.
int numeric_kind(const char *name);
// Canonical spelling of a Unicode operator, or NULL. On a match, set its byte length if given.
const char *unicode_operator(const char *text, size_t *length);
bool kind_unsigned(int kind);
bool kind_float(int kind);
// The unsigned integer type holding a float type's bits, such as u32^ for f32^, or -1.
int bits_kind(int kind);
// Whether every bit pattern of t is a value: numbers, and arrays and structs of them.
bool plain_data(Type *t);

// Validate bytesof^ storage and return its read-only byte view type.
Type *byte_view_type(Program *p, Type *source, Loc loc);
int numeric_bits(int kind);
bool integer_fits(const char *text, int kind);
// The largest value of an integer type, in decimal.
const char *integer_max(int kind);
// Whether a decimal literal rounds to a finite value of the floating type.
bool float_fits(const char *text, int kind);
char *float_constant(const char *text, int kind, Loc loc);
bool derives(Def *d, Def *base);

// The IR contains typed storage and explicit construction/region operations.
enum {
    I_CONST,
    I_STRING,
    I_COPY,
    I_ZERO,
    I_BIN,
    I_UNARY,
    I_ADDR,
    I_DEREF,
    I_FIELD,
    I_INDEX,
    I_CALL,
    I_PRINT,
    I_ASSERT,
    I_LABEL,
    I_BR,
    I_CBR,
    I_RETURN,
    I_REGION,
    I_ROW_SLOT, // Reserve a stable owned row destination; label 1 appends, 0 selects the last row.
    I_ROW_COMMIT, // Publish a successfully constructed row.
    I_ROW_DROP,   // Restore one row's reserved data stacks to the owner's checkpoints.
    I_RELEASE,
    I_PUSH,
    I_LENGTH,
    I_CONVERT,
    I_POP,
    I_CLEAR,
    I_SLICE,
    I_APPEND,
    I_UNION,
    I_CASE_IS,
    I_CASE_GET,
    I_TRUNCATE,
    I_STACK_BEGIN,
    I_STACK_SAVE,   // Save the caller-owned result stack frontier.
    I_STACK_REWIND, // Restore a result construction checkpoint after an error.
    I_RANGE_GUARD,
    I_TERMINATE,   // Exit with the string in slot a; label 1 marks panic^.
    I_DYN,         // Pair reference a with method table label.
    I_DYNCALL,     // Call method table entry label through args[0].
    I_DYNIS,       // Test whether dyn^ a holds type: exactly ref^ T, or ref^ dyn^ D's composers.
    I_DYNCAST,     // Swap dyn^ a's table for the ref^ dyn^ type of the same value.
    I_DYNDATA,     // The data reference of dyn^ a, proven to be type.
    I_CASE_AT,     // Place dst at union a's payload for alternative label; b also sets the tag.
    I_CHAR,        // dst (i64^) = the scalar a holds, or -1: a is the one character of a
                   // string^, or else an i64^ to check.
    I_HELPER,      // dst (i64^, or none when -1) = runtime function text applied to args: a
                   // string^ or slice passes its data and length, an i64^ itself, and the
                   // source line goes last.
    I_SITE,        // dst = the static record of where an error is built: its source, line,
                   // column and message; text is the source (length bytes), a NUL, the message.
    I_STATE_REF,   // Alias dst to context member label.
    I_STATE_BIND,  // Publish entry-owned slot a as context member label.
    I_THREAD,      // Typed channel transfer or thread ABI bridge; label selects operation.
    I_FILL,        // Fill owning array dst with a copies of element b during construction,
                   // or with b < 0 reserve a elements of capacity for a T[max^].
    I_YIELD,       // Suspend at resume point label, the turn already in the result slot.
    I_FRAME,       // Make callee's frame in dst from args, not yet started.
    I_RESUME,      // Run frame reference a to its next yield^ or end: label 0 starts, 1 resumes,
                   // 2 does whichever its state calls for; dst receives the turn.
    I_FRAME_DONE,  // dst = whether frame reference a has finished.
    I_FRAME_RESULT // dst = the final result of finished frame reference a.
};

enum { RANGE_SLOT, RANGE_CONST, RANGE_IV, RANGE_LENGTH, RANGE_ADD, RANGE_SUB, RANGE_MUL };

typedef struct RangeExpr RangeExpr;

struct RangeExpr {
    int kind, slot;
    char *text;
    bool indirect;
    Type *type;
    RangeExpr *a, *b;
};

typedef struct {
    int induction, comparison, limit;
    RangeExpr *upper;
    List indices, arrays, data;
} RangePlan;

typedef struct {
    int op;
    Loc loc;
    int dst, a, b, label, other;
    char *text;
    size_t length; // Explicit byte length for I_STRING.
    Type *type;
    Instance *callee;
    int stack_index;
    bool push_at_top;
    bool index_proven;
    int index_data;
    RangePlan *range;
    List args;
    List targets; // I_DYNCALL: every Instance a table holds at its entry.
} Instr;

typedef struct {
    Type *type;
    bool alias;
    int owner;
    int nested_stack; // One plus the row destination supplying this slot's stack group.
} Slot;

struct Instance {
    bool read_checked;   // This call shape has checked its explicit result-origin contract.
    bool thread_entry;   // Invoked on an independent shard, never with the parent's state.
    bool state_shrinks;  // This call may shrink a task-owned static flex array.
    bool uses_state;     // This instance or a reachable callee accesses the task context.
    bool invalid;        // Partial editor analysis; must never produce executable code.
    bool writes_reached; // A mutable^self^ body writes what its receiver refers to.
    Function *fn;
    Def *self;
    List params, slots, code;
    Type *result;
    char *symbol;
    int id, state, labels, result_slot;
    char *result_local;
    int stack_own, stack_need;
    int callback_need; // exportC^ entries: stacks used before any callback.
    bool may_shrink, emitted, cross_module;
    uint64_t shrinks;     // Parameters through which storage or union payloads may be invalidated.
    List flows;           // Flow records: references this stores through its arguments.
    List returns;         // Flow records: where the references in its result come from.
    bool returns_unknown; // A returned reference did not come from a parameter.
    bool indirect;        // Reached without a summary: dyn^, a function value or exportC^.
    List callers; // ToolInstantiation records for every use of this concrete specialization.
    // A yielding instance: the frame its calls make, what it yields, what it finally returns,
    // and their union, which each start or resume answers.
    Type *frame, *yielded, *final, *turn;
    Type *received;       // What a resume hands in, which yield^ answers; NULL when nothing is.
    int received_slot;    // Where the frame keeps it.
    Def *frame_def;       // The def its frames stand in for in dyn^ tables, once needed.
    int yield_points;     // Resume points so far, numbered from 1.
    size_t *frame_offset; // Byte offset of each slot kept in the frame; 0 keeps it local.
    size_t frame_size, frame_align;
};

// The methods of def iface specialized for concrete, by iface method index;
// entries are NULL for methods that cannot be called through dyn^.
typedef struct {
    Def *concrete, *iface;
    List entries;
    int id;
} DynTable;

void plan_stacks(Program *p);
void analyze_array_growth(Program *p);
void version_range_loops(Program *p);
void report_stacks(Program *p);
void emit_llvm(Program *p, const char *path);
void emit_llvm_builtins(FILE *f);
char *digest_bytes(const void *data, size_t n);
char *digest_file(const char *path);
char *link_identity(const char *text);
const char *tool_resource(const char *compiler, const char *fallback, const char *relative);
int run_process(char *const *args);
// Compile each module and link them with the runtime and C libraries named
// by --link-lib.
int build_objects(Program *p, const char *compiler, const char *clang, const char *runtime,
                  const char *thread_runtime, const char *opt, const char *out, const char *cache,
                  bool reuse, bool verbose, List *libraries, List *c_includes);
void dump_ir(Program *p, const char *path);
// Write the C shims for externC^ functions that take or return a def^ by
// value, or that come from modules with includeC^ headers (headers selects
// which); false when the program needs none.
bool emit_c_shims(Program *p, const char *path, bool headers);
// The C symbol of an externC^ or exportC^ function (the latter's name
// without its module path).
const char *c_function_symbol(Function *f);
// The exportL^ ref^ def^ that t, a ref^ (or a ref^ | nil^), refers to.
Def *lhat_data_def(Type *t);
// The errordef^ t is, or is a variant of, else NULL.
Def *error_group(Type *t);
// What an exportL^ result t answers when it does not fail: t itself unless
// it has error alternatives, then its one other alternative besides nil^,
// or NULL.
Type *lhat_success(Type *t);
char *llvm_type(Type *t);
#endif
