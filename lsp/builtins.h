#ifndef RHAT_LSP_BUILTINS_H
#define RHAT_LSP_BUILTINS_H

// Editor descriptions of compiler operations; NULL results are not inferred here.
typedef struct {
    const char *name, *signature, *result;
    const char *parameters[3];
} LspBuiltin;

// Symbolic T denotes the transferred value or channel element type.
static const LspBuiltin lsp_builtins[] = {
    {"thread_entry^",
     "thread_entry^: p^ entry:p^ ref^ T;, args:ref^ T -> std.sys.ThreadEntry",
     "std.sys.ThreadEntry",
     {"entry", "args"}},
    {"thread_transfer_type^",
     "thread_transfer_type^: p^ args:ref^ T -> ref^read^ std.sys.ThreadTransfer",
     "ref^read^ std.sys.ThreadTransfer",
     {"args"}},
    {"thread_args^",
     "thread_args^: p^ args:ref^ T -> ref^read^ std.sys.ThreadArgs",
     "ref^read^ std.sys.ThreadArgs",
     {"args"}},
    {"thread_stack_count^", "thread_stack_count^: p^ -> i64^", "i64^", {0}},
    {"channel_new^",
     "channel_new^: p^ element:type, capacity:i64^, message_bytes:i64^ -> channel_handle^<T>",
     NULL,
     {"element", "capacity", "message_bytes"}},
    {"channel_send^",
     "channel_send^: p^ handle:ref^read^ channel_handle^<T>, value:ref^read^ T, blocking:i64^ -> i64^",
     "i64^",
     {"handle", "value", "blocking"}},
    {"channel_receive^",
     "channel_receive^: p^ handle:ref^read^ channel_handle^<T>, blocking:i64^ -> std.channel.Message<T> | std.channel.Closed | std.channel.Empty",
     NULL,
     {"handle", "blocking"}},
    {"channel_clone^",
     "channel_clone^: p^ handle:ref^read^ channel_handle^<T> -> channel_handle^<T>",
     NULL,
     {"handle"}},
    {"channel_close^", "channel_close^: p^ handle:ref^read^ channel_handle^<T>", NULL, {"handle"}},
    {"channel_valid^",
     "channel_valid^: p^ handle:ref^read^ channel_handle^<T> -> bool^",
     "bool^",
     {"handle"}},
};

#endif
