/* markdown.h - Markdown-ish inline/table/block renderer */
#ifndef LLAMAC_MARKDOWN_H
#define LLAMAC_MARKDOWN_H

/* ---------- markdown-ish text formatting ---------- */
static void md_inline(const char *s, size_t n, COLORREF col, DWORD base);
static int md_in_link; /* no autolinks inside [text](url) - link ranges must not nest */
/* length of a bare http(s):// URL starting at s, 0 if none */
static size_t md_autolink(const char *s, size_t n)
{
    size_t p = (n > 8 && !_strnicmp(s, "https://", 8)) ? 8 : (n > 7 && !_strnicmp(s, "http://", 7)) ? 7 : 0;
    if (!p)
        return 0;
    size_t j = p;
    int par = 0;
    while (j < n && !isspace((unsigned char)s[j]) && !strchr("<>\"`", s[j]))
    {
        if (s[j] == '(')
            par++;
        else if (s[j] == ')')
            par--;
        j++;
    }
    /* trailing punctuation belongs to the sentence, an unbalanced ')' to the surrounding text */
    while (j > p && (strchr(".,;:!?'*_", s[j - 1]) || (s[j - 1] == ')' && par < 0)))
    {
        if (s[j - 1] == ')')
            par++;
        j--;
    }
    return j > p ? j : 0;
}
/* Capture mode: md_inline() records styled runs here instead of writing to the RichEdit, so a caller
   (md_table) can measure, wrap and pad formatted text itself. Runs point into the source text. */
typedef struct
{
    const char *s;
    size_t n;
    COLORREF col;
    DWORD eff;
    int code;
    const char *url; /* link target, NULL if none */
    size_t un;
} MdRun;
static MdRun *md_cap;
static int md_ncap, md_acap, md_capturing;
static void md_out(const char *s, size_t n, COLORREF col, DWORD eff, int code)
{
    if (!n)
        return;
    if (!md_capturing)
    {
        if (code)
            re_code(s, n, col, eff);
        else
            re_add(s, n, col, eff, FACE);
        return;
    }
    if (md_ncap == md_acap)
    {
        md_acap = md_acap ? md_acap * 2 : 64;
        md_cap = (MdRun *)realloc(md_cap, (size_t)md_acap * sizeof *md_cap);
    }
    MdRun r = {s, n, col, eff, code, NULL, 0};
    md_cap[md_ncap++] = r;
}
/* start of a link range: a RichEdit position, or a run index while capturing */
static LONG md_mark(void) { return md_capturing ? (LONG)md_ncap : re_pos(); }
static void md_link(LONG a, const char *u, size_t n)
{
    if (!md_capturing)
    {
        url_link(a, re_pos(), u, n);
        return;
    }
    for (int k = (int)a; k < md_ncap; k++)
    {
        md_cap[k].url = u;
        md_cap[k].un = n;
    }
}
static void md_span(const char *s, size_t n, COLORREF col, DWORD eff) { md_inline(s, n, col, eff); }
static void md_inline(const char *s, size_t n, COLORREF col, DWORD base)
{
    size_t i = 0, st = 0;
#define FLUSH() md_out(s + st, i - st, col, base, 0)
    while (i < n)
    {
        char c = s[i];
        size_t ul;
        if (!md_in_link && (c == 'h' || c == 'H') && (i == 0 || !isalnum((unsigned char)s[i - 1])) && (ul = md_autolink(s + i, n - i)) > 0)
        {
            FLUSH();
            LONG a = md_mark();
            md_out(s + i, ul, cAccent, base | CFE_UNDERLINE, 0);
            md_link(a, s + i, ul);
            i += ul;
            st = i;
            continue;
        }
        if (c == '`')
        {
            size_t j = i + 1;
            while (j < n && s[j] != '`')
                j++;
            if (j < n && j > i + 1)
            {
                FLUSH();
                md_out(s + i + 1, j - i - 1, cCode, 0, 1);
                i = j + 1;
                st = i;
                continue;
            }
        }
        else if ((c == '*' || c == '_') && i + 1 < n && s[i + 1] == c)
        {
            size_t j = i + 2;
            while (j + 1 < n && !(s[j] == c && s[j + 1] == c))
                j++;
            if (j + 1 < n && j > i + 2)
            {
                FLUSH();
                md_span(s + i + 2, j - i - 2, col, base | CFE_BOLD);
                i = j + 2;
                st = i;
                continue;
            }
        }
        else if (c == '~' && i + 1 < n && s[i + 1] == '~')
        {
            size_t j = i + 2;
            while (j + 1 < n && !(s[j] == '~' && s[j + 1] == '~'))
                j++;
            if (j + 1 < n && j > i + 2)
            {
                FLUSH();
                md_span(s + i + 2, j - i - 2, cDim, base | CFE_STRIKEOUT);
                i = j + 2;
                st = i;
                continue;
            }
        }
        else if ((c == '*' || c == '_') && i + 1 < n && s[i + 1] != ' ' && (c == '*' || i == 0 || !StrChrA("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", s[i - 1])))
        {
            size_t j = i + 1;
            while (j < n && s[j] != c)
                j++;
            if (j < n && j > i + 1 && s[j - 1] != ' ')
            {
                FLUSH();
                md_span(s + i + 1, j - i - 1, col, base | CFE_ITALIC);
                i = j + 1;
                st = i;
                continue;
            }
        }
        else if (c == '[')
        {
            size_t j = i + 1;
            while (j < n && s[j] != ']')
                j++;
            if (j + 1 < n && s[j + 1] == '(')
            {
                size_t k = j + 2;
                while (k < n && s[k] != ')')
                    k++;
                if (k < n)
                {
                    FLUSH();
                    LONG a = md_mark();
                    md_in_link++;
                    md_span(s + i + 1, j - i - 1, cAccent, base | CFE_UNDERLINE);
                    md_in_link--;
                    md_out(" (", 2, cDim, 0, 0);
                    md_out(s + j + 2, k - j - 2, cDim, 0, 0);
                    md_out(")", 1, cDim, 0, 0);
                    if (!md_in_link)
                        md_link(a, s + j + 2, k - j - 2);
                    i = k + 1;
                    st = i;
                    continue;
                }
            }
        }
        i++;
    }
    FLUSH();
#undef FLUSH
}
/* ---------- table cell layout ---------- */
typedef struct
{
    int run; /* index into md_cap, -1 = a single space */
    size_t off, len;
    int w, line;
} MdSeg;
typedef struct
{
    MdSeg *p;
    int n, cap;
} MdSegs;
static void seg_push(MdSegs *v, int run, size_t off, size_t len, int w, int line)
{
    MdSeg *t = v->n ? &v->p[v->n - 1] : NULL;
    if (t && run >= 0 && t->run == run && t->line == line && t->off + t->len == off)
    { /* contiguous text of the same run on the same line: one segment */
        t->len += len;
        t->w += w;
        return;
    }
    if (v->n == v->cap)
    {
        v->cap = v->cap ? v->cap * 2 : 32;
        v->p = (MdSeg *)realloc(v->p, (size_t)v->cap * sizeof *v->p);
    }
    MdSeg g = {run, off, len, w, line};
    v->p[v->n++] = g;
}
/* length of a <br>, <br/> or <br /> tag at s, 0 if none */
static size_t md_br(const char *s, size_t n)
{
    if (n < 4 || s[0] != '<' || _strnicmp(s + 1, "br", 2))
        return 0;
    size_t i = 3;
    while (i < n && s[i] == ' ')
        i++;
    if (i < n && s[i] == '/')
        i++;
    return i < n && s[i] == '>' ? i + 1 : 0;
}
typedef struct
{
    MdSegs *out, word;
    int w, ww, line, col, sp, maxw, maxword;
} MdWrap;
/* places the pending word: on this line after a space, or on the next one; a word wider than the
   column is broken between characters */
static void wrap_word(MdWrap *z)
{
    if (!z->word.n)
        return;
    if (z->ww > z->maxword)
        z->maxword = z->ww;
    if (z->col > 0 && z->w > 0 && z->col + z->sp + z->ww > z->w)
    {
        z->line++;
        z->col = 0;
    }
    else if (z->col > 0 && z->sp)
    {
        seg_push(z->out, -1, 0, 1, 1, z->line);
        z->col++;
    }
    for (int k = 0; k < z->word.n; k++)
    {
        MdSeg *g = &z->word.p[k];
        const char *s = md_cap[g->run].s + g->off;
        for (size_t i = 0; i < g->len;)
        {
            size_t l;
            int cw = u8_cw((const unsigned char *)s + i, g->len - i, &l);
            if (z->w > 0 && z->col > 0 && z->col + cw > z->w)
            {
                z->line++;
                z->col = 0;
            }
            seg_push(z->out, g->run, g->off + i, l, cw, z->line);
            z->col += cw;
            if (z->col > z->maxw)
                z->maxw = z->col;
            i += l;
        }
    }
    z->word.n = 0;
    z->ww = 0;
    z->sp = 0;
}
/* lays out the runs md_cap[a..b) into lines at most w columns wide (w <= 0: only <br> breaks lines);
   returns the line count, *maxw = the widest line, *maxword = the widest word */
static int md_wrap(int a, int b, int w, MdSegs *out, int *maxw, int *maxword)
{
    MdWrap z;
    memset(&z, 0, sizeof z);
    z.out = out;
    z.w = w;
    for (int r = a; r < b; r++)
    {
        const MdRun *u = &md_cap[r];
        for (size_t i = 0; i < u->n;)
        {
            size_t bl;
            if (u->s[i] == ' ' || u->s[i] == '\t')
            {
                wrap_word(&z);
                z.sp = 1;
                i++;
                continue;
            }
            if (!u->code && (bl = md_br(u->s + i, u->n - i)) > 0)
            {
                wrap_word(&z);
                z.line++;
                z.col = 0;
                z.sp = 0;
                i += bl;
                continue;
            }
            size_t j = i;
            while (j < u->n && u->s[j] != ' ' && u->s[j] != '\t' && (u->code || !md_br(u->s + j, u->n - j)))
            {
                size_t l;
                z.ww += u8_cw((const unsigned char *)u->s + j, u->n - j, &l);
                j += l;
            }
            seg_push(&z.word, r, i, j - i, 0, 0); /* a word may continue in the next run: "**bold**," */
            i = j;
        }
    }
    wrap_word(&z);
    free(z.word.p);
    *maxw = z.maxw;
    *maxword = z.maxword;
    return z.line + 1;
}
typedef struct
{
    const char *s;
    size_t l;
} Cell;
#define MAXC 12
static void md_table(const char **ln, size_t *ll, int n)
{
    Cell(*cells)[MAXC] = (Cell(*)[MAXC])calloc((size_t)n, sizeof *cells);
    int *nc = (int *)calloc((size_t)n, sizeof(int));
    char *isep = (char *)calloc((size_t)n, 1);
    int ncols = 0, hdr_sep = -1;
    for (int r = 0; r < n; r++)
    {
        const char *p = ln[r], *e = ln[r] + ll[r];
        int allsep = 1, anydash = 0;
        while (p < e && (*p == ' ' || *p == '|'))
        {
            if (*p == '|')
            {
                p++;
                break;
            }
            p++;
        }
        while (p < e && nc[r] < MAXC)
        {
            const char *q = p;
            while (q < e && *q != '|')
                q++;
            const char *a = p, *b = q;
            while (a < b && (*a == ' ' || *a == '\t'))
                a++;
            while (b > a && (b[-1] == ' ' || b[-1] == '\t' || b[-1] == '\r'))
                b--;
            if (q == e && a == b)
                break;
            cells[r][nc[r]].s = a;
            cells[r][nc[r]].l = (size_t)(b - a);
            nc[r]++;
            for (const char *x = a; x < b; x++)
            {
                if (*x == '-')
                    anydash = 1;
                else if (*x != ':')
                    allsep = 0;
            }
            p = q < e ? q + 1 : e;
        }
        isep[r] = (char)(allsep && anydash && nc[r] > 0);
        if (isep[r] && hdr_sep < 0)
            hdr_sep = r;
        if (nc[r] > ncols)
            ncols = nc[r];
    }
    /* inline formatting of every cell, captured as runs; header cells are bold accent */
    int(*rs)[MAXC] = (int(*)[MAXC])calloc((size_t)n, sizeof *rs), (*ce)[MAXC] = (int(*)[MAXC])calloc((size_t)n, sizeof *ce);
    md_ncap = 0;
    md_capturing = 1;
    for (int r = 0; r < n; r++)
        if (!isep[r])
        {
            int hdr = hdr_sep > 0 && r < hdr_sep;
            for (int c = 0; c < nc[r]; c++)
            {
                rs[r][c] = md_ncap;
                md_inline(cells[r][c].s, cells[r][c].l, hdr ? cAccent : cFg, hdr ? CFE_BOLD : 0);
                ce[r][c] = md_ncap;
            }
        }
    md_capturing = 0;
    /* natural column widths, then shrink the widest columns until the table fits the chat */
    int wd[MAXC] = {0}, minw[MAXC] = {0}, mw, mword;
    MdSegs sg[MAXC];
    memset(sg, 0, sizeof sg);
    for (int r = 0; r < n; r++)
        for (int c = 0; c < (isep[r] ? 0 : nc[r]); c++)
        {
            sg[0].n = 0;
            md_wrap(rs[r][c], ce[r][c], 0, &sg[0], &mw, &mword);
            if (mw > wd[c])
                wd[c] = mw;
            if (mword > minw[c])
                minw[c] = mword;
        }
    int avail = code_cols() + 2, tot = 1 + 3 * ncols;
    for (int c = 0; c < ncols; c++)
        tot += wd[c];
    for (int pass = 0; pass < 2 && tot > avail; pass++)
        while (tot > avail)
        { /* pass 0 keeps whole words; pass 1 breaks them */
            int best = -1;
            for (int c = 0; c < ncols; c++)
                if (wd[c] > (pass ? 3 : minw[c]) && (best < 0 || wd[c] > wd[best]))
                    best = c;
            if (best < 0)
                break;
            wd[best]--;
            tot--;
        }
    SB pd = {0}; /* borders and padding, written in one run */
#define PFLUSH()                                 \
    do                                           \
    {                                            \
        if (pd.n)                                \
            re_add(pd.p, pd.n, cDim, 0, MONO);   \
        pd.n = 0;                                \
    } while (0)
    for (int r = 0; r < n; r++)
    {
        re_bg = cCodeBg;
        if (isep[r])
        {
            sb_putc(&pd, ' ');
            for (int c = 0; c < ncols; c++)
            {
                for (int k = 0; k < wd[c] + 2; k++)
                    sb_puts(&pd, HBAR);
                if (c + 1 < ncols)
                    sb_puts(&pd, CROSS);
            }
            sb_putc(&pd, ' ');
            PFLUSH();
            re_bg = CLR_INVALID;
            re_nl();
            continue;
        }
        int H = 1;
        for (int c = 0; c < nc[r]; c++)
        {
            sg[c].n = 0;
            int lines = md_wrap(rs[r][c], ce[r][c], wd[c], &sg[c], &mw, &mword);
            if (lines > H)
                H = lines;
        }
        for (int L = 0; L < H; L++)
        {
            sb_putc(&pd, ' ');
            for (int c = 0; c < ncols; c++)
            {
                int used = 0;
                sb_putc(&pd, ' ');
                for (int k = 0; c < nc[r] && k < sg[c].n; k++)
                {
                    MdSeg *g = &sg[c].p[k];
                    if (g->line != L)
                        continue;
                    used += g->w;
                    if (g->run < 0)
                    {
                        sb_putc(&pd, ' ');
                        continue;
                    }
                    PFLUSH();
                    MdRun *u = &md_cap[g->run];
                    LONG a = u->url ? re_pos() : 0;
                    re_add(u->s + g->off, g->len, u->col, u->eff, MONO);
                    if (u->url)
                        url_link(a, re_pos(), u->url, u->un);
                }
                for (; used < wd[c]; used++)
                    sb_putc(&pd, ' ');
                sb_putc(&pd, ' ');
                if (c + 1 < ncols)
                    sb_puts(&pd, VBAR);
            }
            sb_putc(&pd, ' ');
            PFLUSH();
            re_bg = CLR_INVALID;
            re_nl();
            re_bg = cCodeBg;
        }
        re_bg = CLR_INVALID;
    }
#undef PFLUSH
    free(pd.p);
    for (int c = 0; c < MAXC; c++)
        free(sg[c].p);
    free(rs);
    free(ce);
    free(cells);
    free(nc);
    free(isep);
}
static void md_render(const char *s, COLORREF col)
{
    const char *p = s;
    while (*p)
    {
        const char *e = strchr(p, '\n');
        if (!e)
            e = p + strlen(p);
        size_t len = (size_t)(e - p), l = len;
        const char *q = p;
        while (l && (*q == ' ' || *q == '\t'))
        {
            q++;
            l--;
        }
        size_t indent = len - l;
        const char *next = *e ? e + 1 : e;
        if (l >= 3 && !strncmp(q, "```", 3))
        {
            char lang[24];
            size_t m = 0;
            const char *a = q + 3;
            size_t kq = l - 3;
            while (kq && *a == ' ')
            {
                a++;
                kq--;
            }
            while (m < kq && m < 23 && !isspace((unsigned char)a[m]))
            {
                lang[m] = (char)tolower((unsigned char)a[m]);
                m++;
            }
            lang[m] = 0;
            const char *body = *e ? e + 1 : e, *x = body, *bend = NULL;
            while (*x)
            {
                const char *xe = strchr(x, '\n');
                if (!xe)
                    xe = x + strlen(x);
                const char *y = x;
                while (y < xe && (*y == ' ' || *y == '\t'))
                    y++;
                if (xe - y >= 3 && !strncmp(y, "```", 3))
                {
                    bend = x;
                    next = *xe ? xe + 1 : xe;
                    break;
                }
                x = *xe ? xe + 1 : xe;
            }
            if (!bend)
            {
                bend = x;
                next = x;
            }
            size_t bl = (size_t)(bend - body);
            if (bl && body[bl - 1] == '\n')
                bl--;
            if (bl && body[bl - 1] == '\r')
                bl--;
            code_block(lang, body, bl);
        }
        else if (l == 0)
            re_nl();
        else if (*q == '|')
        {
            const char *ls[200];
            size_t lls[200];
            int n = 0;
            const char *x = p;
            while (*x && n < 200)
            {
                const char *xe = strchr(x, '\n');
                if (!xe)
                    xe = x + strlen(x);
                const char *y = x;
                while (y < xe && (*y == ' ' || *y == '\t'))
                    y++;
                if (y >= xe || *y != '|')
                    break;
                ls[n] = y;
                lls[n] = (size_t)(xe - y);
                n++;
                x = *xe ? xe + 1 : xe;
            }
            md_table(ls, lls, n);
            next = x;
        }
        else if (*q == '#')
        {
            size_t h = 0;
            while (h < l && q[h] == '#')
                h++;
            if (h < l && q[h] == ' ' && h <= 6)
            {
                int sv = re_pt;
                re_pt = h == 1 ? 17 : h == 2 ? 14
                                  : h == 3   ? 12
                                             : 11;
                md_inline(q + h + 1, l - h - 1, cFg, CFE_BOLD);
                re_nl();
                re_pt = sv;
            }
            else
            {
                md_inline(q, l, col, 0);
                re_nl();
            }
        }
        else if (l >= 3 && (!strncmp(q, "---", 3) || !strncmp(q, "***", 3) || !strncmp(q, "___", 3)) && strspn(q, "-*_ \r") >= l)
        {
            re_rule(cBorder);
        }
        else if (*q == '>')
        {
            const char *r = q + 1;
            size_t rl = l - 1;
            if (rl && *r == ' ')
            {
                r++;
                rl--;
            }
            re_add("\xE2\x96\x8E ", 4, cAccent, 0, FACE);
            md_inline(r, rl, cDim, CFE_ITALIC);
            re_nl();
        }
        else if (l >= 2 && (q[0] == '-' || q[0] == '*' || q[0] == '+') && q[1] == ' ')
        {
            for (size_t k = 0; k < indent; k++)
                re_add(" ", 1, col, 0, FACE);
            re_add(indent >= 2 ? "\xE2\x97\xA6 " : BULLET " ", 4, cAccent, 0, FACE);
            md_inline(q + 2, l - 2, col, 0);
            re_nl();
        }
        else if (isdigit((unsigned char)*q))
        {
            size_t d = 0;
            while (d < l && isdigit((unsigned char)q[d]))
                d++;
            if (d < l - 1 && (q[d] == '.' || q[d] == ')') && q[d + 1] == ' ')
            {
                for (size_t k = 0; k < indent; k++)
                    re_add(" ", 1, col, 0, FACE);
                re_add(q, d + 1, cAccent, CFE_BOLD, FACE);
                re_add(" ", 1, col, 0, FACE);
                md_inline(q + d + 2, l - d - 2, col, 0);
                re_nl();
            }
            else
            {
                md_inline(p, len, col, 0);
                re_nl();
            }
        }
        else
        {
            md_inline(p, len, col, 0);
            re_nl();
        }
        if (!*e && next == e)
            break;
        p = next;
    }
}

#endif /* LLAMAC_MARKDOWN_H */
