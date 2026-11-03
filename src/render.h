/* render.h - Thinking blocks, streaming flush, render_all */
#ifndef LLAMAC_RENDER_H
#define LLAMAC_RENDER_H

/* ---------- collapsible thinking block ---------- */
static void render_think(const char *txt, size_t n)
{
    if (!n)
        return;
    int open = (g_rnode >= 0 && cur && g_rnode < cur->cnt && cur->n[g_rnode].think_open);
    char hdr[80];
    snprintf(hdr, sizeof hdr, "%s Thinking (%u chars)", open ? "\xE2\x96\xBE" : "\xE2\x96\xB8", (unsigned)n);
    if (nthink == capthink)
    {
        capthink = capthink ? capthink * 2 : 8;
        thinks = (Think *)realloc(thinks, (size_t)capthink * sizeof(Think));
    }
    int id = nthink++;
    Think *t = &thinks[id];
    t->node = g_rnode;
    t->collapsed = !open;
    re_track = 1;
    re_add(hdr, strlen(hdr), cDim, CFE_BOLD, FACE);
    re_track = 0;
    t->hs = re_s;
    if (nlinks == caplinks)
    {
        caplinks = caplinks ? caplinks * 2 : 16;
        links = (LinkR *)realloc(links, (size_t)caplinks * sizeof(LinkR));
    }
    links[nlinks].a = re_s;
    links[nlinks].b = re_e;
    links[nlinks].blk = id;
    links[nlinks].act = 2;
    nlinks++;
    re_nl();
    re_track = 1;
    re_add(txt, n, cDim, CFE_ITALIC, FACE);
    re_track = 0;
    t->a = re_s;
    t->b = re_e;
    re_nl();
    if (t->collapsed)
        set_hidden(t->a, t->b + 1, 1);
    re_nl();
}

/* ---------- streaming: coalesce tokens, flush on a timer ---------- */
static SB g_pend;
static int g_pend_kind = -1, g_timer_on;
static size_t gl[2], gc[2];
static void app_amort(char **d, size_t *len, size_t *cap, const char *s)
{
    size_t b = strlen(s);
    if (*len + b + 1 > *cap)
    {
        size_t c = *cap ? *cap : 64;
        while (c < *len + b + 1)
            c *= 2;
        *d = (char *)realloc(*d, c);
        *cap = c;
    }
    memcpy(*d + *len, s, b + 1);
    *len += b;
}
static int at_bottom(void)
{
    SCROLLINFO si;
    memset(&si, 0, sizeof si);
    si.cbSize = sizeof si;
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    GetScrollInfo(hChat, SB_VERT, &si);
    return si.nPos + (int)si.nPage >= si.nMax - 8;
}
static void flush_stream(void)
{
    if (g_pend.n && cur && cur == genConv)
    {
        int follow = at_bottom();
        if (g_pend_kind == 1)
        {
            if (genPhase != 1)
            {
                re_add("Thinking\n", 9, cDim, CFE_ITALIC | CFE_BOLD, FACE);
                genPhase = 1;
            }
            re_add(g_pend.p, g_pend.n, cDim, CFE_ITALIC, FACE);
        }
        else
        {
            if (genPhase == 1)
            {
                re_nl();
                re_nl();
            }
            genPhase = 2;
            re_add(g_pend.p, g_pend.n, cFg, 0, FACE);
        }
        if (follow)
            SendMessageW(hChat, WM_VSCROLL, SB_BOTTOM, 0);
    }
    g_pend.n = 0;
    if (g_pend.p)
        g_pend.p[0] = 0;
}

/* <think>...</think> inside the content (reasoning_format = none) is shown as a dim thinking block */
static void render_content(const char *s)
{
    const char *p = s;
    for (;;)
    {
        const char *a = strstr(p, "<think>");
        if (!a)
        {
            md_render(p, cFg);
            return;
        }
        if (a > p)
        {
            char *seg = xstrndup(p, (size_t)(a - p));
            md_render(seg, cFg);
            free(seg);
        }
        const char *b = a + 7, *e = strstr(b, "</think>");
        size_t tl = e ? (size_t)(e - b) : strlen(b);
        if (G.thoughts && tl)
            render_think(b, tl);
        if (!e)
            return;
        p = e + 8;
        while (*p == '\n' || *p == '\r')
            p++;
    }
}
/* ---------- tool calls and tool results ---------- */
static const char *role_label(int r) { return r == ROLE_USER ? "You" : r == ROLE_ASSIST ? "Assistant"
                                                                   : r == ROLE_TOOL     ? "Tool"
                                                                                        : "System"; }
static void render_tool_result(Node *m)
{
    char h[200];
    size_t full = strlen(m->content), n = full;
    snprintf(h, sizeof h, "\xE2\x96\xB8 %.150s\n", m->tname && *m->tname ? m->tname : "tool");
    re_add(h, strlen(h), cDim, CFE_BOLD, FACE);
    if (n > 4000)
    {
        n = 4000;
        while (n && ((unsigned char)m->content[n] & 0xC0) == 0x80)
            n--;
    }
    if (n)
        re_code(m->content, n, cDim, 0);
    if (n < full)
    {
        const char *t = "\n... (shortened in this view; the model received the full result)";
        re_add(t, strlen(t), cDim, CFE_ITALIC, FACE);
    }
    re_nl();
}
static void render_tool_calls(const char *json)
{
    J *a = json_parse(json);
    for (J *c = a ? a->child : NULL; c; c = c->next)
    {
        J *fn = j_get(c, "function");
        const char *nm = j_str(j_get(fn, "name")), *ar = j_str(j_get(fn, "arguments"));
        SB s = {0};
        size_t al = ar ? strlen(ar) : 0;
        if (al > 300)
        {
            al = 300;
            while (al && ((unsigned char)ar[al] & 0xC0) == 0x80)
                al--;
        }
        sb_puts(&s, "\xE2\x96\xB8 Tool call: ");
        sb_puts(&s, nm ? nm : "?");
        sb_putc(&s, '(');
        if (ar)
            sb_add(&s, ar, al);
        if (ar && al < strlen(ar))
            sb_puts(&s, "...");
        sb_puts(&s, ")\n");
        re_add(sbs(&s), s.n, cAccent, 0, FACE);
        free(s.p);
    }
    j_free(a);
}
/* ---------- incremental chat view ----------
   Only the last rendered message can change while a chat is open (it streams, then gets Markdown-rendered), so
   render_all() keeps the offset of every rendered message and re-renders from the first message that differs. */
typedef struct
{
    int id;         /* node index */
    LONG off;       /* RichEdit character offset where this message starts */
    int nb, nl, nt; /* code blocks / links / think blocks that existed before it */
} RdNode;
static RdNode *g_rd;
static int g_rdn, g_rdcap, g_rd_key = -1;
static LONG re_len(void)
{
    CHARRANGE cr;
    SendMessageW(hChat, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    SendMessageW(hChat, EM_EXGETSEL, 0, (LPARAM)&cr);
    return cr.cpMin;
}
static void blocks_trunc(int nb, int nl, int nt)
{
    for (int i = nb; i < nblk; i++)
        free(blks[i].text);
    if (nb < nblk)
        nblk = nb;
    if (nl < nlinks)
        nlinks = nl;
    if (nt < nthink)
        nthink = nt;
}
static void render_node(const int *p, int i, int n)
{
    Node *m = &cur->n[p[i]];
    char hdr[96];
    Node *pn = &cur->n[m->parent];
    int pos = 0;
    for (int k = 0; k < pn->nk; k++)
        if (pn->kids[k] == p[i])
            pos = k;
    snprintf(hdr, sizeof hdr, "\xE2\x97\x8F  %s", role_label(m->role));
    if (pn->nk > 1)
        snprintf(hdr + strlen(hdr), sizeof hdr - strlen(hdr), "     \xE2\x80\xB9 %d / %d \xE2\x80\xBA", pos + 1, pn->nk);
    re_add(hdr, strlen(hdr), m->role == ROLE_USER ? cUser : m->role == ROLE_TOOL ? cDim : cAsst, CFE_BOLD, FACE);
    re_nl();
    for (int k = 0; k < m->na; k++)
    {
        char b[300];
        snprintf(b, sizeof b, "\xE2\x96\xAB %s\n", m->att[k].name);
        re_add(b, strlen(b), cDim, 0, FACE);
    }
    g_rnode = p[i];
    if (G.thoughts)
        render_think(m->reasoning, strlen(m->reasoning));
    if (m->role == ROLE_TOOL)
        render_tool_result(m);
    else
    {
        render_content(m->content);
        if (m->tcalls && *m->tcalls)
            render_tool_calls(m->tcalls);
    }
    if (!(busy && cur == genConv && p[i] == genNode))
    {
        re_nl();
        if (i + 1 < n)
            re_rule(cBorder);
    }
}
static void render_all(void)
{
    int *p = NULL, n = 0, start = 0;
    int key = (G.dark ? 1 : 0) | (G.thoughts ? 2 : 0) | (g_colw << 2);
    g_pend.n = 0;
    if (g_pend.p)
        g_pend.p[0] = 0;
    if (cur)
        n = conv_path(cur, &p);
    if (cur && cur == g_rd_conv && key == g_rd_key && n > 0 && g_rdn > 0)
    {
        int d = 0;
        while (d < n && d < g_rdn && g_rd[d].id == p[d])
            d++;
        start = d;
        if (start > n - 1)
            start = n - 1;
        if (start > g_rdn - 1)
            start = g_rdn - 1;
        if (start < 0)
            start = 0;
    }
    SendMessageW(hChat, WM_SETREDRAW, FALSE, 0);
    if (start == 0)
    {
        SetWindowTextW(hChat, L"");
        blocks_clear();
        g_rdn = 0;
        {
            PARAFORMAT2 pf;
            memset(&pf, 0, sizeof pf);
            pf.cbSize = sizeof pf;
            pf.dwMask = PFM_LINESPACING;
            pf.bLineSpacingRule = 5;
            pf.dyLineSpacing = 24;
            SendMessageW(hChat, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
        }
        if (cur && n == 0)
        {
            int sv = re_pt;
            re_pt = 20;
            re_nl();
            re_nl();
            re_add("  llama.cpp\n", strlen("  llama.cpp\n"), cAccent, CFE_BOLD, FACE);
            re_pt = 11;
            re_add("  Ask anything. Drop images or text files to attach them.\n", strlen("  Ask anything. Drop images or text files to attach them.\n"), cDim, 0, FACE);
            re_pt = sv;
        }
    }
    else
    {
        const RdNode *rn = &g_rd[start];
        SendMessageW(hChat, EM_SETSEL, (WPARAM)rn->off, (LPARAM)-1);
        SendMessageW(hChat, EM_REPLACESEL, FALSE, (LPARAM)L"");
        blocks_trunc(rn->nb, rn->nl, rn->nt);
        g_rdn = start;
    }
    if (n > g_rdcap)
    {
        g_rdcap = n + 16;
        g_rd = (RdNode *)realloc(g_rd, (size_t)g_rdcap * sizeof(RdNode));
    }
    for (int i = start; i < n; i++)
    {
        g_rd[i].id = p[i];
        g_rd[i].off = re_len();
        g_rd[i].nb = nblk;
        g_rd[i].nl = nlinks;
        g_rd[i].nt = nthink;
        render_node(p, i, n);
        g_rdn = i + 1;
    }
    g_rd_conv = cur;
    g_rd_key = key;
    free(p);
    SendMessageW(hChat, WM_SETREDRAW, TRUE, 0);
    SendMessageW(hChat, WM_VSCROLL, SB_BOTTOM, 0);
    InvalidateRect(hChat, NULL, TRUE);
    InvalidateRect(hMain, &rcHead, FALSE);
}

#endif /* LLAMAC_RENDER_H */
