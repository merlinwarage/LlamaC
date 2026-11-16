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
static char *g_wait_prefill;
static Conv *g_wait_conv;
static int compact_maybe(Conv *c, int est, int threshold, int early); /* compact.h */
static void compact_cancel(void);
static int g_compact_early;
static volatile LONG g_ccancel;
static size_t g_job_chars;  /* request size of the running generation (token calibration) */
static char g_slot_conv[40]; /* chat whose KV cache is in server slot 0 (KV cache per chat) */
/* chats whose KV cache was saved on the server (data\kvslots.txt, one id per line): only these are restored */
static SB g_kv_ids;
static int g_kv_loaded;
/* The KV slot folder (G.kvdir, the server's --slot-save-path when it runs on this PC; relative = exe folder).
   With it, a restore is only asked for when the file exists, deleting a chat deletes its file, and old files are
   removed above G.kvmax GB. Only files named llamac-*.bin are ever deleted. 0 = no folder set / not found. */
static int kv_dir(wchar_t *out)
{
    wchar_t *w, base[MAX_PATH], tmp[MAX_PATH * 2];
    if (!G.kvdir[0])
        return 0;
    w = w_from(G.kvdir);
    if (PathIsRelativeW(w))
    {
        GetModuleFileNameW(NULL, base, MAX_PATH);
        {
            wchar_t *s = wcsrchr(base, L'\\');
            if (s)
                *s = 0;
        }
        _snwprintf(tmp, MAX_PATH * 2, L"%ls\\%ls", base, w);
    }
    else
        _snwprintf(tmp, MAX_PATH * 2, L"%ls", w);
    tmp[MAX_PATH * 2 - 1] = 0;
    free(w);
    if (!GetFullPathNameW(tmp, MAX_PATH, out, NULL))
        return 0;
    {
        DWORD a = GetFileAttributesW(out);
        return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
    }
}
/* the file the worker's slot_action() names for this chat (same character mapping) */
static int kv_file(const char *id, wchar_t *out)
{
    wchar_t dir[MAX_PATH];
    char name[120];
    size_t k = 0;
    if (!kv_dir(dir))
        return 0;
    k = (size_t)snprintf(name, sizeof name, "llamac-");
    for (const char *p = id; *p && k < 100; p++)
        name[k++] = (isalnum((unsigned char)*p) || *p == '-') ? *p : '_';
    snprintf(name + k, sizeof name - k, ".bin");
    {
        wchar_t *wn = w_from(name);
        _snwprintf(out, MAX_PATH, L"%ls\\%ls", dir, wn);
        out[MAX_PATH - 1] = 0;
        free(wn);
    }
    return 1;
}
static void kv_forget(const char *id);
/* keep the llamac-*.bin files under G.kvmax GB: oldest first, never the one just saved */
static void kv_prune(const char *keep_id)
{
    wchar_t dir[MAX_PATH], pat[MAX_PATH], keep[MAX_PATH] = L"", keep2[MAX_PATH] = L"";
    WIN32_FIND_DATAW fd;
    HANDLE h;
    struct
    {
        wchar_t name[MAX_PATH];
        unsigned long long size, time;
    } *f = NULL;
    int n = 0, cap = 0, removed = 0;
    unsigned long long total = 0, limit = (unsigned long long)G.kvmax << 30;
    if (!G.kvmax || !kv_dir(dir))
        return;
    if (keep_id)
        kv_file(keep_id, keep);
    if (g_slot_conv[0]) /* the chat going into the slot: the worker may be restoring its file right now */
        kv_file(g_slot_conv, keep2);
    _snwprintf(pat, MAX_PATH, L"%ls\\llamac-*.bin", dir);
    pat[MAX_PATH - 1] = 0;
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        if (n == cap)
        {
            cap = cap ? cap * 2 : 16;
            f = realloc(f, (size_t)cap * sizeof *f);
        }
        _snwprintf(f[n].name, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName);
        f[n].name[MAX_PATH - 1] = 0;
        f[n].size = ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        f[n].time = ((unsigned long long)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
        total += f[n].size;
        n++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    while (total > limit)
    {
        int old = -1;
        for (int i = 0; i < n; i++)
            if (f[i].size && _wcsicmp(f[i].name, keep) && _wcsicmp(f[i].name, keep2) && (old < 0 || f[i].time < f[old].time))
                old = i;
        if (old < 0)
            break;
        if (DeleteFileW(f[old].name))
        { /* the chat id is the file name between "llamac-" and ".bin" */
            wchar_t *b = wcsrchr(f[old].name, L'\\'), *dot = wcsrchr(f[old].name, L'.');
            if (b && dot && dot > b + 8)
            {
                char *u;
                *dot = 0;
                u = u_from(b + 8);
                kv_forget(u);
                free(u);
            }
            removed++;
        }
        total -= f[old].size;
        f[old].size = 0;
    }
    free(f);
    if (removed)
    {
        char st[160];
        snprintf(st, sizeof st, "KV slot folder: removed %d old saved cache(s) to stay under %d GB", removed, G.kvmax);
        set_status(st);
    }
}
/* is the chat in data\kvslots.txt? */
static int kv_listed(const char *id)
{
    size_t L = strlen(id);
    if (!g_kv_loaded)
    {
        wchar_t p[MAX_PATH];
        size_t n;
        char *b;
        g_kv_loaded = 1;
        data_path(L"kvslots.txt", p);
        b = (char *)read_file(p, &n);
        if (b)
        {
            sb_putc(&g_kv_ids, '\n');
            sb_add(&g_kv_ids, b, n);
            free(b);
        }
    }
    for (const char *q = sbs(&g_kv_ids); (q = strstr(q, id)) != NULL; q += L)
        if ((q == g_kv_ids.p || q[-1] == '\n') && (q[L] == '\n' || q[L] == '\r' || !q[L]))
            return 1;
    return 0;
}
/* can this chat's KV cache be restored? With the slot folder visible the file itself decides, else the list */
static int kv_known(const char *id)
{
    wchar_t f[MAX_PATH];
    if (kv_file(id, f))
        return GetFileAttributesW(f) != INVALID_FILE_ATTRIBUTES;
    return kv_listed(id);
}
static void kv_save_list(void)
{
    wchar_t p[MAX_PATH];
    const char *s = sbs(&g_kv_ids);
    while (*s == '\n')
        s++;
    data_path(L"kvslots.txt", p);
    write_atomic(p, s, strlen(s));
}
static void kv_mark(const char *id)
{
    kv_prune(id);
    if (!*id || kv_listed(id))
        return;
    if (g_kv_ids.n && g_kv_ids.p[g_kv_ids.n - 1] != '\n')
        sb_putc(&g_kv_ids, '\n');
    sb_puts(&g_kv_ids, id);
    sb_putc(&g_kv_ids, '\n');
    kv_save_list();
}
static void kv_forget(const char *id)
{
    SB o = {0};
    const char *s;
    if (!kv_listed(id))
        return;
    s = sbs(&g_kv_ids);
    for (;;)
    {
        const char *e = strchr(s, '\n');
        size_t L = e ? (size_t)(e - s) : strlen(s);
        if (L && !(L == strlen(id) && !strncmp(s, id, L)))
        {
            sb_add(&o, s, L);
            sb_putc(&o, '\n');
        }
        if (!e)
            break;
        s = e + 1;
    }
    free(g_kv_ids.p);
    g_kv_ids = o;
    kv_save_list();
}
/* a message sent while the model is busy waits here and is sent when the reply is complete */
static char *g_queue_text;
static Att g_queue_att[16];
static int g_queue_natt;
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
    return at < 0 ? -1 : g_last_prompt_n + g_last_pred_n + (int)(ch / g_cpt);
}
static int ctx_lim(void) { return G.ctx_limit > 0 ? G.ctx_limit : g_nctx; }
/* prefill: the answer starts with this text and the model continues it (llama-server assistant prefill) */
static void start_gen_px(int parent, int cont, const char *prefill)
{
    int lim = ctx_lim(), est;
    cur->leaf = parent;
    est = est_tokens(cur);
    if (g_compacting && g_compact_early && !g_ccancel && (lim <= 0 || est < 0 || est <= ctx_trigger(lim)))
        compact_cancel(); /* an early summary is not needed yet: the user's message goes first */
    if (!g_compacting && G.autocompact && lim > 0 && est > ctx_trigger(lim))
        /* a big tool result or attachment can push the next prompt over the limit before any generation
           reports its usage, so check the estimate before sending */
        compact_maybe(cur, est, ctx_trigger(lim), 0); /* tier 1, then a summary if still needed */
    if (g_compacting && !g_ccancel)
    {
        g_wait_gen = 1;
        g_wait_conv = cur;
        g_wait_parent = parent;
        g_wait_cont = cont;
        free(g_wait_prefill);
        g_wait_prefill = prefill ? xstrdup(prefill) : NULL;
        cur->leaf = parent;
        set_busy(1); /* Stop cancels the wait and the compression (do_send) */
        render_all();
        set_status("Waiting for context compression to finish (Stop cancels) ...");
        return;
    }
    g_last_prompt_n = g_last_pred_n = 0; /* only this generation's timings may trigger compression */
    if (!cont)
        g_toolround = 0;
    int a = node_add(cur, parent, ROLE_ASSIST);
    if (prefill)
        setstr(&cur->n[a].content, prefill);
    g_meas_conv = cur;
    g_meas_node = a;
    cur->leaf = a;
    Job *j = (Job *)calloc(1, sizeof *j);
    g_req_lim = lim;
    g_cpt = ms_cpt();
    j->body = build_request(cur, &G);
    g_job_chars = g_req_chars;
    j->base_off = prefill ? (int)strlen(prefill) : 0;
    gl[0] = strlen(cur->n[a].content); /* streamed text is appended after the prefill */
    gc[0] = gl[0] + 1;
    if (G.kvslot && strcmp(g_slot_conv, cur->id))
    { /* slot 0 holds another chat's cache: save it under that chat, load this chat's if it was ever saved */
        snprintf(j->save_id, sizeof j->save_id, "%s", g_slot_conv);
        if (kv_known(cur->id))
            snprintf(j->restore_id, sizeof j->restore_id, "%s", cur->id);
        snprintf(g_slot_conv, sizeof g_slot_conv, "%s", cur->id);
    }
    if (g_req_fit_elided || g_req_fit_clipped || g_req_fit_dropped)
    {
        char st[200];
        snprintf(st, sizeof st, "Fitted into the context: %d old tool result(s) shortened, %d clipped, %d message(s) left out",
                 g_req_fit_elided, g_req_fit_clipped, g_req_fit_dropped);
        set_status(st);
    }
    strncpy(j->url, G.url, 255);
    strncpy(j->key, G.key, 255);
    genConv = cur;
    genNode = a;
    genPhase = 0;
    g_stop = 0;
    gl[1] = 0;
    gc[1] = 1;
    set_busy(1);
    cur->mod = now_ms();
    render_all();
    if (!g_req_fit_elided && !g_req_fit_clipped && !g_req_fit_dropped)
        set_status(cont ? "Generating (after tool results)..." : "Generating...");
    HANDLE h = CreateThread(NULL, 0, worker, j, 0, NULL);
    if (h)
        CloseHandle(h);
}
static void start_gen_ex(int parent, int cont) { start_gen_px(parent, cont, NULL); }
static void start_gen(int parent) { start_gen_ex(parent, 0); }
/* Enter while the model is busy: keep the message (and the attachments) until the reply is complete */
static void act_queue(void)
{
    char *t = get_text(hInput);
    char *s = t;
    while (*s == ' ' || *s == '\r' || *s == '\n' || *s == '\t')
        s++;
    if (!*s && !npend)
    {
        free(t);
        return;
    }
    free(g_queue_text);
    g_queue_text = t;
    for (int i = 0; i < g_queue_natt; i++)
    {
        free(g_queue_att[i].name);
        free(g_queue_att[i].data);
        free(g_queue_att[i].mime);
    }
    memcpy(g_queue_att, pend, sizeof(Att) * (size_t)npend); /* the queue takes over the attachments */
    g_queue_natt = npend;
    npend = 0;
    SetWindowTextW(hInput, L"");
    set_status("Queued: your message is sent when the current reply is complete (Stop puts it back in the box).");
}
static void do_send(void);
/* called whenever the app becomes idle */
static void queue_flush(void)
{
    char *keep;
    wchar_t *w;
    if (busy || !g_queue_text || g_compacting)
        return;
    keep = get_text(hInput); /* whatever was typed meanwhile stays in the box */
    w = w_from(g_queue_text);
    SetWindowTextW(hInput, w);
    free(w);
    for (int i = 0; i < g_queue_natt && npend < 16; i++)
        pend[npend++] = g_queue_att[i];
    g_queue_natt = 0;
    free(g_queue_text);
    g_queue_text = NULL;
    do_send();
    w = w_from(keep);
    SetWindowTextW(hInput, w);
    free(w);
    free(keep);
}
static void do_send(void)
{
    if (busy)
    {
        if (g_wait_gen)
        {
            g_wait_gen = 0;
            free(g_wait_prefill);
            g_wait_prefill = NULL;
            compact_cancel();
            set_busy(0);
            set_status("Cancelled (the compression was stopped too)");
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
    { /* going back before tool calls that changed files: offer to restore those files */
        int *pp, np = conv_path(cur, &pp), nids = 0, at = -1;
        char **ids = (char **)calloc((size_t)np + 1, sizeof(char *));
        for (int i = 0; i < np; i++)
            if (pp[i] == u)
                at = i;
        for (int i = at < 0 ? np : at; i < np; i++)
            if (cur->n[pp[i]].role == ROLE_TOOL && cur->n[pp[i]].tcid)
                ids[nids++] = cur->n[pp[i]].tcid;
        if (nids)
        {
            int nf = cp_count(cur->id, ids, nids);
            if (nf > 0)
            {
                wchar_t q[300];
                _snwprintf(q, 300, L"The model changed %d file(s) after this message.\n\nRestore them to how they were before that message?\n\n(No keeps the files as they are.)", nf);
                q[299] = 0;
                if (MessageBoxW(hMain, q, L"llamac - restore files", MB_YESNO | MB_ICONQUESTION) == IDYES)
                {
                    int done = 0, bad = cp_restore(cur->id, ids, nids, &done);
                    char st[160];
                    snprintf(st, sizeof st, bad ? "Restored %d file change(s), %d failed" : "Restored %d file change(s)", done, bad);
                    set_status(st);
                }
            }
        }
        free(ids);
        free(pp);
    }
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
    if (!busy)
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
    cp_delete_conv(cur->id);
    {
        wchar_t kf[MAX_PATH];
        if (kv_file(cur->id, kf)) /* the server has no API for this: only possible with the folder set */
            DeleteFileW(kf);
    }
    kv_forget(cur->id);
    if (!strcmp(g_slot_conv, cur->id))
        g_slot_conv[0] = 0;
    conv_free(cur);
    cur = NULL;
    if (!nconv)
        convs_add(conv_new());
    qsort(convs, (size_t)nconv, sizeof(Conv *), cmp_mod);
    select_conv(convs[0]);
    save_all();
}
/* ---------- chat list context menu: rename / copy original prompt ---------- */
static wchar_t g_ren_buf[256];
static int g_ren_done, g_ren_ok;
static LRESULT CALLBACK RenProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_COMMAND:
        if (HIWORD(w) != BN_CLICKED)
            break; /* edit-control notifications (EN_*) must not close the dialog */
        if (LOWORD(w) == IDOK)
        {
            GetWindowTextW(GetDlgItem(h, 100), g_ren_buf, 256);
            g_ren_ok = 1;
            g_ren_done = 1;
            return 0;
        }
        if (LOWORD(w) == IDCANCEL)
        {
            g_ren_done = 1;
            return 0;
        }
        break;
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)w, cFg);
        SetBkColor((HDC)w, cSurf);
        return (LRESULT)hbrSurf;
    case WM_CLOSE:
        g_ren_done = 1;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
/* modal single-line input; returns a malloc'ed UTF-8 string, or NULL when cancelled */
static char *ask_name(const char *init)
{
    static int reg = 0;
    if (!reg)
    {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof wc);
        wc.cbSize = sizeof wc;
        wc.lpfnWndProc = RenProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = hbrBg;
        wc.lpszClassName = L"LCRen";
        RegisterClassExW(&wc);
        reg = 1;
    }
    RECT r;
    GetWindowRect(hMain, &r);
    HWND h = CreateWindowExW(WS_EX_DLGMODALFRAME, L"LCRen", L"Rename chat", WS_POPUP | WS_CAPTION | WS_SYSMENU, r.left + 120, r.top + 120, 420, 130, hMain, NULL, GetModuleHandleW(NULL), NULL);
    wchar_t *wi = w_from(init);
    HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", wi, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 12, 12, 380, 24, h, (HMENU)100, NULL, NULL);
    HWND b1 = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 222, 48, 80, 26, h, (HMENU)(INT_PTR)IDOK, NULL, NULL);
    HWND b2 = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 312, 48, 80, 26, h, (HMENU)(INT_PTR)IDCANCEL, NULL, NULL);
    free(wi);
    SendMessageW(e, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(b1, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(b2, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(e, EM_SETLIMITTEXT, 200, 0);
    apply_frame(h);
    g_ren_done = g_ren_ok = 0;
    EnableWindow(hMain, FALSE);
    ShowWindow(h, SW_SHOW);
    SetFocus(e);
    SendMessageW(e, EM_SETSEL, 0, -1);
    MSG msg;
    while (!g_ren_done && GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        if (msg.message == WM_KEYDOWN && (msg.wParam == VK_RETURN || msg.wParam == VK_ESCAPE) && GetAncestor(msg.hwnd, GA_ROOT) == h)
        {
            SendMessageW(h, WM_COMMAND, msg.wParam == VK_RETURN ? IDOK : IDCANCEL, 0);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(hMain, TRUE);
    DestroyWindow(h);
    SetForegroundWindow(hMain);
    return g_ren_ok ? u_from(g_ren_buf) : NULL;
}
/* ---------- multi-line text editor (edit a reply before continuing it) ---------- */
static int g_atx_done, g_atx_ok;
static char *g_atx_out;
static LRESULT CALLBACK AskTxtProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_SIZE:
    {
        int W = LOWORD(l), H = HIWORD(l);
        MoveWindow(GetDlgItem(h, 100), 10, 10, W - 20, H - 62, TRUE);
        MoveWindow(GetDlgItem(h, IDOK), W - 300, H - 42, 190, 32, TRUE);
        MoveWindow(GetDlgItem(h, IDCANCEL), W - 100, H - 42, 90, 32, TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (HIWORD(w) != BN_CLICKED)
            break;
        if (LOWORD(w) == IDOK)
        {
            g_atx_out = get_text(GetDlgItem(h, 100));
            g_atx_ok = 1;
            g_atx_done = 1;
            return 0;
        }
        if (LOWORD(w) == IDCANCEL)
        {
            g_atx_done = 1;
            return 0;
        }
        break;
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)w, cFg);
        SetBkColor((HDC)w, cSurf);
        return (LRESULT)hbrSurf;
    case WM_CLOSE:
        g_atx_done = 1;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
/* returns the edited text (malloc'ed UTF-8, \n line ends) or NULL when cancelled */
static char *ask_text(const wchar_t *title, const wchar_t *okLabel, const char *init)
{
    static int reg = 0;
    RECT r;
    HWND h, e;
    MSG msg;
    SB c = {0};
    wchar_t *wi;
    if (!reg)
    {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof wc);
        wc.cbSize = sizeof wc;
        wc.lpfnWndProc = AskTxtProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = hbrBg;
        wc.lpszClassName = L"LCAskTxt";
        RegisterClassExW(&wc);
        reg = 1;
    }
    GetWindowRect(hMain, &r);
    h = CreateWindowExW(WS_EX_DLGMODALFRAME, L"LCAskTxt", title, WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
                        r.left + 80, r.top + 70, 820, 560, hMain, NULL, GetModuleHandleW(NULL), NULL);
    for (const char *p = init; *p; p++)
    {
        if (*p == '\r')
            continue;
        if (*p == '\n')
            sb_putc(&c, '\r');
        sb_putc(&c, *p);
    }
    wi = w_from(sbs(&c));
    free(c.p);
    e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", wi, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                        10, 10, 780, 450, h, (HMENU)100, NULL, NULL);
    free(wi);
    SendMessageW(e, EM_SETLIMITTEXT, 0, 0);
    SendMessageW(e, WM_SETFONT, (WPARAM)hFont, TRUE);
    {
        HWND b1 = CreateWindowExW(0, L"BUTTON", okLabel, WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 10, h, (HMENU)(INT_PTR)IDOK, NULL, NULL);
        HWND b2 = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 10, h, (HMENU)(INT_PTR)IDCANCEL, NULL, NULL);
        SendMessageW(b1, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(b2, WM_SETFONT, (WPARAM)hFont, TRUE);
    }
    {
        RECT cr;
        GetClientRect(h, &cr);
        SendMessageW(h, WM_SIZE, 0, MAKELPARAM(cr.right, cr.bottom));
    }
    apply_frame(h);
    g_atx_done = g_atx_ok = 0;
    g_atx_out = NULL;
    EnableWindow(hMain, FALSE);
    ShowWindow(h, SW_SHOW);
    SetFocus(e);
    SendMessageW(e, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    SendMessageW(e, EM_SCROLLCARET, 0, 0);
    while (!g_atx_done && GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE && GetAncestor(msg.hwnd, GA_ROOT) == h)
        {
            g_atx_done = 1;
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(hMain, TRUE);
    DestroyWindow(h);
    SetForegroundWindow(hMain);
    if (!g_atx_ok)
        return NULL;
    {
        char *d = g_atx_out, *q = g_atx_out;
        for (; *q; q++)
            if (*q != '\r')
                *d++ = *q;
        *d = 0;
    }
    return g_atx_out;
}
/* Edit the last reply and let the model continue from the edited text (a new branch beside the original).
   Correcting one wrong step is cheaper than regenerating, and works well with small models. Also continues
   a reply that was cut off at max tokens. */
static void act_edit_reply(void)
{
    int a = -1;
    char *t;
    if (busy || !cur)
        return;
    for (int i = cur->leaf; i >= 0 && cur->n[i].role != ROLE_ROOT; i = cur->n[i].parent)
        if (cur->n[i].role == ROLE_ASSIST)
        {
            a = i;
            break;
        }
    if (a < 0 || (cur->n[a].tcalls && *cur->n[a].tcalls) || cur->n[a].parent < 0)
    {
        set_status("No reply to edit (replies that call tools cannot be continued)");
        return;
    }
    t = ask_text(L"Edit the reply - the model continues from the end of this text", L"Continue from here", cur->n[a].content);
    if (!t)
        return;
    {
        size_t L = strlen(t);
        while (L && (t[L - 1] == ' ' || t[L - 1] == '\n' || t[L - 1] == '\t'))
            t[--L] = 0; /* trailing whitespace would make the next token start oddly */
    }
    if (!*t)
    {
        free(t);
        return;
    }
    start_gen_px(cur->n[a].parent, 0, t);
    free(t);
}
/* Confidence view: the user picked another token for an uncertain position. The answer is kept up to that token,
   the alternative is put in its place, and the model continues from there as a new branch. */
static void act_branch_token(int node, int tok, const char *alt)
{
    Node *m;
    SB pre = {0};
    if (busy || !cur || node < 0 || node >= cur->cnt)
        return;
    m = &cur->n[node];
    if (tok < 0 || tok >= m->ntk || m->parent < 0 || m->tk[tok].off > (int)strlen(m->content))
        return;
    sb_add(&pre, m->content, (size_t)m->tk[tok].off);
    sb_puts(&pre, alt);
    start_gen_px(m->parent, 0, sbs(&pre));
    free(pre.p);
}
/* the prompt that started the chat: the first user message, or the one stored in a compression summary */
static const char *conv_orig_prompt(Conv *c)
{
    for (int i = c->leaf; i >= 0; i = c->n[i].parent)
        if (c->n[i].parent == c->root || c->n[i].parent < 0)
        {
            if (c->n[i].role == ROLE_SYS)
                return c->n[i].reasoning;
            return c->n[i].role == ROLE_USER ? c->n[i].content : "";
        }
    return "";
}
static void clip_put(const char *utf8, const char *okmsg)
{
    SB s = {0};
    for (const char *p = utf8; *p; p++)
    {
        if (*p == '\r')
            continue;
        if (*p == '\n')
            sb_putc(&s, '\r');
        sb_putc(&s, *p);
    }
    wchar_t *w = w_from(sbs(&s));
    size_t sz = (wcslen(w) + 1) * 2;
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, sz);
    if (g && OpenClipboard(hMain))
    {
        memcpy(GlobalLock(g), w, sz);
        GlobalUnlock(g);
        EmptyClipboard();
        SetClipboardData(CF_UNICODETEXT, g);
        CloseClipboard();
        set_status(okmsg);
    }
    else if (g)
        GlobalFree(g);
    free(w);
    free(s.p);
}
enum
{
    IDM_RENAME = 9001,
    IDM_COPYPROMPT
};
static void conv_menu(int item, int x, int y)
{
    Conv *c;
    HMENU m;
    int cmd;
    if (item < 0 || item >= nmap)
        return;
    c = convs[lmap[item]];
    m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, IDM_RENAME, L"Rename...");
    AppendMenuW(m, MF_STRING | (conv_orig_prompt(c)[0] ? 0 : MF_GRAYED), IDM_COPYPROMPT, L"Copy original prompt");
    cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, 0, hMain, NULL);
    DestroyMenu(m);
    if (cmd == IDM_COPYPROMPT)
        clip_put(conv_orig_prompt(c), "Original prompt copied");
    else if (cmd == IDM_RENAME)
    {
        char *nm = ask_name(c->name);
        if (nm)
        {
            char *t = nm;
            size_t l;
            while (*t == ' ')
                t++;
            l = strlen(t);
            while (l && t[l - 1] == ' ')
                t[--l] = 0;
            if (*t)
            {
                setstr(&c->name, t);
                c->dirty = 1;
                refresh_list();
                save_all();
            }
            free(nm);
        }
    }
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
