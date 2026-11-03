/* regex.h - small ReDoS-safe regex (Thompson NFA simulation) and path glob matcher (portable)
 * Syntax: literals . ^ $ * + ? {n} {n,} {n,m} | ( ) (?: ) [set] [^set] [[:alpha:]] \d \D \w \W \s \S \b \B \t \n \r and escaped punctuation.
 * No back-references or look-around (they cannot be matched in linear time). Matching is byte based. */
#ifndef LLAMAC_REGEX_H
#define LLAMAC_REGEX_H

enum { RX_CHAR, RX_ANY, RX_CLASS, RX_SPLIT, RX_JMP, RX_MATCH, RX_BOL, RX_EOL, RX_WORDB, RX_NWORDB };
typedef struct { int op, x, y; } RxInst;
typedef struct RxN { int t, c, min, max; struct RxN *a, *b; } RxN;
enum { N_EMPTY, N_CHAR, N_ANY, N_CLASS, N_CAT, N_ALT, N_STAR, N_PLUS, N_QUEST, N_REP, N_BOL, N_EOL, N_WORDB, N_NWORDB };
#define RX_MAXINST 4000
typedef struct
{
    RxInst *code;
    int n, cap;
    unsigned char (*cls)[32];
    int ncls, capcls;
    int icase, hit;
    char err[96];
    const char *p;
    int depth;
    RxN **pool;
    int npool, cappool;
    int *cl, *nl, *mark, gen;
} Rx;

static RxN *rxn_new(Rx *r, int t)
{
    RxN *n = (RxN *)calloc(1, sizeof *n);
    n->t = t;
    if (r->npool == r->cappool)
    {
        r->cappool = r->cappool ? r->cappool * 2 : 32;
        r->pool = (RxN **)realloc(r->pool, (size_t)r->cappool * sizeof(RxN *));
    }
    r->pool[r->npool++] = n;
    return n;
}
static void rx_fail(Rx *r, const char *m)
{
    if (!r->err[0])
        snprintf(r->err, sizeof r->err, "%s", m);
}
static int rx_isword(int c) { return c == '_' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static void rx_set_add(unsigned char *s, int c) { s[(c & 255) >> 3] |= (unsigned char)(1 << (c & 7)); }
static int rx_set_has(const unsigned char *s, int c) { return (s[(c & 255) >> 3] >> (c & 7)) & 1; }
static void rx_set_esc(unsigned char *s, int e)
{
    switch (e)
    {
    case 'd':
        for (int c = '0'; c <= '9'; c++)
            rx_set_add(s, c);
        break;
    case 'w':
        for (int c = 0; c < 256; c++)
            if (rx_isword(c))
                rx_set_add(s, c);
        break;
    case 's':
        rx_set_add(s, ' ');
        rx_set_add(s, '\t');
        rx_set_add(s, '\n');
        rx_set_add(s, '\r');
        rx_set_add(s, '\f');
        rx_set_add(s, '\v');
        break;
    case 'D':
    case 'W':
    case 'S':
    {
        unsigned char t[32] = {0};
        rx_set_esc(t, e + 32);
        for (int c = 0; c < 256; c++)
            if (!rx_set_has(t, c))
                rx_set_add(s, c);
        break;
    }
    }
}
static int rx_esc_char(int e)
{
    switch (e)
    {
    case 'n': return '\n';
    case 't': return '\t';
    case 'r': return '\r';
    case 'f': return '\f';
    case 'v': return '\v';
    case '0': return 0;
    }
    return e;
}
static int rx_new_class(Rx *r, const unsigned char *set)
{
    if (r->ncls == r->capcls)
    {
        r->capcls = r->capcls ? r->capcls * 2 : 8;
        r->cls = (unsigned char(*)[32])realloc(r->cls, (size_t)r->capcls * 32);
    }
    memcpy(r->cls[r->ncls], set, 32);
    return r->ncls++;
}
static RxN *rx_parse_alt(Rx *r);
static int rx_posix(const char *nm, int c)
{
    return !strcmp(nm, "alpha") ? isalpha(c) : !strcmp(nm, "digit") ? isdigit(c) : !strcmp(nm, "alnum") ? isalnum(c)
         : !strcmp(nm, "space") ? isspace(c) : !strcmp(nm, "upper") ? isupper(c) : !strcmp(nm, "lower") ? islower(c)
         : !strcmp(nm, "punct") ? ispunct(c) : !strcmp(nm, "xdigit") ? isxdigit(c) : 0;
}
static RxN *rx_parse_class(Rx *r)
{
    unsigned char set[32] = {0};
    int neg = 0, first = 1;
    RxN *n;
    r->p++;
    if (*r->p == '^')
    {
        neg = 1;
        r->p++;
    }
    while (*r->p && (*r->p != ']' || first))
    {
        int lo = (unsigned char)*r->p++, hi;
        first = 0;
        if (lo == '[' && *r->p == ':')
        {
            const char *e = strstr(r->p, ":]");
            char nm[16] = {0};
            size_t L = e ? (size_t)(e - r->p - 1) : 99;
            if (e && L < sizeof nm)
            {
                memcpy(nm, r->p + 1, L);
                for (int c = 0; c < 128; c++)
                    if (rx_posix(nm, c))
                        rx_set_add(set, c);
                r->p = e + 2;
                continue;
            }
        }
        if (lo == '\\' && *r->p)
        {
            int e = (unsigned char)*r->p++;
            if (strchr("dDwWsS", e))
            {
                rx_set_esc(set, e);
                continue;
            }
            lo = rx_esc_char(e);
        }
        hi = lo;
        if (*r->p == '-' && r->p[1] && r->p[1] != ']')
        {
            r->p++;
            hi = (unsigned char)*r->p++;
            if (hi == '\\' && *r->p)
                hi = rx_esc_char((unsigned char)*r->p++);
        }
        if (hi < lo)
        {
            rx_fail(r, "bad range in [...]");
            hi = lo;
        }
        for (int c = lo; c <= hi; c++)
            rx_set_add(set, c);
    }
    if (*r->p != ']')
        rx_fail(r, "missing ]");
    else
        r->p++;
    if (r->icase)
        for (int c = 'a'; c <= 'z'; c++)
            if (rx_set_has(set, c) || rx_set_has(set, c - 32))
            {
                rx_set_add(set, c);
                rx_set_add(set, c - 32);
            }
    if (neg)
        for (int i = 0; i < 32; i++)
            set[i] = (unsigned char)~set[i];
    n = rxn_new(r, N_CLASS);
    n->c = rx_new_class(r, set);
    return n;
}
static RxN *rx_parse_atom(Rx *r)
{
    int c = (unsigned char)*r->p;
    RxN *n;
    if (c == '(')
    {
        r->p++;
        if (r->p[0] == '?' && r->p[1] == ':')
            r->p += 2;
        if (++r->depth > 40)
        {
            rx_fail(r, "pattern nested too deeply");
            return rxn_new(r, N_EMPTY);
        }
        n = rx_parse_alt(r);
        r->depth--;
        if (*r->p != ')')
            rx_fail(r, "missing )");
        else
            r->p++;
        return n;
    }
    if (c == '.') { r->p++; return rxn_new(r, N_ANY); }
    if (c == '^') { r->p++; return rxn_new(r, N_BOL); }
    if (c == '$') { r->p++; return rxn_new(r, N_EOL); }
    if (c == '[')
        return rx_parse_class(r);
    if (c == '\\' && r->p[1])
    {
        int e = (unsigned char)r->p[1];
        r->p += 2;
        if (strchr("dDwWsS", e))
        {
            unsigned char set[32] = {0};
            rx_set_esc(set, e);
            n = rxn_new(r, N_CLASS);
            n->c = rx_new_class(r, set);
            return n;
        }
        if (e == 'b') return rxn_new(r, N_WORDB);
        if (e == 'B') return rxn_new(r, N_NWORDB);
        n = rxn_new(r, N_CHAR);
        n->c = rx_esc_char(e);
        return n;
    }
    if (c == '*' || c == '+' || c == '?')
    {
        rx_fail(r, "nothing to repeat");
        r->p++;
        return rxn_new(r, N_EMPTY);
    }
    r->p++;
    n = rxn_new(r, N_CHAR);
    n->c = c;
    return n;
}
static int rx_num(const char **p)
{
    int v = 0, any = 0;
    while (**p >= '0' && **p <= '9')
    {
        v = v * 10 + (**p - '0');
        if (v > 1000)
            v = 1000;
        (*p)++;
        any = 1;
    }
    return any ? v : -1;
}
static RxN *rx_parse_rep(Rx *r)
{
    RxN *a = rx_parse_atom(r);
    for (;;)
    {
        int c = *r->p;
        RxN *n;
        if (c == '*' || c == '+' || c == '?')
        {
            n = rxn_new(r, c == '*' ? N_STAR : c == '+' ? N_PLUS : N_QUEST);
            n->a = a;
            a = n;
            r->p++;
        }
        else if (c == '{' && r->p[1] >= '0' && r->p[1] <= '9')
        {
            const char *q = r->p + 1;
            int mn = rx_num(&q), mx = mn;
            if (*q == ',')
            {
                q++;
                mx = rx_num(&q);
            }
            if (*q != '}')
                break;
            if (mx != -1 && mx < mn)
            {
                rx_fail(r, "bad {n,m}");
                mx = mn;
            }
            n = rxn_new(r, N_REP);
            n->a = a;
            n->min = mn;
            n->max = mx;
            a = n;
            r->p = q + 1;
        }
        else
            break;
        if (*r->p == '?' || *r->p == '+')
            r->p++;
    }
    return a;
}
static RxN *rx_parse_cat(Rx *r)
{
    RxN *l = NULL;
    while (*r->p && *r->p != '|' && *r->p != ')')
    {
        RxN *x = rx_parse_rep(r);
        if (r->err[0])
            return l ? l : x;
        if (!l)
            l = x;
        else
        {
            RxN *n = rxn_new(r, N_CAT);
            n->a = l;
            n->b = x;
            l = n;
        }
    }
    return l ? l : rxn_new(r, N_EMPTY);
}
static RxN *rx_parse_alt(Rx *r)
{
    RxN *l = rx_parse_cat(r);
    while (*r->p == '|' && !r->err[0])
    {
        RxN *n = rxn_new(r, N_ALT);
        r->p++;
        n->a = l;
        n->b = rx_parse_cat(r);
        l = n;
    }
    return l;
}
static int rx_inst(Rx *r, int op, int x, int y)
{
    if (r->n >= RX_MAXINST)
    {
        rx_fail(r, "pattern too complex");
        return r->n ? r->n - 1 : 0;
    }
    if (r->n == r->cap)
    {
        r->cap = r->cap ? r->cap * 2 : 64;
        r->code = (RxInst *)realloc(r->code, (size_t)r->cap * sizeof(RxInst));
    }
    r->code[r->n].op = op;
    r->code[r->n].x = x;
    r->code[r->n].y = y;
    return r->n++;
}
static void rx_emit(Rx *r, RxN *n)
{
    if (r->err[0] || !n)
        return;
    switch (n->t)
    {
    case N_EMPTY: break;
    case N_CHAR:
        if (r->icase && isalpha(n->c))
        {
            unsigned char set[32] = {0};
            rx_set_add(set, tolower(n->c));
            rx_set_add(set, toupper(n->c));
            rx_inst(r, RX_CLASS, rx_new_class(r, set), 0);
        }
        else
            rx_inst(r, RX_CHAR, n->c, 0);
        break;
    case N_ANY: rx_inst(r, RX_ANY, 0, 0); break;
    case N_CLASS: rx_inst(r, RX_CLASS, n->c, 0); break;
    case N_BOL: rx_inst(r, RX_BOL, 0, 0); break;
    case N_EOL: rx_inst(r, RX_EOL, 0, 0); break;
    case N_WORDB: rx_inst(r, RX_WORDB, 0, 0); break;
    case N_NWORDB: rx_inst(r, RX_NWORDB, 0, 0); break;
    case N_CAT: rx_emit(r, n->a); rx_emit(r, n->b); break;
    case N_ALT:
    {
        int sp = rx_inst(r, RX_SPLIT, 0, 0), jm;
        r->code[sp].x = r->n;
        rx_emit(r, n->a);
        jm = rx_inst(r, RX_JMP, 0, 0);
        r->code[sp].y = r->n;
        rx_emit(r, n->b);
        r->code[jm].x = r->n;
        break;
    }
    case N_STAR:
    {
        int sp = rx_inst(r, RX_SPLIT, 0, 0);
        r->code[sp].x = r->n;
        rx_emit(r, n->a);
        rx_inst(r, RX_JMP, sp, 0);
        r->code[sp].y = r->n;
        break;
    }
    case N_PLUS:
    {
        int st = r->n, sp;
        rx_emit(r, n->a);
        sp = rx_inst(r, RX_SPLIT, st, 0);
        r->code[sp].y = r->n;
        break;
    }
    case N_QUEST:
    {
        int sp = rx_inst(r, RX_SPLIT, 0, 0);
        r->code[sp].x = r->n;
        rx_emit(r, n->a);
        r->code[sp].y = r->n;
        break;
    }
    case N_REP:
    {
        int pend[1001], np = 0;
        for (int i = 0; i < n->min && !r->err[0]; i++)
            rx_emit(r, n->a);
        if (n->max < 0)
        {
            int sp = rx_inst(r, RX_SPLIT, 0, 0);
            r->code[sp].x = r->n;
            rx_emit(r, n->a);
            rx_inst(r, RX_JMP, sp, 0);
            r->code[sp].y = r->n;
        }
        else
        {
            for (int i = n->min; i < n->max && !r->err[0]; i++)
            {
                int sp = rx_inst(r, RX_SPLIT, 0, 0);
                r->code[sp].x = r->n;
                pend[np++] = sp;
                rx_emit(r, n->a);
            }
            for (int i = 0; i < np; i++)
                r->code[pend[i]].y = r->n;
        }
        break;
    }
    }
}
static void rx_free(Rx *r)
{
    for (int i = 0; i < r->npool; i++)
        free(r->pool[i]);
    free(r->pool);
    free(r->code);
    free(r->cls);
    free(r->cl);
    free(r->nl);
    free(r->mark);
    memset(r, 0, sizeof *r);
}
/* 1 on success; on failure r->err says why (call rx_free either way) */
static int rx_compile(Rx *r, const char *pat, int icase)
{
    RxN *root;
    memset(r, 0, sizeof *r);
    r->icase = icase;
    r->p = pat;
    root = rx_parse_alt(r);
    if (!r->err[0] && *r->p == ')')
        rx_fail(r, "unmatched )");
    rx_emit(r, root);
    rx_inst(r, RX_MATCH, 0, 0);
    for (int i = 0; i < r->npool; i++)
        free(r->pool[i]);
    free(r->pool);
    r->pool = NULL;
    r->npool = r->cappool = 0;
    if (r->err[0])
        return 0;
    r->cl = (int *)malloc((size_t)r->n * sizeof(int));
    r->nl = (int *)malloc((size_t)r->n * sizeof(int));
    r->mark = (int *)calloc((size_t)r->n, sizeof(int));
    return 1;
}
static void rx_add(Rx *r, int *list, int *cnt, int pc, const char *s, size_t n, size_t pos)
{
    for (;;)
    {
        RxInst *in = &r->code[pc];
        if (r->mark[pc] == r->gen)
            return;
        r->mark[pc] = r->gen;
        switch (in->op)
        {
        case RX_JMP: pc = in->x; continue;
        case RX_SPLIT:
            rx_add(r, list, cnt, in->x, s, n, pos);
            pc = in->y;
            continue;
        case RX_BOL: if (pos != 0) return; pc++; continue;
        case RX_EOL: if (pos != n) return; pc++; continue;
        case RX_WORDB:
        case RX_NWORDB:
        {
            int a = pos > 0 && rx_isword((unsigned char)s[pos - 1]), b = pos < n && rx_isword((unsigned char)s[pos]);
            if ((a != b) != (in->op == RX_WORDB))
                return;
            pc++;
            continue;
        }
        case RX_MATCH: r->hit = 1; return;
        default: list[(*cnt)++] = pc; return;
        }
    }
}
/* 1 if the pattern matches anywhere inside s[0..n) */
static int rx_search(Rx *r, const char *s, size_t n)
{
    int *cl = r->cl, *nl = r->nl, ncl = 0, nnl;
    r->hit = 0;
    r->gen++;
    rx_add(r, cl, &ncl, 0, s, n, 0);
    for (size_t pos = 0; pos < n && !r->hit; pos++)
    {
        int c = (unsigned char)s[pos], *t;
        nnl = 0;
        r->gen++;
        for (int i = 0; i < ncl; i++)
        {
            RxInst *in = &r->code[cl[i]];
            int ok = in->op == RX_CHAR ? in->x == c : in->op == RX_ANY ? c != '\n' : in->op == RX_CLASS ? rx_set_has(r->cls[in->x], c) : 0;
            if (ok)
                rx_add(r, nl, &nnl, cl[i] + 1, s, n, pos + 1);
        }
        rx_add(r, nl, &nnl, 0, s, n, pos + 1);
        t = cl; cl = nl; nl = t;
        ncl = nnl;
    }
    return r->hit;
}

/* ---------- path glob: * ? ** ; case-insensitive; '/' separates (callers normalise '\\' to '/') ---------- */
static int glob_at(const char *p, const char *s, const char *base)
{
    while (*p)
    {
        if (p[0] == '*' && p[1] == '*')
        {
            p += 2;
            if (*p == '/')
                p++;
            for (;; s++)
            {
                if ((s == base || s[-1] == '/') && glob_at(p, s, base))
                    return 1;
                if (!*s)
                    return 0;
            }
        }
        else if (*p == '*')
        {
            p++;
            for (;; s++)
            {
                if (glob_at(p, s, base))
                    return 1;
                if (!*s || *s == '/')
                    return 0;
            }
        }
        else if (*p == '?')
        {
            if (!*s || *s == '/')
                return 0;
            p++;
            s++;
        }
        else
        {
            if (tolower((unsigned char)*p) != tolower((unsigned char)*s))
                return 0;
            p++;
            s++;
        }
    }
    return !*s;
}
/* no '/' in the pattern: match the file name at any depth (like .gitignore); otherwise match the relative path */
static int glob_match(const char *pat, const char *rel)
{
    if (!strchr(pat, '/'))
    {
        const char *b = strrchr(rel, '/');
        return glob_at(pat, b ? b + 1 : rel, b ? b + 1 : rel);
    }
    return glob_at(pat, rel, rel);
}

#endif /* LLAMAC_REGEX_H */
