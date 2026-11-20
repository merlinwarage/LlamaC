/* tools_dlg.h - "Built-in tools" dialog: switch single built-in tools off to save tokens and avoid needless calls.
   The off list (G.tooloff, "|name|name|") is saved in settings and in profiles. Switched-off tools are not sent to the
   model; a call to one anyway (e.g. remembered from earlier in the chat) is refused. MCP tools: the MCP button.
   chk_dialog() is the shared check-list window; the Skills dialog (skills.h) uses it too. */
#ifndef LLAMAC_TOOLS_DLG_H
#define LLAMAC_TOOLS_DLG_H

#define TD_MAX 64
#define IDC_TD_CHK 4000  /* + row: checkbox */
#define IDC_TD_DESC 4100 /* + row: description */
#define IDC_TD_TOK 4200  /* + row: token estimate */
#define IDC_TD_ALLON 3901
#define IDC_TD_ALLOFF 3902
#define IDC_TD_TOTAL 3903
#define IDC_TD_INFO 3904
typedef struct
{
    char name[72];
    char desc[160];
    int tok;
    int inset; /* allowed by the tool set; rows it excludes are greyed out and keep their tick */
    char *tip; /* hover text (malloc'ed, may be NULL) */
} TdRow;
/* what differs between the check-list dialogs */
typedef struct
{
    const wchar_t *title;
    const char *info;  /* one or two lines above the rows */
    const char *empty; /* shown instead of rows */
    const char *what;  /* plural noun for the total line, e.g. "built-in tools" */
    int liston;        /* 1: the list holds the ticked names, 0: the unticked ones */
    int base;          /* tokens added once when at least one row is ticked */
} ChkSpec;
static TdRow td_rows[TD_MAX];
static int td_chk[TD_MAX]; /* tick state of the owner-drawn checkboxes */
static int td_n, td_done, td_ok;
static char *td_list;
static size_t td_cap;
static const ChkSpec *td_spec;

static void tip_add2(HWND tt, HWND owner, HWND ctl, const char *tip)
{
    wchar_t *w;
    TOOLINFOW ti;
    if (!tt || !ctl || !tip)
        return;
    w = w_from(tip);
    memset(&ti, 0, sizeof ti);
    ti.cbSize = TTTOOLINFOW_V2_SIZE;
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = owner;
    ti.uId = (UINT_PTR)ctl;
    ti.lpszText = w;
    SendMessageW(tt, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    free(w);
}
/* the tokens a tool definition roughly costs in every request, at the model's calibrated characters per token */
static int tool_tokens(const Tool *t, double cpt)
{
    return (int)((strlen(t->name) + strlen(t->desc) + strlen(t->schema) + 30) / cpt);
}
static void td_total(HWND h)
{
    int on = 0, tok = 0, n = 0;
    char s[300];
    wchar_t *w;
    for (int i = 0; i < td_n; i++)
        if (td_rows[i].inset)
        {
            n++;
            if (td_chk[i])
            {
                on++;
                tok += td_rows[i].tok;
            }
        }
    if (on)
        tok += td_spec->base;
    if (!td_spec->liston)
        n = td_n; /* tools: greyed-out rows are counted, as before */
    snprintf(s, sizeof s, "Sent: %d of %d %s, about %d tokens in every request%s", on, n, td_spec->what, tok,
             ms_known() ? "" : " (rough: measured after the first reply with this model)");
    w = w_from(s);
    SetDlgItemTextW(h, IDC_TD_TOTAL, w);
    free(w);
}
/* owner-drawn checkbox: box border green = ticked and sent, red = unticked, grey = not in the tool set (tick kept) */
static void td_draw_chk(DRAWITEMSTRUCT *d, int i)
{
    HDC dc = d->hDC;
    RECT r = d->rcItem, box, tr;
    int sz = 16, inset = td_rows[i].inset, on = td_chk[i];
    COLORREF line = !inset ? cDim : on ? cStr : cErr;
    wchar_t txt[80];
    HGDIOBJ of;
    FillRect(dc, &r, hbrPanel);
    SetRect(&box, r.left + 1, r.top + (r.bottom - r.top - sz) / 2, r.left + 1 + sz, r.top + (r.bottom - r.top - sz) / 2 + sz);
    rrect(dc, &box, 5, cPanel, line);
    InflateRect(&box, -1, -1); /* 2 px border */
    rrect(dc, &box, 4, cPanel, line);
    if (on)
    {
        HPEN p = CreatePen(PS_SOLID, 2, line);
        HGDIOBJ op = SelectObject(dc, p);
        MoveToEx(dc, box.left + 3, box.top + 7, NULL);
        LineTo(dc, box.left + 6, box.top + 10);
        LineTo(dc, box.left + 11, box.top + 3);
        SelectObject(dc, op);
        DeleteObject(p);
    }
    GetWindowTextW(d->hwndItem, txt, 80);
    of = SelectObject(dc, hFont);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, inset ? cFg : cDim);
    SetRect(&tr, r.left + sz + 10, r.top, r.right, r.bottom);
    DrawTextW(dc, txt, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    if ((d->itemState & ODS_FOCUS) && !(d->itemState & ODS_NOFOCUSRECT))
    {
        DrawTextW(dc, txt, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_CALCRECT);
        OffsetRect(&tr, 0, (r.bottom - r.top - (tr.bottom - tr.top)) / 2);
        InflateRect(&tr, 2, 1);
        DrawFocusRect(dc, &tr);
    }
    SelectObject(dc, of);
}
static LRESULT CALLBACK TdProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_ERASEBKGND:
    {
        RECT r;
        GetClientRect(h, &r);
        FillRect((HDC)w, &r, hbrPanel);
        return 1;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    {
        int id = GetDlgCtrlID((HWND)l);
        SetTextColor((HDC)w, (id >= IDC_TD_DESC && id < IDC_TD_TOK + TD_MAX) || id == IDC_TD_INFO ? cDim : cFg);
        SetBkColor((HDC)w, cPanel);
        return (LRESULT)hbrPanel;
    }
    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT *d = (DRAWITEMSTRUCT *)l;
        UINT oid = d->CtlID;
        if (oid >= IDC_TD_CHK && oid < (UINT)(IDC_TD_CHK + td_n))
        {
            td_draw_chk(d, (int)(oid - IDC_TD_CHK));
            return TRUE;
        }
        if (oid == IDC_TD_ALLON || oid == IDC_TD_ALLOFF)
            d->CtlID = IDC_RESET; /* same look as the settings dialog's secondary buttons */
        draw_btn(d);
        d->CtlID = oid;
        return TRUE;
    }
    case WM_COMMAND:
    {
        int id = LOWORD(w);
        if (id >= IDC_TD_CHK && id < IDC_TD_CHK + td_n)
        {
            if (HIWORD(w) == BN_CLICKED || HIWORD(w) == BN_DOUBLECLICKED)
            {
                td_chk[id - IDC_TD_CHK] ^= 1;
                InvalidateRect((HWND)l, NULL, FALSE);
                td_total(h);
            }
        }
        else if (id == IDC_TD_ALLON || id == IDC_TD_ALLOFF)
        { /* greyed-out rows (not in the tool set) keep their tick */
            for (int i = 0; i < td_n; i++)
                if (td_rows[i].inset)
                {
                    td_chk[i] = id == IDC_TD_ALLON;
                    InvalidateRect(GetDlgItem(h, IDC_TD_CHK + i), NULL, FALSE);
                }
            td_total(h);
        }
        else if (id == IDOK)
        { /* names not listed now (their setting is off) keep their state; greyed-out rows cannot change, and a
             greyed-out duplicate (a skill hidden by the project's one) must not overwrite the shown row */
            for (int i = 0; i < td_n; i++)
                if (td_rows[i].inset)
                    name_list_set(td_list, td_cap, td_rows[i].name, td_spec->liston ? td_chk[i] : !td_chk[i]);
            td_ok = 1;
            td_done = 1;
        }
        else if (id == IDCANCEL)
            td_done = 1;
        return 0;
    }
    case DM_GETDEFID:
        return MAKELONG(IDOK, DC_HASDEFID);
    case WM_CLOSE:
        td_done = 1;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
/* the check-list window over td_rows[0..td_n); edits list in place (see ChkSpec.liston); 1 = OK pressed. Frees the
   rows' tips. */
static int chk_dialog(HWND owner, const ChkSpec *spec, char *list, size_t cap)
{
    static int reg = 0;
    const int rowH = 28, colW = 560, nameW = 180, tokW = 80, top = 62;
    int cols, rows, CW, CH;
    RECT wr, orr;
    HWND h, tt;
    DWORD st = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    MSG msg;
    if (!reg)
    {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof wc);
        wc.cbSize = sizeof wc;
        wc.lpfnWndProc = TdProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.lpszClassName = L"LCTools";
        RegisterClassExW(&wc);
        reg = 1;
    }
    cols = td_n > 12 ? 2 : 1;
    rows = (td_n + cols - 1) / cols;
    if (rows < 1)
        rows = 1;
    CW = 20 + cols * colW + 10;
    CH = top + rows * rowH + 110;
    SetRect(&wr, 0, 0, CW, CH);
    AdjustWindowRectEx(&wr, st, FALSE, WS_EX_DLGMODALFRAME);
    GetWindowRect(owner, &orr);
    h = CreateWindowExW(WS_EX_DLGMODALFRAME, L"LCTools", spec->title, st, orr.left + 40, orr.top + 40, wr.right - wr.left, wr.bottom - wr.top,
                        owner, NULL, GetModuleHandleW(NULL), NULL);
    apply_frame(h);
    tt = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT,
                         CW_USEDEFAULT, CW_USEDEFAULT, h, NULL, NULL, NULL);
    if (tt)
    {
        SendMessageW(tt, TTM_SETMAXTIPWIDTH, 0, 420);
        SendMessageW(tt, TTM_SETDELAYTIME, TTDT_AUTOPOP, 30000);
    }
    {
        wchar_t *wl = w_from(spec->info);
        HWND s = CreateWindowExW(0, L"STATIC", wl, WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 20, 12, CW - 40, 40, h, (HMENU)IDC_TD_INFO, NULL, NULL);
        SendMessageW(s, WM_SETFONT, (WPARAM)hFont, TRUE);
        free(wl);
    }
    for (int i = 0; i < td_n; i++)
    {
        int c = i / rows, r = i % rows, x = 20 + c * colW, y = top + r * rowH;
        wchar_t *wn = w_from(td_rows[i].name), *wd = w_from(td_rows[i].desc), tk[24];
        HWND cb = CreateWindowExW(0, L"BUTTON", wn, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, x, y, nameW, 24, h, (HMENU)(INT_PTR)(IDC_TD_CHK + i), NULL, NULL);
        HWND ds = CreateWindowExW(0, L"STATIC", wd, WS_CHILD | WS_VISIBLE | SS_NOPREFIX | SS_ENDELLIPSIS | SS_NOTIFY, x + nameW + 6, y + 4, colW - nameW - tokW - 20, 20, h,
                                  (HMENU)(INT_PTR)(IDC_TD_DESC + i), NULL, NULL);
        HWND ts;
        _snwprintf(tk, 24, L"~%d tok", td_rows[i].tok);
        tk[23] = 0;
        ts = CreateWindowExW(0, L"STATIC", tk, WS_CHILD | WS_VISIBLE | SS_RIGHT, x + colW - tokW - 14, y + 4, tokW, 20, h, (HMENU)(INT_PTR)(IDC_TD_TOK + i), NULL, NULL);
        td_chk[i] = spec->liston ? name_in_list(list, td_rows[i].name) : !name_in_list(list, td_rows[i].name);
        if (!td_rows[i].inset)
        {
            EnableWindow(cb, FALSE);
            EnableWindow(ts, FALSE);
        }
        SendMessageW(cb, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(ds, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(ts, WM_SETFONT, (WPARAM)hFont, TRUE);
        if (tt && td_rows[i].tip)
        { /* the full description on hover */
            tip_add2(tt, h, cb, td_rows[i].tip);
            tip_add2(tt, h, ds, td_rows[i].tip);
        }
        free(wn);
        free(wd);
    }
    if (!td_n)
    {
        wchar_t *we = w_from(spec->empty);
        HWND s = CreateWindowExW(0, L"STATIC", we, WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 20, top, CW - 40, 44, h, NULL, NULL, NULL);
        SendMessageW(s, WM_SETFONT, (WPARAM)hFont, TRUE);
        free(we);
    }
    {
        HWND tot = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 20, CH - 92, CW - 40, 22, h, (HMENU)IDC_TD_TOTAL, NULL, NULL);
        HWND b1 = mkbtn(h, L"All on", IDC_TD_ALLON), b2 = mkbtn(h, L"All off", IDC_TD_ALLOFF), b3 = mkbtn(h, L"OK", IDOK), b4 = mkbtn(h, L"Cancel", IDCANCEL);
        SendMessageW(tot, WM_SETFONT, (WPARAM)hFontB, TRUE);
        MoveWindow(b1, 20, CH - 52, 90, 34, FALSE);
        MoveWindow(b2, 118, CH - 52, 90, 34, FALSE);
        MoveWindow(b3, CW - 190, CH - 52, 80, 34, FALSE);
        MoveWindow(b4, CW - 100, CH - 52, 80, 34, FALSE);
    }
    td_list = list;
    td_cap = cap;
    td_spec = spec;
    td_total(h);
    td_done = td_ok = 0;
    EnableWindow(owner, FALSE);
    ShowWindow(h, SW_SHOW);
    while (!td_done && GetMessageW(&msg, NULL, 0, 0) > 0)
        if (!IsDialogMessageW(h, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    EnableWindow(owner, TRUE);
    DestroyWindow(h);
    SetForegroundWindow(owner);
    for (int i = 0; i < td_n; i++)
    {
        free(td_rows[i].tip);
        td_rows[i].tip = NULL;
    }
    return td_ok;
}
/* use_skill is a built-in tool, but the Skills dialog controls it: not listed or counted here */
static int tool_in_tools_dlg(const Tool *t) { return t->kind == 0 && strcmp(t->rname, "use_skill") != 0; }
/* edits list ("|name|...|" of switched-off tools) in place; 1 = OK pressed. toolset / tools_on: the values to show the
   dialog for (the Settings dialog passes its not yet applied choices) */
static int tools_dialog(HWND owner, char *list, size_t cap, int toolset, int tools_on)
{
    ChkSpec sp;
    char info[400];
    double cpt = ms_cpt(); /* calibrated for the current model (3.0 until measured) */
    td_n = 0;
    tl_enter();
    for (int i = 0; i < g_ntools && td_n < TD_MAX; i++)
        if (tool_in_tools_dlg(&g_tools[i]))
        {
            TdRow *r = &td_rows[td_n++];
            const char *d = g_tools[i].desc, *dot = strstr(d, ". ");
            size_t L = dot ? (size_t)(dot - d) + 1 : strlen(d);
            snprintf(r->name, sizeof r->name, "%s", g_tools[i].name);
            snprintf(r->desc, sizeof r->desc, "%.*s", (int)(L < 150 ? L : 150), d);
            r->tok = tool_tokens(&g_tools[i], cpt);
            r->inset = toolset_allows(toolset, &g_tools[i]);
            r->tip = xstrdup(d);
        }
    tl_leave();
    snprintf(info, sizeof info, "%s\nTools that need a setting (root folder, file writes, shell, screenshots) are listed when it is on. MCP tools: the MCP button.",
             !tools_on      ? "Tools are off in Settings: none of these is sent."
             : toolset == 2 ? "Tool set is None: none of these is sent. Ticks are kept for the All and Chat sets."
             : toolset == 1 ? "Tool set is Chat: greyed-out tools are not sent; their ticks are kept for the All set."
                            : "Untick what you do not need now: fewer tool definitions, fewer tokens and fewer wrong tool calls.");
    memset(&sp, 0, sizeof sp);
    sp.title = L"Built-in tools";
    sp.info = info;
    sp.empty = "No built-in tools are available with the current settings.";
    sp.what = "built-in tools";
    return chk_dialog(owner, &sp, list, cap);
}
/* "Tools 5/18": built-in tools actually sent (ticked and in the tool set) / available */
static void tools_btn_update(void)
{
    int on = 0, ticked = 0, n = 0;
    wchar_t t[32];
    HWND b = B(IDC_TOOLSBTN);
    if (!b)
        return;
    tl_enter();
    for (int i = 0; i < g_ntools; i++)
        if (tool_in_tools_dlg(&g_tools[i]))
        {
            n++;
            ticked += !name_in_list(G.tooloff, g_tools[i].name);
            on += tool_in_set(&g_tools[i]);
        }
    tl_leave();
    _snwprintf(t, 32, L"T\t%d/%d", on, n); /* drawn as a T glyph with a count badge */
    t[31] = 0;
    SetWindowTextW(b, t);
    {
        char tip[200];
        if (G.toolset == 0)
            snprintf(tip, sizeof tip, "Built-in tools: %d of %d sent. Click to choose which ones the model gets.", on, n);
        else
            snprintf(tip, sizeof tip, "Built-in tools: %d of %d sent (tool set: %s; %d ticked). Click to choose which ones the model gets.", on, n,
                     G.toolset == 2 ? "None" : "Chat", ticked);
        main_tip(b, tip);
    }
    ShowWindow(b, G.tools_on ? SW_SHOW : SW_HIDE);
    InvalidateRect(b, NULL, FALSE);
}
/* the Tools button in the input box */
static void act_tools(void)
{
    if (tools_dialog(owner_wnd(), G.tooloff, sizeof G.tooloff, G.toolset, G.tools_on))
    {
        cfg_save();
        tools_btn_update();
        InvalidateRect(hMain, NULL, FALSE);
        set_status("Built-in tools updated: the next request uses the new selection.");
    }
}

#endif /* LLAMAC_TOOLS_DLG_H */
