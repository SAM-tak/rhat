#if !defined(_WIN32)
#define _XOPEN_SOURCE 700
#endif
#include "rhat.h"
#include <errno.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#include <process.h>
#include <windows.h>
#define pid _getpid
#else
#include <unistd.h>
#define pid getpid
#endif
#ifndef RH_RANDOM_PATH
#define RH_RANDOM_PATH "runtime/rhat_random.c"
#endif

static char *directory(const char *path) {
  char *s = str(path), *end = NULL;
  for (char *c = s; *c; c++)
    if (*c == '/' || *c == '\\')
      end = c;
  if (!end)
    return str(".");
  *end = 0;
  return s;
}
static void mkdirs(const char *path) {
  char *copy = str(path);
  for (char *c = copy;; c++) {
    bool end = !*c;
    if (end || *c == '/' || *c == '\\') {
      char saved = *c;
      *c = 0;
      if (*copy && !(c == copy + 2 && copy[1] == ':')) {
#if defined(_WIN32)
        int status = _mkdir(copy);
#else
        int status = mkdir(copy, 0777);
#endif
        if (status && errno != EEXIST)
          fail((Loc){path, 1, 1}, "cannot create cache directory: %s",
               strerror(errno));
      }
      *c = saved;
    }
    if (end)
      break;
  }
}
static char *executable(const char *name) {
#if defined(_WIN32)
  DWORD n = SearchPathA(NULL, name, ".exe", 0, NULL, NULL);
  if (n) {
    char *out = mem((size_t)n + 1);
    if (SearchPathA(NULL, name, ".exe", n + 1, out, NULL))
      return out;
  }
  char *full = _fullpath(NULL, name, 0);
  return full ? full : str(name);
#else
  if (strchr(name, '/')) {
    char *p = realpath(name, NULL);
    return p ? p : str(name);
  }
  const char *env = getenv("PATH");
  char *paths = str(env ? env : "");
  char *at = paths;
  for (;;) {
    char *end = strchr(at, ':');
    if (end)
      *end = 0;
    char *p = fmt("%s/%s", *at ? at : ".", name);
    if (access(p, X_OK) == 0) {
      char *full = realpath(p, NULL);
      return full ? full : p;
    }
    if (!end)
      break;
    at = end + 1;
  }
  return str(name);
#endif
}
static void publish(const char *tmp, const char *path) {
  /* Content-addressed inputs are immutable. Leave an identical published file
   * in place, including while Windows tools have a read handle open on it. */
  char *old_hash = digest_file(path);
  if (old_hash) {
    char *new_hash = digest_file(tmp);
    if (new_hash && !strcmp(old_hash, new_hash)) {
      remove(tmp);
      return;
    }
  }
#if defined(_WIN32)
  DWORD error = 0;
  for (int attempt = 0; attempt < 25; attempt++) {
    if (MoveFileExA(tmp, path,
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      return;
    error = GetLastError();
    if (error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION &&
        error != ERROR_LOCK_VIOLATION)
      break;
    Sleep(10);
  }
  fail((Loc){path, 1, 1}, "cannot publish cache artifact (%lu)",
       (unsigned long)error);
#else
  if (rename(tmp, path))
    fail((Loc){path, 1, 1}, "cannot publish cache artifact");
#endif
}
const char *tool_resource(const char *compiler, const char *fallback,
                          const char *relative) {
  char *candidate =
      fmt("%s/../share/rhat/%s", directory(executable(compiler)), relative);
  struct stat info;
  return !stat(candidate, &info) ? candidate : fallback;
}
static char *compile(const char *input, const char *identity, const char *name,
                     const char *clang, const char *opt, const char *cache,
                     bool reuse, bool verbose) {
  /* Hash the complete native translation unit, including headers and expanded
   * macros. This also makes custom --runtime wrappers safe to cache. The .i
   * input is what Clang subsequently compiles, so the cache key and object
   * always describe the same preprocessed source. */
  size_t input_length = strlen(input);
  if (input_length < 3 || strcmp(input + input_length - 3, ".ll")) {
    static int sequence;
    char *preprocessed =
        fmt("%s/native-%d-%d.tmp.i", cache, (int)pid(), sequence++);
    char *preprocess_args[] = {(char *)clang, "-std=c11",   "-E", (char *)input,
                               "-o",          preprocessed, NULL};
    int status = run_process(preprocess_args);
    if (status)
      fail((Loc){input, 1, 1}, "Clang preprocessing failed (%d)", status);
    char *expanded_hash = digest_file(preprocessed);
    if (!expanded_hash)
      fail((Loc){input, 1, 1}, "cannot read preprocessed runtime");
    char *expanded = fmt("%s/%s.i", cache, expanded_hash);
    publish(preprocessed, expanded);
    input = expanded;
  }
  char *source_hash = digest_file(input);
  if (!source_hash)
    fail((Loc){input, 1, 1}, "cannot read compilation input");
  char *key =
      link_identity(fmt("rhat-objects-v1:%s:%s:%s:host-x86-64:c11:compiler-rt",
                        identity, opt, source_hash));
  char *object = fmt("%s/%s.obj", cache, key),
       *stamp = fmt("%s/%s.sha256", cache, key);
  FILE *f = reuse ? fopen(stamp, "rb") : NULL;
  if (f) {
    char recorded[66] = {0};
    size_t n = fread(recorded, 1, 65, f);
    fclose(f);
    char *actual = digest_file(object);
    if (n == 64 && actual && !strcmp(recorded, actual)) {
      if (verbose)
        fprintf(stderr, "cache hit: %s\n", name);
      return object;
    }
  }
  if (verbose)
    fprintf(stderr, "compile: %s\n", name);
  char *temporary = fmt("%s/%s.%d.tmp.obj", cache, key, (int)pid());
  char *args[] = {
      (char *)clang, "-Wno-override-module", "-std=c11", (char *)opt,
      "-c",          (char *)input,          "-o",       temporary,
      NULL};
  int status = run_process(args);
  if (status)
    fail((Loc){input, 1, 1},
         "Clang object compilation failed (%d); input retained", status);
  char *hash = digest_file(temporary);
  if (!hash)
    fail((Loc){temporary, 1, 1}, "cannot read compiled object");
  publish(temporary, object);
  char *stamp_tmp = fmt("%s.%d.tmp", stamp, (int)pid());
  f = fopen(stamp_tmp, "wb");
  if (!f)
    fail((Loc){stamp_tmp, 1, 1}, "cannot write cache stamp");
  bool bad = fwrite(hash, 1, 64, f) != 64;
  if (fclose(f) || bad)
    fail((Loc){stamp_tmp, 1, 1}, "cannot write cache stamp");
  publish(stamp_tmp, stamp);
  return object;
}
/* Identify a large tool by size and modification time, as ccache's mtime check
 * does; hashing a 100 MB clang dominated the time of every build. */
static char *file_stamp(const char *path)
{
  struct stat st;
  if (stat(path, &st)) {
    return NULL;
  }
  return fmt("%lld:%lld", (long long)st.st_size, (long long)st.st_mtime);
}
int build_objects(Program *p, const char *compiler, const char *clang,
                  const char *runtime, const char *opt, const char *out,
                  const char *cache, bool reuse, bool verbose) {
  if (!cache)
    cache = fmt("%s/.rhat-cache", directory(out));
  mkdirs(cache);
  char *compiler_hash = digest_file(executable(compiler));
  char *clang_path = executable(clang), *clang_hash = file_stamp(clang_path);
  if (!clang_hash) {
    errno = ENOENT;
    return -1;
  }
  if (!compiler_hash)
    fail((Loc){clang, 1, 1},
         "cannot identify compiler executable for object cache");
  char *identity = fmt("%s:%s:%s", compiler_hash, clang_path, clang_hash);
  const char *environment[] = {"INCLUDE", "CPATH", "C_INCLUDE_PATH", "SDKROOT"};
  for (size_t j = 0; j < sizeof(environment) / sizeof(*environment); j++) {
    const char *value = getenv(environment[j]);
    identity = fmt("%s:%s=%s", identity, environment[j], value ? value : "");
  }
  /* Every definition referenced by an imported inline body needs linkage too.
   */
  for (int j = 0; j < p->modules.n; j++) {
    p->emitting = p->modules.v[j];
    char *probe = fmt("%s/probe-%d-%d.ll", cache, (int)pid(), j);
    emit_llvm(p, probe);
    for (int k = 0; k < p->instances.n; k++) {
      Instance *in = p->instances.v[k];
      if (in->emitted && in->fn->module != p->emitting)
        in->cross_module = true;
    }
    remove(probe);
  }
  List args = {0};
  add(&args, clang_path);
  add(&args, "-rtlib=compiler-rt");
  bool random = false;
  for (int j = 0; j < p->modules.n; j++) {
    Module *m = p->modules.v[j];
    p->emitting = m;
    if (m->name && !strcmp(m->name, "std.random"))
      random = true;
    char *temporary = fmt("%s/module-%d-%d.tmp.ll", cache, (int)pid(), j);
    emit_llvm(p, temporary);
    char *ir = fmt("%s/%s.ll", cache, digest_file(temporary));
    publish(temporary, ir);
    if (verbose)
      fprintf(stderr, "IR: %s -> %s\n", m->name ? m->name : "<entry>", ir);
    add(&args, compile(ir, identity, m->name ? m->name : "<entry>", clang_path,
                       opt, cache, reuse, verbose));
  }
  p->emitting = NULL;
  add(&args, compile(runtime, identity, "runtime", clang_path, opt, cache,
                     reuse, verbose));
  if (random) {
    add(&args, compile(tool_resource(compiler, RH_RANDOM_PATH, "rhat_random.c"),
                       identity, "random OS bridge", clang_path, opt, cache,
                       reuse, verbose));
#if defined(_WIN32)
    add(&args, "-lbcrypt");
#endif
  }
#if !defined(_WIN32)
  add(&args, "-lm");
#endif
  add(&args, "-o");
  add(&args, (void *)out);
  add(&args, NULL);
  if (verbose)
    fprintf(stderr, "link: %s\n", out);
  return run_process((char *const *)args.v);
}
