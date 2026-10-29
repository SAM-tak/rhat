// R^ runtime: OS-backed stable-address regions; no malloc/free or GC.
#if !defined(_WIN32)
#define _DEFAULT_SOURCE
#endif
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#endif

// Shared stack ABI. Array/view operations and descriptors live in LLVM IR.
typedef struct {
    unsigned char *base, *top, *limit, *committed;
} RhStack;

#ifndef RH_STACK_BYTES
#define RH_STACK_BYTES (UINT64_C(1024) * 1024 * 1024)
#endif
#define RH_GUARD_BYTES (UINT64_C(1024) * 1024)
#define RH_COMMIT_BYTES (UINT64_C(1024) * 1024)

static RhStack *rh_stacks;
static uint64_t rh_stack_count;

static _Noreturn void rh_panic(const char *why, int32_t line)
{
    fflush(stdout);
    fprintf(stderr, "R^ runtime error at line %" PRId32 ": %s\n", line, why);
    exit(1);
}

#if defined(_WIN32)
static PVOID rh_fault_handler;
static uint64_t rh_page_bytes;

static _Noreturn void rh_fault_die(const char *message, DWORD length)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), message, length, &written, NULL);
    ExitProcess(1);
}

static LONG WINAPI rh_fault(EXCEPTION_POINTERS *ep)
{
    EXCEPTION_RECORD *e = ep->ExceptionRecord;
    if (e->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || e->NumberParameters < 2 ||
        (e->ExceptionInformation[0] != 0 && e->ExceptionInformation[0] != 1)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    uintptr_t hit = (uintptr_t)e->ExceptionInformation[1];
    for (uint64_t j = 0; j < rh_stack_count; j++) {
        RhStack *s = &rh_stacks[j];
        if (!s->base) {
            continue;
        }
        uintptr_t base = (uintptr_t)s->base;
        if (hit < base || hit - base >= RH_STACK_BYTES + RH_GUARD_BYTES) {
            continue;
        }
        if (hit - base >= RH_STACK_BYTES) {
            static const char msg[] = "R^ runtime error: data stack overflow\n";
            rh_fault_die(msg, sizeof(msg) - 1);
        }
        MEMORY_BASIC_INFORMATION info;
        if (!VirtualQuery((void *)hit, &info, sizeof(info)) || info.State != MEM_RESERVE) {
            return EXCEPTION_CONTINUE_SEARCH;
        }
        uintptr_t offset = (hit - base) / RH_COMMIT_BYTES * RH_COMMIT_BYTES;
        uint64_t bytes = RH_COMMIT_BYTES;
        if (bytes > RH_STACK_BYTES - offset) {
            bytes = RH_STACK_BYTES - offset;
        }
        unsigned char *page = s->base + offset;
        if (!VirtualAlloc(page, (SIZE_T)bytes, MEM_COMMIT, PAGE_READWRITE)) {
            static const char msg[] = "R^ runtime error: data stack commit failed\n";
            rh_fault_die(msg, sizeof(msg) - 1);
        }
        if ((uintptr_t)s->committed < (uintptr_t)page + bytes) {
            s->committed = page + bytes;
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

void rh_runtime_init(RhStack *stacks, uint64_t count)
{
    rh_stacks = stacks;
    rh_stack_count = count;
#if defined(_WIN32)
    if (count) {
        SYSTEM_INFO info;
        GetSystemInfo(&info);
        rh_page_bytes = info.dwPageSize;
        if (!rh_page_bytes) {
            rh_panic("page size query failed", 0);
        }
        rh_fault_handler = AddVectoredExceptionHandler(1, rh_fault);
        if (!rh_fault_handler) {
            rh_panic("cannot install data stack fault handler", 0);
        }
    }
#endif
    for (uint64_t j = 0; j < count; j++) {
        RhStack *s = &stacks[j];
#if defined(_WIN32)
        s->base = VirtualAlloc(NULL, (SIZE_T)(RH_STACK_BYTES + RH_GUARD_BYTES), MEM_RESERVE,
                               PAGE_READWRITE);
        if (!s->base) {
            rh_panic("data stack reservation failed", 0);
        }
#else
        void *p = mmap(NULL, (size_t)(RH_STACK_BYTES + RH_GUARD_BYTES), PROT_NONE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) {
            rh_panic("data stack reservation failed", 0);
        }
        s->base = p;
        if (mprotect(p, (size_t)RH_STACK_BYTES, PROT_READ | PROT_WRITE)) {
            rh_panic("cannot enable data stack pages", 0);
        }
#endif
        s->top = s->committed = s->base;
        s->limit = s->base + RH_STACK_BYTES;
    }
}

#if defined(_WIN32)
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#endif

typedef struct {
    const char *data;
    int64_t length;
} RhString;

typedef struct {
    RhString *items;
    int64_t count;
} RhStrings;

// The command arguments after the program name, as UTF-8 R^ strings that live
// until the process exits.
void rh_runtime_args(int argc, char **argv, RhStrings *out)
{
#if defined(_WIN32)
    (void)argv;
    wchar_t **wide = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wide) {
        rh_panic("cannot read command arguments", 0);
    }
#endif
    int64_t count = argc > 1 ? argc - 1 : 0;
    RhString *items = malloc((size_t)(count ? count : 1) * sizeof(*items));
    if (!items) {
        rh_panic("cannot store command arguments", 0);
    }
    for (int64_t j = 0; j < count; j++) {
#if defined(_WIN32)
        int bytes = WideCharToMultiByte(CP_UTF8, 0, wide[j + 1], -1, NULL, 0, NULL, NULL);
        char *text = bytes > 0 ? malloc((size_t)bytes) : NULL;
        if (!text || !WideCharToMultiByte(CP_UTF8, 0, wide[j + 1], -1, text, bytes, NULL, NULL)) {
            rh_panic("cannot convert command arguments", 0);
        }
#else
        const char *text = argv[j + 1];
#endif
        items[j] = (RhString){text, (int64_t)strlen(text)};
    }
#if defined(_WIN32)
    LocalFree(wide);
#endif
    *out = (RhStrings){items, count};
}

// Parse all of text with strtod or strtof. Answers 0, 1 for text that is
// empty, starts with space or has trailing characters, or 2 on overflow.
static int32_t rh_parse_float(const char *text, int64_t length, bool single, double *wide,
                              float *narrow)
{
    if (length <= 0 || (size_t)length >= SIZE_MAX || strchr(" \t\n\v\f\r", text[0]) ||
        memchr(text, 0, (size_t)length)) {
        return 1;
    }
    char small[64];
    char *copy = length < (int64_t)sizeof(small) ? small : malloc((size_t)length + 1);
    if (!copy) {
        rh_panic("cannot copy number text", 0);
    }
    memcpy(copy, text, (size_t)length);
    copy[length] = 0;
    char *end = NULL;
    errno = 0;
    double value = 0;
    if (single) {
        *narrow = strtof(copy, &end);
        value = *narrow;
    }
    else {
        *wide = value = strtod(copy, &end);
    }
    int32_t status = end != copy + length ? 1 : errno == ERANGE && isinf(value) ? 2 : 0;
    if (copy != small) {
        free(copy);
    }
    return status;
}

typedef unsigned __int128 rh_u128;

// Read all of s as a number: kind is its width in bits, plus 256 for an unsigned integer and 512
// for a real, and an f8 or f16 is read as a double. The value goes to out. Answers 0, 1 for text
// that is no number (an integer is an optional sign and decimal digits), or 2 for one the type
// cannot hold.
int64_t rh_text_parse(const unsigned char *s, int64_t n, void *out, int64_t kind, int32_t line)
{
    (void)line;
    int bits = (int)(kind & 255), form = (int)(kind >> 8);
    if (form == 2) {
        if (bits == 32) {
            return rh_parse_float((const char *)s, n, true, NULL, out);
        }
        double *wide = out;
        int32_t status = rh_parse_float((const char *)s, n, false, wide, NULL);
        // The first value that rounds past the largest finite f16 or f8 (E4M3FN).
        double limit = bits == 16 ? 65520.0 : bits == 8 ? 464.0 : 0;
        return !status && limit && isfinite(*wide) && fabs(*wide) >= limit ? 2 : status;
    }
    int64_t at = n > 0 && (s[0] == '+' || s[0] == '-');
    bool negative = at && s[0] == '-';
    if (at == n) {
        return 1;
    }
    for (int64_t j = at; j < n; j++) {
        if (s[j] < '0' || s[j] > '9') {
            return 1;
        }
    }
    rh_u128 most = ~(rh_u128)0, value = 0;
    bool over = false;
    for (int64_t j = at; j < n && !over; j++) {
        unsigned digit = s[j] - '0';
        over = value > (most - digit) / 10;
        value = over ? value : value * 10 + digit;
    }
    rh_u128 limit = form == 1 ? (bits == 128 ? most : ((rh_u128)1 << bits) - 1)
                              : ((rh_u128)1 << (bits - 1)) - (negative ? 0 : 1);
    if (over || value > limit || (form == 1 && negative && value)) {
        return 2;
    }
    rh_u128 stored = negative ? (rh_u128)0 - value : value;
    memcpy(out, &stored, (size_t)bits / 8);
    return 0;
}

void rh_runtime_shutdown(void)
{
    for (uint64_t j = 0; j < rh_stack_count; j++) {
#if defined(_WIN32)
        if (!VirtualFree(rh_stacks[j].base, 0, MEM_RELEASE)) {
            rh_panic("data stack release failed", 0);
        }
#else
        if (munmap(rh_stacks[j].base, (size_t)(RH_STACK_BYTES + RH_GUARD_BYTES))) {
            rh_panic("data stack release failed", 0);
        }
#endif
        rh_stacks[j].base = NULL;
    }
    rh_stack_count = 0;
#if defined(_WIN32)
    if (rh_fault_handler) {
        RemoveVectoredExceptionHandler(rh_fault_handler);
    }
    rh_fault_handler = NULL;
#endif
    rh_stacks = NULL;
}

_Noreturn void rh_stack_overflow(int32_t line)
{
    rh_panic("data stack overflow", line);
}

_Noreturn void rh_division_fail(int32_t remainder, int32_t line)
{
    rh_panic(remainder ? "invalid integer remainder" : "invalid integer division", line);
}

_Noreturn void rh_index_fail(int32_t line)
{
    rh_panic("array index out of bounds", line);
}

_Noreturn void rh_pop_fail(int32_t line)
{
    rh_panic("pop from empty array", line);
}

_Noreturn void rh_truncate_fail(int32_t line)
{
    rh_panic("truncate length out of bounds", line);
}

_Noreturn void rh_slice_fail(int32_t line)
{
    rh_panic("slice range out of bounds", line);
}

// The first data stack an exportC^ function entered from C may use: the
// stack index after those the R^ caller of the current externC^ call keeps.
uint64_t rh_callback_sp;

// Enter an exportC^ function needing need stacks. One level of callback is
// planned at every C call; deeper nesting must still fit the stacks.
uint64_t rh_callback_enter(uint64_t need)
{
    if (need > rh_stack_count || rh_callback_sp > rh_stack_count - need) {
        rh_panic("C callbacks nest deeper than the data stacks allow", 0);
    }
    return rh_callback_sp;
}

// Return the pages of shrunk flex^ storage to the OS. One commit chunk above
// the new end stays committed so that small regrowth does not fault again.
void rh_flex_trim(RhStack *s, unsigned char *end)
{
    uint64_t used = (uint64_t)(end - s->base);
    uint64_t keep =
        (used + RH_COMMIT_BYTES - 1) / RH_COMMIT_BYTES * RH_COMMIT_BYTES + RH_COMMIT_BYTES;
    if (keep >= (uint64_t)(s->committed - s->base)) {
        return;
    }
    unsigned char *from = s->base + keep;
#if defined(_WIN32)
    if (!VirtualFree(from, (SIZE_T)(s->committed - from), MEM_DECOMMIT)) {
        rh_panic("data stack decommit failed", 0);
    }
#else
    if (madvise(from, (size_t)(s->committed - from), MADV_DONTNEED)) {
        rh_panic("data stack release failed", 0);
    }
#endif
    s->committed = from;
}

// panic^, or an error that try^ passed out of the entry file, ends the
// process with its message or error variant name.
_Noreturn void rh_terminate(const char *text, int64_t length, int32_t line, int32_t panic)
{
    fflush(stdout);
    fprintf(stderr, "R^ runtime error at line %" PRId32 ": %s%.*s\n", line,
            panic ? "panic: " : "unhandled error ", (int)length, text);
    exit(1);
}

// Where an error was built: its source, line and column.
typedef struct {
    const char *source;
    int64_t length;
    uint32_t line, column;
} RhSite;

_Noreturn void rh_unhandled(const char *name, int64_t length, const char *message,
                            int64_t message_length, const RhSite *site, int32_t line, int32_t panic)
{
    fflush(stdout);
    // A message that only repeats the variant's name, its default, is shown once.
    if (message_length == length && !memcmp(message, name, (size_t)length)) {
        message_length = 0;
    }
    fprintf(stderr, "R^ runtime error at line %" PRId32 ": %s%.*s%s%.*s", line,
            panic ? "panic: " : "unhandled error ", (int)length, name, message_length ? ": " : "",
            (int)message_length, message);
    if (site) {
        fprintf(stderr, " (raised at %.*s:%" PRIu32 ":%" PRIu32 ")", (int)site->length,
                site->source, site->line, site->column);
    }
    fputc('\n', stderr);
    exit(1);
}

_Noreturn void rh_assert_fail(int32_t line)
{
    rh_panic("assertion failed", line);
}

_Noreturn void rh_float_cast_fail(int32_t line)
{
    rh_panic("floating value out of integer range", line);
}

// A real in its shortest spelling that reads back the same: positional
// unless its exponent is below -6 or at least 21, as JavaScript writes
// numbers, with .0 when it would read as an integer, and nan for every NaN.
static int rh_real_text(char *out, size_t cap, double n)
{
    if (n != n) {
        return snprintf(out, cap, "nan");
    }
    int digits = 1;
    while (digits < 17 && (snprintf(out, cap, "%.*e", digits - 1, n), strtod(out, NULL) != n)) {
        digits++;
    }
    snprintf(out, cap, "%.*e", digits - 1, n);
    const char *e = strchr(out, 'e');
    int exponent = e ? atoi(e + 1) : 0, length;
    if (!e || exponent < -6 || exponent >= 21) {
        length = snprintf(out, cap, "%.*g", digits, n);
    }
    else if (exponent >= digits - 1) {
        // Past its digits, a whole number goes on in zeros, not the binary value's.
        char mantissa[24];
        int count = 0;
        for (const char *c = out; c < e; c++) {
            if (*c >= '0' && *c <= '9') {
                mantissa[count++] = *c;
            }
        }
        length = snprintf(out, cap, "%s%.*s%0*d", out[0] == '-' ? "-" : "", count, mantissa,
                          exponent - (digits - 1), 0);
        length -= exponent == digits - 1;
    }
    else {
        length = snprintf(out, cap, "%.*f", digits - 1 - exponent, n);
    }
    bool whole = true;
    for (int j = 0; j < length; j++) {
        whole &= (out[j] >= '0' && out[j] <= '9') || out[j] == '-';
    }
    return whole ? length + snprintf(out + length, cap - (size_t)length, ".0") : length;
}

void rh_print_f64(double n)
{
    char text[40];
    fwrite(text, 1, (size_t)rh_real_text(text, sizeof text, n), stdout);
}

void rh_print_bool(int64_t b)
{
    fputs(b ? "true" : "false", stdout);
}

void rh_print_string(const char *s, uint64_t n)
{
    fwrite(s, 1, (size_t)n, stdout);
}

void rh_print_space(void)
{
    fputc(' ', stdout);
}

int64_t rh_text_format(unsigned char *out, int64_t cap, const void *value, int64_t kind,
                       const char *format, int64_t length, int32_t line);

// Write a Unicode scalar value as UTF-8.
void rh_print_char(uint32_t c)
{
    unsigned char out[48];
    fwrite(out, 1, (size_t)rh_text_format(out, sizeof out, &c, 32 | 4 << 8, "", 0, 0), stdout);
}

// The width of the UTF-8 scalar value at s within n bytes, with its value
// in *code, or 0 when the bytes there encode none.
static int64_t rh_utf8(const unsigned char *s, int64_t n, int64_t *code)
{
    int64_t width = n <= 0 ? 0 : s[0] < 0x80 ? 1 : s[0] >= 0xF0 ? 4 : s[0] >= 0xE0 ? 3 : 2;
    if (!width || width > n || (s[0] >= 0x80 && (s[0] < 0xC2 || s[0] > 0xF4))) {
        return 0;
    }
    int64_t c = width == 1 ? s[0] : s[0] & (0x7F >> width);
    for (int64_t j = 1; j < width; j++) {
        if ((s[j] & 0xC0) != 0x80) {
            return 0;
        }
        c = c << 6 | (s[j] & 0x3F);
    }
    int64_t least = width == 2 ? 0x80 : width == 3 ? 0x800 : width == 4 ? 0x10000 : 0;
    *code = c;
    return c < least || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF) ? 0 : width;
}

// Whether the bytes are UTF-8.
int64_t rh_text_valid(const unsigned char *s, int64_t n, int32_t line)
{
    (void)line;
    int64_t code;
    for (int64_t at = 0, w; at < n; at += w) {
        if (!(w = rh_utf8(s + at, n - at, &code))) {
            return 0;
        }
    }
    return 1;
}

// The number of scalar values in UTF-8 text: its bytes that start one.
int64_t rh_text_count(const unsigned char *s, int64_t n, int32_t line)
{
    (void)line;
    int64_t count = 0;
    for (int64_t at = 0; at < n; at++) {
        count += (s[at] & 0xC0) != 0x80;
    }
    return count;
}

// The first offset where t occurs in s, or -1.
int64_t rh_text_find(const unsigned char *s, int64_t n, const unsigned char *t, int64_t m,
                     int32_t line)
{
    (void)line;
    for (int64_t at = 0; at + m <= n; at++) {
        if (!memcmp(s + at, t, (size_t)m)) {
            return at;
        }
    }
    return -1;
}

int64_t rh_text_starts(const unsigned char *s, int64_t n, const unsigned char *t, int64_t m,
                       int32_t line)
{
    (void)line;
    return m <= n && !memcmp(s, t, (size_t)m);
}

int64_t rh_text_ends(const unsigned char *s, int64_t n, const unsigned char *t, int64_t m,
                     int32_t line)
{
    (void)line;
    return m <= n && !memcmp(s + n - m, t, (size_t)m);
}

// -1, 0 or 1 as s orders before, with or after t byte by byte.
int64_t rh_text_compare(const unsigned char *s, int64_t n, const unsigned char *t, int64_t m,
                        int32_t line)
{
    (void)line;
    int order = memcmp(s, t, (size_t)(n < m ? n : m));
    return order < 0 ? -1 : order > 0 ? 1 : n < m ? -1 : n > m;
}

_Noreturn static void rh_text_fail(const char *what, int32_t line)
{
    fflush(stdout);
    fprintf(stderr, "R^ runtime error at line %" PRId32 ": %s\n", line, what);
    exit(1);
}

// The scalar value at byte offset at, which must start one.
int64_t rh_text_decode(const unsigned char *s, int64_t n, int64_t at, int32_t line)
{
    int64_t code;
    if (at < 0 || at >= n || !rh_utf8(s + at, n - at, &code)) {
        rh_text_fail("decode position is not the start of a character", line);
    }
    return code;
}

// The offset after the scalar value at byte offset at.
int64_t rh_text_next(const unsigned char *s, int64_t n, int64_t at, int32_t line)
{
    int64_t code, width = at < 0 || at >= n ? 0 : rh_utf8(s + at, n - at, &code);
    if (!width) {
        rh_text_fail("decode position is not the start of a character", line);
    }
    return at + width;
}

// Fail unless lo and hi both fall between characters.
void rh_text_boundary(const unsigned char *s, int64_t n, int64_t lo, int64_t hi, int32_t line)
{
    if ((lo < n && (s[lo] & 0xC0) == 0x80) || (hi < n && (s[hi] & 0xC0) == 0x80)) {
        rh_text_fail("string slice cuts a character", line);
    }
}

// The scalar value c if it is one, else -1.
int64_t rh_char_of_whole(int64_t c)
{
    return c >= 0 && c <= 0x10FFFF && (c < 0xD800 || c > 0xDFFF) ? c : -1;
}

// The scalar value of text if it is exactly one in UTF-8, else -1.
int64_t rh_char_of_text(const unsigned char *s, int64_t n)
{
    int64_t code;
    return n && rh_utf8(s, n, &code) == n ? code : -1;
}

void rh_print_newline(void)
{
    fputc('\n', stdout);
}

// Byte-oriented integer helpers keep the C/LLVM ABI identical for i128 and
// narrow integers, and do not require a C compiler-specific 128-bit ABI.
// An integer of bits bits in decimal, in at most 41 bytes.
static int rh_integer_text(char *out, const void *value, int32_t bits, int32_t uns)
{
    unsigned char bytes[16] = {0};
    int n = bits / 8, length = 0;
    memcpy(bytes, value, (size_t)n);
    if (!uns && (bytes[n - 1] & 128)) {
        out[length++] = '-';
        unsigned carry = 1;
        for (int j = 0; j < n; j++) {
            unsigned v = (unsigned)(bytes[j] ^ 255u) + carry;
            bytes[j] = (unsigned char)v;
            carry = v >> 8;
        }
    }
    char digits[40];
    int count = 0;
    bool nonzero;
    do {
        unsigned remainder = 0;
        nonzero = false;
        for (int j = n - 1; j >= 0; j--) {
            unsigned v = remainder * 256 + bytes[j];
            bytes[j] = (unsigned char)(v / 10);
            remainder = v % 10;
            nonzero = nonzero || bytes[j] != 0;
        }
        digits[count++] = (char)('0' + remainder);
    } while (nonzero);
    while (count) {
        out[length++] = digits[--count];
    }
    return length;
}

void rh_print_integer(const void *value, int32_t bits, int32_t uns)
{
    char text[41];
    fwrite(text, 1, (size_t)rh_integer_text(text, value, bits, uns), stdout);
}

// A number in C format f of length n, which holds exactly one numeric
// conversion with flags, width and precision, no * and no length modifier;
// the conversion decides whether the number reads as integer or real.
// Anything else ends the program, as does text longer than cap.
static int64_t rh_formatted(char *out, int64_t cap, const char *f, int64_t n, int bits, int form,
                            const void *value, int32_t line)
{
    char spec[80];
    int length = 0, conversions = 0;
    char conversion = 0;
    for (int64_t j = 0; j < n; j++) {
        if (length > 72) {
            rh_text_fail("a format is at most 72 bytes", line);
        }
        spec[length++] = f[j];
        if (f[j] != '%') {
            continue;
        }
        if (j + 1 < n && f[j + 1] == '%') {
            spec[length++] = f[++j];
            continue;
        }
        while (j + 1 < n && strchr("-+ #0", f[j + 1]) && length < 72) {
            spec[length++] = f[++j];
        }
        while (j + 1 < n && ((f[j + 1] >= '0' && f[j + 1] <= '9') || f[j + 1] == '.') &&
               length < 72) {
            spec[length++] = f[++j];
        }
        conversion = j + 1 < n ? f[++j] : 0;
        if (!conversion || !strchr("diouxXfFeEgGaA", conversion) || ++conversions > 1) {
            rh_text_fail("a format holds one numeric conversion such as %d or %.2f", line);
        }
        if (strchr("diouxX", conversion)) {
            spec[length++] = 'l';
            spec[length++] = 'l';
        }
        spec[length++] = conversion;
    }
    if (!conversions || bits > 64) {
        rh_text_fail(bits > 64 ? "a format writes integers of at most 64 bits"
                               : "a format holds one numeric conversion such as %d or %.2f",
                     line);
    }
    spec[length] = 0;
    double real = form == 2 ? (bits == 32 ? *(const float *)value : *(const double *)value) : 0;
    long long whole = 0;
    if (form != 2) {
        whole = bits == 8    ? (form ? *(const uint8_t *)value : *(const int8_t *)value)
                : bits == 16 ? (form ? *(const uint16_t *)value : *(const int16_t *)value)
                : bits == 32
                    ? (form ? (long long)*(const uint32_t *)value : *(const int32_t *)value)
                    : *(const long long *)value;
        real = form ? (double)(unsigned long long)whole : (double)whole;
    }
    bool integral = strchr("diouxX", conversion) != NULL;
    if (integral && form == 2) {
        whole = (long long)real;
    }
    int written =
        integral ? snprintf(out, (size_t)cap, spec, whole) : snprintf(out, (size_t)cap, spec, real);
    if (written < 0 || written >= cap) {
        rh_text_fail("formatted text is longer than 255 bytes", line);
    }
    return written;
}

// Write the value at value into out, at least 256 bytes, and answer its
// length: in format f of length n when n > 0. kind is its width in bits,
// plus 256 for unsigned, 512 for a real, 768 for bool^ and 1024 for char^.
int64_t rh_text_format(unsigned char *out, int64_t cap, const void *value, int64_t kind,
                       const char *format, int64_t length, int32_t line)
{
    int bits = (int)(kind & 255), form = (int)(kind >> 8);
    if (length) {
        return rh_formatted((char *)out, cap, format, length, bits, form, value, line);
    }
    if (form == 2) {
        return rh_real_text((char *)out, (size_t)cap,
                            bits == 32 ? *(const float *)value : *(const double *)value);
    }
    if (form == 3) {
        bool truth = *(const unsigned char *)value & 1;
        memcpy(out, truth ? "true" : "false", truth ? 4 : 5);
        return truth ? 4 : 5;
    }
    if (form == 4) {
        uint32_t c = *(const uint32_t *)value;
        int n = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
        out[0] = (unsigned char)(n == 1 ? c : (0xF00u >> n & 0xF0u) | (c >> (6 * (n - 1))));
        for (int j = 1; j < n; j++) {
            out[j] = (unsigned char)(0x80u | ((c >> (6 * (n - 1 - j))) & 0x3Fu));
        }
        return n;
    }
    return rh_integer_text((char *)out, value, bits, form == 1);
}
