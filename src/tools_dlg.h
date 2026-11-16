/* tools_dlg.h - "Built-in tools" dialog: switch single built-in tools off to save tokens and avoid needless calls.
   The off list (G.tooloff, "|name|name|") is saved in settings and in profiles. Switched-off tools are not sent to the
   model; a call to one anyway (e.g. remembered from earlier in the chat) is refused. MCP tools: the MCP button. */
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
} TdRow;
static TdRow td_rows[TD_MAX];
static int td_n, td_done, td_ok;
static char *td_list;
static size_t td_cap;

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
    int on = 0, tok = 0;
    wchar_t s[160];
    for (int i = 0; i < td_n; i++)
        if (SendMessageW(GetDlgItem(h, IDC_TD_CHK + i), BM_GETCHECK, 0, 0) == BST_CHECKED)
        {
            on++;
            tok += td_rows[i].tok;
        }
    _snwprintf(s, 160, L"On: %d of %d built-in tools, about %d tokens in every request%s", on, td_n, tok,
               ms_known() ? L"" : L" (rough: measured after the first reply with this model)");
    s[159] = 0;
    SetDlgItemTextW(h, IDC_TD_TOTAL, s);
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
            td_total(h);
        else if (id == IDC_TD_ALLON || id == IDC_TD_ALLOFF)
        {
            for (int i = 0; i < td_n; i++)
                SendMessageW(GetDlgItem(h, IDC_TD_CHK + i), BM_SETCHECK, id == IDC_TD_ALLON ? BST_CHECKED : BST_UNCHECKED, 0);
            td_total(h);
        }
        else if (id == IDOK)
        { /* tools not listed now (their setting is off) keep their state */
            for (int i = 0; i < td_n; i++)
                name_list_set(td_list, td_cap, td_rows[i].name, SendMessageW(GetDlgItem(h, IDC_TD_CHK + i), BM_GETCHECK, 0, 0) != BST_CHECKED);
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
/* edits list ("|name|...|" of switched-off tools) in place; 1 = OK pressed */
static int tools_dialog(HWND owner, char *list, size_t cap)
{
    static int reg = 0;
    const int rowH = 28, colW = 560, nameW = 180, tokW = 80, top = 62;
    int cols, rows, CW, CH;
    RECT wr, orr;
    HWND h, tt;
    DWORD st = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    MSG msg;
    double cpt = ms_cpt(); /* calibrated for the current model (3.0 until measured) */
    td_n = 0;
    tl_enter();
    for (int i = 0; i < g_ntools && td_n < TD_MAX; i++)
        if (g_tools[i].kind == 0)
        {
            TdRow *r = &td_rows[td_n++];
            const char *d = g_tools[i].desc, *dot = strstr(d, ". ");
            size_t L = dot ? (size_t)(dot - d) + 1 : strlen(d);
            snprintf(r->name, sizeof r->name, "%s", g_tools[i].name);
            snprintf(r->desc, sizeof r->desc, "%.*s", (int)(L < 150 ? L : 150), d);
            r->tok = tool_tokens(&g_tools[i], cpt);
        }
    tl_leave();
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
    h = CreateWindowExW(WS_EX_DLGMODALFRAME, L"LCTools", L"Built-in tools", st, orr.left + 40, orr.top + 40, wr.right - wr.left, wr.bottom - wr.top,
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
        const char *info = !G.tools_on                 ? "Tools are off in Settings: none of these is sent."
                           : G.toolset == 2            ? "Tool set is None: none of these is sent, whatever is ticked here."
                           : G.toolset == 1            ? "Tool set is Chat: of these only date, memory, guides and ask_user are sent."
                                                       : "Untick what you do not need now: fewer tool definitions, fewer tokens and fewer wrong tool calls.";
        char line[400];
        wchar_t *wl;
        HWND s;
        snprintf(line, sizeof line, "%s\nTools that need a setting (root folder, file writes, shell, screenshots) are listed when it is on. MCP tools: the MCP button.", info);
        wl = w_from(line);
        s = CreateWindowExW(0, L"STATIC", wl, WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 20, 12, CW - 40, 40, h, (HMENU)IDC_TD_INFO, NULL, NULL);
        SendMessageW(s, WM_SETFONT, (WPARAM)hFont, TRUE);
        free(wl);
    }
    for (int i = 0; i < td_n; i++)
    {
        int c = i / rows, r = i % rows, x = 20 + c * colW, y = top + r * rowH;
        wchar_t *wn = w_from(td_rows[i].name), *wd = w_from(td_rows[i].desc), tk[24];
        HWND cb = CreateWindowExW(0, L"BUTTON", wn, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, x, y, nameW, 24, h, (HMENU)(INT_PTR)(IDC_TD_CHK + i), NULL, NULL);
        HWND ds = CreateWindowExW(0, L"STATIC", wd, WS_CHILD | WS_VISIBLE | SS_NOPREFIX | SS_ENDELLIPSIS | SS_NOTIFY, x + nameW + 6, y + 4, colW - nameW - tokW - 20, 20, h,
                                  (HMENU)(INT_PTR)(IDC_TD_DESC + i), NULL, NULL);
        HWND ts;
        _snwprintf(tk, 24, L"~%d tok", td_rows[i].tok);
        tk[23] = 0;
        ts = CreateWindowExW(0, L"STATIC", tk, WS_CHILD | WS_VISIBLE | SS_RIGHT, x + colW - tokW - 14, y + 4, tokW, 20, h, (HMENU)(INT_PTR)(IDC_TD_TOK + i), NULL, NULL);
        SetWindowTheme(cb, L"", L""); /* lets WM_CTLCOLORBTN colour the text in the dark theme */
        SendMessageW(cb, BM_SETCHECK, name_in_list(list, td_rows[i].name) ? BST_UNCHECKED : BST_CHECKED, 0);
        SendMessageW(cb, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(ds, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(ts, WM_SETFONT, (WPARAM)hFont, TRUE);
        if (tt)
        { /* the full description on hover */
            tl_enter();
            for (int k = 0; k < g_ntools; k++)
                if (!strcmp(g_tools[k].name, td_rows[i].name))
                {
                    tip_add2(tt, h, cb, g_tools[k].desc);
                    tip_add2(tt, h, ds, g_tools[k].desc);
                }
            tl_leave();
        }
        free(wn);
        free(wd);
    }
    if (!td_n)
    {
        HWND s = CreateWindowExW(0, L"STATIC", L"No built-in tools are available with the current settings.", WS_CHILD | WS_VISIBLE, 20, top, CW - 40, 24, h, NULL, NULL, NULL);
        SendMessageW(s, WM_SETFONT, (WPARAM)hFont, TRUE);
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
    return td_ok;
}
/* "Tools 14/18": built-in tools that are on / available */
static void tools_btn_update(void)
{
    int on = 0, n = 0;
    wchar_t t[32];
    HWND b = GetDlgItem(hMain, IDC_TOOLSBTN);
    if (!b)
        return;
    tl_enter();
    for (int i = 0; i < g_ntools; i++)
        if (g_tools[i].kind == 0)
        {
            n++;
            on += !name_in_list(G.tooloff, g_tools[i].name);
        }
    tl_leave();
    _snwprintf(t, 32, L"Tools %d/%d", on, n);
    t[31] = 0;
    SetWindowTextW(b, t);
    ShowWindow(b, G.tools_on ? SW_SHOW : SW_HIDE);
    InvalidateRect(b, NULL, FALSE);
}
/* the Tools button in the input box */
static void act_tools(void)
{
    if (tools_dialog(hMain, G.tooloff, sizeof G.tooloff))
    {
        cfg_save();
        tools_btn_update();
        InvalidateRect(hMain, NULL, FALSE);
        set_status("Built-in tools updated: the next request uses the new selection.");
    }
}

#endif /* LLAMAC_TOOLS_DLG_H */
