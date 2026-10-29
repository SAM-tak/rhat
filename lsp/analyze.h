#ifndef RHAT_LSP_ANALYZE_H
#define RHAT_LSP_ANALYZE_H
#include "cJSON.h"
/* Check one root against a JSON snapshot of all unsaved buffers and paths.
   Returns an independently owned index, retaining partial data on failure. */
cJSON *rhat_lsp_analyze(const char *path, const cJSON *documents, const cJSON *module_paths,
                        const char *stdlib);
#endif
