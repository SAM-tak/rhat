#if !defined(_WIN32)
#define _XOPEN_SOURCE 700
#endif
#include "rhat.h"
#include <errno.h>
#if defined(_WIN32)
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif
#ifndef RH_RUNTIME_PATH
#define RH_RUNTIME_PATH "runtime/rhat_runtime.c"
#endif
#ifndef RH_DEFAULT_CLANG
#define RH_DEFAULT_CLANG "clang"
#endif
#if defined(_WIN32)
/* The CRT spawn API joins arguments into a command line without quoting. */
static char *windows_argument(const char *arg) {
  char *out = mem(strlen(arg) * 2 + 3), *p = out;
  *p++ = '"';
  while (*arg) {
    size_t slashes = 0;
    while (*arg == '\\') {
      slashes++;
      arg++;
    }
    if (*arg == '"' || !*arg) {
      for (size_t n = 0; n < slashes * 2; n++)
        *p++ = '\\';
      if (*arg == '"') {
        *p++ = '\\';
        *p++ = *arg++;
      }
    } else {
      for (size_t n = 0; n < slashes; n++)
        *p++ = '\\';
      *p++ = *arg++;
    }
  }
  *p++ = '"';
  *p = 0;
  return out;
}
#endif
static int spawn(char *const *args) {
#if defined(_WIN32)
  List quoted = {0};
  for (int n = 0; args[n]; n++)
    add(&quoted, windows_argument(args[n]));
  add(&quoted, NULL);
  return (int)_spawnvp(_P_WAIT, args[0], (const char *const *)quoted.v);
#else
  pid_t child = fork();
  if (child < 0)
    return -1;
  if (!child) {
    execvp(args[0], args);
    perror(args[0]);
    _exit(127);
  }
  int status;
  pid_t waited;
  do {
    waited = waitpid(child, &status, 0);
  } while (waited < 0 && errno == EINTR);
  if (waited < 0)
    return -1;
  return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
}
static bool same_path(const char *a, const char *b) {
  if (!a || !b)
    return false;
#if defined(_WIN32)
  char *x = _fullpath(NULL, a, 0), *y = _fullpath(NULL, b, 0);
  bool equal = x && y && !_stricmp(x, y);
#else
  char *x = realpath(a, NULL), *y = realpath(b, NULL);
  bool equal = x && y ? !strcmp(x, y) : !strcmp(a, b);
#endif
  free(x);
  free(y);
  return equal;
}
static void usage(void) {
  puts("R^ compiler 0.1\nUsage: rhat check SOURCE\n       rhat emit SOURCE -o "
       "OUTPUT.ll [--dump-ir OUTPUT.rir]\n       rhat build SOURCE -o OUTPUT "
       "[--clang PATH] [--runtime PATH] [-O0|-O2]\n       rhat run SOURCE "
       "[--clang PATH] [--runtime PATH]\nOptions: --stacks, --emit-llvm PATH, "
       "--dump-ir "
       "PATH\nTarget: host x86-64, C runtime ABI");
}
int main(int argc, char **argv) {
  if (argc < 2 || !strcmp(argv[1], "--help") || !strcmp(argv[1], "-h")) {
    usage();
    return argc < 2 ? 2 : 0;
  }
  if (!strcmp(argv[1], "--version")) {
    puts("rhat 0.1.0");
    return 0;
  }
  const char *command = argv[1];
  if (strcmp(command, "check") && strcmp(command, "emit") &&
      strcmp(command, "build") && strcmp(command, "run")) {
    usage();
    return 2;
  }
  if (argc < 3) {
    usage();
    return 2;
  }
  const char *source = argv[2], *out = NULL, *ll = NULL, *rir = NULL,
             *clang = getenv("RHAT_CLANG"), *runtime = getenv("RHAT_RUNTIME"),
             *opt = "-O0";
  if (!clang || !*clang)
    clang = RH_DEFAULT_CLANG;
  if (!runtime)
    runtime = RH_RUNTIME_PATH;
  bool stacks = false;
  for (int j = 3; j < argc; j++) {
    char *s = argv[j];
    if (!strcmp(s, "--stacks")) {
      stacks = true;
      continue;
    }
    if (!strcmp(s, "-O0") || !strcmp(s, "-O2")) {
      opt = s;
      continue;
    }
    if (j + 1 >= argc) {
      fprintf(stderr, "missing value for '%s'\n", s);
      return 2;
    }
    const char *v = argv[++j];
    if (!strcmp(s, "-o"))
      out = v;
    else if (!strcmp(s, "--clang"))
      clang = v;
    else if (!strcmp(s, "--runtime"))
      runtime = v;
    else if (!strcmp(s, "--emit-llvm"))
      ll = v;
    else if (!strcmp(s, "--dump-ir"))
      rir = v;
    else {
      fprintf(stderr, "unknown option '%s'\n", s);
      return 2;
    }
  }
  if (!strcmp(command, "emit") && !out && !ll) {
    fputs("emit requires -o OUTPUT.ll\n", stderr);
    return 2;
  }
  bool native = !strcmp(command, "build") || !strcmp(command, "run");
  if (native && !out) {
#if defined(_WIN32)
    out = fmt("%s.exe", source);
#else
    out = fmt("%s.out", source);
#endif
  }
  if (native && !ll)
    ll = fmt("%s.ll", out);
  const char *outputs[] = {out, ll, rir};
  for (int j = 0; j < 3; j++) {
    if (same_path(outputs[j], source) || same_path(outputs[j], runtime)) {
      fputs("output would overwrite a source or runtime file\n", stderr);
      return 2;
    }
    for (int k = 0; k < j; k++) {
      if (same_path(outputs[j], outputs[k])) {
        fputs("output paths must be distinct\n", stderr);
        return 2;
      }
    }
  }
  Program p = {0};
  parse(&p, source);
  analyze(&p);
  plan_stacks(&p);
  analyze_array_growth(&p);
  if (stacks)
    report_stacks(&p);
  if (rir)
    dump_ir(&p, rir);
  if (!strcmp(command, "check"))
    return 0;
  if (!strcmp(command, "emit")) {
    emit_llvm(&p, ll ? ll : out);
    return 0;
  }
  emit_llvm(&p, ll);
  char *args[] = {(char *)clang,        "-Wno-override-module",
                  "-std=c11",           (char *)opt,
                  "-rtlib=compiler-rt", (char *)ll,
                  (char *)runtime,      "-o",
                  (char *)out,          NULL};
  int status = spawn(args);
  if (status < 0) {
    int error = errno;
    fprintf(stderr,
            "could not start Clang '%s': %s\n"
            "Use --clang PATH or set RHAT_CLANG to the Clang executable.\n"
            "LLVM IR retained at %s\n",
            clang, strerror(error), ll);
    return 1;
  }
  if (status) {
    fprintf(stderr, "Clang failed (%d); LLVM IR retained at %s\n", status, ll);
    return 1;
  }
  if (!strcmp(command, "run")) {
    char *exe = (char *)out;
#if !defined(_WIN32)
    if (!strchr(exe, '/'))
      exe = fmt("./%s", out);
#endif
    char *run[] = {exe, NULL};
    status = spawn(run);
    if (status < 0) {
      perror(exe);
      return 1;
    }
    return status;
  }
  return 0;
}
