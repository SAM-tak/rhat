#include "server.h"
#include "analyze.h"
#include "position.h"
#include "rpc.h"
#include "tooling.h"
#include "uri.h"
#include <ctype.h>
#include <limits.h>

typedef struct Job Job;
struct Job {
  cJSON *message, *documents, *paths;
  unsigned revision;
  Job *next;
};
typedef struct {
  LspRpcOut out;
  LhatMutex mutex;
  LhatCondition ready;
  LhatThread worker;
  cJSON *documents, *paths, *cancelled, *active_id;
  Job *head, *tail;
  unsigned revision;
  bool initialized, shutdown, stop;
  const char *stdlib;
} Server;
static const char *text(const cJSON *object, const char *key)
{
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(object, key);
  return cJSON_IsString(v) ? v->valuestring : "";
}
static int number(const cJSON *object, const char *key)
{
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(object, key);
  return cJSON_IsNumber(v) ? v->valueint : 0;
}
static cJSON *copy(const cJSON *item)
{
  return cJSON_Duplicate(item, true);
}
static char *canonical_uri(const char *uri)
{
  char *path = lsp_uri_to_absolute_path(uri);
  if (!path) {
    return NULL;
  }
  char *result = lsp_absolute_path_to_uri(path);
  free(path);
  return result;
}
static cJSON *document(cJSON *documents, const char *uri)
{
  char *canonical = canonical_uri(uri);
  cJSON *d, *found = NULL;
  cJSON_ArrayForEach(d, documents)
  {
    if (canonical && !strcmp(text(d, "uri"), canonical)) {
      found = d;
      break;
    }
  }
  free(canonical);
  return found;
}
static bool same_id(cJSON *a, cJSON *b)
{
  return !strcmp(text(a, "id"), text(b, "id"));
}
static cJSON *symbol_at(cJSON *index, const char *uri, size_t offset)
{
  cJSON *s;
  cJSON_ArrayForEach(s, cJSON_GetObjectItem(index, "symbols"))
  {
    cJSON *use = cJSON_GetObjectItem(s, "use");
    size_t start = (size_t)number(use, "offset");
    if (!strcmp(text(use, "uri"), uri) && start <= offset &&
        offset <= start + (size_t)number(s, "length")) {
      return s;
    }
  }
  return NULL;
}
static cJSON *wire_location(cJSON *location)
{
  cJSON *out = cJSON_CreateObject();
  cJSON_AddStringToObject(out, "uri", text(location, "uri"));
  cJSON_AddItemToObject(out, "range",
                        copy(cJSON_GetObjectItem(location, "range")));
  return out;
}
static cJSON *markup(const char *value)
{
  cJSON *out = cJSON_CreateObject();
  cJSON_AddStringToObject(out, "kind", "plaintext");
  cJSON_AddStringToObject(out, "value", value);
  return out;
}
static cJSON *hover(cJSON *symbol)
{
  if (!symbol) {
    return NULL;
  }
  cJSON *out = cJSON_CreateObject();
  cJSON_AddItemToObject(out, "contents", markup(text(symbol, "detail")));
  cJSON_AddItemToObject(
      out, "range",
      copy(cJSON_GetObjectItem(cJSON_GetObjectItem(symbol, "use"), "range")));
  return out;
}
static cJSON *references(cJSON *index, cJSON *symbol, const char *uri,
                         bool include_declarations, bool highlights)
{
  cJSON *out = cJSON_CreateArray(), *s;
  if (!symbol) {
    return out;
  }
  cJSON_ArrayForEach(s, cJSON_GetObjectItem(index, "symbols"))
  {
    if (!same_id(s, symbol)) {
      continue;
    }
    if (!include_declarations &&
        cJSON_IsTrue(cJSON_GetObjectItem(s, "declaration"))) {
      continue;
    }
    cJSON *use = cJSON_GetObjectItem(s, "use");
    if (highlights && strcmp(text(use, "uri"), uri)) {
      continue;
    }
    cJSON *item = highlights ? cJSON_CreateObject() : wire_location(use);
    if (highlights) {
      cJSON_AddItemToObject(item, "range",
                            copy(cJSON_GetObjectItem(use, "range")));
      cJSON_AddNumberToObject(item, "kind", 1);
    }
    cJSON_AddItemToArray(out, item);
  }
  return out;
}
static cJSON *combined_index(cJSON *cache)
{
  cJSON *out = cJSON_CreateObject(),
        *symbols = cJSON_AddArrayToObject(out, "symbols"), *entry;
  cJSON_ArrayForEach(entry, cache)
  {
    cJSON *s;
    cJSON_ArrayForEach(s, cJSON_GetObjectItem(entry, "symbols"))
    {
      bool duplicate = false;
      cJSON *old;
      cJSON_ArrayForEach(old, symbols)
      {
        if (same_id(s, old) &&
            cJSON_Compare(cJSON_GetObjectItem(s, "use"),
                          cJSON_GetObjectItem(old, "use"), true) &&
            cJSON_IsTrue(cJSON_GetObjectItem(s, "declaration")) ==
                cJSON_IsTrue(cJSON_GetObjectItem(old, "declaration"))) {
          duplicate = true;
          break;
        }
      }
      if (!duplicate) {
        cJSON_AddItemToArray(symbols, copy(s));
      }
    }
  }
  return out;
}
static bool contains_case(const char *s, const char *needle)
{
  if (!*needle) {
    return true;
  }
  for (; *s; s++) {
    size_t j = 0;
    while (needle[j] && s[j] &&
           tolower((unsigned char)s[j]) == tolower((unsigned char)needle[j])) {
      j++;
    }
    if (!needle[j]) {
      return true;
    }
  }
  return false;
}
static cJSON *symbols(cJSON *index, const char *uri, const char *query)
{
  cJSON *out = cJSON_CreateArray(), *s;
  cJSON_ArrayForEach(s, cJSON_GetObjectItem(index, "symbols"))
  {
    if (!cJSON_IsTrue(cJSON_GetObjectItem(s, "declaration"))) {
      continue;
    }
    cJSON *use = cJSON_GetObjectItem(s, "use");
    if (uri && strcmp(text(use, "uri"), uri)) {
      continue;
    }
    if (!contains_case(text(s, "name"), query)) {
      continue;
    }
    if (number(s, "kind") == TOOL_PARAMETER) {
      continue;
    }
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "name", text(s, "name"));
    cJSON_AddNumberToObject(item, "kind", number(s, "kind"));
    cJSON_AddStringToObject(item, "containerName", text(s, "owner"));
    cJSON_AddItemToObject(item, "location", wire_location(use));
    cJSON_AddItemToArray(out, item);
  }
  return out;
}
static bool identifier(unsigned char c)
{
  return isalnum(c) || c >= 128 || c == '_' || c == '^';
}
static void completion_item(cJSON *out, const char *label, const char *detail,
                            int kind)
{
  cJSON *old;
  cJSON_ArrayForEach(old, out)
  {
    if (!strcmp(text(old, "label"), label)) {
      return;
    }
  }
  cJSON *item = cJSON_CreateObject();
  cJSON_AddStringToObject(item, "label", label);
  cJSON_AddStringToObject(item, "detail", detail);
  cJSON_AddNumberToObject(item, "kind", kind);
  cJSON_AddItemToArray(out, item);
}
/* Local visibility is bounded by the enclosing brace pair. The scanner skips
   comments and strings, so text containing braces cannot open a fake scope. */
static size_t skip_quoted(const char *s, size_t n)
{
  char quote = s[n++];
  while (s[n]) {
    if (s[n] == '\\' && s[n + 1]) {
      n += 2;
    }
    else if (s[n++] == quote) {
      break;
    }
  }
  return n;
}
static size_t skip_comment(const char *s, size_t n)
{
  if (s[n + 1] != '[') {
    while (s[n] && s[n] != '\n') {
      n++;
    }
    return n;
  }
  int depth = 1;
  n += 2;
  while (s[n] && depth) {
    if (s[n] == '#' && s[n + 1] == '[') {
      depth++;
      n += 2;
    }
    else if (s[n] == ']' && s[n + 1] == '#') {
      depth--;
      n += 2;
    }
    else {
      n++;
    }
  }
  return n;
}
static bool in_scope(const char *s, size_t declaration, size_t cursor,
                     bool parameter)
{
  if (declaration > cursor) {
    return false;
  }
  size_t stack[1024], depth = 0, scope = SIZE_MAX;
  for (size_t n = 0; s[n];) {
    if (n <= declaration && n + 1 > declaration && !parameter) {
      scope = depth ? stack[depth - 1] : SIZE_MAX;
    }
    if (s[n] == '#') {
      n = skip_comment(s, n);
      continue;
    }
    if (s[n] == '"' || s[n] == '\'' || s[n] == '`') {
      n = skip_quoted(s, n);
      continue;
    }
    if (s[n] == '{') {
      if (depth < 1024) {
        stack[depth++] = n;
      }
      if (parameter && n > declaration && scope == SIZE_MAX) {
        scope = n;
      }
    }
    if (s[n] == '}' && depth) {
      size_t start = stack[--depth];
      if (start == scope) {
        return cursor <= n;
      }
    }
    n++;
  }
  return true;
}
static bool inside_owner(cJSON *index, const char *uri, const char *buffer,
                         size_t cursor, const char *owner)
{
  cJSON *symbol;
  cJSON_ArrayForEach(symbol, cJSON_GetObjectItem(index, "symbols"))
  {
    cJSON *use = cJSON_GetObjectItem(symbol, "use");
    if (number(symbol, "kind") == 23 &&
        cJSON_IsTrue(cJSON_GetObjectItem(symbol, "declaration")) &&
        !strcmp(text(symbol, "qualified"), owner) &&
        !strcmp(text(use, "uri"), uri)) {
      return in_scope(buffer, (size_t)number(use, "offset"), cursor, true);
    }
  }
  return false;
}
static cJSON *completion(cJSON *index, const char *uri, const char *buffer,
                         size_t cursor)
{
  cJSON *out = cJSON_CreateArray();
  size_t begin = cursor;
  while (begin && identifier((unsigned char)buffer[begin - 1])) {
    begin--;
  }
  size_t dot = begin;
  bool member = dot && buffer[dot - 1] == '.';
  char receiver[256] = "", type[1024] = "";
  size_t receiver_begin = dot;
  if (member) {
    size_t end = --receiver_begin;
    while (receiver_begin &&
           (identifier((unsigned char)buffer[receiver_begin - 1]) ||
            buffer[receiver_begin - 1] == '.')) {
      receiver_begin--;
    }
    size_t length = end - receiver_begin;
    if (length < sizeof(receiver)) {
      memcpy(receiver, buffer + receiver_begin, length);
    }
    cJSON *r = symbol_at(index, uri, end ? end - 1 : end);
    if (r) {
      snprintf(type, sizeof(type), "%s", text(r, "type"));
    }
    if (!*type) {
      cJSON *s;
      cJSON_ArrayForEach(s, cJSON_GetObjectItem(index, "symbols"))
      {
        cJSON *use = cJSON_GetObjectItem(s, "use");
        if (!strcmp(text(use, "uri"), uri) &&
            !strcmp(text(s, "name"), receiver) &&
            (size_t)number(use, "offset") < cursor) {
          snprintf(type, sizeof(type), "%s", text(s, "type"));
        }
      }
    }
  }
  const char *base = type;
  while (!strncmp(base, "ref^ ", 5)) {
    base += 5;
  }
  cJSON *s;
  cJSON_ArrayForEach(s, cJSON_GetObjectItem(index, "symbols"))
  {
    if (!cJSON_IsTrue(cJSON_GetObjectItem(s, "declaration"))) {
      continue;
    }
    cJSON *use = cJSON_GetObjectItem(s, "use");
    const char *owner = text(s, "owner"), *name = text(s, "name");
    if (member) {
      bool matches = *base && !strcmp(owner, base);
      cJSON *receiver_type;
      cJSON_ArrayForEach(receiver_type, cJSON_GetObjectItem(s, "receivers"))
      {
        matches |= *base && !strcmp(receiver_type->valuestring, base);
      }
      const char *qualified = text(s, "qualified");
      if (!matches && !*owner &&
          !strncmp(qualified, receiver, strlen(receiver)) &&
          qualified[strlen(receiver)] == '.') {
        matches = true;
      }
      cJSON *i;
      cJSON_ArrayForEach(i, cJSON_GetObjectItem(index, "imports"))
      {
        if (!strcmp(text(i, "uri"), uri) &&
            !strcmp(text(i, "alias"), receiver) && !*owner) {
          const char *module = text(i, "module");
          matches |= !strncmp(qualified, module, strlen(module)) &&
                     qualified[strlen(module)] == '.';
        }
      }
      if (!matches || (cJSON_IsTrue(cJSON_GetObjectItem(s, "private")) &&
                       !inside_owner(index, uri, buffer, cursor, owner))) {
        continue;
      }
      if (!*owner && strcmp(text(use, "uri"), uri) &&
          !cJSON_IsTrue(cJSON_GetObjectItem(s, "public"))) {
        continue;
      }
    }
    else {
      if (*owner || strcmp(text(use, "uri"), uri)) {
        continue;
      }
      int kind = number(s, "kind");
      if ((kind == 13 || kind == TOOL_PARAMETER) &&
          !in_scope(buffer, (size_t)number(use, "offset"), cursor,
                    kind == TOOL_PARAMETER)) {
        continue;
      }
    }
    if (strncmp(name, buffer + begin, cursor - begin)) {
      continue;
    }
    int kind = number(s, "kind");
    completion_item(out, name, text(s, "detail"),
                    kind == 12   ? 3
                    : kind == 23 ? 22
                    : kind == 8  ? 5
                    : kind == 6  ? 2
                    : kind == 14 ? 21
                                 : 6);
  }
  if (member &&
      (*base == '[' || strchr(base, '[') || !strcmp(base, "string^"))) {
    const char *array[] = {"len",    "slice", "push",    "pop",
                           "append", "clear", "truncate"};
    for (int j = 0; j < 7; j++) {
      if (j >= 2 && !strstr(base, "[>..]")) {
        continue;
      }
      if (!strncmp(array[j], buffer + begin, cursor - begin)) {
        completion_item(out, array[j], "built-in array member", j ? 2 : 5);
      }
    }
  }
  if (!member) {
    const char *keywords[] = {
        "let^",     "var^",      "f^",        "p^",       "template^",
        "def^",     "errordef^", "self^",     "this^",    "return^",
        "if^",      "else^",     "el^",       "for^",     "repeat^",
        "while^",   "next^",     "break^",    "when^",    "from^",
        "to^",      "until^",    "by^",       "do^",      "fits^",
        "try^",     "catch^",    "import^",   "module^",  "public^",
        "private^", "abstract^", "override^", "mutable^", "true^",
        "false^",   "nil^",      "ref^",      "print",     "assert"};
    for (size_t j = 0; j < sizeof(keywords) / sizeof(*keywords); j++) {
      if (!strncmp(keywords[j], buffer + begin, cursor - begin)) {
        completion_item(out, keywords[j], "R^", 14);
      }
    }
    for (int k = T_BOOL; k < T_STRUCT; k++) {
      const char *name = builtin_name(k);
      if (!strncmp(name, buffer + begin, cursor - begin)) {
        completion_item(out, name, "built-in type", 7);
      }
    }
  }
  return out;
}
static cJSON *signature_help(cJSON *index, const char *uri, const char *buffer,
                             size_t cursor)
{
  size_t open = cursor;
  int depth = 0, argument = 0;
  while (open) {
    char c = buffer[--open];
    if (c == ')') {
      depth++;
    }
    else if (c == '(') {
      if (!depth) {
        break;
      }
      depth--;
    }
    else if (c == ',' && !depth) {
      argument++;
    }
  }
  if (buffer[open] != '(') {
    return NULL;
  }
  size_t end = open;
  while (end && isspace((unsigned char)buffer[end - 1])) {
    end--;
  }
  cJSON *symbol = symbol_at(index, uri, end ? end - 1 : 0);
  if (!symbol || !cJSON_IsArray(cJSON_GetObjectItem(symbol, "parameters"))) {
    return NULL;
  }
  cJSON *out = cJSON_CreateObject(),
        *all = cJSON_AddArrayToObject(out, "signatures");
  cJSON *sig = cJSON_CreateObject(),
        *parameters = cJSON_AddArrayToObject(sig, "parameters");
  cJSON_AddStringToObject(sig, "label", text(symbol, "detail"));
  cJSON *p;
  cJSON_ArrayForEach(p, cJSON_GetObjectItem(symbol, "parameters"))
  {
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "label", p->valuestring);
    cJSON_AddItemToArray(parameters, item);
  }
  cJSON_AddItemToArray(all, sig);
  cJSON_AddNumberToObject(out, "activeSignature", 0);
  int count = cJSON_GetArraySize(parameters);
  cJSON_AddNumberToObject(out, "activeParameter",
                          count && argument >= count ? count - 1 : argument);
  return out;
}
static bool namespace_name(cJSON *index, const char *uri, const char *buffer,
                           size_t start, size_t end)
{
  size_t root = start;
  while (root) {
    size_t at = root;
    while (at && isspace((unsigned char)buffer[at - 1])) {
      at--;
    }
    if (!at || buffer[at - 1] != '.') {
      break;
    }
    at--;
    while (at && isspace((unsigned char)buffer[at - 1])) {
      at--;
    }
    size_t tail = at;
    while (at && identifier((unsigned char)buffer[at - 1]) &&
           buffer[at - 1] != '^') {
      at--;
    }
    if (at == tail) {
      break;
    }
    root = at;
  }
  /* A checked local or receiver takes precedence over an import spelling. */
  if (symbol_at(index, uri, root)) {
    return false;
  }
  cJSON *import;
  cJSON_ArrayForEach(import, cJSON_GetObjectItem(index, "imports"))
  {
    if (strcmp(text(import, "uri"), uri)) {
      continue;
    }
    const char *names[] = {text(import, "alias"), text(import, "module")};
    for (int j = 0; j < 2; j++) {
      const char *name = names[j];
      size_t at = root;
      bool contains = false;
      while (*name) {
        size_t length = strcspn(name, ".");
        if (strncmp(buffer + at, name, length) ||
            identifier((unsigned char)buffer[at + length])) {
          break;
        }
        contains |= at == start && at + length == end;
        at += length;
        name += length;
        if (!*name) {
          if (contains) {
            return true;
          }
          break;
        }
        while (isspace((unsigned char)buffer[at])) {
          at++;
        }
        if (buffer[at++] != '.') {
          break;
        }
        while (isspace((unsigned char)buffer[at])) {
          at++;
        }
        name++;
      }
    }
  }
  return false;
}
static cJSON *semantic_tokens(cJSON *index, const char *uri, const char *buffer)
{
  cJSON *out = cJSON_CreateObject(),
        *data = cJSON_AddArrayToObject(out, "data");
  int last_line = 0, last_col = 0;
  size_t length = strlen(buffer);
  for (size_t at = 0; at < length;) {
    size_t start = at;
    int kind = -1, modifiers = 0;
    unsigned char c = (unsigned char)buffer[at];
    if (isspace(c)) {
      at++;
      continue;
    }
    if (c == '#') {
      at = skip_comment(buffer, at);
    }
    else if (c == '"' || c == '\'' || c == '`') {
      at = skip_quoted(buffer, at);
    }
    else if (isdigit(c)) {
      at++;
      while (identifier((unsigned char)buffer[at]) || buffer[at] == '.') {
        at++;
      }
    }
    else if (identifier(c)) {
      at++;
      while (buffer[at] != '^' && identifier((unsigned char)buffer[at])) {
        at++;
      }
      /* A hat terminates the word; the next name may start immediately. */
      while (buffer[at] == '^') {
        at++;
      }
      cJSON *s = symbol_at(index, uri, start);
      if (s && buffer[at - 1] != '^') {
        int symbol_kind = number(s, "kind");
        kind = symbol_kind == 23               ? 1
               : symbol_kind == 12             ? 2
               : symbol_kind == TOOL_PARAMETER ? 4
               : symbol_kind == 8              ? 6
               : *text(s, "owner")             ? 3
                                               : 5;
        modifiers = cJSON_IsTrue(cJSON_GetObjectItem(s, "declaration")) ? 1 : 0;
      }
      else if (buffer[at - 1] != '^' &&
               namespace_name(index, uri, buffer, start, at)) {
        kind = 11;
      }
    }
    else {
      at++;
    }
    /* Let TextMate style lexical tokens, including the separately dimmed hat.
       Semantic tokens describe resolved names only, as in the L^ client. */
    if (kind < 0) {
      continue;
    }
    /* LSP tokens are split at physical line boundaries, even inside strings. */
    while (start < at) {
      size_t end = start;
      while (end < at && buffer[end] != '\n' && buffer[end] != '\r') {
        end++;
      }
      if (end > start) {
        LspPosition a = lsp_position_at(buffer, length, (uint32_t)start),
                    b = lsp_position_at(buffer, length, (uint32_t)end);
        int values[] = {a.line - last_line,
                        a.line == last_line ? a.character - last_col
                                            : a.character,
                        b.character - a.character, kind, modifiers};
        for (int j = 0; j < 5; j++) {
          cJSON_AddItemToArray(data, cJSON_CreateNumber(values[j]));
        }
        last_line = a.line;
        last_col = a.character;
      }
      start = end < at ? end + 1 : end;
    }
  }
  return out;
}
static bool cancelled(Server *s, cJSON *id)
{
  bool result = false;
  lhat_mutex_lock(&s->mutex);
  cJSON *v;
  cJSON_ArrayForEach(v, s->cancelled)
  {
    if (cJSON_Compare(v, id, true)) {
      result = true;
      break;
    }
  }
  lhat_mutex_unlock(&s->mutex);
  return result;
}
static bool current(Server *s, Job *job)
{
  lhat_mutex_lock(&s->mutex);
  bool result = s->revision == job->revision && !s->shutdown && !s->stop;
  lhat_mutex_unlock(&s->mutex);
  return result;
}
static void publish(Server *s, Job *job, cJSON *indexes, cJSON **published)
{
  if (!current(s, job)) {
    return;
  }
  cJSON *groups = cJSON_CreateObject(), *uri, *index;
  cJSON_ArrayForEach(uri, *published)
  {
    cJSON_AddItemToObject(groups, uri->valuestring, cJSON_CreateArray());
  }
  cJSON *doc;
  cJSON_ArrayForEach(doc, job->documents)
  {
    if (!cJSON_GetObjectItem(groups, text(doc, "uri"))) {
      cJSON_AddItemToObject(groups, text(doc, "uri"), cJSON_CreateArray());
    }
  }
  cJSON_ArrayForEach(index, indexes)
  {
    cJSON *error;
    cJSON_ArrayForEach(error, cJSON_GetObjectItem(index, "diagnostics"))
    {
      const char *key = text(error, "uri");
      cJSON *list = cJSON_GetObjectItem(groups, key);
      if (!list) {
        list = cJSON_AddArrayToObject(groups, key);
      }
      bool duplicate = false;
      cJSON *old;
      cJSON_ArrayForEach(old, list)
      {
        duplicate |= !strcmp(text(old, "message"), text(error, "message")) &&
                     cJSON_Compare(cJSON_GetObjectItem(old, "range"),
                                   cJSON_GetObjectItem(error, "range"), true);
      }
      if (duplicate) {
        continue;
      }
      cJSON *d = cJSON_CreateObject();
      cJSON_AddItemToObject(d, "range",
                            copy(cJSON_GetObjectItem(error, "range")));
      cJSON_AddStringToObject(d, "message", text(error, "message"));
      cJSON_AddStringToObject(d, "source", "rhatls");
      cJSON_AddNumberToObject(d, "severity", 1);
      cJSON_AddItemToArray(list, d);
    }
  }
  cJSON_Delete(*published);
  *published = cJSON_CreateArray();
  cJSON_ArrayForEach(uri, groups)
  {
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "uri", uri->string);
    doc = document(job->documents, uri->string);
    if (doc) {
      cJSON_AddNumberToObject(params, "version", number(doc, "version"));
    }
    cJSON_AddItemToObject(params, "diagnostics", copy(uri));
    if (cJSON_GetArraySize(uri)) {
      cJSON_AddItemToArray(*published, cJSON_CreateString(uri->string));
    }
    lsp_rpc_send_notification(&s->out, "textDocument/publishDiagnostics",
                              params);
  }
  cJSON_Delete(groups);
}
static cJSON *check_document(Server *s, Job *job, cJSON *doc)
{
  return rhat_lsp_analyze(text(doc, "path"), job->documents, job->paths,
                          s->stdlib);
}
static int worker(void *raw)
{
  Server *s = raw;
  cJSON *cache = cJSON_CreateObject(), *published = cJSON_CreateArray();
  unsigned cached_revision = UINT_MAX;
  for (;;) {
    lhat_mutex_lock(&s->mutex);
    while (!s->head && !s->stop) {
      lhat_condition_wait(&s->ready, &s->mutex);
    }
    if (s->stop) {
      lhat_mutex_unlock(&s->mutex);
      break;
    }
    Job *job = s->head;
    s->head = job->next;
    if (!s->head) {
      s->tail = NULL;
    }
    s->active_id = copy(cJSON_GetObjectItem(job->message, "id"));
    lhat_mutex_unlock(&s->mutex);
    cJSON *message = job->message, *id = cJSON_GetObjectItem(message, "id"),
          *params = cJSON_GetObjectItem(message, "params");
    const char *method = text(message, "method");
    if (cached_revision != job->revision) {
      cJSON_Delete(cache);
      cache = cJSON_CreateObject();
      cached_revision = job->revision;
    }
    if (!id) {
      if (current(s, job)) {
        cJSON *indexes = cJSON_CreateArray(), *doc;
        cJSON_ArrayForEach(doc, job->documents)
        {
          cJSON *index = check_document(s, job, doc);
          if (cJSON_GetObjectItem(cache, text(doc, "uri"))) {
            cJSON_ReplaceItemInObjectCaseSensitive(cache, text(doc, "uri"),
                                                   copy(index));
          }
          else {
            cJSON_AddItemToObject(cache, text(doc, "uri"), copy(index));
          }
          cJSON_AddItemToArray(indexes, index);
        }
        publish(s, job, indexes, &published);
        cJSON_Delete(indexes);
      }
    }
    else if (cancelled(s, id)) {
      lsp_rpc_send_error(&s->out, id, -32800, "Request cancelled");
    }
    else {
      cJSON *td = cJSON_GetObjectItem(params, "textDocument");
      cJSON *doc = document(job->documents, text(td, "uri"));
      cJSON *result = NULL;
      if (doc || !strcmp(method, "workspace/symbol")) {
        cJSON *index =
            doc ? cJSON_GetObjectItem(cache, text(doc, "uri")) : NULL;
        if (doc && !index) {
          index = check_document(s, job, doc);
          cJSON_AddItemToObject(cache, text(doc, "uri"), index);
        }
        const char *uri = doc ? text(doc, "uri") : "",
                   *buffer = doc ? text(doc, "text") : "";
        cJSON *pos = cJSON_GetObjectItem(params, "position");
        size_t offset =
            lsp_offset_at(buffer, strlen(buffer), number(pos, "line"),
                          number(pos, "character"));
        cJSON *symbol = index ? symbol_at(index, uri, offset) : NULL;
        if (!strcmp(method, "textDocument/hover")) {
          result = hover(symbol);
        }
        else if (!strcmp(method, "textDocument/definition")) {
          result = symbol ? wire_location(cJSON_GetObjectItem(symbol, "target"))
                          : NULL;
        }
        else if (!strcmp(method, "textDocument/references")) {
          cJSON *combined = combined_index(cache);
          result = references(combined, symbol, uri,
                              cJSON_IsTrue(cJSON_GetObjectItem(
                                  cJSON_GetObjectItem(params, "context"),
                                  "includeDeclaration")),
                              false);
          cJSON_Delete(combined);
        }
        else if (!strcmp(method, "textDocument/documentHighlight")) {
          result = references(index, symbol, uri, true, true);
        }
        else if (!strcmp(method, "textDocument/documentSymbol")) {
          result = symbols(index, uri, "");
        }
        else if (!strcmp(method, "textDocument/completion")) {
          size_t start = offset;
          while (start && identifier((unsigned char)buffer[start - 1])) {
            start--;
          }
          cJSON *repaired = NULL;
          if (start && buffer[start - 1] == '.' &&
              cJSON_GetArraySize(cJSON_GetObjectItem(index, "diagnostics"))) {
            cJSON *docs = copy(job->documents),
                  *repair_doc = document(docs, uri);
            char *fixed = malloc(strlen(buffer) + 1);
            memcpy(fixed, buffer, start - 1);
            strcpy(fixed + start - 1, buffer + offset);
            cJSON_ReplaceItemInObjectCaseSensitive(repair_doc, "text",
                                                   cJSON_CreateString(fixed));
            free(fixed);
            repaired = rhat_lsp_analyze(text(doc, "path"), docs, job->paths,
                                        s->stdlib);
            cJSON_Delete(docs);
          }
          result = completion(repaired ? repaired : index, uri, buffer, offset);
          cJSON_Delete(repaired);
        }
        else if (!strcmp(method, "textDocument/signatureHelp")) {
          result = signature_help(index, uri, buffer, offset);
        }
        else if (!strcmp(method, "textDocument/semanticTokens/full")) {
          result = semantic_tokens(index, uri, buffer);
        }
        else if (!strcmp(method, "workspace/symbol")) {
          cJSON *combined = combined_index(cache);
          result = symbols(combined, NULL, text(params, "query"));
          cJSON_Delete(combined);
        }
      }
      if (cancelled(s, id)) {
        cJSON_Delete(result);
        lsp_rpc_send_error(&s->out, id, -32800, "Request cancelled");
      }
      else if (!current(s, job)) {
        cJSON_Delete(result);
        lsp_rpc_send_error(&s->out, id, -32801, "Document changed");
      }
      else {
        lsp_rpc_send_response(&s->out, id, result);
      }
    }
    lhat_mutex_lock(&s->mutex);
    cJSON *cancel = s->cancelled->child;
    while (cancel) {
      cJSON *next = cancel->next;
      if (cJSON_Compare(cancel, id, true)) {
        cJSON_Delete(cJSON_DetachItemViaPointer(s->cancelled, cancel));
      }
      cancel = next;
    }
    cJSON_Delete(s->active_id);
    s->active_id = NULL;
    lhat_mutex_unlock(&s->mutex);
    cJSON_Delete(job->message);
    cJSON_Delete(job->documents);
    cJSON_Delete(job->paths);
    free(job);
  }
  cJSON_Delete(cache);
  cJSON_Delete(published);
  return 0;
}
static void enqueue(Server *s, cJSON *message)
{
  Job *job = calloc(1, sizeof(*job));
  job->message = copy(message);
  lhat_mutex_lock(&s->mutex);
  if (!cJSON_GetObjectItem(message, "id")) {
    Job **link = &s->head;
    while (*link) {
      Job *old = *link;
      if (!cJSON_GetObjectItem(old->message, "id")) {
        *link = old->next;
        cJSON_Delete(old->message);
        cJSON_Delete(old->documents);
        cJSON_Delete(old->paths);
        free(old);
      }
      else {
        link = &old->next;
      }
    }
    s->tail = NULL;
    for (Job *old = s->head; old; old = old->next) {
      s->tail = old;
    }
  }
  job->documents = copy(s->documents);
  job->paths = copy(s->paths);
  job->revision = s->revision;
  if (s->tail) {
    s->tail->next = job;
  }
  else {
    s->head = job;
  }
  s->tail = job;
  lhat_condition_signal(&s->ready);
  lhat_mutex_unlock(&s->mutex);
}
static void recheck(Server *s)
{
  cJSON *message = cJSON_CreateObject();
  cJSON_AddStringToObject(message, "method", "recheck");
  enqueue(s, message);
  cJSON_Delete(message);
}
static cJSON *initialize(Server *s, cJSON *params)
{
  cJSON *folder;
  cJSON_ArrayForEach(folder, cJSON_GetObjectItem(params, "workspaceFolders"))
  {
    char *path = lsp_uri_to_absolute_path(text(folder, "uri"));
    if (path) {
      cJSON_AddItemToArray(s->paths, cJSON_CreateString(path));
      free(path);
    }
  }
  if (!cJSON_GetArraySize(s->paths)) {
    char *path = lsp_uri_to_absolute_path(text(params, "rootUri"));
    if (path) {
      cJSON_AddItemToArray(s->paths, cJSON_CreateString(path));
      free(path);
    }
  }
  cJSON *options = cJSON_GetObjectItem(params, "initializationOptions"), *path;
  cJSON_ArrayForEach(path, cJSON_GetObjectItem(options, "modulePaths"))
  {
    if (cJSON_IsString(path)) {
      cJSON_AddItemToArray(s->paths, copy(path));
    }
  }
  cJSON *out = cJSON_CreateObject(),
        *caps = cJSON_AddObjectToObject(out, "capabilities");
  cJSON_AddStringToObject(caps, "positionEncoding", "utf-16");
  cJSON *sync = cJSON_AddObjectToObject(caps, "textDocumentSync");
  cJSON_AddBoolToObject(sync, "openClose", true);
  cJSON_AddNumberToObject(sync, "change", 2);
  cJSON_AddBoolToObject(sync, "save", true);
  const char *providers[] = {
      "hoverProvider",          "definitionProvider",
      "referencesProvider",     "documentHighlightProvider",
      "documentSymbolProvider", "workspaceSymbolProvider"};
  for (int j = 0; j < 6; j++) {
    cJSON_AddBoolToObject(caps, providers[j], true);
  }
  cJSON *completions = cJSON_AddObjectToObject(caps, "completionProvider"),
        *triggers = cJSON_AddArrayToObject(completions, "triggerCharacters");
  cJSON_AddItemToArray(triggers, cJSON_CreateString("."));
  cJSON_AddBoolToObject(completions, "resolveProvider", false);
  cJSON *sig = cJSON_AddObjectToObject(caps, "signatureHelpProvider");
  triggers = cJSON_AddArrayToObject(sig, "triggerCharacters");
  cJSON_AddItemToArray(triggers, cJSON_CreateString("("));
  cJSON_AddItemToArray(triggers, cJSON_CreateString(","));
  cJSON *tokens = cJSON_AddObjectToObject(caps, "semanticTokensProvider"),
        *legend = cJSON_AddObjectToObject(tokens, "legend");
  cJSON *types = cJSON_AddArrayToObject(legend, "tokenTypes"),
        *mods = cJSON_AddArrayToObject(legend, "tokenModifiers");
  const char *names[] = {"keyword",   "type",     "function", "method",
                         "parameter", "variable", "property", "number",
                         "string",    "comment",  "operator", "namespace"};
  for (int j = 0; j < 12; j++) {
    cJSON_AddItemToArray(types, cJSON_CreateString(names[j]));
  }
  cJSON_AddItemToArray(mods, cJSON_CreateString("declaration"));
  cJSON_AddItemToArray(mods, cJSON_CreateString("readonly"));
  cJSON_AddBoolToObject(tokens, "full", true);
  cJSON *info = cJSON_AddObjectToObject(out, "serverInfo");
  cJSON_AddStringToObject(info, "name", "rhatls");
  cJSON_AddStringToObject(info, "version", "0.1.0");
  return out;
}
static void sync_document(Server *s, const char *method, cJSON *params)
{
  cJSON *td = cJSON_GetObjectItem(params, "textDocument");
  const char *uri = text(td, "uri");
  lhat_mutex_lock(&s->mutex);
  cJSON *doc = document(s->documents, uri);
  if (!strcmp(method, "textDocument/didOpen")) {
    char *path = lsp_uri_to_absolute_path(uri), *canonical = canonical_uri(uri);
    if (path && canonical) {
      if (doc) {
        cJSON_Delete(cJSON_DetachItemViaPointer(s->documents, doc));
      }
      doc = cJSON_CreateObject();
      cJSON_AddStringToObject(doc, "uri", canonical);
      cJSON_AddStringToObject(doc, "path", path);
      cJSON_AddStringToObject(doc, "text", text(td, "text"));
      cJSON_AddNumberToObject(doc, "version", number(td, "version"));
      cJSON_AddItemToArray(s->documents, doc);
    }
    free(path);
    free(canonical);
  }
  else if (doc && !strcmp(method, "textDocument/didClose")) {
    cJSON_Delete(cJSON_DetachItemViaPointer(s->documents, doc));
  }
  else if (doc && !strcmp(method, "textDocument/didChange") &&
           number(td, "version") > number(doc, "version")) {
    cJSON *change;
    cJSON_ArrayForEach(change, cJSON_GetObjectItem(params, "contentChanges"))
    {
      const char *old = text(doc, "text"), *replacement = text(change, "text");
      cJSON *range = cJSON_GetObjectItem(change, "range");
      size_t start = 0, end = strlen(old);
      if (range) {
        cJSON *a = cJSON_GetObjectItem(range, "start"),
              *b = cJSON_GetObjectItem(range, "end");
        start = lsp_offset_at(old, strlen(old), number(a, "line"),
                              number(a, "character"));
        end = lsp_offset_at(old, strlen(old), number(b, "line"),
                            number(b, "character"));
      }
      if (end < start || strlen(old) + strlen(replacement) > 16 * 1024 * 1024) {
        continue;
      }
      char *next = malloc(start + strlen(replacement) + strlen(old + end) + 1);
      memcpy(next, old, start);
      strcpy(next + start, replacement);
      strcpy(next + start + strlen(replacement), old + end);
      cJSON_ReplaceItemInObjectCaseSensitive(doc, "text",
                                             cJSON_CreateString(next));
      free(next);
    }
    cJSON_ReplaceItemInObjectCaseSensitive(
        doc, "version", cJSON_CreateNumber(number(td, "version")));
  }
  s->revision++;
  lhat_mutex_unlock(&s->mutex);
  recheck(s);
}
int rhat_lsp_run(const char *executable, const char *stdlib)
{
  (void)executable;
  Server s = {0};
  s.stdlib = stdlib;
  s.documents = cJSON_CreateArray();
  s.paths = cJSON_CreateArray();
  s.cancelled = cJSON_CreateArray();
  lsp_rpc_out_init(&s.out, stdout);
  lhat_mutex_init(&s.mutex);
  lhat_condition_init(&s.ready);
  lhat_transport_use_binary_stdio();
  if (!lhat_thread_start(&s.worker, worker, &s)) {
    return 2;
  }
  LhatStream in = lhat_stream_of_file(stdin);
  char *body;
  size_t length;
  const char *methods[] = {"textDocument/hover",
                           "textDocument/definition",
                           "textDocument/references",
                           "textDocument/documentHighlight",
                           "textDocument/documentSymbol",
                           "textDocument/completion",
                           "textDocument/signatureHelp",
                           "textDocument/semanticTokens/full",
                           "workspace/symbol"};
  while (lhat_transport_read_message(&in, &body, &length)) {
    cJSON *message = cJSON_ParseWithLength(body, length);
    free(body);
    if (!cJSON_IsObject(message)) {
      cJSON_Delete(message);
      lsp_rpc_send_error(&s.out, NULL, -32700, "Parse error");
      continue;
    }
    cJSON *id = cJSON_GetObjectItem(message, "id"),
          *params = cJSON_GetObjectItem(message, "params");
    const char *method = text(message, "method");
    if (strcmp(text(message, "jsonrpc"), "2.0") ||
        !cJSON_IsString(cJSON_GetObjectItem(message, "method")) ||
        (id && !cJSON_IsNumber(id) && !cJSON_IsString(id) &&
         !cJSON_IsNull(id))) {
      lsp_rpc_send_error(&s.out, NULL, -32600, "Invalid Request");
      cJSON_Delete(message);
      continue;
    }
    if (!strcmp(method, "exit")) {
      cJSON_Delete(message);
      break;
    }
    if (!strcmp(method, "initialize") && !s.initialized && !s.shutdown) {
      s.initialized = true;
      lsp_rpc_send_response(&s.out, id, initialize(&s, params));
    }
    else if (!s.initialized) {
      if (id) {
        lsp_rpc_send_error(&s.out, id, -32002, "Server not initialized");
      }
    }
    else if (s.shutdown) {
      if (id) {
        lsp_rpc_send_error(&s.out, id, -32600, "Server is shut down");
      }
    }
    else if (!strcmp(method, "shutdown")) {
      lhat_mutex_lock(&s.mutex);
      s.shutdown = true;
      lhat_mutex_unlock(&s.mutex);
      lsp_rpc_send_response(&s.out, id, NULL);
    }
    else if (!strcmp(method, "$/cancelRequest")) {
      lhat_mutex_lock(&s.mutex);
      cJSON *cancel_id = cJSON_GetObjectItem(params, "id");
      bool pending = s.active_id && cJSON_Compare(s.active_id, cancel_id, true);
      for (Job *job = s.head; job; job = job->next) {
        cJSON *job_id = cJSON_GetObjectItem(job->message, "id");
        pending |= job_id && cJSON_Compare(job_id, cancel_id, true);
      }
      if (pending) {
        cJSON_AddItemToArray(s.cancelled, copy(cancel_id));
      }
      lhat_mutex_unlock(&s.mutex);
    }
    else if (!strcmp(method, "textDocument/didOpen") ||
             !strcmp(method, "textDocument/didChange") ||
             !strcmp(method, "textDocument/didClose") ||
             !strcmp(method, "textDocument/didSave")) {
      sync_document(&s, method, params);
    }
    else if (!strcmp(method, "workspace/didChangeWatchedFiles")) {
      lhat_mutex_lock(&s.mutex);
      s.revision++;
      lhat_mutex_unlock(&s.mutex);
      recheck(&s);
    }
    else {
      bool handled = false;
      for (int j = 0; j < 9; j++) {
        if (!strcmp(method, methods[j])) {
          enqueue(&s, message);
          handled = true;
          break;
        }
      }
      if (!handled && id) {
        lsp_rpc_send_error(&s.out, id, -32601, "Method not found");
      }
    }
    cJSON_Delete(message);
  }
  lhat_mutex_lock(&s.mutex);
  s.stop = true;
  lhat_condition_signal(&s.ready);
  lhat_mutex_unlock(&s.mutex);
  lhat_thread_join(&s.worker);
  while (s.head) {
    Job *job = s.head;
    s.head = job->next;
    cJSON_Delete(job->message);
    cJSON_Delete(job->documents);
    cJSON_Delete(job->paths);
    free(job);
  }
  cJSON_Delete(s.documents);
  cJSON_Delete(s.paths);
  cJSON_Delete(s.cancelled);
  lhat_condition_destroy(&s.ready);
  lhat_mutex_destroy(&s.mutex);
  lsp_rpc_out_dispose(&s.out);
  return s.shutdown ? 0 : 1;
}
