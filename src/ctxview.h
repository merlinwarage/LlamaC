/* ctxview.h - "What is in the context" window: token counts per part of the request (server tokenizer),
   the largest tool results, per-model statistics, and buttons to shorten old tool results or compress now. */
#ifndef LLAMAC_CTXVIEW_H
#define LLAMAC_CTXVIEW_H

enum
{
    IDC_CV_TEXT = 100,
    IDC_CV_ELIDE,
    IDC_CV_COMPRESS,
    IDC_CV_CLOSE
};
static HWND hCtxView;
static void ctxview_fill(HWND e);

/* tokens in s by the server's tokenizer; falls back to the calibrated estimate when /tokenize fails */
static int count_tokens(const char *s, int *exact)
{
    SB b = {0};
    char *r;
    int n = -1;
    if (!*s)
        return 0;
    sb_puts(&b, "{\"content\":");
    sb_json(&b, s);
    sb_puts(&b, ",\"add_special\":false}");
    r = http_req_t(L"POST", "/tokenize", b.p, 30000);
    free(b.p);
    if (r)
    {
        J *j = json_parse(r);
        J *t = j_get(j, "tokens");
        if (t && t->t == J_ARR)
        {
            n = 0;
            for (J *c = t->child; c; c = c->next)
                n++;
        }
        j_free(j);
        free(r);
    }
    if (n < 0)
    {
        *exact = 0;
        n = (int)(strlen(s) / g_cpt);
    }
    return n;
}
static void cv_bar(SB *o, const char *label, int tok, int total, const char *note)
{
    int w = total > 0 ? (int)((long long)tok * 30 / total) : 0;
    sb_printf(o, "%-20s %8d  ", label, tok);
    for (int i = 0; i < 30; i++)
        sb_puts(o, i < w ? "\xE2\x96\x88" : (i == 0 && tok > 0 ? "\xE2\x96\x8F" : " "));
    if (note && *note)
    {
        sb_puts(o, "  ");
        sb_puts(o, note);
    }
    sb_putc(o, '\n');
}
static void ctxview_fill(HWND e)
{
    SB o = {0}, sys = {0}, usr = {0}, asst = {0}, tcall = {0}, tres = {0}, att = {0}, summ = {0};
    int *p, n, lim = G.ctx_limit > 0 ? G.ctx_limit : g_nctx, exact = 1, ntool = 0, nel = 0, ntl = 0;
    char *tl, *dig, note[160];
    int big[5] = {-1, -1, -1, -1, -1};
    int t_sys, t_dig, t_sum, t_tools, t_usr, t_asst, t_tcall, t_tres, t_att, total;
    if (!cur)
        return;
    set_status("Counting tokens with the server's tokenizer ...");
    n = conv_path(cur, &p);
    sb_puts(&sys, G.sys);
    dig = (g_dig_conv == cur && g_dig_txt) ? xstrdup(g_dig_txt) : mem_digest_for(n ? cur->n[p[0]].content : "");
    tl = tools_schema_build();
    if (tl)
        for (const char *q = tl; *q; q++)
            ntl += !strncmp(q, "\"function\":{", 12);
    for (int i = 0; i < n; i++)
    {
        Node *m = &cur->n[p[i]];
        if (m->role == ROLE_SYS)
        {
            sb_puts(&summ, m->content);
            sb_puts(&summ, m->reasoning);
            continue;
        }
        if (m->role == ROLE_USER)
            sb_puts(&usr, m->content);
        else if (m->role == ROLE_ASSIST)
        {
            sb_puts(&asst, m->content);
            if (m->tcalls)
                sb_puts(&tcall, m->tcalls);
        }
        else if (m->role == ROLE_TOOL)
        {
            ntool++;
            if (m->elided)
            {
                SB t = {0};
                nel++;
                tool_stub(&t, cur, p, i);
                sb_puts(&tres, sbs(&t));
                free(t.p);
            }
            else
            {
                sb_puts(&tres, m->content);
                for (int k = 0; k < 5; k++)
                    if (big[k] < 0 || strlen(m->content) > strlen(cur->n[big[k]].content))
                    {
                        memmove(big + k + 1, big + k, (size_t)(4 - k) * sizeof(int));
                        big[k] = p[i];
                        break;
                    }
            }
        }
        for (int a = 0; a < m->na; a++)
            if (m->att[a].kind == ATT_TEXT)
                sb_puts(&att, m->att[a].data);
    }
    t_sys = count_tokens(sbs(&sys), &exact) + (G.remind[0] ? count_tokens(G.remind, &exact) : 0);
    t_dig = dig ? count_tokens(dig, &exact) : 0;
    t_sum = count_tokens(sbs(&summ), &exact);
    t_tools = tl ? count_tokens(tl, &exact) : 0;
    t_usr = count_tokens(sbs(&usr), &exact);
    t_asst = count_tokens(sbs(&asst), &exact);
    t_tcall = count_tokens(sbs(&tcall), &exact);
    t_tres = count_tokens(sbs(&tres), &exact);
    t_att = count_tokens(sbs(&att), &exact);
    total = t_sys + t_dig + t_sum + t_tools + t_usr + t_asst + t_tcall + t_tres + t_att;
    sb_printf(&o, "Context of \"%.80s\"  -  %s\r\n\r\n", cur->name, exact ? "counted by the server's tokenizer" : "ESTIMATED (/tokenize failed)");
    if (lim > 0)
        sb_printf(&o, "Context size %d   |   compression starts at %d   |   reply reserve %d\r\n\r\n", lim, ctx_trigger(lim), reply_reserve(&G, lim));
    else
        sb_puts(&o, "Context size unknown (server not reachable?)\r\n\r\n");
    cv_bar(&o, "System prompt", t_sys, total, G.remind[0] ? "incl. reminder" : "");
    cv_bar(&o, "Memory digest", t_dig, total, "");
    cv_bar(&o, "Summary", t_sum, total, "");
    snprintf(note, sizeof note, "%d tool(s), tool set: %s", ntl, G.toolset == 2 ? "none" : G.toolset == 1 ? "chat" : "all");
    cv_bar(&o, "Tool definitions", t_tools, total, note);
    cv_bar(&o, "User messages", t_usr, total, "");
    cv_bar(&o, "Assistant replies", t_asst, total, "");
    cv_bar(&o, "Tool calls", t_tcall, total, "");
    snprintf(note, sizeof note, "%d result(s), %d already shortened", ntool, nel);
    cv_bar(&o, "Tool results", t_tres, total, note);
    cv_bar(&o, "Attachments (text)", t_att, total, "");
    sb_puts(&o, "--------------------------------------------------------------------\r\n");
    sb_printf(&o, "%-20s %8d", "Total content", total);
    if (lim > 0)
        sb_printf(&o, "   (%d%% of the context)", (int)((long long)total * 100 / lim));
    sb_puts(&o, "\r\nThe chat template adds a few tokens per message on top of this.\r\n");
    if (cur == g_meas_conv && g_last_prompt_n > 0)
        sb_printf(&o, "Last request measured by the server: %d prompt + %d generated tokens.\r\n", g_last_prompt_n, g_last_pred_n);
    if (big[0] >= 0)
    {
        sb_puts(&o, "\r\nLargest tool results still sent in full:\r\n");
        for (int k = 0; k < 5 && big[k] >= 0; k++)
        {
            Node *m = &cur->n[big[k]];
            sb_printf(&o, "  ~%6d tok  %s\r\n", (int)(strlen(m->content) / g_cpt), m->tname ? m->tname : "tool");
        }
    }
    sb_puts(&o, "\r\nModel: ");
    sb_puts(&o, G.model[0] ? G.model : "(none selected)");
    sb_puts(&o, "\r\n");
    {
        SB r = {0};
        ms_report(&r);
        for (const char *q = sbs(&r); *q; q++)
        {
            if (*q == '\n')
                sb_putc(&o, '\r');
            sb_putc(&o, *q);
        }
        free(r.p);
    }
    {
        wchar_t *w = w_from(sbs(&o));
        SetWindowTextW(e, w);
        free(w);
    }
    set_status(exact ? "Context counted" : "Context estimated (the server's /tokenize did not answer)");
    free(o.p);
    free(sys.p);
    free(usr.p);
    free(asst.p);
    free(tcall.p);
    free(tres.p);
    free(att.p);
    free(summ.p);
    free(dig);
    free(tl);
    free(p);
}
static LRESULT CALLBACK CtxProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_SIZE:
    {
        int W = LOWORD(l), H = HIWORD(l);
        MoveWindow(GetDlgItem(h, IDC_CV_TEXT), 10, 10, W - 20, H - 62, TRUE);
        MoveWindow(GetDlgItem(h, IDC_CV_ELIDE), 10, H - 42, 220, 32, TRUE);
        MoveWindow(GetDlgItem(h, IDC_CV_COMPRESS), 240, H - 42, 150, 32, TRUE);
        MoveWindow(GetDlgItem(h, IDC_CV_CLOSE), W - 100, H - 42, 90, 32, TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (HIWORD(w) != BN_CLICKED)
            break;
        if (LOWORD(w) == IDC_CV_ELIDE && cur)
        {
            size_t s = compact_elide(cur);
            char st[160];
            if (s && cur == g_meas_conv && g_last_prompt_n > (int)(s / g_cpt))
                g_last_prompt_n -= (int)(s / g_cpt);
            snprintf(st, sizeof st, s ? "Old tool results shortened (about %d tokens)" : "No old tool results to shorten (only results before your latest message are)", (int)(s / g_cpt));
            if (s)
                save_all();
            ctxview_fill(GetDlgItem(h, IDC_CV_TEXT));
            set_status(st);
            InvalidateRect(hMain, NULL, FALSE);
        }
        else if (LOWORD(w) == IDC_CV_COMPRESS && cur)
        {
            if (busy || g_compacting)
                set_status("Busy - try again when the current reply or compression has finished");
            else
            {
                int on = G.autocompact;
                G.autocompact = 1; /* an explicit request works even with auto-compression off */
                g_clast_fail = 0;
                compact_start(cur, 0);
                G.autocompact = on;
                if (!g_compacting)
                    set_status("Nothing to compress yet (the chat needs at least 8 messages)");
                DestroyWindow(h);
            }
        }
        else if (LOWORD(w) == IDC_CV_CLOSE)
            DestroyWindow(h);
        return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)w, cFg);
        SetBkColor((HDC)w, cSurf);
        return (LRESULT)hbrSurf;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        hCtxView = NULL;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static void act_context(void)
{
    static int reg = 0;
    static HFONT mono;
    RECT r;
    HWND e;
    if (hCtxView)
    {
        ctxview_fill(GetDlgItem(hCtxView, IDC_CV_TEXT));
        SetForegroundWindow(hCtxView);
        return;
    }
    if (!reg)
    {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof wc);
        wc.cbSize = sizeof wc;
        wc.lpfnWndProc = CtxProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = hbrBg;
        wc.lpszClassName = L"LCCtx";
        RegisterClassExW(&wc);
        mono = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Consolas");
        reg = 1;
    }
    GetWindowRect(hMain, &r);
    hCtxView = CreateWindowExW(0, L"LCCtx", L"What is in the context", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                               r.left + 100, r.top + 80, 860, 620, hMain, NULL, GetModuleHandleW(NULL), NULL);
    e = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                        0, 0, 10, 10, hCtxView, (HMENU)IDC_CV_TEXT, NULL, NULL);
    SendMessageW(e, WM_SETFONT, (WPARAM)mono, TRUE);
    {
        HWND b1 = CreateWindowExW(0, L"BUTTON", L"Shorten old tool results", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 10, hCtxView, (HMENU)IDC_CV_ELIDE, NULL, NULL);
        HWND b2 = CreateWindowExW(0, L"BUTTON", L"Compress now", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 10, hCtxView, (HMENU)IDC_CV_COMPRESS, NULL, NULL);
        HWND b3 = CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 10, hCtxView, (HMENU)IDC_CV_CLOSE, NULL, NULL);
        SendMessageW(b1, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(b2, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(b3, WM_SETFONT, (WPARAM)hFont, TRUE);
    }
    apply_frame(hCtxView);
    {
        RECT cr;
        GetClientRect(hCtxView, &cr);
        SendMessageW(hCtxView, WM_SIZE, 0, MAKELPARAM(cr.right, cr.bottom));
    }
    ctxview_fill(e);
}

#endif /* LLAMAC_CTXVIEW_H */
