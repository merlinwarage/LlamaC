/* docs.h - Text from PDF, DOCX and XLSX files (portable; needs util.h, inflate.h).
   Used by read_file, summarize_file and attachments. Text only: no OCR, no images, no layout beyond lines,
   paragraphs, table cells and sheet rows. doc_text() returns NULL with a reason for anything it cannot read. */
#ifndef LLAMAC_DOCS_H
#define LLAMAC_DOCS_H

#define DOC_MAX_OUT (4u * 1024 * 1024) /* most text extracted from one document */

/* ======================= ZIP (DOCX / XLSX containers) ======================= */
static unsigned zr16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static unsigned zr32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }
/* the uncompressed content of one entry, NULL when missing or unsupported (caller frees) */
static char *zip_get(const unsigned char *z, size_t n, const char *name)
{
    size_t e = n >= 22 ? n - 22 : 0, stop = n > 65557 ? n - 65557 : 0, cd;
    unsigned cnt;
    if (n < 22)
        return NULL;
    while (e > stop && zr32(z + e) != 0x06054b50)
        e--;
    if (zr32(z + e) != 0x06054b50)
        return NULL;
    cnt = zr16(z + e + 10);
    cd = zr32(z + e + 16);
    for (unsigned i = 0; i < cnt && cd + 46 <= n && zr32(z + cd) == 0x02014b50; i++)
    {
        unsigned meth = zr16(z + cd + 10), csz = zr32(z + cd + 20), usz = zr32(z + cd + 24);
        unsigned nl = zr16(z + cd + 28), xl = zr16(z + cd + 30), cl = zr16(z + cd + 32), lo = zr32(z + cd + 42);
        if (cd + 46 + nl > n)
            return NULL;
        if (nl == strlen(name) && !memcmp(z + cd + 46, name, nl))
        {
            size_t d;
            SB o = {0};
            if (lo + 30 > n || zr32(z + lo) != 0x04034b50)
                return NULL;
            d = lo + 30 + zr16(z + lo + 26) + zr16(z + lo + 28);
            if (d + csz > n)
                return NULL;
            if (meth == 0)
                sb_add(&o, (const char *)z + d, csz);
            else if (meth == 8)
                inflate_raw(z + d, csz, &o, usz ? usz : DOC_MAX_OUT * 4);
            else
                return NULL;
            return o.p ? o.p : xstrdup("");
        }
        cd += 46 + nl + xl + cl;
    }
    return NULL;
}

/* ======================= XML helpers ======================= */
/* appends s[0..n) with XML entities decoded */
static void xml_text(SB *o, const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        if (s[i] != '&')
        {
            sb_putc(o, s[i]);
            continue;
        }
        {
            const char *e = (const char *)memchr(s + i, ';', n - i);
            size_t L = e ? (size_t)(e - (s + i)) : 0;
            if (!e || L > 10)
            {
                sb_putc(o, '&');
                continue;
            }
            if (L == 4 && !strncmp(s + i, "&amp", 4))
                sb_putc(o, '&');
            else if (L == 3 && !strncmp(s + i, "&lt", 3))
                sb_putc(o, '<');
            else if (L == 3 && !strncmp(s + i, "&gt", 3))
                sb_putc(o, '>');
            else if (L == 5 && !strncmp(s + i, "&quot", 5))
                sb_putc(o, '"');
            else if (L == 5 && !strncmp(s + i, "&apos", 5))
                sb_putc(o, '\'');
            else if (s[i + 1] == '#')
            {
                unsigned cp = (unsigned)(s[i + 2] == 'x' ? strtoul(s + i + 3, NULL, 16) : strtoul(s + i + 2, NULL, 10));
                put_utf8(o, cp);
            }
            else
            {
                sb_add(o, s + i, L + 1);
            }
            i += L;
        }
    }
}
/* value of attribute `att` in the tag starting at t (up to '>'), copied into out */
static int xml_attr(const char *t, const char *att, char *out, size_t cap)
{
    const char *gt = strchr(t, '>');
    size_t al = strlen(att);
    for (const char *p = t; p && p < gt; p++)
        if ((p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n' || p[-1] == '\r') && !strncmp(p, att, al) && p[al] == '=' && (p[al + 1] == '"' || p[al + 1] == '\''))
        {
            char q = p[al + 1];
            const char *v = p + al + 2, *e = strchr(v, q);
            size_t n;
            if (!e)
                return 0;
            n = (size_t)(e - v) < cap - 1 ? (size_t)(e - v) : cap - 1;
            memcpy(out, v, n);
            out[n] = 0;
            return 1;
        }
    return 0;
}
/* does the tag at p (just after '<') have this name (followed by space, '/' or '>')? */
static int xml_is(const char *p, const char *name)
{
    size_t n = strlen(name);
    return !strncmp(p, name, n) && (p[n] == ' ' || p[n] == '>' || p[n] == '/' || p[n] == '\t' || p[n] == '\r' || p[n] == '\n');
}

/* ======================= DOCX ======================= */
/* paragraphs as lines, headings as #, list items as -, table cells separated by " | ", rows by lines */
static char *docx_text(const unsigned char *z, size_t n, char *err, size_t es)
{
    char *x = zip_get(z, n, "word/document.xml"), *p;
    SB o = {0};
    size_t pstart = 0;
    int head = 0, list = 0;
    if (!x)
    {
        snprintf(err, es, "not a Word document (word/document.xml missing)");
        return NULL;
    }
    for (p = x; *p && o.n < DOC_MAX_OUT;)
    {
        char *lt = strchr(p, '<'), *gt;
        if (!lt)
            break;
        gt = strchr(lt, '>');
        if (!gt)
            break;
        p = gt + 1;
        if (xml_is(lt + 1, "w:p"))
        {
            pstart = o.n;
            head = list = 0;
        }
        else if (xml_is(lt + 1, "w:pStyle"))
        {
            char v[64];
            if (xml_attr(lt, "w:val", v, sizeof v))
            {
                if (!_strnicmp(v, "Heading", 7) && v[7] >= '1' && v[7] <= '6')
                    head = v[7] - '0';
                else if (!_stricmp(v, "Title"))
                    head = 1;
                else if (!_strnicmp(v, "List", 4))
                    list = 1;
            }
        }
        else if (xml_is(lt + 1, "w:numPr"))
            list = 1;
        else if (xml_is(lt + 1, "w:t") && gt[-1] != '/')
        {
            char *end = strstr(p, "</w:t>");
            if (!end)
                break;
            xml_text(&o, p, (size_t)(end - p));
            p = end + 6;
        }
        else if (xml_is(lt + 1, "w:tab"))
            sb_putc(&o, '\t');
        else if (xml_is(lt + 1, "w:br") || xml_is(lt + 1, "w:cr"))
            sb_putc(&o, '\n');
        else if (!strncmp(lt, "</w:p>", 6))
        {
            if (head || list)
            { /* prefix the paragraph */
                char pre[12];
                int k = 0;
                if (head)
                {
                    while (k < head)
                        pre[k++] = '#';
                    pre[k++] = ' ';
                }
                else
                {
                    pre[k++] = '-';
                    pre[k++] = ' ';
                }
                sb_add(&o, pre, (size_t)k); /* grow, then shift the paragraph right */
                memmove(o.p + pstart + k, o.p + pstart, o.n - (size_t)k - pstart);
                memcpy(o.p + pstart, pre, (size_t)k);
            }
            sb_putc(&o, '\n');
        }
        else if (!strncmp(lt, "</w:tc>", 7))
        { /* a cell's paragraph already ended the line: turn that into a separator */
            if (o.n && o.p[o.n - 1] == '\n')
                o.n--;
            sb_puts(&o, " | ");
        }
        else if (!strncmp(lt, "</w:tr>", 7))
        {
            if (o.n >= 3 && !strncmp(o.p + o.n - 3, " | ", 3))
                o.n -= 3;
            o.p[o.n] = 0;
            sb_putc(&o, '\n');
        }
    }
    free(x);
    return o.p ? o.p : xstrdup("");
}

/* ======================= XLSX ======================= */
static int xl_col(const char *ref) /* "BC12" -> 54 (0-based) */
{
    int c = 0;
    for (; *ref >= 'A' && *ref <= 'Z'; ref++)
        c = c * 26 + (*ref - 'A' + 1);
    return c - 1;
}
static void xl_colname(int c, char *out)
{
    char t[8];
    int k = 0;
    c++;
    while (c > 0 && k < 7)
    {
        int r = (c - 1) % 26;
        t[k++] = (char)('A' + r);
        c = (c - 1) / 26;
    }
    for (int i = 0; i < k; i++)
        out[i] = t[k - 1 - i];
    out[k] = 0;
}
/* every sheet as "## Sheet: name" and one line per row: "12: A12 | B12 | ..." (empty cells stay empty) */
static char *xlsx_text(const unsigned char *z, size_t n, char *err, size_t es)
{
    char *wb = zip_get(z, n, "xl/workbook.xml"), *rels = zip_get(z, n, "xl/_rels/workbook.xml.rels"), *ss = zip_get(z, n, "xl/sharedStrings.xml");
    char **str = NULL;
    int nstr = 0, cap = 0, nsheets = 0;
    SB o = {0};
    if (!wb)
    {
        snprintf(err, es, "not an Excel workbook (xl/workbook.xml missing)");
        free(rels);
        free(ss);
        return NULL;
    }
    for (char *p = ss; p && (p = strstr(p, "<si")) != NULL;)
    { /* shared strings: all <t> runs of one <si> */
        char *end = strstr(p, "</si>");
        SB s = {0};
        if (!end)
            break;
        for (char *t = p; (t = strstr(t, "<t")) != NULL && t < end;)
        {
            char *gt = strchr(t, '>'), *te;
            if (!gt || (t[2] != '>' && t[2] != ' '))
            {
                t += 2;
                continue;
            }
            if (gt[-1] == '/')
            {
                t = gt;
                continue;
            }
            te = strstr(gt, "</t>");
            if (!te || te > end)
                break;
            xml_text(&s, gt + 1, (size_t)(te - gt - 1));
            t = te + 4;
        }
        if (nstr == cap)
        {
            cap = cap ? cap * 2 : 256;
            str = (char **)realloc(str, (size_t)cap * sizeof(char *));
        }
        str[nstr++] = s.p ? s.p : xstrdup("");
        p = end + 5;
    }
    for (char *p = wb; (p = strstr(p, "<sheet ")) != NULL && o.n < DOC_MAX_OUT; p++)
    {
        char name[200] = "", rid[64] = "", target[300] = "", path[400];
        char *sx;
        xml_attr(p, "name", name, sizeof name);
        xml_attr(p, "r:id", rid, sizeof rid);
        nsheets++;
        if (rels && rid[0])
            for (char *r = rels; (r = strstr(r, "<Relationship ")) != NULL; r++)
            {
                char id[64];
                if (xml_attr(r, "Id", id, sizeof id) && !strcmp(id, rid))
                {
                    xml_attr(r, "Target", target, sizeof target);
                    break;
                }
            }
        if (!target[0])
            snprintf(target, sizeof target, "worksheets/sheet%d.xml", nsheets);
        if (target[0] == '/')
            snprintf(path, sizeof path, "%s", target + 1);
        else
            snprintf(path, sizeof path, "xl/%s", target);
        sx = zip_get(z, n, path);
        {
            SB nm = {0};
            xml_text(&nm, name, strlen(name));
            sb_printf(&o, "%s## Sheet: %s\n", o.n ? "\n" : "", sbs(&nm));
            free(nm.p);
        }
        if (!sx)
        {
            sb_puts(&o, "(sheet data not found)\n");
            continue;
        }
        for (char *r = sx; (r = strstr(r, "<row")) != NULL && o.n < DOC_MAX_OUT;)
        {
            char rn[16] = "", *rend = strstr(r, "</row>");
            int col = 0, any = 0;
            SB line = {0};
            if (r[4] != ' ' && r[4] != '>')
            {
                r += 4;
                continue;
            }
            {
                char *tg = strchr(r, '>');
                if (!rend || !tg || tg[-1] == '/')
                { /* <row .../> without cells: its "</row>" would belong to the next row */
                    r += 4;
                    continue;
                }
            }
            xml_attr(r, "r", rn, sizeof rn);
            for (char *c = r; (c = strstr(c, "<c")) != NULL && c < rend;)
            {
                char ref[16] = "", t[16] = "";
                char *cgt = strchr(c, '>'), *cend;
                int cc;
                SB v = {0};
                if (c[2] != ' ' && c[2] != '>')
                {
                    c += 2;
                    continue;
                }
                if (!cgt)
                    break;
                xml_attr(c, "r", ref, sizeof ref);
                xml_attr(c, "t", t, sizeof t);
                cc = ref[0] ? xl_col(ref) : col;
                if (cgt[-1] == '/')
                { /* empty cell */
                    c = cgt;
                    continue;
                }
                cend = strstr(cgt, "</c>");
                if (!cend || cend > rend)
                    break;
                if (!strcmp(t, "inlineStr"))
                {
                    char *ts = strstr(cgt, "<t"), *tg = ts ? strchr(ts, '>') : NULL, *te = tg ? strstr(tg, "</t>") : NULL;
                    if (te && te < cend)
                        xml_text(&v, tg + 1, (size_t)(te - tg - 1));
                }
                else
                {
                    char *vs = strstr(cgt, "<v>"), *ve = vs ? strstr(vs, "</v>") : NULL;
                    if (ve && ve < cend)
                    {
                        if (!strcmp(t, "s"))
                        {
                            int k = atoi(vs + 3);
                            if (k >= 0 && k < nstr)
                                sb_puts(&v, str[k]);
                        }
                        else if (!strcmp(t, "b"))
                            sb_puts(&v, vs[3] == '1' ? "TRUE" : "FALSE");
                        else
                            xml_text(&v, vs + 3, (size_t)(ve - vs - 3));
                    }
                }
                if (v.n)
                {
                    if (cc > col + 50)
                    { /* far to the right: name the column instead of 50 empty fields */
                        char cn[8];
                        xl_colname(cc, cn);
                        sb_printf(&line, " | [%s] ", cn);
                        col = cc;
                    }
                    while (col < cc)
                    {
                        sb_puts(&line, " | ");
                        col++;
                    }
                    for (char *q = v.p; *q; q++) /* keep one row on one line */
                        sb_putc(&line, *q == '\n' || *q == '\r' ? ' ' : *q);
                    any = 1;
                }
                free(v.p);
                c = cend + 4;
            }
            if (any)
                sb_printf(&o, "%s: %s\n", rn[0] ? rn : "?", sbs(&line));
            free(line.p);
            r = rend + 6;
        }
        free(sx);
    }
    for (int i = 0; i < nstr; i++)
        free(str[i]);
    free(str);
    free(wb);
    free(rels);
    free(ss);
    return o.p ? o.p : xstrdup("");
}

/* ======================= PDF ======================= */
typedef struct
{
    int num;
    const char *body; /* the object (after "obj"), up to the stream or "endobj" */
    size_t blen;
    const unsigned char *stream; /* raw (still encoded) stream data, or NULL */
    size_t slen;
    char *own; /* body memory owned here (objects from object streams) */
} PdfObj;
typedef struct
{
    unsigned code;
    char u[12]; /* UTF-8 */
} PdfMapE;
typedef struct
{
    PdfMapE *e;
    int n, cap, bytes; /* bytes per code: 1 or 2 */
} PdfMap;
typedef struct
{
    const unsigned char *buf;
    size_t len;
    PdfObj *o;
    int n, cap;
    int *idx; /* object number -> index in o, or -1 */
    int maxnum;
    /* fonts seen: object number -> its ToUnicode map (NULL = none) */
    int *fnum;
    PdfMap **fmap;
    int nf;
} Pdf;

static int pdf_ws(int c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == 0; }
static int pdf_delim(int c) { return pdf_ws(c) || strchr("()<>[]{}/%", c) != NULL; }
static const char *pdf_skipws(const char *p, const char *e)
{
    while (p < e)
    {
        if (pdf_ws((unsigned char)*p))
            p++;
        else if (*p == '%')
            while (p < e && *p != '\n' && *p != '\r')
                p++;
        else
            break;
    }
    return p;
}
/* end of the object starting at p (name, number, string, hex string, array, dict, ref "n g R", keyword) */
static const char *pdf_skip(const char *p, const char *e)
{
    p = pdf_skipws(p, e);
    if (p >= e)
        return e;
    if (*p == '(')
    {
        int d = 0;
        for (; p < e; p++)
        {
            if (*p == '\\')
                p++;
            else if (*p == '(')
                d++;
            else if (*p == ')' && --d == 0)
                return p + 1;
        }
        return e;
    }
    if (*p == '<' && p + 1 < e && p[1] == '<')
    {
        p += 2;
        for (;;)
        {
            p = pdf_skipws(p, e);
            if (p >= e)
                return e;
            if (*p == '>' && p + 1 < e && p[1] == '>')
                return p + 2;
            p = pdf_skip(p, e);
        }
    }
    if (*p == '<')
    {
        const char *q = (const char *)memchr(p, '>', (size_t)(e - p));
        return q ? q + 1 : e;
    }
    if (*p == '[')
    {
        p++;
        for (;;)
        {
            p = pdf_skipws(p, e);
            if (p >= e)
                return e;
            if (*p == ']')
                return p + 1;
            p = pdf_skip(p, e);
        }
    }
    if (*p == '/')
    {
        p++;
        while (p < e && !pdf_delim((unsigned char)*p))
            p++;
        return p;
    }
    if (isdigit((unsigned char)*p) || *p == '-' || *p == '+' || *p == '.')
    { /* number, or a reference "n g R" */
        const char *a = p, *b, *c;
        while (a < e && !pdf_delim((unsigned char)*a))
            a++;
        b = pdf_skipws(a, e);
        if (b < e && isdigit((unsigned char)*b))
        {
            c = b;
            while (c < e && isdigit((unsigned char)*c))
                c++;
            c = pdf_skipws(c, e);
            if (c < e && *c == 'R' && (c + 1 >= e || pdf_delim((unsigned char)c[1])))
                return c + 1;
        }
        return a;
    }
    if (pdf_delim((unsigned char)*p))
        return p + 1; /* a stray delimiter: always make progress */
    while (p < e && !pdf_delim((unsigned char)*p))
        p++;
    return p;
}
/* value of /key in the dictionary at d (d points at or before "<<"); NULL when absent */
static const char *pdf_get(const char *d, const char *e, const char *key, const char **vend)
{
    size_t kl = strlen(key);
    d = pdf_skipws(d, e);
    if (d + 1 >= e || d[0] != '<' || d[1] != '<')
        return NULL;
    d += 2;
    for (;;)
    {
        const char *k, *v;
        d = pdf_skipws(d, e);
        if (d >= e || (*d == '>' && d + 1 < e && d[1] == '>'))
            return NULL;
        if (*d != '/')
            return NULL;
        k = d + 1;
        d = pdf_skip(d, e);
        v = pdf_skipws(d, e);
        d = pdf_skip(v, e);
        if ((size_t)(d - v) > 0 && (size_t)(v - k) >= kl && !strncmp(k, key, kl) && pdf_delim((unsigned char)k[kl]))
        {
            if (vend)
                *vend = d;
            return v;
        }
    }
}
static int pdf_ref(const char *v, const char *e) /* "12 0 R" -> 12, else -1 */
{
    const char *q;
    int n;
    if (!v || v >= e || !isdigit((unsigned char)*v))
        return -1;
    n = atoi(v);
    q = pdf_skip(v, e);
    return (q > v && q[-1] == 'R') ? n : -1;
}
static PdfObj *pdf_obj(Pdf *P, int num)
{
    if (num < 0 || num > P->maxnum || P->idx[num] < 0)
        return NULL;
    return &P->o[P->idx[num]];
}
/* a dict value that may be a reference: the referenced object's body, else the value itself */
static const char *pdf_deref(Pdf *P, const char *v, const char *ve, const char **end)
{
    int r = pdf_ref(v, ve);
    if (r >= 0)
    {
        PdfObj *o = pdf_obj(P, r);
        if (!o)
            return NULL;
        *end = o->body + o->blen;
        return pdf_skipws(o->body, *end);
    }
    *end = ve;
    return v;
}
static int pdf_is(const char *v, const char *e, const char *name) /* value is the name /name */
{
    size_t n = strlen(name);
    return v && v < e && (size_t)(e - v) >= n && !strncmp(v, name, n) && (v + n >= e || pdf_delim((unsigned char)v[n]));
}
static void pdf_add(Pdf *P, int num, const char *body, size_t blen, const unsigned char *st, size_t sl, char *own)
{
    if (num < 0 || num > 8000000)
        return;
    if (P->n == P->cap)
    {
        P->cap = P->cap ? P->cap * 2 : 256;
        P->o = (PdfObj *)realloc(P->o, (size_t)P->cap * sizeof(PdfObj));
    }
    P->o[P->n].num = num;
    P->o[P->n].body = body;
    P->o[P->n].blen = blen;
    P->o[P->n].stream = st;
    P->o[P->n].slen = sl;
    P->o[P->n].own = own;
    P->n++;
}
/* stream data decoded (FlateDecode or none); NULL for other filters */
static char *pdf_stream(Pdf *P, PdfObj *o, size_t *outn)
{
    const char *e = o->body + o->blen, *ve, *f;
    SB out = {0};
    if (!o->stream)
        return NULL;
    f = pdf_get(o->body, e, "Filter", &ve);
    if (f)
    {
        const char *fe;
        f = pdf_deref(P, f, ve, &fe);
        if (f && *f == '[')
        { /* a filter array: only [/FlateDecode] is handled */
            f = pdf_skipws(f + 1, fe);
            if (!pdf_is(f, fe, "/FlateDecode") || *pdf_skipws(pdf_skip(f, fe), fe) != ']')
                return NULL;
        }
        else if (!pdf_is(f, fe, "/FlateDecode"))
            return NULL;
        inflate_zlib(o->stream, o->slen, &out, 64u * 1024 * 1024);
    }
    else
        sb_add(&out, (const char *)o->stream, o->slen);
    *outn = out.n;
    return out.p ? out.p : xstrdup("");
}

/* finds every "n g obj" in the file, then the objects inside object streams */
static int pdf_load(Pdf *P, const unsigned char *buf, size_t len)
{
    const char *s = (const char *)buf, *e = s + len;
    memset(P, 0, sizeof *P);
    P->buf = buf;
    P->len = len;
    for (const char *p = s; p + 3 < e;)
    {
        const char *o = (const char *)memchr(p, 'o', (size_t)(e - p - 2));
        const char *q, *body, *end, *st = NULL;
        int num;
        if (!o)
            break;
        p = o + 1;
        if (o[1] != 'b' || o[2] != 'j' || (o + 3 < e && !pdf_delim((unsigned char)o[3])) || o - s < 4)
            continue;
        q = o - 1; /* back over " g " and "n" */
        while (q > s && pdf_ws((unsigned char)*q))
            q--;
        if (!isdigit((unsigned char)*q))
            continue;
        while (q > s && isdigit((unsigned char)*q))
            q--;
        if (!pdf_ws((unsigned char)*q))
            continue;
        while (q > s && pdf_ws((unsigned char)*q))
            q--;
        if (!isdigit((unsigned char)*q))
            continue;
        while (q > s && isdigit((unsigned char)q[-1]))
            q--;
        num = atoi(q);
        body = o + 3;
        end = NULL;
        {
            const char *d = pdf_skipws(body, e), *de = pdf_skip(d, e), *k = pdf_skipws(de, e);
            if (k + 6 <= e && !strncmp(k, "stream", 6))
            { /* stream data: /Length when it is a plain number, else up to "endstream" */
                const char *lv, *lve;
                size_t sl = 0;
                st = k + 6;
                if (st < e && *st == '\r')
                    st++;
                if (st < e && *st == '\n')
                    st++;
                lv = pdf_get(d, de, "Length", &lve);
                if (lv && pdf_ref(lv, lve) < 0)
                    sl = (size_t)strtoul(lv, NULL, 10);
                if (sl && st + sl <= e)
                { /* trust /Length only when "endstream" follows it */
                    const char *ck = st + sl;
                    while (ck < e && (*ck == '\r' || *ck == '\n' || *ck == ' '))
                        ck++;
                    if (ck + 9 > e || strncmp(ck, "endstream", 9))
                        sl = 0;
                }
                else
                    sl = 0;
                if (!sl)
                {
                    const char *es = st;
                    sl = 0;
                    while (es + 9 <= e && (es = (const char *)memchr(es, 'e', (size_t)(e - es))) != NULL)
                    {
                        if (es + 9 <= e && !strncmp(es, "endstream", 9))
                        {
                            sl = (size_t)(es - st);
                            break;
                        }
                        es++;
                    }
                    if (!sl)
                        sl = (size_t)(e - st);
                    while (sl && (st[sl - 1] == '\n' || st[sl - 1] == '\r'))
                        sl--;
                }
                pdf_add(P, num, body, (size_t)(k - body), (const unsigned char *)st, sl, NULL);
                p = st + sl;
                continue;
            }
            end = de;
        }
        pdf_add(P, num, body, (size_t)(end - body), NULL, 0, NULL);
        p = end;
    }
    /* index (later definitions win: incremental updates append newer versions) */
    for (int i = 0; i < P->n; i++)
        if (P->o[i].num > P->maxnum)
            P->maxnum = P->o[i].num;
    P->idx = (int *)malloc(((size_t)P->maxnum + 1) * sizeof(int));
    for (int i = 0; i <= P->maxnum; i++)
        P->idx[i] = -1;
    for (int i = 0; i < P->n; i++)
        P->idx[P->o[i].num] = i;
    { /* object streams (PDF 1.5+): many objects, fonts and pages among them, are compressed inside these */
        int n0 = P->n;
        for (int i = 0; i < n0; i++)
        {
            PdfObj *o = &P->o[i];
            const char *oe = o->body + o->blen, *tv, *tve, *nv, *nve, *fv, *fve;
            char *data;
            size_t dn;
            int cnt, first;
            if (!o->stream || !(tv = pdf_get(o->body, oe, "Type", &tve)) || !pdf_is(tv, tve, "/ObjStm"))
                continue;
            nv = pdf_get(o->body, oe, "N", &nve);
            fv = pdf_get(o->body, oe, "First", &fve);
            if (!nv || !fv)
                continue;
            cnt = atoi(nv);
            first = atoi(fv);
            data = pdf_stream(P, o, &dn);
            if (!data || first < 0 || (size_t)first > dn)
            {
                free(data);
                continue;
            }
            {
                int *nums = (int *)malloc(((size_t)cnt + 1) * sizeof(int)), *offs = (int *)malloc(((size_t)cnt + 1) * sizeof(int));
                const char *h = data, *he = data + first;
                int k = 0;
                for (; k < cnt; k++)
                {
                    h = pdf_skipws(h, he);
                    if (h >= he)
                        break;
                    nums[k] = atoi(h);
                    h = pdf_skip(h, he);
                    h = pdf_skipws(h, he);
                    offs[k] = atoi(h);
                    h = pdf_skip(h, he);
                }
                for (int j = 0; j < k; j++)
                {
                    size_t a = (size_t)first + (size_t)offs[j], b = j + 1 < k ? (size_t)first + (size_t)offs[j + 1] : dn;
                    if (a > dn || b > dn || b < a)
                        continue;
                    if (nums[j] <= P->maxnum && P->idx[nums[j]] >= 0)
                        continue; /* a direct object of that number exists */
                    pdf_add(P, nums[j], NULL, b - a, NULL, 0, xstrndup(data + a, b - a));
                    P->o[P->n - 1].body = P->o[P->n - 1].own;
                }
                free(nums);
                free(offs);
            }
            free(data);
        }
        if (P->n > n0)
        {
            int mx = P->maxnum;
            for (int i = n0; i < P->n; i++)
                if (P->o[i].num > mx)
                    mx = P->o[i].num;
            if (mx > P->maxnum)
            {
                P->idx = (int *)realloc(P->idx, ((size_t)mx + 1) * sizeof(int));
                for (int i = P->maxnum + 1; i <= mx; i++)
                    P->idx[i] = -1;
                P->maxnum = mx;
            }
            for (int i = n0; i < P->n; i++)
                if (P->idx[P->o[i].num] < 0)
                    P->idx[P->o[i].num] = i;
        }
    }
    return P->n;
}
static void pdf_free(Pdf *P)
{
    for (int i = 0; i < P->n; i++)
        free(P->o[i].own);
    for (int i = 0; i < P->nf; i++)
        if (P->fmap[i])
        {
            free(P->fmap[i]->e);
            free(P->fmap[i]);
        }
    free(P->o);
    free(P->idx);
    free(P->fnum);
    free(P->fmap);
}
/* ---- ToUnicode CMaps ---- */
static unsigned pdf_hexval(const char *h, const char *e, int *ndig)
{
    unsigned v = 0;
    int k = 0;
    for (; h < e && *h != '>'; h++)
        if (isxdigit((unsigned char)*h))
        {
            v = (v << 4) | (unsigned)(isdigit((unsigned char)*h) ? *h - '0' : (tolower((unsigned char)*h) - 'a' + 10));
            k++;
        }
    if (ndig)
        *ndig = k;
    return v;
}
/* <hex> UTF-16BE -> UTF-8 in out (cap 12) */
static void pdf_u16(const char *h, const char *e, char *out, unsigned add)
{
    unsigned short u[8];
    int n = 0, k = 0;
    SB o = {0};
    unsigned cur = 0;
    for (; h < e && *h != '>'; h++)
        if (isxdigit((unsigned char)*h))
        {
            cur = (cur << 4) | (unsigned)(isdigit((unsigned char)*h) ? *h - '0' : (tolower((unsigned char)*h) - 'a' + 10));
            if (++k == 4)
            {
                if (n < 8)
                    u[n++] = (unsigned short)cur;
                cur = 0;
                k = 0;
            }
        }
    if (n && add)
        u[n - 1] = (unsigned short)(u[n - 1] + add);
    for (int i = 0; i < n; i++)
    {
        unsigned cp = u[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < n)
        {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (u[i + 1] - 0xDC00);
            i++;
        }
        put_utf8(&o, cp);
    }
    snprintf(out, 12, "%s", sbs(&o));
    free(o.p);
}
static void pdfmap_add(PdfMap *m, unsigned code, const char *u)
{
    if (m->n == m->cap)
    {
        m->cap = m->cap ? m->cap * 2 : 128;
        m->e = (PdfMapE *)realloc(m->e, (size_t)m->cap * sizeof(PdfMapE));
    }
    m->e[m->n].code = code;
    snprintf(m->e[m->n].u, sizeof m->e[m->n].u, "%s", u);
    m->n++;
}
static int pdfmap_cmp(const void *a, const void *b)
{
    unsigned x = ((const PdfMapE *)a)->code, y = ((const PdfMapE *)b)->code;
    return x < y ? -1 : x > y;
}
static PdfMap *pdf_cmap(const char *s, size_t n)
{
    PdfMap *m = (PdfMap *)calloc(1, sizeof *m);
    const char *e = s + n, *p;
    m->bytes = 2;
    if ((p = strstr(s, "begincodespacerange")) != NULL)
    {
        const char *h = strchr(p, '<');
        int nd = 0;
        if (h)
            pdf_hexval(h + 1, e, &nd);
        m->bytes = nd <= 2 ? 1 : 2;
    }
    for (p = s; (p = strstr(p, "beginbfchar")) != NULL && p < e;)
    {
        const char *end = strstr(p, "endbfchar");
        if (!end)
            break;
        for (const char *q = p; (q = (const char *)memchr(q, '<', (size_t)(end - q))) != NULL;)
        {
            const char *q2 = (const char *)memchr(q + 1, '<', (size_t)(end - q - 1));
            char u[12];
            unsigned code;
            if (!q2)
                break;
            code = pdf_hexval(q + 1, end, NULL);
            pdf_u16(q2 + 1, end, u, 0);
            pdfmap_add(m, code, u);
            q = (const char *)memchr(q2, '>', (size_t)(end - q2));
            if (!q)
                break;
            q++;
        }
        p = end;
    }
    for (p = s; (p = strstr(p, "beginbfrange")) != NULL && p < e;)
    {
        const char *end = strstr(p, "endbfrange"), *q = p + 12;
        if (!end)
            break;
        for (;;)
        {
            const char *a = (const char *)memchr(q, '<', (size_t)(end - q)), *b, *c;
            unsigned lo, hi;
            if (!a)
                break;
            b = (const char *)memchr(a + 1, '<', (size_t)(end - a - 1));
            if (!b)
                break;
            lo = pdf_hexval(a + 1, end, NULL);
            hi = pdf_hexval(b + 1, end, NULL);
            c = pdf_skipws((const char *)memchr(b, '>', (size_t)(end - b)) + 1, end);
            if (hi < lo || hi - lo > 65535)
                break;
            if (*c == '[')
            { /* one destination per code */
                const char *d = c + 1;
                for (unsigned k = lo; k <= hi; k++)
                {
                    char u[12];
                    d = (const char *)memchr(d, '<', (size_t)(end - d));
                    if (!d)
                        break;
                    pdf_u16(d + 1, end, u, 0);
                    pdfmap_add(m, k, u);
                    d++;
                }
                q = (const char *)memchr(c, ']', (size_t)(end - c));
                if (!q)
                    break;
                q++;
            }
            else if (*c == '<')
            { /* consecutive destinations */
                for (unsigned k = lo; k <= hi; k++)
                {
                    char u[12];
                    pdf_u16(c + 1, end, u, k - lo);
                    pdfmap_add(m, k, u);
                }
                q = (const char *)memchr(c, '>', (size_t)(end - c));
                if (!q)
                    break;
                q++;
            }
            else
                break;
        }
        p = end;
    }
    qsort(m->e, (size_t)m->n, sizeof(PdfMapE), pdfmap_cmp);
    return m;
}
static const char *pdfmap_find(const PdfMap *m, unsigned code)
{
    int lo = 0, hi = m->n - 1;
    while (lo <= hi)
    {
        int mid = (lo + hi) / 2;
        if (m->e[mid].code == code)
            return m->e[mid].u;
        if (m->e[mid].code < code)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}
/* the ToUnicode map of a font object (cached); NULL = none */
static PdfMap *pdf_font_map(Pdf *P, int fontnum)
{
    PdfObj *f;
    PdfMap *m = NULL;
    for (int i = 0; i < P->nf; i++)
        if (P->fnum[i] == fontnum)
            return P->fmap[i];
    f = pdf_obj(P, fontnum);
    if (f)
    {
        const char *fe = f->body + f->blen, *v, *ve;
        v = pdf_get(f->body, fe, "ToUnicode", &ve);
        if (v)
        {
            PdfObj *t = pdf_obj(P, pdf_ref(v, ve));
            size_t n;
            char *d = t ? pdf_stream(P, t, &n) : NULL;
            if (d)
            {
                m = pdf_cmap(d, n);
                free(d);
            }
        }
    }
    P->fnum = (int *)realloc(P->fnum, (size_t)(P->nf + 1) * sizeof(int));
    P->fmap = (PdfMap **)realloc(P->fmap, (size_t)(P->nf + 1) * sizeof(PdfMap *));
    P->fnum[P->nf] = fontnum;
    P->fmap[P->nf] = m;
    P->nf++;
    return m;
}
/* ---- content streams ---- */
typedef struct
{
    SB *out;
    double ly, lx, fs, ex; /* last line y, start x of the last run, font size, estimated end x of the last run */
    double scale;          /* text matrix scale (some generators use "1 Tf" and put the size into Tm) */
    int started;
    PdfMap *map;
} PdfTx;
static double pdf_fs(const PdfTx *T)
{
    double f = (T->fs > 0 ? T->fs : 10) * (T->scale > 0 ? T->scale : 1);
    return f > 0.5 ? f : 10;
}
static void pdf_newline(PdfTx *T)
{
    if (T->out->n && T->out->p[T->out->n - 1] != '\n')
        sb_putc(T->out, '\n');
}
static void pdf_space(PdfTx *T)
{
    if (T->out->n && T->out->p[T->out->n - 1] != ' ' && T->out->p[T->out->n - 1] != '\n')
        sb_putc(T->out, ' ');
}
/* bytes of a string operand */
static void pdf_strbytes(const char *s, const char *e, SB *b)
{
    if (*s == '(')
    {
        int d = 0;
        for (s++; s < e; s++)
        {
            char c = *s;
            if (c == '\\' && s + 1 < e)
            {
                char n = *++s;
                if (n >= '0' && n <= '7')
                {
                    int v = n - '0', k = 1;
                    while (k < 3 && s + 1 < e && s[1] >= '0' && s[1] <= '7')
                        v = v * 8 + (*++s - '0'), k++;
                    c = (char)v;
                }
                else if (n == 'n')
                    c = '\n';
                else if (n == 'r')
                    c = '\r';
                else if (n == 't')
                    c = '\t';
                else if (n == 'b')
                    c = '\b';
                else if (n == 'f')
                    c = '\f';
                else if (n == '\r' || n == '\n')
                {
                    if (n == '\r' && s + 1 < e && s[1] == '\n')
                        s++;
                    continue;
                }
                else
                    c = n;
            }
            else if (c == '(')
                d++;
            else if (c == ')')
            {
                if (d-- == 0)
                    break;
            }
            sb_putc(b, c);
        }
    }
    else if (*s == '<')
    {
        int k = 0;
        unsigned v = 0;
        for (s++; s < e && *s != '>'; s++)
            if (isxdigit((unsigned char)*s))
            {
                v = (v << 4) | (unsigned)(isdigit((unsigned char)*s) ? *s - '0' : (tolower((unsigned char)*s) - 'a' + 10));
                if (++k == 2)
                {
                    sb_putc(b, (char)v);
                    v = 0;
                    k = 0;
                }
            }
        if (k == 1)
            sb_putc(b, (char)(v << 4));
    }
}
/* WinAnsi (cp1252) code -> Unicode, for simple fonts without a ToUnicode map */
static unsigned pdf_winansi(unsigned char c)
{
    static const unsigned short hi[32] = {0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
                                          0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178};
    if (c >= 0x80 && c < 0xA0)
        return hi[c - 0x80] ? hi[c - 0x80] : '?';
    return c;
}
static void pdf_show(PdfTx *T, const char *s, const char *e)
{
    SB b = {0};
    int glyphs = 0;
    pdf_strbytes(s, e, &b);
    if (T->map)
    {
        int w = T->map->bytes;
        for (size_t i = 0; i + (size_t)w <= b.n; i += (size_t)w)
        {
            unsigned code = w == 2 ? (((unsigned char)b.p[i] << 8) | (unsigned char)b.p[i + 1]) : (unsigned char)b.p[i];
            const char *u = pdfmap_find(T->map, code);
            if (u)
                sb_puts(T->out, u);
            glyphs++;
        }
    }
    else
        for (size_t i = 0; i < b.n; i++)
        {
            unsigned char c = (unsigned char)b.p[i];
            if (c >= 0x20 || c == '\t')
                put_utf8(T->out, pdf_winansi(c));
            glyphs++;
        }
    T->ex += glyphs * pdf_fs(T) * 0.5; /* rough advance: half an em per glyph */
    free(b.p);
}
/* a text position change: new line when y moves, a space when x jumps past the estimated end of the last run */
static void pdf_moveto(PdfTx *T, double x, double y)
{
    double fs = pdf_fs(T);
    if (T->started && (y - T->ly > fs * 0.5 || T->ly - y > fs * 0.5))
        pdf_newline(T);
    else if (T->started && x > T->ex + fs * 0.2)
        pdf_space(T);
    T->ly = y;
    T->lx = x;
    T->ex = x;
    T->started = 1;
}
static void pdf_content(Pdf *P, const char *s, size_t n, const char *res, const char *rese, PdfTx *T, int depth);
/* resources dict (inline or ref) -> font map for /name */
static PdfMap *pdf_res_font(Pdf *P, const char *res, const char *rese, const char *name, size_t nl)
{
    const char *fv, *fve, *fd, *fde;
    if (!res)
        return NULL;
    fv = pdf_get(res, rese, "Font", &fve);
    if (!fv)
        return NULL;
    fd = pdf_deref(P, fv, fve, &fde);
    if (!fd)
        return NULL;
    {
        char key[128];
        const char *v, *ve;
        snprintf(key, sizeof key, "%.*s", (int)(nl < 120 ? nl : 120), name);
        v = pdf_get(fd, fde, key, &ve);
        if (!v)
            return NULL;
        return pdf_font_map(P, pdf_ref(v, ve));
    }
}
static void pdf_do_xobject(Pdf *P, const char *res, const char *rese, const char *name, size_t nl, PdfTx *T, int depth)
{
    const char *xv, *xve, *xd, *xde, *v, *ve;
    char key[128];
    PdfObj *o;
    if (!res || depth > 3)
        return;
    xv = pdf_get(res, rese, "XObject", &xve);
    if (!xv || !(xd = pdf_deref(P, xv, xve, &xde)))
        return;
    snprintf(key, sizeof key, "%.*s", (int)(nl < 120 ? nl : 120), name);
    v = pdf_get(xd, xde, key, &ve);
    o = v ? pdf_obj(P, pdf_ref(v, ve)) : NULL;
    if (o)
    {
        const char *oe = o->body + o->blen, *st = pdf_get(o->body, oe, "Subtype", &ve);
        if (pdf_is(st, ve, "/Form"))
        {
            size_t dn;
            char *d = pdf_stream(P, o, &dn);
            const char *r2 = pdf_get(o->body, oe, "Resources", &ve), *r2e = NULL;
            if (r2)
                r2 = pdf_deref(P, r2, ve, &r2e);
            if (d)
            {
                pdf_content(P, d, dn, r2 ? r2 : res, r2 ? r2e : rese, T, depth + 1);
                free(d);
            }
        }
    }
}
/* interprets the text operators of a content stream */
static void pdf_content(Pdf *P, const char *s, size_t n, const char *res, const char *rese, PdfTx *T, int depth)
{
    const char *e = s + n, *p = s;
    const char *op[8]; /* the last operands (start positions) */
    const char *ope[8];
    int nop = 0;
    double tlx = 0, tly = 0, leading = 0; /* line start (Tlm) */
    while (p < e && T->out->n < DOC_MAX_OUT)
    {
        const char *t, *te;
        p = pdf_skipws(p, e);
        if (p >= e)
            break;
        t = p;
        if (*p == '(' || *p == '<' || *p == '[' || *p == '/' || isdigit((unsigned char)*p) || *p == '-' || *p == '+' || *p == '.')
        { /* operand (numbers are not read as references here: "1 0 0 1 0 0 cm" would look like one) */
            if (isdigit((unsigned char)*p) || *p == '-' || *p == '+' || *p == '.')
            {
                te = p;
                while (te < e && !pdf_delim((unsigned char)*te))
                    te++;
            }
            else
                te = pdf_skip(p, e);
            if (nop == 8)
            {
                memmove((void *)op, (void *)(op + 1), 7 * sizeof op[0]);
                memmove((void *)ope, (void *)(ope + 1), 7 * sizeof ope[0]);
                nop--;
            }
            op[nop] = t;
            ope[nop++] = te;
            p = te > p ? te : p + 1;
            continue;
        }
        te = p;
        while (te < e && !pdf_delim((unsigned char)*te))
            te++;
        if (te == p)
        {
            p++;
            nop = 0;
            continue;
        }
        {
            size_t L = (size_t)(te - p);
#define OPIS(x) (L == strlen(x) && !strncmp(p, x, L))
            if (OPIS("BI"))
            { /* inline image: skip its data up to EI */
                const char *ei = te;
                while (ei + 2 < e && !(pdf_ws((unsigned char)ei[0]) && ei[1] == 'E' && ei[2] == 'I' && (ei + 3 >= e || pdf_delim((unsigned char)ei[3]))))
                    ei++;
                te = ei + 3 < e ? ei + 3 : e;
            }
            else if (OPIS("Tf") && nop >= 2 && *op[nop - 2] == '/')
            {
                T->map = pdf_res_font(P, res, rese, op[nop - 2] + 1, (size_t)(ope[nop - 2] - op[nop - 2] - 1));
                T->fs = fabs(atof(op[nop - 1]));
            }
            else if ((OPIS("Td") || OPIS("TD")) && nop >= 2)
            {
                double tx = atof(op[nop - 2]), ty = atof(op[nop - 1]);
                if (OPIS("TD"))
                    leading = -ty;
                tlx += tx;
                tly += ty;
                pdf_moveto(T, tlx, tly);
            }
            else if (OPIS("Tm") && nop >= 6)
            {
                double a = fabs(atof(op[nop - 6])), d = fabs(atof(op[nop - 3]));
                tlx = atof(op[nop - 2]);
                tly = atof(op[nop - 1]);
                T->scale = d > 0 ? d : a > 0 ? a : 1;
                pdf_moveto(T, tlx, tly);
            }
            else if (OPIS("TL") && nop >= 1)
                leading = atof(op[nop - 1]);
            else if (OPIS("T*"))
            {
                tly -= leading;
                pdf_newline(T);
                T->ly = tly;
                T->ex = tlx;
            }
            else if (OPIS("BT"))
            {
                tlx = tly = 0;
                T->scale = 1;
            }
            else if (OPIS("Tj") && nop >= 1)
                pdf_show(T, op[nop - 1], ope[nop - 1]);
            else if ((OPIS("'") || OPIS("\"")) && nop >= 1)
            {
                pdf_newline(T);
                pdf_show(T, op[nop - 1], ope[nop - 1]);
            }
            else if (OPIS("TJ") && nop >= 1 && *op[nop - 1] == '[')
            {
                const char *a = op[nop - 1] + 1, *ae = ope[nop - 1] - 1;
                while (a < ae)
                {
                    const char *b;
                    a = pdf_skipws(a, ae);
                    if (a >= ae)
                        break;
                    if (*a == '(' || *a == '<')
                    {
                        b = pdf_skip(a, ae);
                        pdf_show(T, a, b);
                    }
                    else
                    { /* a kerning adjustment in 1/1000 em; a large negative one is a word gap */
                        b = a;
                        while (b < ae && !pdf_delim((unsigned char)*b))
                            b++;
                        if (b == a)
                            b++;
                        else
                        {
                            double k = atof(a);
                            if (k < -200)
                                pdf_space(T);
                            T->ex -= k / 1000.0 * pdf_fs(T);
                        }
                    }
                    a = b;
                }
            }
            else if (OPIS("Do") && nop >= 1 && *op[nop - 1] == '/')
                pdf_do_xobject(P, res, rese, op[nop - 1] + 1, (size_t)(ope[nop - 1] - op[nop - 1] - 1), T, depth);
#undef OPIS
        }
        nop = 0;
        p = te;
    }
}
static void pdf_page(Pdf *P, PdfObj *pg, const char *res, const char *rese, SB *out, int pno)
{
    const char *e = pg->body + pg->blen, *cv, *cve;
    PdfTx T;
    memset(&T, 0, sizeof T);
    T.out = out;
    T.fs = 10;
    sb_printf(out, "%s--- page %d ---\n", out->n ? "\n" : "", pno);
    cv = pdf_get(pg->body, e, "Contents", &cve);
    if (!cv)
        return;
    {
        SB all = {0};
        int r = pdf_ref(cv, cve);
        if (r >= 0)
        { /* a stream, or a reference to an array of streams */
            PdfObj *o = pdf_obj(P, r);
            if (o && o->stream)
            {
                size_t dn;
                char *d = pdf_stream(P, o, &dn);
                if (d)
                    sb_add(&all, d, dn);
                free(d);
            }
            else if (o)
            {
                const char *ae = o->body + o->blen;
                cv = pdf_skipws(o->body, ae);
                cve = ae;
                r = -1;
            }
        }
        if (r < 0 && cv && *cv == '[')
            for (const char *a = cv + 1; a < cve;)
            {
                const char *b;
                int rr;
                a = pdf_skipws(a, cve);
                if (a >= cve || *a == ']')
                    break;
                b = pdf_skip(a, cve);
                rr = pdf_ref(a, b);
                if (rr >= 0)
                {
                    PdfObj *o = pdf_obj(P, rr);
                    size_t dn;
                    char *d = o ? pdf_stream(P, o, &dn) : NULL;
                    if (d)
                    {
                        sb_add(&all, d, dn);
                        sb_putc(&all, '\n');
                    }
                    free(d);
                }
                a = b > a ? b : a + 1;
            }
        if (all.n)
            pdf_content(P, all.p, all.n, res, rese, &T, 0);
        free(all.p);
    }
    pdf_newline(&T);
}
/* walks the page tree in order; resources are inherited from parent nodes */
static void pdf_pages(Pdf *P, int num, const char *res, const char *rese, SB *out, int *pno, int depth)
{
    PdfObj *o = pdf_obj(P, num);
    const char *e, *tv, *tve, *rv, *rve, *kv, *kve;
    if (!o || depth > 30 || out->n >= DOC_MAX_OUT)
        return;
    e = o->body + o->blen;
    rv = pdf_get(o->body, e, "Resources", &rve);
    if (rv)
    {
        const char *re2;
        const char *r2 = pdf_deref(P, rv, rve, &re2);
        if (r2)
        {
            res = r2;
            rese = re2;
        }
    }
    tv = pdf_get(o->body, e, "Type", &tve);
    if (pdf_is(tv, tve, "/Page"))
    {
        pdf_page(P, o, res, rese, out, ++*pno);
        return;
    }
    kv = pdf_get(o->body, e, "Kids", &kve);
    if (kv && *kv == '[')
        for (const char *a = kv + 1; a < kve;)
        {
            const char *b;
            a = pdf_skipws(a, kve);
            if (a >= kve || *a == ']')
                break;
            b = pdf_skip(a, kve);
            pdf_pages(P, pdf_ref(a, b), res, rese, out, pno, depth + 1);
            a = b > a ? b : a + 1;
        }
}
static char *pdf_text(const unsigned char *buf, size_t len, char *err, size_t es)
{
    Pdf P;
    SB out = {0};
    int pno = 0, root = -1;
    if (len < 8 || memcmp(buf, "%PDF", 4))
    {
        snprintf(err, es, "not a PDF file");
        return NULL;
    }
    if (!pdf_load(&P, buf, len))
    {
        snprintf(err, es, "no PDF objects found (damaged file?)");
        pdf_free(&P);
        return NULL;
    }
    for (int i = 0; i < P.n && root < 0; i++)
    { /* the catalog's /Pages */
        const char *e = P.o[i].body + P.o[i].blen, *tv, *tve, *pv, *pve;
        tv = pdf_get(P.o[i].body, e, "Type", &tve);
        if (tv && pdf_is(tv, tve, "/Catalog") && (pv = pdf_get(P.o[i].body, e, "Pages", &pve)) != NULL)
            root = pdf_ref(pv, pve);
    }
    {
        const char *s = (const char *)buf;
        for (size_t i = len > 4096 ? len - 4096 : 0; i + 8 < len; i++) /* the trailer names an encryption dictionary */
            if (s[i] == '/' && !strncmp(s + i, "/Encrypt", 8) && pdf_delim((unsigned char)s[i + 8]))
            {
                snprintf(err, es, "the PDF is encrypted; open it in a PDF viewer and save or print an unprotected copy");
                pdf_free(&P);
                return NULL;
            }
    }
    if (root >= 0)
        pdf_pages(&P, root, NULL, NULL, &out, &pno, 0);
    if (!pno)
    { /* no usable page tree: every page object in file order */
        for (int i = 0; i < P.n; i++)
        {
            const char *e = P.o[i].body + P.o[i].blen, *tv, *tve, *rv, *rve, *re2 = NULL, *r2 = NULL;
            tv = pdf_get(P.o[i].body, e, "Type", &tve);
            if (!tv || !pdf_is(tv, tve, "/Page"))
                continue;
            rv = pdf_get(P.o[i].body, e, "Resources", &rve);
            if (rv)
                r2 = pdf_deref(&P, rv, rve, &re2);
            pdf_page(&P, &P.o[i], r2, re2, &out, ++pno);
        }
    }
    {
        size_t text = 0;
        for (size_t i = 0; i < out.n; i++)
            text += !pdf_ws((unsigned char)out.p[i]);
        if (pno && text < (size_t)pno * 20)
            sb_printf(&out, "\n[Almost no text found on %d page(s): the PDF is probably scanned images, which need OCR.]\n", pno);
    }
    pdf_free(&P);
    return out.p ? out.p : xstrdup("");
}

/* ======================= entry point ======================= */
/* 1 when the file name has an extension handled here */
static int doc_kind(const char *name)
{
    const char *d = strrchr(name, '.');
    if (!d)
        return 0;
    if (!_stricmp(d, ".pdf"))
        return 1;
    if (!_stricmp(d, ".docx") || !_stricmp(d, ".docm"))
        return 2;
    if (!_stricmp(d, ".xlsx") || !_stricmp(d, ".xlsm"))
        return 3;
    return 0;
}
/* text of a PDF / DOCX / XLSX file; NULL with the reason in err */
static char *doc_text(const char *name, const unsigned char *buf, size_t len, char *err, size_t es)
{
    int k = doc_kind(name);
    err[0] = 0;
    if (k == 1)
        return pdf_text(buf, len, err, es);
    if (k == 2 || k == 3)
    {
        if (len < 4 || zr32(buf) != 0x04034b50)
        {
            snprintf(err, es, "not a ZIP-based Office file (old .doc / .xls formats are not supported)");
            return NULL;
        }
        return k == 2 ? docx_text(buf, len, err, es) : xlsx_text(buf, len, err, es);
    }
    snprintf(err, es, "unsupported document type");
    return NULL;
}

#endif /* LLAMAC_DOCS_H */
