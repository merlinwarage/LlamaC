/* textops.h - portable text helpers for the file tools: line numbering, edit snippets, mismatch hints, grep (needs util.h, regex.h) */
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

#endif /* LLAMAC_TEXTOPS_H */
