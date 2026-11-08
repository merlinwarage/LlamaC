/* actions.h - Chat actions: send, regenerate, edit, delete, branch */
#ifndef LLAMAC_ACTIONS_H
#define LLAMAC_ACTIONS_H

/* ---------- chat actions ---------- */
static void set_busy(int b)
{
    busy = b;
    SetWindowTextW(B(IDC_SEND), b ? L"Stop" : L"Send");
    InvalidateRect(B(IDC_SEND), NULL, FALSE);
}
static int g_toolround; /* tool rounds used for the current user message */
/* A generation requested while compression runs waits for it, so the request is built from the
   compressed conversation instead of the full one that is about to overflow. */
static int g_wait_gen, g_wait_parent, g_wait_cont;
static Conv *g_wait_conv;
static void compact_start(Conv *c); /* compact.h */
/* The last measured generation: its timings (g_last_prompt_n + g_last_pred_n) cover the path up to and
   including g_meas_node. Invalidated after a compression, which changes the prompt. */
static Conv *g_meas_conv;
static int g_meas_node = -1;
/* compress once the prompt passes this. Auto leaves room for the reply (reasoning models need thousands
   of tokens): max(15%, 4096 tokens), at most half the context. */
static int ctx_trigger(int lim)
{
    int pct = G.compact_pct;
    if (pct > 0)
    {
        if (pct < 30)
            pct = 30;
        if (pct > 95)
            pct = 95;
        return (int)((long long)lim * pct / 100);
    }
    int r = lim * 15 / 100;
    if (r < 4096)
        r = 4096;
    if (r > lim / 2)
        r = lim / 2;
    return lim - r;
}
/* tokens the next request will use: measured part + ~3 chars/token for what was added since
   (tool results, a new user message). -1 = no usable measurement for this path. */
static int est_tokens(Conv *c)
{
    int *p, n, at = -1;
    size_t ch = 0;
    if (c != g_meas_conv || g_meas_node < 0 || g_last_prompt_n <= 0)
        return -1;
    n = conv_path(c, &p);
    for (int i = 0; i < n; i++)
        if (p[i] == g_meas_node)
            at = i;
    for (int i = at < 0 ? n : at + 1; i < n; i++)
    {
        Node *m = &c->n[p[i]];
        ch += strlen(m->content);
        if (m->tcalls)
            ch += strlen(m->tcalls);
        for (int a = 0; a < m->na; a++)
            ch += m->att[a].kind == ATT_TEXT ? strlen(m->att[a].data) : 3000; /* image/audio: rough */
    }
    free(p);
    return at < 0 ? -1 : g_last_prompt_n + g_last_pred_n + (int)(ch / 3);
}
static void start_gen_ex(int parent, int cont)
{
    cur->leaf = parent;
    if (!g_compacting && G.autocompact)
    { /* a big tool result or attachment can push the next prompt over the limit before any
         generation reports its usage, so check the estimate before sending */
        int lim = G.ctx_limit > 0 ? G.ctx_limit : g_nctx, est = est_tokens(cur);
        if (lim > 0 && est > ctx_trigger(lim))
            compact_start(cur); /* sets g_compacting when it starts; the wait below then applies */
    }
    if (g_compacting)
    {
        g_wait_gen = 1;
        g_wait_conv = cur;
        g_wait_parent = parent;
        g_wait_cont = cont;
        cur->leaf = parent;
        set_busy(1); /* Stop cancels the wait (do_send) */
        render_all();
        set_status("Waiting for context compression to finish...");
        return;
    }
    g_last_prompt_n = g_last_pred_n = 0; /* only this generation's timings may trigger compression */
    if (!cont)
        g_toolround = 0;
    int a = node_add(cur, parent, ROLE_ASSIST);
    g_meas_conv = cur;
    g_meas_node = a;
    cur->leaf = a;
    Job *j = (Job *)calloc(1, sizeof *j);
    j->body = build_request(cur, &G);
    strncpy(j->url, G.url, 255);
    strncpy(j->key, G.key, 255);
    genConv = cur;
    genNode = a;
    genPhase = 0;
    g_stop = 0;
    gl[0] = gl[1] = 0;
    gc[0] = gc[1] = 1;
    set_busy(1);
    cur->mod = now_ms();
    render_all();
    set_status(cont ? "Generating (after tool results)..." : "Generating...");
    HANDLE h = CreateThread(NULL, 0, worker, j, 0, NULL);
    if (h)
        CloseHandle(h);
}
static void start_gen(int parent) { start_gen_ex(parent, 0); }
static void do_send(void)
{
    if (busy)
    {
        if (g_wait_gen)
        {
            g_wait_gen = 0;
            set_busy(0);
            set_status("Cancelled (compression continues in the background)");
            return;
        }
        stop_gen();
        return;
    }
    if (!cur)
        new_chat();
    char *txt = get_text(hInput);
    char *t = txt;
    size_t l;
    while (*t == ' ' || *t == '\r' || *t == '\n' || *t == '\t')
        t++;
    l = strlen(t);
    while (l && (t[l - 1] == ' ' || t[l - 1] == '\r' || t[l - 1] == '\n' || t[l - 1] == '\t'))
        t[--l] = 0;
    if (!*t && !npend)
    {
        free(txt);
        return;
    }
    int u = node_add(cur, cur->leaf, ROLE_USER);
    setstr(&cur->n[u].content, t);
    for (int i = 0; i < npend; i++)
        att_add(&cur->n[u], pend[i].kind, pend[i].name, pend[i].data, pend[i].mime);
    if (!strcmp(cur->name, "New chat"))
    {
        const char *src = *t ? t : (npend ? pend[0].name : "Chat");
        char title[64];
        size_t n = strlen(src);
        if (n > 50)
        {
            n = 50;
            while (n && ((unsigned char)src[n] & 0xC0) == 0x80)
                n--;
        }
        memcpy(title, src, n);
        title[n] = 0;
        for (char *c = title; *c; c++)
            if (*c == '\n' || *c == '\r')
                *c = ' ';
        setstr(&cur->name, title);
    }
    pend_clear();
    SetWindowTextW(hInput, L"");
    free(txt);
    refresh_list();
    start_gen(u);
}
static void act_regen(void)
{
    if (busy || !cur || cur->leaf == cur->root)
        return;
    Node *l = &cur->n[cur->leaf];
    if (l->role == ROLE_ASSIST)
        start_gen(l->parent);
    else if (l->role == ROLE_USER || l->role == ROLE_TOOL)
        start_gen(cur->leaf);
}
static void act_edit(void)
{
    if (busy || !cur)
        return;
    int *p;
    int n = conv_path(cur, &p), u = -1;
    for (int i = n - 1; i >= 0; i--)
        if (cur->n[p[i]].role == ROLE_USER)
        {
            u = p[i];
            break;
        }
    free(p);
    if (u < 0)
        return;
    Node *m = &cur->n[u];
    wchar_t *w = w_from(m->content);
    pend_clear();
    for (int k = 0; k < m->na; k++)
        pend_add(m->att[k].kind, m->att[k].name, m->att[k].data, m->att[k].mime);
    cur->leaf = m->parent;
    render_all();
    SetWindowTextW(hInput, w);
    free(w);
    SetFocus(hInput);
    SendMessageW(hInput, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    set_status("Editing: your next send creates a new branch");
}
static void act_delmsg(void)
{
    if (busy || !cur || cur->leaf == cur->root)
        return;
    int l = cur->leaf, p = cur->n[l].parent;
    Node *pn = &cur->n[p];
    for (int k = 0; k < pn->nk; k++)
        if (pn->kids[k] == l)
        {
            memmove(&pn->kids[k], &pn->kids[k + 1], (size_t)(pn->nk - k - 1) * sizeof(int));
            pn->nk--;
            break;
        }
    cur->leaf = p;
    cur->mod = now_ms();
    render_all();
    save_all();
}
static void act_nav(int dir)
{
    if (busy || !cur)
        return;
    for (int i = cur->leaf; i >= 0 && cur->n[i].role != ROLE_ROOT; i = cur->n[i].parent)
    {
        Node *pn = &cur->n[cur->n[i].parent];
        if (pn->nk > 1)
        {
            int k = 0;
            while (pn->kids[k] != i)
                k++;
            k = (k + dir + pn->nk) % pn->nk;
            cur->leaf = deepest(cur, pn->kids[k]);
            render_all();
            save_all();
            return;
        }
    }
    set_status("No branches yet (edit a message or regenerate to create one)");
}
static void act_copy(void)
{
    if (!cur)
        return;
    for (int i = cur->leaf; i >= 0 && cur->n[i].role != ROLE_ROOT; i = cur->n[i].parent)
        if (cur->n[i].role == ROLE_ASSIST)
        {
            wchar_t *w = w_from(cur->n[i].content);
            size_t sz = (wcslen(w) + 1) * 2;
            HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, sz);
            if (g && OpenClipboard(hMain))
            {
                memcpy(GlobalLock(g), w, sz);
                GlobalUnlock(g);
                EmptyClipboard();
                SetClipboardData(CF_UNICODETEXT, g);
                CloseClipboard();
                set_status("Copied last response");
            }
            free(w);
            return;
        }
}
static void act_delconv(void)
{
    if (busy || !cur)
        return;
    if (MessageBoxW(hMain, L"Delete this conversation?", L"llamac", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;
    for (int i = 0; i < nconv; i++)
        if (convs[i] == cur)
        {
            memmove(&convs[i], &convs[i + 1], (size_t)(nconv - i - 1) * sizeof *convs);
            nconv--;
            break;
        }
    conv_file_delete(cur);
    conv_free(cur);
    cur = NULL;
    if (!nconv)
        convs_add(conv_new());
    qsort(convs, (size_t)nconv, sizeof(Conv *), cmp_mod);
    select_conv(convs[0]);
    save_all();
}
static void act_export(void)
{
    if (!cur)
        return;
    wchar_t f[MAX_PATH] = L"conversation.json";
    OPENFILENAMEW o;
    memset(&o, 0, sizeof o);
    o.lStructSize = sizeof o;
    o.hwndOwner = hMain;
    o.lpstrFile = f;
    o.nMaxFile = MAX_PATH;
    o.lpstrFilter = L"JSON\0*.json\0";
    o.lpstrDefExt = L"json";
    o.Flags = OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&o))
        return;
    SB s = {0};
    sb_putc(&s, '[');
    conv_json(&s, cur);
    sb_putc(&s, ']');
    write_file(f, sbs(&s), s.n);
    free(s.p);
    set_status("Exported (WebUI-compatible JSON)");
}
static void act_import(void)
{
    if (busy)
        return;
    wchar_t f[MAX_PATH] = L"";
    OPENFILENAMEW o;
    memset(&o, 0, sizeof o);
    o.lStructSize = sizeof o;
    o.hwndOwner = hMain;
    o.lpstrFile = f;
    o.nMaxFile = MAX_PATH;
    o.lpstrFilter = L"JSON\0*.json\0";
    o.Flags = OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameW(&o))
        return;
    size_t len;
    unsigned char *b = read_file(f, &len);
    if (!b)
        return;
    J *r = json_parse((char *)b);
    int added = 0;
    if (r)
    {
        if (r->t == J_ARR)
        {
            for (J *e = r->child; e; e = e->next)
            {
                Conv *c = conv_from_json(e, 1);
                if (c)
                {
                    convs_add(c);
                    cur = c;
                    added++;
                }
            }
        }
        else
        {
            Conv *c = conv_from_json(r, 1);
            if (c)
            {
                convs_add(c);
                cur = c;
                added++;
            }
        }
    }
    j_free(r);
    free(b);
    if (added)
    {
        refresh_list();
        render_all();
        save_all();
        set_status("Imported");
    }
    else
        set_status("Import failed: unrecognised file");
}

#endif /* LLAMAC_ACTIONS_H */
