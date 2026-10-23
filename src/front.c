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
} Token;
enum { TOK_WORD = 256, TOK_INT, TOK_FLOAT, TOK_STRING, TOK_EOF };
typedef struct {
  Program *p;
  List tokens;
  int at;
  bool header;
} Parser;
static Token *tok(Parser *p) { return p->tokens.v[p->at]; }
static bool is(Parser *p, const char *s) { return !strcmp(tok(p)->text, s); }
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
  if (t->kind != TOK_WORD)
    fail(t->loc, "expected a name");
  return t->text;
}
static char *slice(const char *s, size_t n) {
  char *r = mem(n + 1);
  memcpy(r, s, n);
  return r;
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
    if (isalpha((unsigned char)*s) || *s == '_') {
      while (isalnum((unsigned char)*s) || *s == '_')
        s++;
      if (*s == '^')
        s++;
      t->kind = TOK_WORD;
    } else if (isdigit((unsigned char)*s)) {
      t->kind = TOK_INT;
      while (isdigit((unsigned char)*s))
        s++;
      if (*s == '.' && isdigit((unsigned char)s[1])) {
        t->kind = TOK_FLOAT;
        s++;
        while (isdigit((unsigned char)*s))
          s++;
      }
      if (*s == 'e' || *s == 'E') {
        t->kind = TOK_FLOAT;
        s++;
        if (*s == '+' || *s == '-')
          s++;
        if (!isdigit((unsigned char)*s))
          fail(t->loc, "invalid exponent");
        while (isdigit((unsigned char)*s))
          s++;
      }
    } else if (*s == '"') {
      s++;
      char *out = mem(strlen(s) + 1);
      int n = 0;
      while (*s && *s != '"') {
        if (*s == '\n' || *s == '\r')
          fail(t->loc, "newline in string");
        if (*s == '\\') {
          s++;
          switch (*s) {
          case 'n':
            out[n++] = '\n';
            break;
          case 'r':
            out[n++] = '\r';
            break;
          case 't':
            out[n++] = '\t';
            break;
          case '\\':
            out[n++] = '\\';
            break;
          case '"':
            out[n++] = '"';
            break;
          default:
            fail(t->loc, "unsupported string escape");
          }
          if (*s)
            s++;
        } else
          out[n++] = *s++;
      }
      if (*s != '"')
        fail(t->loc, "unterminated string");
      s++;
      t->kind = TOK_STRING;
      t->text = out;
    } else {
      t->kind = (unsigned char)*s++;
      if ((start[0] == ':' && *s == '=') || (start[0] == '-' && *s == '>') ||
          (strchr("=!<>", start[0]) && *s == '=') ||
          (start[0] == '&' && *s == '&') || (start[0] == '|' && *s == '|') ||
          (start[0] == '.' && *s == '.'))
        s++;
    }
    if (!t->text)
      t->text = slice(start, (size_t)(s - start));
    col += (int)(s - start);
    add(&p->tokens, t);
  }
  Token *end = mem(sizeof(*end));
  end->kind = TOK_EOF;
  end->text = "<eof>";
  end->loc = (Loc){p->p->path, line, col};
  add(&p->tokens, end);
}
static Spec *spec(Parser *p) {
  Spec *s = mem(sizeof(*s));
  s->loc = tok(p)->loc;
  if (take(p, "&")) {
    s->kind = SPEC_REF;
    s->elem = spec(p);
    return s;
  }
  s->kind = SPEC_NAME;
  s->name = word(p);
  while (take(p, "[")) {
    Spec *w = mem(sizeof(*w));
    w->loc = s->loc;
    w->elem = s;
    if (take(p, ">")) {
      need(p, "..");
      w->kind = SPEC_GROW;
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
static Expr *expr(Parser *p, int min);
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
static bool type_constructor(Parser *p, const char *name) {
  for (int n = 0; n + 1 < p->tokens.n; n++) {
    Token *t = p->tokens.v[n], *next_token = p->tokens.v[n + 1];
    if (!strcmp(t->text, "def^") && !strcmp(next_token->text, name))
      return true;
  }
  return false;
}
static Expr *primary(Parser *p) {
  Token *t = next(p);
  Expr *e = NULL;
  if (t->kind == TOK_INT || t->kind == TOK_FLOAT || t->kind == TOK_STRING) {
    e = node(t->kind == TOK_INT     ? E_INT
             : t->kind == TOK_FLOAT ? E_FLOAT
                                    : E_STRING,
             t->loc);
    e->text = t->text;
  } else if (!strcmp(t->text, "true^") || !strcmp(t->text, "false^")) {
    e = node(E_BOOL, t->loc);
    e->text = t->text;
  } else if (!strcmp(t->text, "(")) {
    e = expr(p, 0);
    need(p, ")");
  } else if (!strcmp(t->text, "[")) {
    e = node(E_ARRAY, t->loc);
    entries(p, e, "]", false);
  } else if (!strcmp(t->text, "{")) {
    e = node(E_RECORD, t->loc);
    entries(p, e, "}", true);
  } else if (!strcmp(t->text, "&") || !strcmp(t->text, "-") ||
             !strcmp(t->text, "!") || !strcmp(t->text, "*")) {
    e = node(E_UNARY, t->loc);
    e->text = t->text;
    e->a = expr(p, 8);
  } else if (t->kind == TOK_WORD) {
    if (!strcmp(t->text, "f^") || !strcmp(t->text, "p^"))
      fail(t->loc,
           "function values and closures are not supported in this version");
    e = node(!strcmp(t->text, "self^") ? E_SELF : E_NAME, t->loc);
    e->text = t->text;
  } else
    fail(t->loc, "expected an expression, found '%s'", t->text);
  for (;;) {
    if (take(p, ".")) {
      Expr *n = node(E_MEMBER, e->loc);
      n->a = e;
      n->text = word(p);
      e = n;
    } else if (take(p, "(")) {
      Expr *n = node(E_CALL, e->loc);
      n->a = e;
      entries(p, n, ")", false);
      e = n;
    } else if (take(p, "[")) {
      Expr *n = node(E_INDEX, e->loc);
      n->a = e;
      n->b = expr(p, 0);
      need(p, "]");
      e = n;
    } else if (!p->header && e->kind == E_NAME &&
               type_constructor(p, e->text) && take(p, "{")) {
      Expr *n = node(E_RECORD, e->loc);
      n->text = e->text;
      entries(p, n, "}", true);
      e = n;
    } else
      break;
  }
  return e;
}
static int prec(const char *s) {
  if (!strcmp(s, "or^") || !strcmp(s, "||"))
    return 1;
  if (!strcmp(s, "and^") || !strcmp(s, "&&"))
    return 2;
  if (!strcmp(s, "=") || !strcmp(s, "==") || !strcmp(s, "!=") ||
      !strcmp(s, "<") || !strcmp(s, ">") || !strcmp(s, "<=") ||
      !strcmp(s, ">="))
    return 3;
  if (!strcmp(s, "+") || !strcmp(s, "-"))
    return 4;
  if (!strcmp(s, "*") || !strcmp(s, "/") || !strcmp(s, "%"))
    return 5;
  return -1;
}
static Expr *expr(Parser *p, int min) {
  Expr *e = primary(p);
  for (;;) {
    Token *t = tok(p);
    int pr = prec(t->text);
    if (pr < min)
      break;
    next(p);
    Expr *b = node(E_BIN, t->loc);
    b->text = t->text;
    b->a = e;
    b->b = expr(p, pr + 1);
    e = b;
  }
  return e;
}
static void block(Parser *p, List *out);
static Stmt *stmt(Parser *p) {
  Token *t = tok(p);
  Stmt *s = mem(sizeof(*s));
  s->loc = t->loc;
  if (take(p, "let^") || take(p, "var^")) {
    s->kind = S_BIND;
    s->mut = !strcmp(t->text, "var^");
    s->name = word(p);
    if (take(p, ":"))
      s->spec = spec(p);
    need(p, "=");
    s->a = expr(p, 0);
  } else if (take(p, "return^")) {
    s->kind = S_RETURN;
    if (!is(p, "}") && !is(p, ";") && tok(p)->loc.line == t->loc.line)
      s->a = expr(p, 0);
  } else if (take(p, "if^") || take(p, "while^")) {
    s->kind = !strcmp(t->text, "if^") ? S_IF : S_WHILE;
    p->header = true;
    s->a = expr(p, 0);
    p->header = false;
    block(p, &s->body);
    if (s->kind == S_IF && (take(p, "else^") || take(p, "el^")))
      block(p, &s->other);
  } else if (is(p, "{")) {
    s->kind = S_BLOCK;
    block(p, &s->body);
  } else {
    s->a = expr(p, 0);
    if (take(p, ":=")) {
      s->kind = S_ASSIGN;
      s->b = expr(p, 0);
    } else
      s->kind = S_EXPR;
  }
  take(p, ";");
  return s;
}
static void block(Parser *p, List *out) {
  need(p, "{");
  while (!is(p, "}")) {
    if (tok(p)->kind == TOK_EOF)
      fail(tok(p)->loc, "unterminated block");
    if (take(p, ";"))
      continue;
    add(out, stmt(p));
  }
  need(p, "}");
}
static Function *function(Parser *p, char *name, Def *owner, bool templ,
                          bool abstr) {
  Function *f = mem(sizeof(*f));
  f->loc = tok(p)->loc;
  f->name = name;
  f->owner = owner;
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
      a->name = word(p);
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
  else
    block(p, &f->body);
  return f;
}
static void definition(Parser *p) {
  Def *d = mem(sizeof(*d));
  d->loc = tok(p)->loc;
  d->name = word(p);
  d->id = p->p->defs.n;
  add(&p->p->defs, d);
  if (take(p, ":")) {
    do {
      add(&d->base_names, word(p));
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
        f->name = word(p);
        need(p, ":");
        f->spec = spec(p);
        if (take(p, "="))
          f->init = expr(p, 0);
        if (f->abstract && f->init)
          fail(f->loc, "abstract field cannot have an initializer");
        add(&d->own_fields, f);
      }
      continue;
    }
    bool priv = false, abstr = false, over = false, templ = false;
    while (is(p, "private^") || is(p, "abstract^") || is(p, "override^") ||
           is(p, "template^")) {
      if (take(p, "private^"))
        priv = true;
      else if (take(p, "abstract^"))
        abstr = true;
      else if (take(p, "override^"))
        over = true;
      else {
        next(p);
        templ = true;
      }
    }
    char *name = word(p);
    need(p, abstr ? ":" : "=");
    Function *f = function(p, name, d, templ, abstr);
    f->priv = priv;
    f->override = over;
    add(&d->own_methods, f);
  }
}
void parse(Program *p, const char *path) {
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
  lex(&q);
  while (tok(&q)->kind != TOK_EOF) {
    if (take(&q, ";"))
      continue;
    if (take(&q, "def^")) {
      definition(&q);
      continue;
    }
    bool templ = take(&q, "template^");
    need(&q, "let^");
    char *name = word(&q);
    need(&q, "=");
    templ = take(&q, "template^") || templ;
    add(&p->functions, function(&q, name, NULL, templ, false));
    take(&q, ";");
  }
}
