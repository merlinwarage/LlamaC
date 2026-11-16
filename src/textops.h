/* textops.h - portable text helpers for the file tools and the chat view (needs util.h, regex.h) */
#ifndef LLAMAC_TEXTOPS_H
#define LLAMAC_TEXTOPS_H

static size_t tx_cut(const char *s, size_t len, size_t cap)
{
    if (len <= cap)
        return len;
    while (cap && ((unsigned char)s[cap] & 0xC0) == 0x80)
        cap--;
    return cap;
}
/* "     7<TAB>text" per line; numbering starts at 'first' */
static void tx_numbered(SB *o, const char *s, size_t n, int first)
{
    size_t i = 0;
    int no = first;
    while (i < n)
    {
        size_t j = i;
        while (j < n && s[j] != '\n')
            j++;
        sb_printf(o, "%6d\t", no++);
        sb_add(o, s + i, j - i);
        if (j < n)
            sb_putc(o, '\n');
        i = j + 1;
    }
}
static int tx_line_of(const char *s, size_t off)
{
    int no = 1;
    for (size_t i = 0; i < off && s[i]; i++)
        if (s[i] == '\n')
            no++;
    return no;
}
/* numbered lines from..to (1-based, inclusive); long lines are shortened */
static void tx_snippet(SB *o, const char *text, int from, int to)
{
    const char *p = text;
    int no = 1;
    if (from < 1)
        from = 1;
    while (*p && no <= to)
    {
        const char *e = strchr(p, '\n');
        size_t L = e ? (size_t)(e - p) : strlen(p);
        if (no >= from)
        {
            size_t l2 = L, c;
            if (l2 && p[l2 - 1] == '\r')
                l2--;
            c = tx_cut(p, l2, 200);
            sb_printf(o, "%6d\t", no);
            sb_add(o, p, c);
            if (c < l2)
                sb_puts(o, " ...");
            sb_putc(o, '\n');
        }
        no++;
        if (!e)
            break;
        p = e + 1;
    }
}
/* old_text not found: line where its first non-blank line does occur (0 = nowhere) */
static int tx_first_line_hint(const char *hay, const char *needle)
{
    const char *p = needle;
    while (*p)
    {
        const char *e = strchr(p, '\n');
        size_t L = e ? (size_t)(e - p) : strlen(p);
        const char *a = p, *b = p + L;
        while (a < b && (*a == ' ' || *a == '\t' || *a == '\r'))
            a++;
        while (b > a && (b[-1] == ' ' || b[-1] == '\t' || b[-1] == '\r'))
            b--;
        if (b - a >= 3)
        {
            char *t = xstrndup(a, (size_t)(b - a));
            const char *hit = strstr(hay, t);
            free(t);
            return hit ? tx_line_of(hay, (size_t)(hit - hay)) : 0;
        }
        if (!e)
            break;
        p = e + 1;
    }
    return 0;
}
/* grep one buffer. "rel:LINE: text" for matches, "rel-LINE- text" for context, "--" between groups. Returns matching lines. */
static int tx_grep_buf(SB *out, const char *rel, const char *buf, size_t len, Rx *rx, int ctx, int max_matches)
{
    int nl = 1, matches = 0, last = 0, k = 0;
    size_t *st;
    char *hit;
    if (ctx < 0)
        ctx = 0;
    if (ctx > 5)
        ctx = 5;
    for (size_t i = 0; i < len; i++)
        if (buf[i] == '\n')
            nl++;
    st = (size_t *)malloc(((size_t)nl + 1) * sizeof(size_t));
    hit = (char *)calloc((size_t)nl + 1, 1);
    st[0] = 0;
    for (size_t i = 0; i < len; i++)
        if (buf[i] == '\n')
            st[++k] = i + 1;
    st[nl] = len + 1;
    for (int i = 0; i < nl && matches < max_matches; i++)
    {
        size_t L = st[i + 1] - 1 - st[i];
        if (L && buf[st[i] + L - 1] == '\r')
            L--;
        if (rx_search(rx, buf + st[i], L))
        {
            hit[i] = 1;
            matches++;
        }
    }
    for (int i = 0; i < nl; i++)
    {
        int lo = i - ctx < 0 ? 0 : i - ctx, hi = i + ctx >= nl ? nl - 1 : i + ctx, show = 0;
        for (int q = lo; q <= hi && !show; q++)
            if (hit[q])
                show = 1;
        if (!show)
            continue;
        if (last && i > last)
            sb_puts(out, "--\n");
        {
            size_t L = st[i + 1] - 1 - st[i], c;
            if (L && buf[st[i] + L - 1] == '\r')
                L--;
            c = tx_cut(buf + st[i], L, 300);
            sb_puts(out, rel);
            sb_printf(out, hit[i] ? ":%d: " : "-%d- ", i + 1);
            sb_add(out, buf + st[i], c);
            if (c < L)
                sb_puts(out, " ...");
            sb_putc(out, '\n');
        }
        last = i + 1;
    }
    free(st);
    free(hit);
    return matches;
}

/* ---------- streaming markdown ----------
   md_render() works line by line, except for ``` fences (a block up to the closing fence) and tables (consecutive lines
   starting with '|'). So text can be rendered as final markdown as soon as these units are complete.
   Returns the end offset of the last complete unit inside s[from..n), or 'from' when there is none.
   Only lines that already ended with '\n' are considered; s[from] must not be inside a fence. */
static size_t tx_block_end(const char *s, size_t from, size_t n)
{
    size_t i = from, best = from, tstart = (size_t)-1;
    int fence = 0;
    while (i < n)
    {
        const char *nlp = (const char *)memchr(s + i, '\n', n - i);
        size_t e, a;
        int fence_line, table_line;
        if (!nlp)
            break;
        e = (size_t)(nlp - s);
        a = i;
        while (a < e && (s[a] == ' ' || s[a] == '\t'))
            a++;
        fence_line = (e - a >= 3 && !strncmp(s + a, "```", 3));
        if (fence)
        {
            if (fence_line)
            {
                fence = 0;
                best = e + 1;
            }
        }
        else
        {
            table_line = (a < e && s[a] == '|');
            if (tstart != (size_t)-1 && !table_line)
            {
                best = i; /* the table above is complete */
                tstart = (size_t)-1;
            }
            if (fence_line)
                fence = 1;
            else if (table_line)
            {
                if (tstart == (size_t)-1)
                    tstart = i;
            }
            else
                best = e + 1;
        }
        i = e + 1;
    }
    return best;
}

/* ---------- line tables ---------- */
typedef struct
{
    const char *s;
    size_t n;     /* length without the line break */
} TxLine;
/* splits s into lines (a trailing '\r' is not part of a line); caller frees the array */
static TxLine *tx_lines(const char *s, int *count)
{
    int n = 1, k = 0;
    TxLine *L;
    for (const char *p = s; *p; p++)
        if (*p == '\n')
            n++;
    L = (TxLine *)malloc((size_t)n * sizeof(TxLine));
    for (const char *p = s;; )
    {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        L[k].s = p;
        L[k].n = (len && p[len - 1] == '\r') ? len - 1 : len;
        k++;
        if (!e)
            break;
        p = e + 1;
    }
    *count = k;
    return L;
}
static int tx_blank(const TxLine *l)
{
    for (size_t i = 0; i < l->n; i++)
        if (l->s[i] != ' ' && l->s[i] != '\t')
            return 0;
    return 1;
}
/* equal when compared without leading/trailing whitespace and with inner whitespace runs as one space */
static int tx_ws_eq(const TxLine *a, const TxLine *b)
{
    size_t i = 0, j = 0;
    while (i < a->n && (a->s[i] == ' ' || a->s[i] == '\t'))
        i++;
    while (j < b->n && (b->s[j] == ' ' || b->s[j] == '\t'))
        j++;
    for (;;)
    {
        int ea, eb;
        size_t i2 = i, j2 = j;
        while (i2 < a->n && (a->s[i2] == ' ' || a->s[i2] == '\t'))
            i2++;
        while (j2 < b->n && (b->s[j2] == ' ' || b->s[j2] == '\t'))
            j2++;
        ea = i2 >= a->n;
        eb = j2 >= b->n;
        if (ea || eb)
            return ea && eb;
        if ((i2 > i) != (j2 > j)) /* whitespace on one side only */
            return 0;
        i = i2;
        j = j2;
        if (a->s[i] != b->s[j])
            return 0;
        i++;
        j++;
    }
}
static size_t tx_indent(const TxLine *l)
{
    size_t i = 0;
    while (i < l->n && (l->s[i] == ' ' || l->s[i] == '\t'))
        i++;
    return i;
}

/* ---------- whitespace-tolerant edit ----------
   Used when old_text does not occur exactly. Lines are compared ignoring indentation, trailing spaces and the
   width of inner whitespace; blank lines at the start and end of old_text are ignored. new_text is re-indented by
   the difference between old_text's and the file's indentation of the first matched line. Returns the new file
   text, or NULL: *count = number of matches (0 = none, >1 and !all = ambiguous). *line = first matched line. */
static char *tx_ws_replace(const char *hay, const char *oldt, const char *newt, int all, int *count, int *line)
{
    int nh, no, nn, a = 0, b, na = 0, nb, k, hits = 0;
    TxLine *H = tx_lines(hay, &nh), *O = tx_lines(oldt, &no), *N;
    int *at = NULL;
    char *out = NULL;
    int crlf = strstr(hay, "\r\n") != NULL;
    *count = 0;
    *line = 0;
    b = no;
    while (a < b && tx_blank(&O[a]))
        a++;
    while (b > a && tx_blank(&O[b - 1]))
        b--;
    k = b - a;
    if (k <= 0)
    {
        free(H);
        free(O);
        return NULL;
    }
    at = (int *)malloc((size_t)(nh + 1) * sizeof(int));
    for (int i = 0; i + k <= nh;)
    {
        int ok = 1;
        for (int j = 0; j < k && ok; j++)
            ok = tx_ws_eq(&H[i + j], &O[a + j]);
        if (ok)
        {
            at[hits++] = i;
            i += k; /* no overlapping matches */
        }
        else
            i++;
    }
    *count = hits;
    if (!hits || (hits > 1 && !all))
    {
        free(H);
        free(O);
        free(at);
        return NULL;
    }
    *line = at[0] + 1;
    N = tx_lines(newt, &nn);
    nb = nn;
    /* drop as many blank edge lines from new_text as old_text had */
    while (na < a && na < nb && tx_blank(&N[na]))
        na++;
    for (int t = no - b; t > 0 && nb > na && tx_blank(&N[nb - 1]); t--)
        nb--;
    {
        SB o = {0};
        size_t pos = 0;
        const char *eol = crlf ? "\r\n" : "\n";
        size_t oi = tx_indent(&O[a]);
        for (int m = 0; m < hits; m++)
        {
            const TxLine *f = &H[at[m]], *l = &H[at[m] + k - 1];
            size_t fi = tx_indent(f), start = (size_t)(f->s - hay), end = (size_t)(l->s - hay) + l->n;
            sb_add(&o, hay + pos, start - pos);
            for (int j = na; j < nb; j++)
            {
                const TxLine *x = &N[j];
                size_t xi = tx_indent(x);
                if (j > na)
                    sb_puts(&o, eol);
                if (!tx_blank(x) && xi >= oi)
                { /* replace old_text's base indentation with the file's */
                    sb_add(&o, f->s, fi);
                    sb_add(&o, x->s + oi, x->n - oi);
                }
                else
                    sb_add(&o, x->s, x->n);
            }
            pos = end;
        }
        sb_puts(&o, hay + pos);
        out = o.p ? o.p : xstrdup("");
    }
    free(H);
    free(O);
    free(N);
    free(at);
    return out;
}

/* ---------- log folding for command output ----------
   Removes ANSI escape sequences and carriage-return progress redraws, folds runs of identical lines and drops
   repeats of long lines seen before (the same compiler warning printed for every file). Caller frees. */
static unsigned long long tx_hash(const char *s, size_t n)
{
    unsigned long long h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++)
        h = (h ^ (unsigned char)s[i]) * 1099511628211ULL;
    return h ? h : 1;
}
static char *tx_fold(const char *s)
{
    SB clean = {0}, o = {0};
    unsigned long long *set;
    size_t cap = 4096, used = 0;
    long long dropped = 0;
    /* 1. ANSI escapes and \r redraws */
    for (const char *p = s; *p; p++)
    {
        if (*p == 0x1B && p[1] == '[')
        {
            p += 2;
            while (*p && !((*p >= '@' && *p <= '~')))
                p++;
            if (!*p)
                break;
            continue;
        }
        if (*p == '\r')
        {
            if (p[1] == '\n')
                continue;
            /* a bare \r rewrites the line: drop what was written since the last \n */
            while (clean.n && clean.p[clean.n - 1] != '\n')
                clean.n--;
            if (clean.p)
                clean.p[clean.n] = 0;
            continue;
        }
        sb_putc(&clean, *p);
    }
    set = (unsigned long long *)calloc(cap, sizeof *set);
    {
        const char *p = sbs(&clean), *prev = NULL;
        size_t prevn = 0;
        int rep = 0;
        for (;;)
        {
            const char *e = strchr(p, '\n');
            size_t n = e ? (size_t)(e - p) : strlen(p);
            int same = prev && n == prevn && !memcmp(p, prev, n);
            if (same)
                rep++;
            else
            {
                if (rep)
                    sb_printf(&o, "[previous line repeated %d more time%s]\n", rep, rep > 1 ? "s" : "");
                rep = 0;
                if (n >= 30)
                {
                    unsigned long long h = tx_hash(p, n);
                    size_t i = (size_t)(h & (cap - 1));
                    int seen = 0;
                    while (set[i])
                    {
                        if (set[i] == h)
                        {
                            seen = 1;
                            break;
                        }
                        i = (i + 1) & (cap - 1);
                    }
                    if (seen)
                    {
                        dropped++;
                        prev = NULL; /* nothing was printed for it */
                        prevn = 0;
                        if (!e)
                            break;
                        p = e + 1;
                        continue;
                    }
                    if (used * 2 < cap)
                    {
                        set[i] = h;
                        used++;
                    }
                }
                sb_add(&o, p, n);
                if (e)
                    sb_putc(&o, '\n');
            }
            prev = p;
            prevn = n;
            if (!e)
                break;
            p = e + 1;
        }
        if (rep)
            sb_printf(&o, "[previous line repeated %d more time%s]\n", rep, rep > 1 ? "s" : "");
    }
    if (dropped)
        sb_printf(&o, "[%lld repeated line%s omitted]\n", dropped, dropped > 1 ? "s" : "");
    free(set);
    free(clean.p);
    return o.p ? o.p : xstrdup("");
}

/* ---------- line diff (for the approval dialog) ----------
   Common prefix/suffix are trimmed; the middle is diffed with an LCS table when it is small enough, otherwise
   shown as removed + added. Output is a unified-style diff with 2 lines of context, at most 'cap' bytes. */
static void tx_diff_line(SB *o, char tag, const TxLine *l)
{
    sb_putc(o, tag);
    sb_putc(o, ' ');
    sb_add(o, l->s, tx_cut(l->s, l->n, 160));
    if (l->n > 160)
        sb_puts(o, " ...");
    sb_putc(o, '\n');
}
static char *tx_diff(const char *a, const char *b, size_t cap)
{
    int na, nb, p = 0, sa, sb2;
    TxLine *A = tx_lines(a, &na), *B = tx_lines(b, &nb);
    SB o = {0};
    char *ops = NULL; /* ' ' keep, '-' del, '+' add */
    int nops = 0;
    int *ai = NULL, *bi = NULL;
    while (p < na && p < nb && A[p].n == B[p].n && !memcmp(A[p].s, B[p].s, A[p].n))
        p++;
    sa = na;
    sb2 = nb;
    while (sa > p && sb2 > p && A[sa - 1].n == B[sb2 - 1].n && !memcmp(A[sa - 1].s, B[sb2 - 1].s, A[sa - 1].n))
        sa--, sb2--;
    {
        int m = sa - p, n = sb2 - p, total = p + (na - sa) + m + n + 2;
        ops = (char *)malloc((size_t)total);
        ai = (int *)malloc((size_t)total * sizeof(int));
        bi = (int *)malloc((size_t)total * sizeof(int));
        for (int i = 0; i < p; i++)
            ops[nops] = ' ', ai[nops] = i, bi[nops++] = i;
        if ((long long)m * n <= 4000000LL && m > 0 && n > 0)
        {
            unsigned short *T = (unsigned short *)calloc((size_t)(m + 1) * (size_t)(n + 1), sizeof(unsigned short));
#define TT(i, j) T[(size_t)(i) * (size_t)(n + 1) + (size_t)(j)]
            for (int i = m - 1; i >= 0; i--)
                for (int j = n - 1; j >= 0; j--)
                {
                    const TxLine *x = &A[p + i], *y = &B[p + j];
                    if (x->n == y->n && !memcmp(x->s, y->s, x->n))
                        TT(i, j) = (unsigned short)(TT(i + 1, j + 1) + 1);
                    else
                        TT(i, j) = TT(i + 1, j) > TT(i, j + 1) ? TT(i + 1, j) : TT(i, j + 1);
                }
            {
                int i = 0, j = 0;
                while (i < m || j < n)
                {
                    if (i < m && j < n && A[p + i].n == B[p + j].n && !memcmp(A[p + i].s, B[p + j].s, A[p + i].n))
                        ops[nops] = ' ', ai[nops] = p + i, bi[nops++] = p + j, i++, j++;
                    else if (i < m && (j >= n || TT(i + 1, j) >= TT(i, j + 1))) /* deletions first */
                        ops[nops] = '-', ai[nops] = p + i, bi[nops++] = -1, i++;
                    else
                        ops[nops] = '+', ai[nops] = -1, bi[nops++] = p + j, j++;
                }
            }
#undef TT
            free(T);
        }
        else
        {
            for (int i = 0; i < m; i++)
                ops[nops] = '-', ai[nops] = p + i, bi[nops++] = -1;
            for (int j = 0; j < n; j++)
                ops[nops] = '+', ai[nops] = -1, bi[nops++] = p + j;
        }
        for (int i = sa; i < na; i++)
            ops[nops] = ' ', ai[nops] = i, bi[nops++] = sb2 + (i - sa);
    }
    /* hunks with 2 lines of context */
    {
        int i = 0, adds = 0, dels = 0;
        for (int q = 0; q < nops; q++)
            adds += ops[q] == '+', dels += ops[q] == '-';
        sb_printf(&o, "%d line(s) removed, %d added\n", dels, adds);
        while (i < nops)
        {
            int s, e;
            while (i < nops && ops[i] == ' ')
                i++;
            if (i >= nops)
                break;
            s = i - 2 < 0 ? 0 : i - 2;
            e = i;
            for (;;)
            { /* extend while the next change is within 4 kept lines */
                int q = e;
                while (q < nops && ops[q] != ' ')
                    q++;
                e = q;
                {
                    int r = q;
                    while (r < nops && ops[r] == ' ' && r - q < 5)
                        r++;
                    if (r < nops && ops[r] != ' ' && r - q < 5)
                    {
                        e = r;
                        continue;
                    }
                }
                break;
            }
            e = e + 2 > nops ? nops : e + 2;
            {
                int la = -1, lb = -1;
                for (int q = s; q < e; q++)
                {
                    if (la < 0 && ai[q] >= 0)
                        la = ai[q];
                    if (lb < 0 && bi[q] >= 0)
                        lb = bi[q];
                }
                sb_printf(&o, "@@ line %d @@\n", (lb >= 0 ? lb : la >= 0 ? la : 0) + 1);
            }
            for (int q = s; q < e && o.n < cap; q++)
                tx_diff_line(&o, ops[q], ops[q] == '+' ? &B[bi[q]] : &A[ai[q]]);
            if (o.n >= cap)
            {
                sb_puts(&o, "... (diff shortened)\n");
                break;
            }
            i = e;
        }
    }
    free(ops);
    free(ai);
    free(bi);
    free(A);
    free(B);
    return o.p ? o.p : xstrdup("(no changes)\n");
}

/* ---------- BM25 keyword relevance (memory digest and memory_search) ---------- */
#define BM25_MAXT 64
typedef struct
{
    unsigned long long h[BM25_MAXT];
    int n;
} TxTerms;
/* lowercased word hashes; ASCII letters/digits and any UTF-8 byte form words; very short and stop words skipped */
static void tx_terms(const char *s, TxTerms *t)
{
    static const char *stop[] = {"the", "and", "for", "are", "was", "with", "that", "this", "you", "your", "have", "has", "not", "but", "from", "its", "into", "what", "how", "can", "will", "about", "a", "az", "egy", "es", "is", "hogy", "nem", NULL};
    t->n = 0;
    while (*s && t->n < BM25_MAXT)
    {
        char w[48];
        size_t k = 0;
        while (*s && !(isalnum((unsigned char)*s) || ((unsigned char)*s & 0x80)))
            s++;
        while (*s && (isalnum((unsigned char)*s) || ((unsigned char)*s & 0x80)))
        {
            if (k < sizeof w - 1)
                w[k++] = (char)tolower((unsigned char)*s);
            s++;
        }
        w[k] = 0;
        if (k < 2)
            continue;
        {
            int sw = 0;
            for (int i = 0; stop[i]; i++)
                if (!strcmp(stop[i], w))
                    sw = 1;
            if (sw)
                continue;
        }
        t->h[t->n++] = tx_hash(w, k);
    }
}
/* scores[i] = BM25 of document i for the query (0 = no shared term). k1 1.2, b 0.75. */
static void tx_bm25(const char *query, const char *const *docs, int nd, double *scores)
{
    TxTerms q, *D = (TxTerms *)malloc((size_t)(nd ? nd : 1) * sizeof(TxTerms));
    double avg = 0;
    tx_terms(query, &q);
    for (int i = 0; i < nd; i++)
    {
        tx_terms(docs[i], &D[i]);
        avg += D[i].n;
        scores[i] = 0;
    }
    avg = nd ? avg / nd : 1;
    if (avg < 1)
        avg = 1;
    for (int k = 0; k < q.n; k++)
    {
        int df = 0, dup = 0;
        for (int j = 0; j < k; j++)
            if (q.h[j] == q.h[k])
                dup = 1;
        if (dup)
            continue;
        for (int i = 0; i < nd; i++)
            for (int j = 0; j < D[i].n; j++)
                if (D[i].h[j] == q.h[k])
                {
                    df++;
                    break;
                }
        if (!df)
            continue;
        {
            double idf = log(1.0 + (nd - df + 0.5) / (df + 0.5));
            for (int i = 0; i < nd; i++)
            {
                int tf = 0;
                for (int j = 0; j < D[i].n; j++)
                    tf += D[i].h[j] == q.h[k];
                if (tf)
                    scores[i] += idf * tf * 2.2 / (tf + 1.2 * (0.25 + 0.75 * D[i].n / avg));
            }
        }
    }
    free(D);
}

/* ---------- find_symbol: lines that define a name (functions, types, classes, macros, constants) ----------
   Language-agnostic patterns for C/C++, C#, Java, JS/TS, Python, Go, Rust, PHP, Kotlin, Swift. */
#define SYM_NRX 6
static int tx_sym_name_ok(const char *n)
{
    size_t k = 0;
    if (!n || !(isalpha((unsigned char)*n) || *n == '_' || *n == '$'))
        return 0;
    for (; n[k]; k++)
        if (!(isalnum((unsigned char)n[k]) || n[k] == '_' || n[k] == '$') || k > 100)
            return 0;
    return 1;
}
/* compiles the definition patterns for this name into rx[SYM_NRX]; 0 on failure */
static int tx_sym_compile(Rx *rx, const char *name)
{
    char nm[220], pat[1200];
    size_t k = 0;
    static const char *const P[SYM_NRX] = {
        /* keyword definitions: def/class/struct/function/fn/func/type/const/let/var..., with modifiers */
        "^\\s*(?:(?:export|default|public|private|protected|internal|static|abstract|final|async|pub(?:\\([a-z]+\\))?|unsafe|inline|extern|virtual|override|sealed|partial|open|data|declare|typedef)\\s+)*"
        "(?:def|class|struct|union|enum|interface|trait|type|typedef|function\\*?|func|fn|module|namespace|record|object|impl|macro_rules!|let|const|var|val|module_function)\\s+\\*?\\s*%s\\b",
        /* C preprocessor */
        "^\\s*#\\s*define\\s+%s\\b",
        /* C-like function or method: a type, then the name and "(" (calls have no type before them) */
        "^\\s*[A-Za-z_][\\w:<>,\\*&\\[\\]\\s]*[\\s\\*&]%s\\s*\\(",
        /* name at the start of the line: GNU style "type\\nname(", JS/TS class methods "name(...) {" */
        "^\\s*(?:(?:async|static|get|set|public|private|protected|override)\\s+)*%s\\s*\\([^;]*\\)\\s*(?::\\s*[^{;]+)?\\{?\\s*$",
        /* the end of "typedef struct { ... } name;" */
        "^\\s*\\}\\s*%s\\s*[;,]",
        /* Go methods, JS/TS assignments of functions */
        "^\\s*(?:func\\s*\\([^)]*\\)\\s*%s\\s*\\(|(?:this\\.|exports\\.|module\\.exports\\.)?%s\\s*[:=]\\s*(?:async\\s+)?(?:function\\b|\\([^)]*\\)\\s*=>|[A-Za-z_$][\\w$]*\\s*=>))",
    };
    for (const char *p = name; *p && k < sizeof nm - 2; p++)
    {
        if (*p == '$')
            nm[k++] = '\\';
        nm[k++] = *p;
    }
    nm[k] = 0;
    for (int i = 0; i < SYM_NRX; i++)
    {
        snprintf(pat, sizeof pat, P[i], nm, nm);
        memset(&rx[i], 0, sizeof rx[i]);
        if (!rx_compile(&rx[i], pat, 0))
        {
            for (int j = 0; j <= i; j++)
                rx_free(&rx[j]);
            return 0;
        }
    }
    return 1;
}
/* 0 = no definition, 1 = definition, 2 = declaration only (a C prototype ending in ';') */
static int tx_sym_line(Rx *rx, const char *s, size_t n)
{
    static const char *const ctl[] = {"if", "for", "while", "switch", "return", "else", "case", "do", "sizeof", "catch", "new",
                                      "throw", "await", "yield", "print", "assert", "goto", "delete", "echo", NULL};
    const char *a = s;
    size_t e = n;
    while (a < s + n && (*a == ' ' || *a == '\t'))
        a++;
    for (int i = 0; ctl[i]; i++)
    {
        size_t L = strlen(ctl[i]);
        if ((size_t)(s + n - a) >= L && !strncmp(a, ctl[i], L) && (a + L == s + n || !(isalnum((unsigned char)a[L]) || a[L] == '_')))
            return 0;
    }
    for (int i = 0; i < SYM_NRX; i++)
        if (rx_search(&rx[i], s, n))
        {
            while (e && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r'))
                e--;
            return (i == 2 && e && s[e - 1] == ';') ? 2 : 1;
        }
    return 0;
}

#endif /* LLAMAC_TEXTOPS_H */
