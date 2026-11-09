#ifndef RHAT_FORMAT_H
#define RHAT_FORMAT_H
#include "rhat.h"

// Formatting settings follow EditorConfig; the two language-specific keys use rhat_.
typedef enum { RHAT_EOL_KEEP, RHAT_EOL_LF, RHAT_EOL_CRLF, RHAT_EOL_CR } RhatEndOfLine;

typedef struct {
    bool indent_with_tabs;
    uint32_t indent_size, tab_width, max_line_length;
    RhatEndOfLine end_of_line;
    bool final_newline, brace_next_line, space_after_hat;
} RhatFormatOptions;

typedef enum {
    RHAT_FORMAT_OK,
    RHAT_FORMAT_SYNTAX_ERROR,
    RHAT_FORMAT_CHANGED_TOKENS,
    RHAT_FORMAT_OUT_OF_MEMORY
} RhatFormatStatus;

// Set defaults, then overlay governing .editorconfig files, nearest last.
void rhat_format_defaults(RhatFormatOptions *options);
void rhat_format_read_editorconfig(const char *path, RhatFormatOptions *options);
// Format syntax without resolving imports or types; the caller frees *out.
RhatFormatStatus rhat_format(const char *text, size_t length, const RhatFormatOptions *options,
                             char **out, size_t *out_length);
// Format/check a source file for the CLI without compiling or loading its imports.
int rhat_format_file(const char *path, bool check);

// The ordinary parser supplies original token spans and grammatical extents.
enum { FORMAT_OP, FORMAT_WORD, FORMAT_LITERAL, FORMAT_SCOPE };

// Operator roles come from expression parsing, keeping type delimiters distinct.
enum { FORMAT_OPERATOR_NONE, FORMAT_OPERATOR_PREFIX, FORMAT_OPERATOR_BINARY };

typedef struct {
    const char *text;
    uint32_t offset, length;
    int kind;
    int operator_role;
    bool hat, clause; // Clause labels are identified by the parser, not their spelling.
    bool array_type, parameter_separator; // Wrapping hints supplied by the parser.
    bool nested_array;                    // A trailing element-type bracket inside an array policy.
} FormatToken;

typedef struct {
    int first, last, kind; // Token indexes, last exclusive; also a syntax fingerprint.
    uint32_t start, end;
    bool block, row;
} FormatSpan;

typedef struct FormatSyntax {
    List tokens, spans;
} FormatSyntax;
#endif
