#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include "port/thread.h"
#else
#include <unistd.h>
#endif
#ifndef RH_STDLIB_PATH
#define RH_STDLIB_PATH "stdlib"
#endif
int main(int argc, char **argv)
{
  const char *stdlib = NULL;
  char installed[4096];
  for (int j = 1; j < argc; j++) {
    if (!strcmp(argv[j], "--version")) {
      puts("rhatls 0.1.0");
      return 0;
    }
    if (!strcmp(argv[j], "--help")) {
      puts("R^ language server\nUsage: rhatls [--stdio] [--stdlib DIR]");
      return 0;
    }
    if (!strcmp(argv[j], "--stdio")) {
      continue;
    }
    if (!strcmp(argv[j], "--stdlib") && j + 1 < argc) {
      stdlib = argv[++j];
      continue;
    }
    fprintf(stderr, "unknown rhatls option: %s\n", argv[j]);
    return 2;
  }
  if (!stdlib) {
#ifdef _WIN32
    DWORD length = GetModuleFileNameA(NULL, installed, sizeof(installed));
    if (!length || length >= sizeof(installed)) {
      installed[0] = 0;
    }
#else
    char *resolved = realpath(argv[0], NULL);
    snprintf(installed, sizeof(installed), "%s", resolved ? resolved : "");
    free(resolved);
#endif
    char *end = NULL;
    for (char *p = installed; *p; p++) {
      if (*p == '/' || *p == '\\') {
        end = p;
      }
    }
    if (end && (size_t)(end - installed) + 40 < sizeof(installed)) {
      strcpy(end, "/../share/rhat/stdlib/std/random.rh");
      FILE *probe = fopen(installed, "rb");
      if (probe) {
        fclose(probe);
        *strstr(end, "/std/random.rh") = 0;
        stdlib = installed;
      }
    }
    if (!stdlib) {
      stdlib = RH_STDLIB_PATH;
    }
  }
  return rhat_lsp_run(argv[0], stdlib);
}
