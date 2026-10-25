#include "rhat.h"
#include <ctype.h>
#include <errno.h>

void *mem(size_t n) {
  void *p = calloc(1, n ? n : 1);
  if (!p) {
    fputs("out of memory\n", stderr);
    exit(1);
  }
  return p;
}
char *str(const char *s) {
  size_t n = strlen(s) + 1;
  char *p = mem(n);
  memcpy(p, s, n);
  return p;
}
char *fmt(const char *s, ...) {
  va_list a, b;
  va_start(a, s);
  va_copy(b, a);
  int n = vsnprintf(NULL, 0, s, a);
  va_end(a);
  if (n < 0)
    exit(1);
  char *p = mem((size_t)n + 1);
  vsnprintf(p, (size_t)n + 1, s, b);
  va_end(b);
  return p;
}
void add(List *a, void *v) {
  if (a->n == a->cap) {
    a->cap = a->cap ? a->cap * 2 : 8;
    void **p = realloc(a->v, (size_t)a->cap * sizeof(void *));
    if (!p)
      exit(1);
    a->v = p;
  }
  a->v[a->n++] = v;
}
_Noreturn void fail(Loc l, const char *s, ...) {
  fprintf(stderr, "%s:%d:%d: error: ", l.file ? l.file : "rhat", l.line, l.col);
  va_list a;
  va_start(a, s);
  vfprintf(stderr, s, a);
  va_end(a);
  fputc('\n', stderr);
  exit(1);
}
typedef struct {
  char *text;
  Loc loc;
  int kind;
  size_t length;
} Token;
enum {
  TOK_WORD = 256,
  TOK_INT,
  TOK_FLOAT,
  TOK_STRING,
  TOK_QUOTED,
  TOK_SCOPE,
  TOK_EOF
};
typedef struct {
  Program *p;
  Module *module;
  Function *enclosing;
  List tokens;
  int at, serial, scope_depth;
  bool header;
} Parser;
static Token *tok(Parser *p) { return p->tokens.v[p->at]; }
static bool is(Parser *p, const char *s) {
  return tok(p)->kind != TOK_STRING && tok(p)->kind != TOK_QUOTED &&
         !strcmp(tok(p)->text, s);
}
static bool take(Parser *p, const char *s) {
  if (is(p, s)) {
    p->at++;
    return true;
  }
  return false;
}
static Token *next(Parser *p) {
  Token *t = tok(p);
  if (t->kind != TOK_EOF)
    p->at++;
  return t;
}
static void need(Parser *p, const char *s) {
  if (!take(p, s))
    fail(tok(p)->loc, "expected '%s', found '%s'", s, tok(p)->text);
}
static char *word(Parser *p) {
  Token *t = next(p);
  if (t->kind != TOK_WORD && t->kind != TOK_QUOTED)
    fail(t->loc, "expected a name");
  return t->text;
}
static char *qualified_word(Parser *p) {
  char *name = word(p);
  while (take(p, "."))
    name = fmt("%s.%s", name, word(p));
  return name;
}
static char *expression_path(Expr *e) {
  if (e->kind == E_NAME)
    return e->text;
  if (e->kind == E_MEMBER) {
    char *base = expression_path(e->a);
    if (base)
      return fmt("%s.%s", base, e->text);
  }
  return NULL;
}
static bool type_constructor(Parser *p, Expr *e) {
  char *name = expression_path(e);
  if (!name)
    return false;
  for (int j = 0; j + 1 < p->tokens.n; j++) {
    Token *a = p->tokens.v[j], *b = p->tokens.v[j + 1];
    if (!strcmp(a->text, "def^") && !strcmp(b->text, name))
      return true;
    size_t n = strlen(b->text);
    if (!strcmp(a->text, "errordef^") && !strncmp(name, b->text, n) &&
        name[n] == '.')
      return true;
  }
  for (int j = 0; j < p->module->imports.n; j++) {
    Import *i = p->module->imports.v[j];
    const char *prefix = i->alias ? i->alias : i->name;
    size_t n = strlen(prefix);
    if (!strncmp(name, prefix, n) && name[n] == '.')
      return true;
  }
  return false;
}
static char *decl_word(Parser *p) {
  Loc loc = tok(p)->loc;
  char *name = word(p);
  for (int k = 0; k < T_STRUCT; k++)
    if (!strcmp(name, builtin_name(k)))
      fail(loc, "built-in type name '%s' cannot be redeclared", name);
  if (strchr(name, '^'))
    fail(loc, "hat identifiers cannot be declared by users");
  return name;
}
static char *slice(const char *s, size_t n) {
  char *r = mem(n + 1);
  memcpy(r, s, n);
  return r;
}
static int digit_value(unsigned char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}
static const char *digits(const char *s, int base, Loc loc) {
  bool previous = false, any = false;
  while (*s) {
    int d = digit_value((unsigned char)*s);
    if (d >= 0 && d < base) {
      previous = any = true;
      s++;
    } else if (*s == '_') {
      int next = digit_value((unsigned char)s[1]);
      if (!previous || next < 0 || next >= base)
        fail(loc, "numeric separator must be between digits");
      previous = false;
      s++;
    } else
      break;
  }
  if (!any)
    fail(loc, "expected a digit in numeric literal");
  return s;
}
static char *decimal_base(const char *start, const char *end, int base) {
  size_t n = (size_t)(end - start);
  char *out = mem(n * 2 + 2);
  out[0] = '0';
  size_t count = 1;
  for (const char *p = start; p < end; p++) {
    if (*p == '_')
      continue;
    int carry = digit_value((unsigned char)*p);
    for (size_t j = 0; j < count; j++) {
      int v = (out[j] - '0') * base + carry;
      out[j] = (char)('0' + v % 10);
      carry = v / 10;
    }
    while (carry) {
      out[count++] = (char)('0' + carry % 10);
      carry /= 10;
    }
  }
  while (count > 1 && out[count - 1] == '0')
    count--;
  for (size_t j = 0; j < count / 2; j++) {
    char c = out[j];
    out[j] = out[count - 1 - j];
    out[count - 1 - j] = c;
  }
  return out;
}
static void unicode_escape(char *out, size_t *n, unsigned value, Loc loc) {
  if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
    fail(loc, "invalid Unicode scalar value");
  if (value < 0x80)
    out[(*n)++] = (char)value;
  else if (value < 0x800) {
    out[(*n)++] = (char)(0xc0 | (value >> 6));
    out[(*n)++] = (char)(0x80 | (value & 63));
  } else if (value < 0x10000) {
    out[(*n)++] = (char)(0xe0 | (value >> 12));
    out[(*n)++] = (char)(0x80 | ((value >> 6) & 63));
    out[(*n)++] = (char)(0x80 | (value & 63));
  } else {
    out[(*n)++] = (char)(0xf0 | (value >> 18));
    out[(*n)++] = (char)(0x80 | ((value >> 12) & 63));
    out[(*n)++] = (char)(0x80 | ((value >> 6) & 63));
    out[(*n)++] = (char)(0x80 | (value & 63));
  }
}
static void lex(Parser *p) {
  const char *s = p->p->source;
  int line = 1, col = 1;
  while (*s) {
    if (isspace((unsigned char)*s)) {
      if (*s++ == '\n') {
        line++;
        col = 1;
      } else
        col++;
      continue;
    }
    if (s[0] == '#' && s[1] == '[') {
      s += 2;
      col += 2;
      int depth = 1;
      while (*s && depth) {
        if (s[0] == '#' && s[1] == '[') {
          depth++;
          s += 2;
          col += 2;
        } else if (s[0] == ']' && s[1] == '#') {
          depth--;
          s += 2;
          col += 2;
        } else if (*s++ == '\n') {
          line++;
          col = 1;
        } else
          col++;
      }
      if (depth)
        fail((Loc){p->p->path, line, col}, "unterminated comment");
      continue;
    }
    if (*s == '#') {
      while (*s && *s != '\n') {
        s++;
        col++;
      }
      continue;
    }
    Token *t = mem(sizeof(*t));
    t->loc = (Loc){p->p->path, line, col};
    const char *start = s;
    const char *operators[] = {"≤", "≦", "≥", "≧", "≠"};
    int unicode = -1;
    for (int j = 0; j < 5; j++)
      if (!strncmp(s, operators[j], strlen(operators[j])))
        unicode = j;
    if (unicode >= 0) {
      s += strlen(operators[unicode]);
      t->text = unicode < 2 ? "<=" : unicode < 4 ? ">=" : "!=";
      t->kind = '=';
    } else if (*s == '$') {
      s++;
      if (*s == '^') {
        while (*s == '^')
          s++;
      } else {
        while (*s == '$')
          s++;
      }
      if (!(isalpha((unsigned char)*s) || *s == '_' || *s == '`' ||
            (unsigned char)*s >= 128))
        fail(t->loc, "scope prefix must be adjacent to an identifier");
      t->kind = TOK_SCOPE;
    } else if (*s == '`') {
      s++;
      char *out = mem(strlen(s) + 1);
      size_t n = 0;
      bool closed = false;
      while (*s) {
        if (*s == '\n' || *s == '\r')
          fail(t->loc, "newline in quoted identifier");
        if (*s == '`') {
          s++;
          if (*s != '`') {
            closed = true;
            break;
          }
        }
        if ((unsigned char)*s < 32 || (unsigned char)*s == 127)
          fail(t->loc, "control character in quoted identifier");
        out[n++] = *s++;
      }
      if (!closed || !n)
        fail(t->loc, "empty or unterminated quoted identifier");
      t->text = out;
      t->kind = TOK_QUOTED;
    } else if (isalpha((unsigned char)*s) || *s == '_' ||
               (unsigned char)*s >= 128) {
      while (isalnum((unsigned char)*s) || *s == '_' ||
             (unsigned char)*s >= 128) {
        bool symbol = false;
        for (int j = 0; j < 5; j++)
          if (!strncmp(s, operators[j], strlen(operators[j])))
            symbol = true;
        if (symbol)
          break;
        s++;
      }
      while (*s == '^')
        s++;
      t->kind = TOK_WORD;
    } else if (isdigit((unsigned char)*s)) {
      t->kind = TOK_INT;
      if (s[0] == '0' && strchr("xXoObB", s[1]) && s[1]) {
        int base = s[1] == 'x' || s[1] == 'X'   ? 16
                   : s[1] == 'o' || s[1] == 'O' ? 8
                                                : 2;
        s += 2;
        const char *begin = s;
        s = digits(s, base, t->loc);
        t->text = decimal_base(begin, s, base);
      } else {
        s = digits(s, 10, t->loc);
        if (*s == '.' && isdigit((unsigned char)s[1])) {
          t->kind = TOK_FLOAT;
          s = digits(s + 1, 10, t->loc);
        }
        if (*s == 'e' || *s == 'E') {
          t->kind = TOK_FLOAT;
          s++;
          if (*s == '+' || *s == '-')
            s++;
          s = digits(s, 10, t->loc);
        }
        t->text = mem((size_t)(s - start) + 1);
        size_t n = 0;
        for (const char *q = start; q < s; q++)
          if (*q != '_')
            t->text[n++] = *q;
      }
      if (isalnum((unsigned char)*s) || *s == '_')
        fail(t->loc, "invalid numeric literal");
    } else if (*s == '"' || *s == '\'') {
      char quote = *s++;
      bool line_string = quote == '"' && s[0] == '"' && s[1] == '"';
      if (line_string)
        s += 2;
      char *out = mem(strlen(s) + 1);
      size_t n = 0;
      bool closed = line_string;
      while (*s && (!line_string || (*s != '\n' && *s != '\r'))) {
        if (!line_string && *s == quote) {
          s++;
          if (quote != '\'' || *s != '\'') {
            closed = true;
            break;
          }
          out[n++] = *s++;
          continue;
        }
        if (*s == '\r' && s[1] == '\n') {
          out[n++] = '\n';
          s += 2;
          continue;
        }
        if (!line_string && quote == '"' && *s == '\\') {
          s++;
          char c = *s++;
          switch (c) {
          case 'n':
            out[n++] = '\n';
            break;
          case 'r':
            out[n++] = '\r';
            break;
          case 't':
            out[n++] = '\t';
            break;
          case '0':
            out[n++] = 0;
            break;
          case '\\':
          case '"':
            out[n++] = c;
            break;
          case '\n':
            break;
          case '\r':
            if (*s == '\n')
              s++;
            break;
          case 'x': {
            int a = digit_value((unsigned char)s[0]);
            if (a < 0 || !s[0])
              fail(t->loc, "hex escape requires two digits");
            int b = digit_value((unsigned char)s[1]);
            if (b < 0)
              fail(t->loc, "hex escape requires two digits");
            out[n++] = (char)((a << 4) | b);
            s += 2;
            break;
          }
          case 'u': {
            if (*s++ != '{')
              fail(t->loc, "Unicode escape requires braces");
            unsigned cp = 0;
            int count = 0;
            while (*s && *s != '}') {
              int d = digit_value((unsigned char)*s++);
              if (d < 0 || ++count > 6)
                fail(t->loc, "invalid Unicode escape");
              cp = cp * 16 + (unsigned)d;
            }
            if (*s != '}' || !count)
              fail(t->loc, "invalid Unicode escape");
            s++;
            unicode_escape(out, &n, cp, t->loc);
            break;
          }
          default:
            fail(t->loc, "unsupported string escape");
          }
        } else
          out[n++] = *s++;
      }
      if (!closed)
        fail(t->loc, "unterminated string");
      t->kind = TOK_STRING;
      t->text = out;
      t->length = n;
    }
    else if (*s == '?') {
      const char *ops[] = {
          "?:=", "?+=", "?-=", "?*=", "?/=", "?%=", "?.", "?[", "?(", "??"};
      t->kind = '?';
      s++;
      for (size_t j = 0; j < sizeof(ops) / sizeof(*ops); j++) {
        size_t n = strlen(ops[j]);
        if (!strncmp(start, ops[j], n)) {
          s = start + n;
          break;
        }
      }
    }
    else {
      t->kind = (unsigned char)*s++;
      if ((strchr("+-*/%", start[0]) && *s == '=') ||
          (start[0] == '=' && *s == '/') || (start[0] == ':' && *s == '=') ||
          (start[0] == '-' && *s == '>') ||
          (strchr("=!<>", start[0]) && *s == '=') ||
          (start[0] == '&' && *s == '&') || (start[0] == '|' && *s == '|') ||
          (start[0] == '.' && *s == '.'))
        s++;
    }
    if (!t->text)
      t->text = slice(start, (size_t)(s - start));
    if (t->kind != TOK_STRING && t->kind != TOK_QUOTED &&
        !strcmp(t->text, "=/"))
      t->text = "!=";
    if (t->kind != TOK_STRING && t->kind != TOK_QUOTED &&
        (!strcmp(t->text, "==") || !strcmp(t->text, "||")))
      fail(t->loc, "use '=', 'and^', or 'or^'; C-style comparison/logical "
                   "operators are not supported");
    for (const char *q = start; q < s; q++) {
      if (*q == '\n') {
        line++;
        col = 1;
      } else
        col++;
    }
    add(&p->tokens, t);
  }
  Token *end = mem(sizeof(*end));
  end->kind = TOK_EOF;
  end->text = "<eof>";
  end->loc = (Loc){p->p->path, line, col};
  add(&p->tokens, end);
}
static Spec *spec(Parser *p);
static Spec *spec_part(Parser *p)
{
  Spec *s = mem(sizeof(*s));
  s->loc = tok(p)->loc;
  if (take(p, "&&")) {
    s->kind = SPEC_REF;
    Spec *inner = mem(sizeof(*inner));
    inner->kind = SPEC_REF;
    inner->loc = s->loc;
    inner->elem = spec_part(p);
    s->elem = inner;
    return s;
  }
  if (take(p, "&")) {
    s->kind = SPEC_REF;
    s->elem = spec_part(p);
    return s;
  }
  if (take(p, "(")) {
    s = spec(p);
    need(p, ")");
  } else {
    s->kind = SPEC_NAME;
    s->name = qualified_word(p);
  }
  while (take(p, "[")) {
    Spec *w = mem(sizeof(*w));
    w->loc = s->loc;
    w->elem = s;
    if (take(p, ">")) {
      need(p, "..");
      w->kind = SPEC_GROW;
    } else if (take(p, ":")) {
      w->kind = SPEC_SLICE;
    } else {
      Token *n = next(p);
      if (n->kind != TOK_INT)
        fail(n->loc, "fixed array length must be an integer constant");
      errno = 0;
      long k = strtol(n->text, NULL, 10);
      if (errno || k <= 0 || k > 1000000)
        fail(n->loc, "array length out of range (1..1000000)");
      w->kind = SPEC_ARRAY;
      w->count = (int)k;
    }
    need(p, "]");
    s = w;
  }
  return s;
}
static Spec *spec(Parser *p)
{
  Spec *s = spec_part(p);
  while (take(p, "|")) {
    Spec *sum = mem(sizeof(*sum));
    sum->kind = SPEC_UNION;
    sum->loc = s->loc;
    sum->elem = s;
    sum->other = spec_part(p);
    s = sum;
  }
  return s;
}
static Expr *expr(Parser *p, int min);
static Function *function(Parser *p, char *name, Def *owner, bool abstr);
static Expr *node(int k, Loc l) {
  Expr *e = mem(sizeof(*e));
  e->kind = k;
  e->loc = l;
  return e;
}
static void entries(Parser *p, Expr *e, const char *end, bool named) {
  if (take(p, end))
    return;
  do {
    if (named) {
      add(&e->names, word(p));
      need(p, "=");
    }
    add(&e->args, expr(p, 0));
    if (is(p, end))
      break;
  } while (take(p, ","));
  need(p, end);
}
static bool branch_word(Parser *p) {
  const char *words[] = {"elseif^", "elsif^", "elif^", "ei^", "else^", "el^"};
  for (size_t j = 0; j < sizeof(words) / sizeof(*words); j++)
    if (is(p, words[j]))
      return true;
  return false;
}
static bool branch(Parser *p) {
  if (!branch_word(p))
    return false;
  Token *word = next(p);
  bool default_allowed = !strcmp(word->text, "el^");
  if (!strcmp(word->text, "else^")) {
    default_allowed = !take(p, "if^");
    if (default_allowed && !is(p, ":"))
      fail(tok(p)->loc, "else^ requires ':' or if^");
  }
  if (!default_allowed && is(p, ":"))
    fail(tok(p)->loc, "conditional branch requires a condition");
  return true;
}
static Expr *if_tail(Parser *p, Expr *condition, Loc loc) {
  Expr *e = node(E_IF, loc);
  e->a = condition;
  e->b = expr(p, 0);
  if (!branch(p))
    fail(tok(p)->loc, "if^ expression requires a final el^: branch");
  if (take(p, ":")) {
    e->c = expr(p, 0);
    need(p, ";");
  } else {
    bool header = p->header;
    p->header = true;
    Expr *next_condition = expr(p, 0);
    p->header = header;
    need(p, ":");
    e->c = if_tail(p, next_condition, next_condition->loc);
  }
  return e;
}
static Expr *for_expression(Parser *p, Loc loc);
static Expr *primary(Parser *p) {
  Token *t = next(p);
  Expr *e = NULL;
  if (t->kind == TOK_SCOPE) {
    e = node(E_NAME, t->loc);
    e->text = word(p);
    e->scope_kind = t->text[1] == '^' ? 2 : 1;
    e->scope_count = (int)strlen(t->text) - 1;
  } else if (t->kind == TOK_QUOTED) {
    e = node(E_NAME, t->loc);
    e->text = t->text;
  } else if (t->kind == TOK_INT || t->kind == TOK_FLOAT ||
             t->kind == TOK_STRING) {
    e = node(t->kind == TOK_INT     ? E_INT
             : t->kind == TOK_FLOAT ? E_FLOAT
                                    : E_STRING,
             t->loc);
    e->text = t->text;
    e->length = t->length;
  } else if (!strcmp(t->text, "true^") || !strcmp(t->text, "false^")) {
    e = node(E_BOOL, t->loc);
    e->text = t->text;
  } else if (!strcmp(t->text, "(")) {
    bool header = p->header;
    p->header = false;
    e = expr(p, 0);
    p->header = header;
    need(p, ")");
  } else if (!strcmp(t->text, "[")) {
    e = node(E_ARRAY, t->loc);
    entries(p, e, "]", false);
  } else if (!strcmp(t->text, "{")) {
    fail(t->loc, "bare braces are blocks; construct a nominal value with "
                 "Type{field=value}");
  } else if (!strcmp(t->text, "&") || !strcmp(t->text, "-") ||
             !strcmp(t->text, "!") || !strcmp(t->text, "*") ||
             !strcmp(t->text, "bitnot^")) {
    e = node(E_UNARY, t->loc);
    e->text = t->text;
    e->a = expr(p, 8);
  } else if (!strcmp(t->text, "for^")) {
    e = for_expression(p, t->loc);
  } else if (!strcmp(t->text, "if^")) {
    bool header = p->header;
    p->header = true;
    Expr *condition = expr(p, 0);
    p->header = header;
    need(p, ":");
    e = if_tail(p, condition, t->loc);
  } else if (!strcmp(t->text, "f^") || !strcmp(t->text, "p^") ||
             !strcmp(t->text, "template^")) {
    p->at--;
    e = node(E_FUNCTION, t->loc);
    e->function = function(p, fmt("<anonymous@%d:%d>", t->loc.line, t->loc.col),
                           NULL, false);
  } else if (!strcmp(t->text, "error^")) {
    e = node(E_RECORD, t->loc);
    e->error_constructor = true;
    e->text = qualified_word(p);
    need(p, "{");
    entries(p, e, "}", true);
  } else if (t->kind == TOK_WORD) {
    e = node(!strcmp(t->text, "self^") ? E_SELF : E_NAME, t->loc);
    e->text = t->text;
    if (!strncmp(e->text, "it^^", 4)) {
      e->scope_kind = 3;
      e->scope_count = (int)strlen(e->text) - 3;
      e->text = "it^";
    }
  } else
    fail(t->loc, "expected an expression, found '%s'", t->text);
  for (;;) {
    if (take(p, ".") || take(p, "?.")) {
      Expr *n = node(E_MEMBER, e->loc);
      n->optional = !strcmp(((Token *)p->tokens.v[p->at - 1])->text, "?.");
      n->a = e;
      n->text = word(p);
      e = n;
    }
    else if (is(p, "(") &&
             ((Token *)p->tokens.v[p->at - 1])->loc.line == tok(p)->loc.line) {
      next(p);
      Expr *n = node(E_CALL, e->loc);
      n->a = e;
      entries(p, n, ")", false);
      e = n;
    }
    else if (take(p, "[") || take(p, "?[")) {
      Expr *n = node(E_INDEX, e->loc);
      n->optional = !strcmp(((Token *)p->tokens.v[p->at - 1])->text, "?[");
      n->a = e;
      n->b = expr(p, 0);
      need(p, "]");
      e = n;
    }
    else if (take(p, "?")) {
      Expr *n = node(E_PRESENT, e->loc);
      n->a = e;
      e = n;
    }
    else if (is(p, "?(")) {
      fail(tok(p)->loc, "optional callable values are not supported; use an "
                        "optional receiver?.method(...)");
    }
    else if (!p->header && type_constructor(p, e) && take(p, "{")) {
      Expr *n = node(E_RECORD, e->loc);
      n->text = expression_path(e);
      entries(p, n, "}", true);
      e = n;
    }
    else {
      break;
    }
  }
  return e;
}
static int prec(const char *s) {
  if (!strcmp(s, "??")) {
    return 6;
  }
  if (!strcmp(s, "or^"))
    return 1;
  if (!strcmp(s, "and^"))
    return 2;
  if (!strcmp(s, "=") || !strcmp(s, "==") || !strcmp(s, "!=") ||
      !strcmp(s, "<") || !strcmp(s, ">") || !strcmp(s, "<=") ||
      !strcmp(s, ">="))
    return 3;
  if (!strcmp(s, "+") || !strcmp(s, "-"))
    return 4;
  if (!strcmp(s, "*") || !strcmp(s, "/") || !strcmp(s, "%") ||
      !strcmp(s, "bitand^") || !strcmp(s, "bitor^") || !strcmp(s, "bitxor^") ||
      !strcmp(s, "bitshift^"))
    return 5;
  return -1;
}
static Expr *expr(Parser *p, int min) {
  Expr *e = primary(p);
  for (;;) {
    Token *t = tok(p);
    int pr =
        t->kind == TOK_STRING || t->kind == TOK_QUOTED ? -1 : prec(t->text);
    if (pr < min)
      break;
    next(p);
    if (pr == 3) {
      Expr *chain = node(E_CHAIN, t->loc);
      add(&chain->args, e);
      for (;;) {
        add(&chain->names, t->text);
        add(&chain->args, expr(p, pr + 1));
        t = tok(p);
        if (t->kind == TOK_STRING || t->kind == TOK_QUOTED ||
            prec(t->text) != 3)
          break;
        next(p);
      }
      if (chain->names.n == 1) {
        Expr *single = node(E_BIN, chain->loc);
        single->text = chain->names.v[0];
        single->a = chain->args.v[0];
        single->b = chain->args.v[1];
        e = single;
      } else
        e = chain;
    } else {
      Expr *binary = node(!strcmp(t->text, "??") ? E_COALESCE : E_BIN, t->loc);
      binary->text = t->text;
      binary->a = e;
      binary->b = expr(p, pr + 1);
      e = binary;
    }
  }
  return e;
}
static void block(Parser *p, List *out);
static Stmt *stmt(Parser *p);
static void clause_body(Parser *p, List *out) {
  while (!is(p, "}") && !branch_word(p)) {
    if (tok(p)->kind == TOK_EOF)
      fail(tok(p)->loc, "unterminated if^");
    if (take(p, ";"))
      continue;
    add(out, stmt(p));
  }
}
static Stmt *if_statement(Parser *p, Expr *condition, Loc loc) {
  Stmt *s = mem(sizeof(*s));
  s->kind = S_IF;
  s->loc = loc;
  s->a = condition;
  clause_body(p, &s->body);
  if (branch(p)) {
    if (take(p, ":"))
      clause_body(p, &s->other);
    else {
      bool header = p->header;
      p->header = true;
      Expr *next_condition = expr(p, 0);
      p->header = header;
      need(p, ":");
      Stmt *nested = if_statement(p, next_condition, next_condition->loc);
      nested->internal = true;
      add(&s->other, nested);
    }
  }
  return s;
}
static const char *assignment_operator(Parser *p) {
  const char *ops[] = {":=",  "+=",  "-=",  "*=",  "/=",  "%=",
                       "?:=", "?+=", "?-=", "?*=", "?/=", "?%="};
  for (size_t j = 0; j < sizeof(ops) / sizeof(*ops); j++)
    if (is(p, ops[j]))
      return ops[j];
  return NULL;
}
static Expr *named(char *name, Loc loc) {
  Expr *e = node(E_NAME, loc);
  e->text = name;
  return e;
}
static Expr *number(char *text, Loc loc) {
  Expr *e = node(E_INT, loc);
  e->text = text;
  return e;
}
static Expr *binary(char *op, Expr *a, Expr *b) {
  Expr *e = node(E_BIN, a->loc);
  e->text = op;
  e->a = a;
  e->b = b;
  return e;
}
static Expr *contextual(Expr *value, Expr *hint) {
  Expr *e = node(E_EXPECT, value->loc);
  e->a = value;
  e->b = hint;
  return e;
}
static Stmt *binding(char *name, Expr *value, bool mut) {
  Stmt *s = mem(sizeof(*s));
  s->kind = S_BIND;
  s->loc = value->loc;
  s->name = name;
  s->a = value;
  s->mut = mut;
  return s;
}
static Stmt *assignment(Expr *target, Expr *value, char *op) {
  Stmt *s = mem(sizeof(*s));
  s->kind = S_ASSIGN;
  s->loc = target->loc;
  s->a = target;
  s->b = value;
  s->op = op;
  return s;
}
static bool match_clause(Parser *p) {
  return is(p, "when^") || is(p, "other^") || is(p, "el^") || is(p, "else^");
}
static Expr *match_condition(Parser *p, char *subject, bool *yes, bool *no) {
  Expr *condition = NULL;
  do {
    Expr *low = expr(p, 0), *test = NULL;
    if (take(p, "to^")) {
      Expr *high = expr(p, 0);
      test = binary("and^", binary(">=", named(subject, low->loc), low),
                    binary("<=", named(subject, high->loc), high));
    } else {
      if (low->kind == E_BOOL) {
        if (!strcmp(low->text, "true^"))
          *yes = true;
        else
          *no = true;
      }
      test = binary("=", named(subject, low->loc), low);
    }
    condition = condition ? binary("or^", condition, test) : test;
  } while (take(p, ","));
  need(p, ":");
  return condition;
}
static void match_body(Parser *p, List *out) {
  while (!is(p, "}") && !match_clause(p)) {
    if (tok(p)->kind == TOK_EOF)
      fail(tok(p)->loc, "unterminated value match");
    if (!take(p, ";"))
      add(out, stmt(p));
  }
}
static Stmt *match_statement(Parser *p, Stmt *scope, Expr *subject) {
  char *name = fmt("\001match%d", ++p->serial);
  add(&scope->body, binding(name, subject, false));
  add(&scope->body, binding("it^", named(name, subject->loc), false));
  need(p, "{");
  if (!match_clause(p))
    fail(tok(p)->loc, "for^ requires a driving clause or when^/other^ arms");
  p->scope_depth++;
  List *arm = &scope->body;
  bool yes = false, no = false;
  while (take(p, "when^")) {
    Stmt *s = mem(sizeof(*s));
    s->kind = S_IF;
    s->loc = tok(p)->loc;
    s->internal = true;
    s->a = match_condition(p, name, &yes, &no);
    match_body(p, &s->body);
    add(arm, s);
    arm = &s->other;
  }
  if (take(p, "other^") || take(p, "el^") || take(p, "else^")) {
    need(p, ":");
    match_body(p, arm);
  }
  need(p, "}");
  p->scope_depth--;
  return scope;
}
static Expr *match_expression(Parser *p, Expr *scope, Expr *subject) {
  char *name = fmt("\001match%d", ++p->serial);
  add(&scope->bindings, binding(name, subject, false));
  add(&scope->bindings, binding("it^", named(name, subject->loc), false));
  need(p, ":");
  Expr **arm = &scope->a, *last = NULL;
  bool yes = false, no = false;
  while (take(p, "when^")) {
    Expr *e = node(E_IF, tok(p)->loc);
    e->a = match_condition(p, name, &yes, &no);
    e->b = expr(p, 0);
    *arm = e;
    arm = &e->c;
    last = e;
  }
  if (take(p, "other^") || take(p, "el^") || take(p, "else^")) {
    need(p, ":");
    *arm = expr(p, 0);
  } else if (yes && no && last) {
    *last = *last->b;
  } else
    fail(tok(p)->loc,
         "value match expression requires other^: or exhaustive bool patterns");
  need(p, ";");
  return scope;
}
static Expr *for_expression(Parser *p, Loc loc) {
  Expr *scope = node(E_SCOPE, loc);
  bool header = p->header;
  p->header = true;
  Expr *subject = NULL;
  if (is(p, "let^") || is(p, "var^")) {
    do {
      Stmt *b = stmt(p);
      if (b->kind != S_BIND)
        fail(b->loc, "a focus introduces one binding");
      add(&scope->bindings, b);
      subject = named(b->name, b->loc);
    } while (take(p, "for^"));
  }
  if (take(p, "if^")) {
    Expr *condition = expr(p, 0);
    need(p, ":");
    p->header = header;
    scope->a = if_tail(p, condition, loc);
    return scope;
  }
  if (take(p, "do^")) {
    need(p, ":");
    p->header = header;
    scope->a = expr(p, 0);
    need(p, ";");
    return scope;
  }
  if (!subject)
    subject = expr(p, 0);
  p->header = header;
  return match_expression(p, scope, subject);
}
static int loop_section(Parser *p) {
  const char *names[] = {"prolog^", "pre^",  "first^",
                         "main^",   "last^", "epilog^"};
  for (int j = 0; j < 6; j++)
    if (is(p, names[j]))
      return j;
  if (is(p, "prologue^"))
    return 0;
  if (is(p, "premain^"))
    return 1;
  if (is(p, "epilogue^"))
    return 5;
  return -1;
}
static void loop_body(Parser *p, Stmt *loop) {
  need(p, "{");
  p->scope_depth++;
  List *parts[] = {&loop->prolog, &loop->pre,  &loop->first,
                   &loop->body,   &loop->last, &loop->epilog};
  int section = -1;
  bool explicit = false, body = false;
  while (!is(p, "}")) {
    if (tok(p)->kind == TOK_EOF)
      fail(tok(p)->loc, "unterminated loop");
    if (take(p, ";"))
      continue;
    int next_section = loop_section(p);
    if (next_section >= 0) {
      if (next_section <= section)
        fail(tok(p)->loc, "loop sections must follow prolog^, pre^, first^, "
                          "main^, last^, epilog^ order");
      explicit = true;
      section = next_section;
      next(p);
      need(p, ":");
      body |= section >= 1 && section <= 4;
    } else {
      if (section < 0) {
        section = 3;
        body = true;
      }
      Stmt *s = stmt(p);
      if (section == 2) {
        if (s->kind == S_BIND && !strcmp(s->name, "_^"))
          s->name = fmt("\001first_discard%d", ++p->serial);
        if (s->kind == S_BIND_MANY)
          for (int j = 0; j < s->body.n; j++) {
            Stmt *b = s->body.v[j];
            if (!strcmp(b->name, "_^"))
              b->name = fmt("\001first_discard%d", ++p->serial);
          }
      }
      add(parts[section], s);
    }
  }
  need(p, "}");
  p->scope_depth--;
  if (explicit && !body)
    fail(loop->loc, "loop requires pre^, first^, main^, or last^");
}
static Stmt *scoped_statement(Stmt *scope, Expr *value) {
  Expr *e = node(E_SCOPE, scope->loc);
  e->bindings = scope->body;
  e->a = value;
  Stmt *s = mem(sizeof(*s));
  s->kind = S_EXPR;
  s->loc = scope->loc;
  s->a = e;
  return s;
}
static Stmt *matched_statement(Parser *p, Stmt *scope, Expr *subject) {
  Expr *e = node(E_SCOPE, scope->loc);
  e->bindings = scope->body;
  match_expression(p, e, subject);
  Stmt *s = mem(sizeof(*s));
  s->kind = S_EXPR;
  s->loc = scope->loc;
  s->a = e;
  return s;
}
static Stmt *loop_statement(Parser *p, bool repeat, Loc loc) {
  Stmt *scope = mem(sizeof(*scope));
  scope->kind = S_BLOCK;
  scope->loc = loc;
  Stmt *loop = mem(sizeof(*loop));
  loop->kind = S_WHILE;
  loop->loc = loc;
  bool header = p->header;
  p->header = true;
  char *counter = NULL, *limit = NULL;
  if (!repeat && (is(p, "let^") || is(p, "var^"))) {
    do {
      Stmt *focus = stmt(p);
      add(&scope->body, focus);
      if (focus->kind == S_BIND)
        add(&loop->focuses, focus->name);
    } while (take(p, "for^"));
    if (take(p, "if^")) {
      Expr *condition = expr(p, 0);
      p->header = header;
      if (take(p, ":"))
        return scoped_statement(scope, if_tail(p, condition, loc));
      need(p, "{");
      p->scope_depth++;
      Stmt *branch = if_statement(p, condition, loc);
      branch->internal = true;
      need(p, "}");
      p->scope_depth--;
      add(&scope->body, branch);
      return scope;
    }
    if (take(p, "do^")) {
      need(p, ":");
      p->header = header;
      Expr *value = expr(p, 0);
      need(p, ";");
      return scoped_statement(scope, value);
    }
    if (is(p, ":")) {
      Stmt *focus = scope->body.v[scope->body.n - 1];
      p->header = header;
      return matched_statement(p, scope, named(focus->name, loc));
    }
    if (is(p, "{")) {
      Stmt *focus = scope->body.v[scope->body.n - 1];
      p->header = header;
      return match_statement(p, scope, named(focus->name, loc));
    }
  } else if (!repeat && tok(p)->kind == TOK_WORD &&
             (p->at + 1 < p->tokens.n &&
              !strcmp(((Token *)p->tokens.v[p->at + 1])->text, ":="))) {
    Stmt *focus = stmt(p);
    add(&scope->body, focus);
    add(&loop->focuses, focus->a->text);
  } else if (!is(p, "{") && !is(p, "while^") && !is(p, "until^")) {
    char *focus = "it^";
    Spec *annotation = NULL;
    Expr *start = NULL;
    if (!repeat && tok(p)->kind == TOK_WORD && p->at + 1 < p->tokens.n &&
        (!strcmp(((Token *)p->tokens.v[p->at + 1])->text, "from^") ||
         (!strcmp(((Token *)p->tokens.v[p->at + 1])->text, ":") &&
          p->at + 2 < p->tokens.n &&
          strcmp(((Token *)p->tokens.v[p->at + 2])->text, "when^") &&
          strcmp(((Token *)p->tokens.v[p->at + 2])->text, "other^") &&
          strcmp(((Token *)p->tokens.v[p->at + 2])->text, "el^") &&
          strcmp(((Token *)p->tokens.v[p->at + 2])->text, "else^")))) {
      focus = decl_word(p);
      if (take(p, ":"))
        annotation = spec(p);
      need(p, "from^");
      start = expr(p, 0);
    } else
      start = expr(p, 0);
    if (!repeat && is(p, "{")) {
      p->header = header;
      return match_statement(p, scope, start);
    }
    if (!repeat && is(p, ":")) {
      p->header = header;
      return matched_statement(p, scope, start);
    }
    counter = fmt("\001counter%d", ++p->serial);
    limit = fmt("\001limit%d", p->serial);
    if (repeat) {
      add(&scope->body, binding(limit, start, false));
      ((Stmt *)scope->body.v[scope->body.n - 1])->integer = true;
      add(&scope->body,
          binding(counter, contextual(number("0", loc), named(limit, loc)),
                  true));
      loop->a = binary("<", named(counter, loc), named(limit, loc));
      add(&loop->update,
          assignment(named(counter, loc),
                     contextual(number("1", loc), named(counter, loc)), "+"));
    } else {
      bool down = take(p, "downto^");
      if (!down)
        need(p, "to^");
      Expr *end = expr(p, 0);
      Stmt *initial = binding(
          counter,
          !annotation &&
                  (start->kind == E_INT || start->kind == E_FLOAT ||
                   (start->kind == E_UNARY && !strcmp(start->text, "-") &&
                    (start->a->kind == E_INT || start->a->kind == E_FLOAT)))
              ? contextual(start, end)
              : start,
          true);
      initial->spec = annotation;
      initial->integer = true;
      add(&scope->body, initial);
      add(&scope->body,
          binding(limit, contextual(end, named(counter, loc)), false));
      Expr *step = take(p, "step^") ? expr(p, 0) : number("1", loc);
      char *stride = fmt("\001step%d", p->serial),
           *active = fmt("\001active%d", p->serial);
      add(&scope->body,
          binding(stride, contextual(step, named(counter, loc)), false));
      Expr *positive =
          binary(">", named(stride, loc),
                 contextual(number("0", loc), named(counter, loc)));
      Expr *bounds =
          binary(down ? ">=" : "<=", named(counter, loc), named(limit, loc));
      if ((step->kind == E_INT && !strcmp(step->text, "0")) ||
          (step->kind == E_UNARY && !strcmp(step->text, "-") &&
           step->a->kind == E_INT))
        fail(step->loc, "step^ must be positive");
      Expr *check = node(E_CALL, loc);
      check->a = named("assert", loc);
      add(&check->args, positive);
      Stmt *validate = mem(sizeof(*validate));
      validate->kind = S_EXPR;
      validate->loc = loc;
      validate->a = check;
      add(&scope->body, validate);
      add(&scope->body, binding(active, bounds, true));
      loop->a = named(active, loc);
      add(&scope->body, binding(focus, named(counter, loc), false));
      add(&loop->focuses, focus);
      Stmt *refresh = assignment(named(focus, loc), named(counter, loc), NULL);
      refresh->internal = true;
      add(&loop->pre, refresh);
      Expr *a = node(E_CALL, loc), *b = node(E_CALL, loc),
           *c = node(E_CALL, loc);
      a->a = named("u128^", loc);
      b->a = named("u128^", loc);
      c->a = named("u128^", loc);
      add(&a->args, named(down ? counter : limit, loc));
      add(&b->args, named(down ? limit : counter, loc));
      add(&c->args, named(stride, loc));
      Stmt *advance = mem(sizeof(*advance));
      advance->kind = S_IF;
      advance->loc = loc;
      advance->a = binary(">", c, binary("-", a, b));
      Expr *no = node(E_BOOL, loc);
      no->text = "false^";
      add(&advance->body, assignment(named(active, loc), no, NULL));
      add(&advance->other, assignment(named(counter, loc), named(stride, loc),
                                      down ? "-" : "+"));
      add(&loop->update, advance);
    }
  }
  if (counter && (is(p, "while^") || is(p, "until^")))
    fail(loc, "counted loop cannot take another driving clause");
  if (!repeat && !scope->body.n && !counter)
    fail(loc, "for^ requires a focus; use repeat^ for a focusless loop");
  if (!repeat && (is(p, "while^") || is(p, "until^")) && scope->body.n) {
    Stmt *focus = scope->body.v[scope->body.n - 1];
    if (focus->kind == S_BIND && !focus->mut)
      fail(focus->loc, "while^/until^ focus requires var^");
  }
  if (take(p, "while^") || take(p, "until^")) {
    Token *keyword = p->tokens.v[p->at - 1];
    loop->a = expr(p, 0);
    if (!strcmp(keyword->text, "until^")) {
      Expr *not = node(E_UNARY, loc);
      not->text = "!";
      not->a = loop->a;
      loop->a = not;
    }
  }
  if (!loop->a) {
    if (!repeat)
      fail(loc, "for^ requires a driving clause");
    loop->a = node(E_BOOL, loc);
    loop->a->text = "true^";
  }
  if (take(p, "next^")) {
    if (repeat || counter)
      fail(loc,
           "repeat^ and counted loops do not accept an explicit update clause");
    do {
      Stmt *update = stmt(p);
      if (update->kind != S_ASSIGN &&
          !(update->kind == S_EXPR && update->a->kind == E_CALL))
        fail(update->loc, "next^ update requires an assignment or call");
      add(&loop->update, update);
    } while (take(p, ","));
  }
  p->header = header;
  loop_body(p, loop);
  add(&scope->body, loop);
  return scope;
}
static Stmt *stmt(Parser *p) {
  Token *t = tok(p);
  Stmt *s = mem(sizeof(*s));
  s->loc = t->loc;
  if (take(p, "let^") || take(p, "var^")) {
    s->kind = S_BIND;
    s->mut = !strcmp(t->text, "var^");
    List bindings = {0}, values = {0};
    do {
      Stmt *binding = mem(sizeof(*binding));
      binding->kind = S_BIND;
      binding->loc = tok(p)->loc;
      binding->name = take(p, "_^") ? "_^" : decl_word(p);
      binding->mut = s->mut;
      if (take(p, ":"))
        binding->spec = spec(p);
      add(&bindings, binding);
    } while (!p->header && take(p, ","));
    if (!(take(p, "=") || (s->mut && take(p, ":="))))
      need(p, "=");
    do {
      add(&values, expr(p, 0));
    } while (!p->header && take(p, ","));
    if (bindings.n != values.n)
      fail(s->loc, "the number of bindings and values does not match; tuple "
                   "returns are not implemented");
    for (int j = 0; j < bindings.n; j++)
      ((Stmt *)bindings.v[j])->a = values.v[j];
    if (bindings.n == 1)
      *s = *(Stmt *)bindings.v[0];
    else {
      s->kind = S_BIND_MANY;
      s->body = bindings;
    }
  } else if (take(p, "for^") || take(p, "repeat^")) {
    s = loop_statement(p, !strcmp(t->text, "repeat^"), t->loc);
  } else if (t->kind == TOK_WORD &&
             (!strncmp(t->text, "break^", 6) || !strncmp(t->text, "next^", 5) ||
              !strncmp(t->text, "skip^", 5) ||
              !strncmp(t->text, "continue^", 9))) {
    next(p);
    s->kind = !strncmp(t->text, "break^", 6) ? S_BREAK : S_NEXT;
    s->depth = 0;
    for (const char *q = strchr(t->text, '^'); q && *q == '^'; q++)
      s->depth++;
    if (take(p, "[")) {
      if (s->depth > 1)
        fail(t->loc, "jump depth must use hats or brackets, not both");
      Token *depth = next(p);
      if (depth->kind != TOK_INT)
        fail(depth->loc, "jump depth must be a positive integer literal");
      errno = 0;
      long count = strtol(depth->text, NULL, 10);
      if (errno || count <= 0 || count > 65536)
        fail(depth->loc, "jump depth must be positive and at most 65536");
      s->depth = (int)count;
      need(p, "]");
    }
    if (s->depth < 1)
      fail(t->loc, "jump depth must be positive");
  } else if (take(p, "return^")) {
    s->kind = S_RETURN;
    if (tok(p)->kind != TOK_EOF && !is(p, "}") && !is(p, ";") &&
        !branch_word(p) && !match_clause(p) && loop_section(p) < 0 &&
        tok(p)->loc.line == t->loc.line)
      s->a = expr(p, 0);
  } else if (take(p, "while^")) {
    fail(t->loc,
         "while^ is a driving clause; write repeat^ while^ or for^ ... while^");
  } else if (take(p, "if^")) {
    bool header = p->header;
    p->header = true;
    s->a = expr(p, 0);
    p->header = header;
    if (take(p, ":")) {
      s->kind = S_EXPR;
      s->a = if_tail(p, s->a, t->loc);
      return s;
    }
    need(p, "{");
    p->scope_depth++;
    s = if_statement(p, s->a, t->loc);
    need(p, "}");
    p->scope_depth--;
  } else if (is(p, "{")) {
    s->kind = S_BLOCK;
    block(p, &s->body);
  } else {
    s->a = expr(p, 0);
    add(&s->targets, s->a);
    while ((!p->header || s->a->kind != E_CALL) && take(p, ","))
      add(&s->targets, expr(p, 0));
    const char *op = assignment_operator(p);
    if (op) {
      next(p);
      s->kind = S_ASSIGN;
      if (*op == '?') {
        s->optional = true;
        op++;
      }
      if (strcmp(op, ":="))
        s->op = slice(op, strlen(op) - 1);
      do {
        add(&s->values, expr(p, 0));
      } while ((!p->header || s->values.n < s->targets.n) && take(p, ","));
      if (s->targets.n != s->values.n)
        fail(s->loc, "the number of targets and values does not match");
      s->b = s->values.v[0];
    } else {
      if (s->targets.n != 1)
        fail(s->loc, "multiple expressions require an assignment");
      s->kind = S_EXPR;
    }
  }
  take(p, ";");
  return s;
}
static void block(Parser *p, List *out) {
  need(p, "{");
  p->scope_depth++;
  while (!is(p, "}")) {
    if (tok(p)->kind == TOK_EOF)
      fail(tok(p)->loc, "unterminated block");
    if (take(p, ";"))
      continue;
    add(out, stmt(p));
  }
  need(p, "}");
  p->scope_depth--;
}
static Function *function(Parser *p, char *name, Def *owner, bool abstr) {
  Function *f = mem(sizeof(*f));
  f->loc = tok(p)->loc;
  f->enclosing = p->enclosing;
  f->lexical = p->scope_depth + 2;
  f->name = name;
  f->owner = owner;
  bool templ = take(p, "template^");
  f->templ = templ;
  f->abstract = abstr;
  if (take(p, "p^"))
    f->procedure = true;
  else
    need(p, "f^");
  if (take(p, "mutable^"))
    f->mutable_self = true;
  if (!is(p, "{") && !is(p, "->") && !is(p, ";")) {
    do {
      Param *a = mem(sizeof(*a));
      a->loc = tok(p)->loc;
      a->name = take(p, "self^") ? "self^"
                : take(p, "_^")  ? "_^"
                                 : decl_word(p);
      a->self = !strcmp(a->name, "self^");
      if (a->self && (!owner || f->params.n))
        fail(a->loc, "self^ must be the first parameter of a method");
      if (take(p, ":"))
        a->spec = spec(p);
      if (!a->self && !a->spec && !templ)
        fail(a->loc, "untyped parameter requires template^");
      add(&f->params, a);
    } while (take(p, ","));
  }
  if (f->mutable_self && (!owner || !f->params.n ||
                          !((Param *)f->params.v[0])->self || f->procedure))
    fail(f->loc, "mutable^self^ requires an f^ method receiver");
  if (take(p, "->"))
    f->result = spec(p);
  if (abstr)
    need(p, ";");
  else {
    Function *enclosing = p->enclosing;
    p->enclosing = f;
    block(p, &f->body);
    p->enclosing = enclosing;
    if (!f->procedure && f->body.n == 1) {
      Stmt *only = f->body.v[0];
      if (only->kind == S_EXPR)
        only->kind = S_RETURN;
    }
  }
  return f;
}
static void definition(Parser *p) {
  Def *d = mem(sizeof(*d));
  d->loc = tok(p)->loc;
  d->name = decl_word(p);
  d->id = p->p->defs.n;
  add(&p->p->defs, d);
  if (take(p, "=")) {
    do {
      add(&d->union_specs, spec_part(p));
    } while (take(p, "|"));
    take(p, ";");
    return;
  }
  if (take(p, ":")) {
    do {
      add(&d->base_names, qualified_word(p));
    } while (take(p, ","));
  }
  need(p, "{");
  while (!take(p, "}")) {
    if (tok(p)->kind == TOK_EOF)
      fail(tok(p)->loc, "unterminated def^");
    if (take(p, ",") || take(p, ";"))
      continue;
    if (take(p, "self^")) {
      need(p, "{");
      while (!take(p, "}")) {
        if (take(p, ",") || take(p, ";"))
          continue;
        Field *f = mem(sizeof(*f));
        f->loc = tok(p)->loc;
        f->owner = f->initializer_owner = d;
        while (is(p, "private^") || is(p, "abstract^")) {
          if (take(p, "private^"))
            f->priv = true;
          else {
            next(p);
            f->abstract = true;
          }
        }
        f->name = decl_word(p);
        need(p, ":");
        f->spec = spec(p);
        if (take(p, "=") || take(p, ":="))
          f->init = expr(p, 0);
        if (f->abstract && f->init)
          fail(f->loc, "abstract field cannot have an initializer");
        add(&d->own_fields, f);
      }
      continue;
    }
    bool priv = false, abstr = false, over = false;
    while (is(p, "private^") || is(p, "abstract^") || is(p, "override^")) {
      if (take(p, "private^"))
        priv = true;
      else if (take(p, "abstract^"))
        abstr = true;
      else {
        next(p);
        over = true;
      }
    }
    char *name = decl_word(p);
    need(p, abstr ? ":" : "=");
    Function *f = function(p, name, d, abstr);
    f->priv = priv;
    f->override = over;
    add(&d->own_methods, f);
  }
}
void parse_file(Program *p, const char *path, Module *module) {
  p->path = path;
  FILE *f = fopen(path, "rb");
  if (!f)
    fail((Loc){path, 1, 1}, "cannot open source");
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  if (n < 0 || n > 16 * 1024 * 1024)
    fail((Loc){path, 1, 1}, "source too large");
  rewind(f);
  p->source = mem((size_t)n + 1);
  if (fread(p->source, 1, (size_t)n, f) != (size_t)n)
    fail((Loc){path, 1, 1}, "cannot read source");
  fclose(f);
  Parser q = {0};
  q.p = p;
  q.module = module;
  lex(&q);
  p->entry = mem(sizeof(*p->entry));
  p->entry->loc = (Loc){path, 1, 1};
  p->entry->name = "<top-level>";
  p->entry->procedure = true;
  module->entry = p->entry;
  if (take(&q, "module^")) {
    module->name = qualified_word(&q);
    take(&q, ";");
  }
  while (tok(&q)->kind != TOK_EOF) {
    if (take(&q, ";"))
      continue;
    bool public = take(&q, "public^");
    if (take(&q, "import^") ||
        (is(&q, "let^") && q.at + 3 < q.tokens.n &&
         !strcmp(((Token *)q.tokens.v[q.at + 2])->text, "=") &&
         !strcmp(((Token *)q.tokens.v[q.at + 3])->text, "import^"))) {
      if (public)
        fail(tok(&q)->loc, "imports cannot be public^");
      Import *i = mem(sizeof(*i));
      i->loc = tok(&q)->loc;
      if (take(&q, "let^")) {
        i->alias = decl_word(&q);
        need(&q, "=");
        need(&q, "import^");
      }
      i->name = qualified_word(&q);
      add(&module->imports, i);
      take(&q, ";");
      continue;
    }
    if (take(&q, "errordef^")) {
      Def *group = mem(sizeof(*group));
      group->loc = tok(&q)->loc;
      group->name = decl_word(&q);
      group->module = module;
      group->public = public;
      group->id = p->defs.n;
      add(&p->defs, group);
      add(&module->defs, group);
      need(&q, "{");
      do {
        if (is(&q, "}"))
          break;
        Def *arm = mem(sizeof(*arm));
        arm->loc = tok(&q)->loc;
        arm->name = fmt("%s.%s", group->name, decl_word(&q));
        arm->module = module;
        arm->public = public;
        arm->error_variant = true;
        arm->id = p->defs.n;
        add(&p->defs, arm);
        add(&module->defs, arm);
        Spec *alternative = mem(sizeof(*alternative));
        alternative->kind = SPEC_NAME;
        alternative->loc = arm->loc;
        alternative->name = arm->name;
        add(&group->union_specs, alternative);
      } while (take(&q, ","));
      need(&q, "}");
      if (!group->union_specs.n) {
        fail(group->loc, "errordef^ requires a variant");
      }
      continue;
    }
    if (take(&q, "def^")) {
      definition(&q);
      Def *d = p->defs.v[p->defs.n - 1];
      d->module = module;
      d->public = public;
      add(&module->defs, d);
      continue;
    }
    bool binding = false;
    if (is(&q, "let^") && q.at + 3 < q.tokens.n) {
      Token *equal = q.tokens.v[q.at + 2];
      Token *initializer = q.tokens.v[q.at + 3];
      binding = !strcmp(equal->text, "=") && initializer->kind == TOK_WORD &&
                (!strcmp(initializer->text, "f^") ||
                 !strcmp(initializer->text, "p^") ||
                 !strcmp(initializer->text, "template^"));
    }
    if (!binding) {
      Stmt *s = stmt(&q);
      if (!module->root) {
        if ((s->kind != S_BIND && s->kind != S_BIND_MANY) || s->mut)
          fail(s->loc, "library top level permits only declarations and "
                       "immutable constants");
        List bindings = {0};
        if (s->kind == S_BIND)
          add(&bindings, s);
        else
          bindings = s->body;
        for (int j = 0; j < bindings.n; j++) {
          Stmt *value = bindings.v[j];
          Constant *c = mem(sizeof(*c));
          bool discard = !strcmp(value->name, "_^");
          c->name =
              discard ? fmt("\001constant_discard%d", ++q.serial) : value->name;
          c->binding = value;
          c->public = public && !discard;
          add(&module->constants, c);
        }
      } else {
        if (public)
          fail(s->loc, "public^ executable bindings are not supported in the "
                       "entry file");
        add(&p->entry->body, s);
      }
      continue;
    }
    int binding_start = q.at;
    need(&q, "let^");
    char *name = decl_word(&q);
    need(&q, "=");
    Function *parsed_function = function(&q, name, NULL, false);
    /* A called literal initializes a value; it does not declare a named
     * function. */
    if (is(&q, "(") || is(&q, ".") || is(&q, "[") ||
        (tok(&q)->kind != TOK_STRING && prec(tok(&q)->text) >= 0)) {
      q.at = binding_start;
      add(&p->entry->body, stmt(&q));
      continue;
    }
    parsed_function->module = module;
    parsed_function->public = public;
    add(&module->functions, parsed_function);
    add(&p->functions, parsed_function);
    take(&q, ";");
  }
}
