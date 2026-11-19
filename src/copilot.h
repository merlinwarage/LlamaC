/* copilot.h - Copilot mode: tray icon, global hotkey and a floating panel.
   The panel does not copy anything: it takes over the main window's composer controls and chat view with SetParent
   (same HWNDs, so attachments, the queue, streaming, links and tools keep working) and render_all() shows only the
   latest exchange while it hosts them (g_cp_on). Open app moves them back. */
#ifndef LLAMAC_COPILOT_H
#define LLAMAC_COPILOT_H

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif
#define CP_HDR 38   /* header: drag area, title, status line, buttons */
#define CP_PAD 10   /* inner padding left / right / bottom */
#define CP_GAP 6    /* between the chat and the composer */
#define CP_EDGE 6   /* left / right border that resizes the width */
#define CP_MINW 460 /* narrowest panel */
#define CP_HK_ID 1  /* RegisterHotKey id on hMain */
#define CP_TIMER 1  /* panel timer: grows the chat area with the answer */

static int g_cp_chatH;    /* height of the chat area; 0 = collapsed (header + composer only) */
static Conv *g_cp_exconv; /* exchange shown when the chat area was last sized: it never shrinks while it is the same */
static int g_cp_exnode = -1;
static int g_cp_sizing; /* the user is moving / resizing the panel */
static int g_cp_tray;   /* tray icon added */
static int g_cp_hk;     /* hotkey registered */
static int g_cp_exit;   /* Exit from the tray menu: WM_CLOSE really closes */
static UINT g_msgTaskbar; /* "TaskbarCreated": Explorer restarted, the tray icon must be added again */
static RECT cp_rcTools, cp_rcBox;

static void layout(void); /* main_window.h */

/* ---------- hotkey ---------- */
/* "Ctrl+Shift+Space", "Win+Alt+L", "F9" -> RegisterHotKey modifiers and virtual key; 0 = not valid */
static int cp_hk_parse(const char *s, UINT *mods, UINT *vk)
{
    static const struct
    {
        const char *name;
        UINT vk;
    } KEYS[] = {{"space", VK_SPACE}, {"insert", VK_INSERT}, {"ins", VK_INSERT}, {"home", VK_HOME}, {"end", VK_END}, {"pageup", VK_PRIOR}, {"pgup", VK_PRIOR}, {"pagedown", VK_NEXT}, {"pgdn", VK_NEXT}, {"pause", VK_PAUSE}};
    *mods = 0;
    *vk = 0;
    while (*s)
    {
        char tok[32];
        size_t n = 0;
        while (*s == ' ' || *s == '+')
            s++;
        while (*s && *s != '+')
        {
            if (n < sizeof tok - 1)
                tok[n++] = *s;
            s++;
        }
        while (n && tok[n - 1] == ' ')
            n--;
        tok[n] = 0;
        if (!n)
            continue;
        if (!_stricmp(tok, "ctrl") || !_stricmp(tok, "control"))
            *mods |= MOD_CONTROL;
        else if (!_stricmp(tok, "alt"))
            *mods |= MOD_ALT;
        else if (!_stricmp(tok, "shift"))
            *mods |= MOD_SHIFT;
        else if (!_stricmp(tok, "win"))
            *mods |= MOD_WIN;
        else if (*vk)
            return 0; /* a second key */
        else if (n == 1 && isalnum((unsigned char)tok[0]))
            *vk = (UINT)toupper((unsigned char)tok[0]);
        else if ((tok[0] == 'F' || tok[0] == 'f') && n >= 2 && n <= 3 && isdigit((unsigned char)tok[1]) && (n == 2 || isdigit((unsigned char)tok[2])))
        {
            int f = atoi(tok + 1);
            if (f < 1 || f > 24)
                return 0;
            *vk = VK_F1 + (UINT)(f - 1);
        }
        else
        {
            for (size_t k = 0; k < sizeof KEYS / sizeof KEYS[0] && !*vk; k++)
                if (!_stricmp(tok, KEYS[k].name))
                    *vk = KEYS[k].vk;
            if (!*vk)
                return 0;
        }
    }
    if (!*vk)
        return 0;
    if (!*mods && !(*vk >= VK_F1 && *vk <= VK_F24) && *vk != VK_PAUSE)
        return 0; /* a bare letter or Space would stop working everywhere else */
    return 1;
}
static void cp_hk_apply(void)
{
    UINT mods, vk;
    char msg[160];
    if (g_cp_hk)
    {
        UnregisterHotKey(hMain, CP_HK_ID);
        g_cp_hk = 0;
    }
    if (!G.copilot || !G.cphk[0])
        return;
    if (!cp_hk_parse(G.cphk, &mods, &vk))
    {
        snprintf(msg, sizeof msg, "Copilot hotkey \"%.60s\" is not valid - see Settings > Display.", G.cphk);
        set_status(msg);
        return;
    }
    if (RegisterHotKey(hMain, CP_HK_ID, mods | MOD_NOREPEAT, vk))
        g_cp_hk = 1;
    else
    {
        snprintf(msg, sizeof msg, "Copilot hotkey %.60s is taken by another program - choose another in Settings > Display.", G.cphk);
        set_status(msg);
    }
}

/* ---------- tray icon ---------- */
static void cp_tray(int on)
{
    NOTIFYICONDATAW nd;
    memset(&nd, 0, sizeof nd);
    nd.cbSize = sizeof nd;
    nd.hWnd = hMain;
    nd.uID = 1;
    if (!on)
    {
        if (g_cp_tray)
            Shell_NotifyIconW(NIM_DELETE, &nd);
        g_cp_tray = 0;
        return;
    }
    nd.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nd.uCallbackMessage = WM_APP_TRAY;
    nd.hIcon = (HICON)GetClassLongPtrW(hMain, GCLP_HICONSM);
    if (!nd.hIcon)
        nd.hIcon = (HICON)GetClassLongPtrW(hMain, GCLP_HICON);
    if (!nd.hIcon)
        nd.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    {
        wchar_t *hk = w_from(G.cphk);
        _snwprintf(nd.szTip, sizeof nd.szTip / sizeof nd.szTip[0], hk[0] ? L"LlamaC  (%ls)" : L"LlamaC", hk);
        nd.szTip[sizeof nd.szTip / sizeof nd.szTip[0] - 1] = 0;
        free(hk);
    }
    if (Shell_NotifyIconW(g_cp_tray ? NIM_MODIFY : NIM_ADD, &nd))
        g_cp_tray = 1;
}

/* ---------- panel geometry ----------
   The panel is placed from an anchor: G.cpx = left, G.cpw = width, G.cpy = the edge that stays put - the bottom when it
   grows upwards (G.cpup, panel in the lower half of the screen), else the top. The header is always on top; the chat
   area is above the composer when it grows upwards, below it otherwise. */
static void cp_work(POINT pt, RECT *wk)
{
    MONITORINFO mi;
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    *wk = mi.rcWork;
}
static POINT cp_anchor_pt(void)
{
    POINT pt;
    pt.x = G.cpx + G.cpw / 2;
    pt.y = G.cpup ? G.cpy - 1 : G.cpy;
    return pt;
}
static int cp_collapsed_h(void) { return CP_HDR + g_boxH + CP_PAD; }
static int cp_max_chat(const RECT *wk)
{
    int m = (wk->bottom - wk->top) * 65 / 100 - cp_collapsed_h();
    return m < 160 ? 160 : m;
}
static void cp_default_place(void)
{
    POINT o = {0, 0};
    RECT wk;
    cp_work(o, &wk); /* the primary monitor */
    G.cpw = 680;
    if (G.cpw > wk.right - wk.left - 40)
        G.cpw = wk.right - wk.left - 40;
    G.cpx = (wk.left + wk.right - G.cpw) / 2;
    G.cpup = 1;
    G.cpy = wk.bottom - (wk.bottom - wk.top) / 6;
}
static void cp_place(void)
{
    RECT wk, now;
    POINT pt;
    int W, H, x, top;
    if (G.cpw < CP_MINW)
        cp_default_place();
    pt = cp_anchor_pt();
    if (!MonitorFromPoint(pt, MONITOR_DEFAULTTONULL))
    { /* the monitor it was left on is gone */
        cp_default_place();
        pt = cp_anchor_pt();
    }
    cp_work(pt, &wk);
    W = G.cpw;
    if (W > wk.right - wk.left)
        W = wk.right - wk.left;
    H = cp_collapsed_h() + (g_cp_chatH ? g_cp_chatH + CP_GAP : 0);
    if (H > wk.bottom - wk.top)
        H = wk.bottom - wk.top;
    top = G.cpup ? G.cpy - H : G.cpy;
    if (top + H > wk.bottom)
        top = wk.bottom - H;
    if (top < wk.top)
        top = wk.top;
    x = G.cpx;
    if (x + W > wk.right)
        x = wk.right - W;
    if (x < wk.left)
        x = wk.left;
    GetWindowRect(hPanel, &now);
    if (now.left != x || now.top != top || now.right - now.left != W || now.bottom - now.top != H)
        SetWindowPos(hPanel, NULL, x, top, W, H, SWP_NOZORDER | SWP_NOACTIVATE);
}
static int cp_btn_w(HWND b)
{
    wchar_t t[64];
    SIZE sz = {0, 0};
    HDC dc = GetDC(b);
    HGDIOBJ old = SelectObject(dc, hFontS);
    GetWindowTextW(b, t, 64);
    GetTextExtentPoint32W(dc, t, (int)wcslen(t), &sz);
    SelectObject(dc, old);
    ReleaseDC(b, dc);
    return sz.cx + 20;
}
/* positions the hosted controls inside the panel */
static void cp_layout(void)
{
    RECT rc;
    int W, left = CP_PAD, cw, yBox, x;
    if (!hPanel || !g_cp_on)
        return;
    GetClientRect(hPanel, &rc);
    W = rc.right;
    cw = W - 2 * CP_PAD;
    { /* header: [LlamaC  status ...........] [Copy] [New] [Open app] [Hide] */
        const int ids[] = {IDC_CP_HIDE, IDC_CP_OPEN, IDC_CP_NEW, IDC_CP_COPY};
        x = W - 8;
        for (int i = 0; i < 4; i++)
        {
            HWND b = GetDlgItem(hPanel, ids[i]);
            int bw = cp_btn_w(b);
            x -= bw;
            MoveWindow(b, x, 7, bw, CP_HDR - 14, TRUE);
            x -= 2;
        }
        MoveWindow(hStatus, 82, (CP_HDR - 18) / 2, x - 8 - 82 > 0 ? x - 8 - 82 : 0, 18, TRUE);
    }
    g_colw = W - 2 - 28; /* the same whether collapsed or not: expanding must not force a full re-render */
    if (g_cp_chatH)
    {
        int chatTop = G.cpup ? CP_HDR : CP_HDR + g_boxH + CP_GAP;
        yBox = G.cpup ? CP_HDR + g_cp_chatH + CP_GAP : CP_HDR;
        MoveWindow(hChat, 1, chatTop, W - 2, g_cp_chatH, TRUE);
    }
    else
    { /* collapsed: parked below the client area (clipped). ShowWindow(SW_HIDE) would not last: render_all's
         WM_SETREDRAW TRUE sets WS_VISIBLE again */
        yBox = CP_HDR;
        MoveWindow(hChat, 1, rc.bottom + 4, W - 2, 140, FALSE);
    }
    SendMessageW(hChat, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(14, 14));
    { /* composer: the same arrangement as in the main window, the input grows upwards */
        int nb = G.tools_on ? 4 : 2, gw = nb * 42 + 2, yRow = yBox + g_boxH - DOCK_H;
        const int gids[] = {IDC_ATTACH, IDC_THINKBTN, IDC_TOOLSBTN, IDC_MCPBTN};
        SetRect(&cp_rcTools, left, yRow, left + gw, yRow + DOCK_H);
        for (int i = 0; i < nb; i++)
            MoveWindow(B(gids[i]), left + 3 + i * 42, yRow + 4, 38, DOCK_H - 8, TRUE);
        SetRect(&cp_rcBox, left + gw + 8, yBox, left + cw - DOCK_H - 8, yBox + g_boxH);
        MoveWindow(hInput, cp_rcBox.left + 14, yBox + 13, cp_rcBox.right - cp_rcBox.left - 28, g_boxH - 24, TRUE);
        MoveWindow(B(IDC_SEND), left + cw - DOCK_H, yRow, DOCK_H, DOCK_H, TRUE);
    }
    InvalidateRect(hPanel, NULL, TRUE);
}
static void cp_relayout(void)
{
    if (!hPanel || !g_cp_on)
        return;
    cp_place();
    cp_layout();
}

/* ---------- growing with the answer ---------- */
static int cp_content_h(void)
{ /* height of everything in the chat view (EM_GETTEXTLENGTHEX does not move the selection, unlike re_len) */
    GETTEXTLENGTHEX gl = {GTL_NUMCHARS | GTL_PRECISE, 1200};
    LONG n = (LONG)SendMessageW(hChat, EM_GETTEXTLENGTHEX, (WPARAM)&gl, 0);
    POINTL pt = {0, 0};
    POINT sp = {0, 0};
    if (n <= 0)
        return 0;
    SendMessageW(hChat, EM_POSFROMCHAR, (WPARAM)&pt, n - 1);
    SendMessageW(hChat, EM_GETSCROLLPOS, 0, (LPARAM)&sp);
    return pt.y + sp.y + g_lineH * 3 / 2; /* pt is the top of the last line (a finished message ends with a blank one) */
}
static int cp_last_user(const Conv *c)
{
    for (int i = c->leaf; i >= 0 && c->n[i].role != ROLE_ROOT; i = c->n[i].parent)
        if (c->n[i].role == ROLE_USER)
            return i;
    return -1;
}
static void cp_fit(void)
{
    int want = 0, ex = cur ? cp_last_user(cur) : -1;
    if (!g_cp_on || !hPanel || g_cp_sizing)
        return;
    if (cur && cur->leaf != cur->root)
    {
        RECT wk;
        cp_work(cp_anchor_pt(), &wk);
        want = g_cp_chatH ? cp_content_h() : 0;
        if (want < 140)
            want = 140;
        if (want > cp_max_chat(&wk))
            want = cp_max_chat(&wk);
        if (cur == g_cp_exconv && ex == g_cp_exnode && want < g_cp_chatH)
            want = g_cp_chatH; /* the same exchange: no shrinking while it streams or re-renders */
    }
    g_cp_exconv = cur;
    g_cp_exnode = ex;
    if (want != g_cp_chatH)
    {
        g_cp_chatH = want;
        cp_relayout();
    }
}
/* after the user moved or resized the panel: remember where, and which way it grows from there */
static void cp_moved(void)
{
    RECT r, wk;
    POINT c;
    int oldw = G.cpw;
    GetWindowRect(hPanel, &r);
    c.x = (r.left + r.right) / 2;
    c.y = (r.top + r.bottom) / 2;
    cp_work(c, &wk);
    /* the panel stays where it was dropped: lower half -> its bottom edge is the anchor and it grows upwards,
       upper half -> its top edge, growing downwards (the chat moves below the composer) */
    G.cpx = r.left;
    G.cpw = r.right - r.left;
    G.cpup = c.y > (wk.top + wk.bottom) / 2;
    G.cpy = G.cpup ? r.bottom : r.top;
    cfg_save();
    cp_relayout();
    if (G.cpw != oldw)
        render_all(); /* code blocks are padded to the column width */
}

/* ---------- panel window ---------- */
static void cp_paint(HWND h)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT rc, t, line;
    HBRUSH lb = CreateSolidBrush(cBorder);
    GetClientRect(h, &rc);
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, hFontB);
    SetTextColor(dc, cFg);
    SetRect(&t, 16, 0, 80, CP_HDR);
    DrawTextW(dc, L"LlamaC", -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SetRect(&line, 0, CP_HDR - 1, rc.right, CP_HDR);
    FillRect(dc, &line, lb);
    rrect(dc, &cp_rcTools, 14, cSurf, cBorder);
    for (int i = 1; i < (G.tools_on ? 4 : 2); i++)
    {
        RECT v = {cp_rcTools.left + 1 + i * 42, cp_rcTools.top + 12, cp_rcTools.left + 2 + i * 42, cp_rcTools.bottom - 12};
        FillRect(dc, &v, lb);
    }
    rrect(dc, &cp_rcBox, 14, cSurf, cBorder);
    DeleteObject(lb);
    EndPaint(h, &ps);
}
static void cp_hide(void)
{
    if (!hPanel)
        return;
    KillTimer(hPanel, CP_TIMER);
    ShowWindow(hPanel, SW_HIDE);
}
static void cp_to_main(void);
static LRESULT CALLBACK PanelProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_ERASEBKGND:
    {
        RECT r;
        GetClientRect(h, &r);
        FillRect((HDC)w, &r, hbrBg);
        return 1;
    }
    case WM_PAINT:
        cp_paint(h);
        return 0;
    case WM_SIZE:
        cp_layout();
        return 0;
    case WM_NCHITTEST:
    {
        POINT pt = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        RECT rc;
        ScreenToClient(h, &pt);
        GetClientRect(h, &rc);
        if (pt.x < CP_EDGE)
            return HTLEFT;
        if (pt.x >= rc.right - CP_EDGE)
            return HTRIGHT;
        return pt.y < CP_HDR ? HTCAPTION : HTCLIENT; /* the header drags the panel */
    }
    case WM_NCLBUTTONDBLCLK:
        return 0; /* no maximize on a header double-click */
    case WM_ENTERSIZEMOVE:
        g_cp_sizing = 1;
        return 0;
    case WM_EXITSIZEMOVE:
        g_cp_sizing = 0;
        cp_moved();
        return 0;
    case WM_SIZING:
    { /* only the width is resizable */
        RECT *r = (RECT *)l, now;
        GetWindowRect(h, &now);
        r->top = now.top;
        r->bottom = now.bottom;
        if (r->right - r->left < CP_MINW)
        {
            if (w == WMSZ_LEFT)
                r->left = r->right - CP_MINW;
            else
                r->right = r->left + CP_MINW;
        }
        return TRUE;
    }
    case WM_ACTIVATE:
        if (LOWORD(w) != WA_INACTIVE && !IsChild(h, GetFocus()))
            SetFocus(hInput);
        return 0;
    case WM_TIMER:
        if (w == CP_TIMER)
            cp_fit();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(w))
        {
        case IDC_CP_COPY:
            act_copy();
            return 0;
        case IDC_CP_NEW:
            new_chat();
            return 0;
        case IDC_CP_OPEN:
            cp_to_main();
            return 0;
        case IDC_CP_HIDE:
            cp_hide();
            return 0;
        }
        return SendMessageW(hMain, m, w, l); /* the hosted controls: handled by the main window as always */
    case WM_DRAWITEM:
    case WM_MEASUREITEM:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX:
    case WM_DROPFILES:
        return SendMessageW(hMain, m, w, l);
    case WM_CLOSE: /* Alt+F4 */
        cp_hide();
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static void cp_create(void)
{
    WNDCLASSEXW wc;
    HINSTANCE hi = GetModuleHandleW(NULL);
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = PanelProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = L"LCPanel";
    RegisterClassExW(&wc);
    /* tool window: no taskbar button, not in Alt+Tab; topmost: it floats over the app being worked in */
    hPanel = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"LCPanel", L"LlamaC", WS_POPUP | WS_CLIPCHILDREN, 0, 0, 680, 120, NULL, NULL, hi, NULL);
    if (!hPanel)
        return;
    main_tip(mkbtn(hPanel, L"Copy", IDC_CP_COPY), "Copy the last reply");
    main_tip(mkbtn(hPanel, L"New", IDC_CP_NEW), "New chat: clears the panel (Ctrl+Shift+N)");
    main_tip(mkbtn(hPanel, L"Open app", IDC_CP_OPEN), "Open the LlamaC window with the whole conversation");
    main_tip(mkbtn(hPanel, L"Hide", IDC_CP_HIDE), "Hide the panel (Esc). The hotkey or the tray icon brings it back.");
    ChangeWindowMessageFilterEx(hPanel, WM_DROPFILES, MSGFLT_ALLOW, NULL);
    ChangeWindowMessageFilterEx(hPanel, 0x0049 /* WM_COPYGLOBALDATA */, MSGFLT_ALLOW, NULL);
    DragAcceptFiles(hPanel, TRUE);
    apply_frame(hPanel); /* rounded corners and border on Windows 11 */
}
/* the controls that move between the main window and the panel */
static void cp_move(HWND to)
{
    HWND c[] = {hChat, hInput, hStatus, B(IDC_ATTACH), B(IDC_THINKBTN), B(IDC_TOOLSBTN), B(IDC_MCPBTN), B(IDC_SEND)};
    for (int i = 0; i < (int)(sizeof c / sizeof c[0]); i++)
        if (c[i])
            SetParent(c[i], to);
}
static void cp_show(void)
{
    if (!hPanel)
        cp_create();
    if (!hPanel)
        return;
    if (IsWindowVisible(hMain))
        ShowWindow(hMain, SW_HIDE);
    if (!g_cp_on)
    {
        cp_move(hPanel);
        g_cp_on = 1;
        g_cp_chatH = (cur && cur->leaf != cur->root) ? 140 : 0; /* measured by cp_fit below */
        g_cp_exconv = NULL;
        apply_frame(hPanel); /* the theme may have changed */
        cp_relayout();
        render_all();
    }
    ShowWindow(hPanel, SW_SHOW);
    SetWindowPos(hPanel, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetForegroundWindow(hPanel);
    SetFocus(hInput);
    SetTimer(hPanel, CP_TIMER, 150, NULL);
    cp_fit();
}
/* back to the main window: the controls go home, the whole conversation is shown */
static void cp_to_main(void)
{
    cp_hide();
    if (g_cp_on)
    {
        cp_move(hMain);
        g_cp_on = 0;
        layout();
        render_all();
    }
    ShowWindow(hMain, IsIconic(hMain) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(hMain);
    SetFocus(hInput);
}
/* hotkey (by_fg: hide only when the panel is the active window) and tray click (hide whenever it is shown) */
static void cp_toggle(int by_fg)
{
    if (hPanel && IsWindowVisible(hPanel) && (!by_fg || GetForegroundWindow() == hPanel))
        cp_hide();
    else
        cp_show();
}
static void cp_tray_menu(void)
{
    HMENU m = CreatePopupMenu();
    POINT pt;
    wchar_t t[96], *hk = w_from(G.cphk);
    int cmd;
    _snwprintf(t, 96, g_cp_hk ? L"Show panel\t%ls" : L"Show panel", hk);
    t[95] = 0;
    free(hk);
    AppendMenuW(m, MF_STRING, 1, t);
    AppendMenuW(m, MF_STRING, 2, L"Open LlamaC");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, 3, L"Exit");
    SetMenuDefaultItem(m, 1, FALSE);
    GetCursorPos(&pt);
    SetForegroundWindow(hMain); /* otherwise the menu does not close when clicking elsewhere */
    cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hMain, NULL);
    PostMessageW(hMain, WM_NULL, 0, 0);
    DestroyMenu(m);
    if (cmd == 1)
        cp_show();
    else if (cmd == 2)
        cp_to_main();
    else if (cmd == 3)
    {
        g_cp_exit = 1;
        PostMessageW(hMain, WM_CLOSE, 0, 0);
    }
}
/* at start-up and after Settings > OK: tray icon and hotkey follow the setting */
static void cp_apply(void)
{
    if (!g_msgTaskbar)
    {
        g_msgTaskbar = RegisterWindowMessageW(L"TaskbarCreated");
        if (g_msgTaskbar)
            ChangeWindowMessageFilterEx(hMain, g_msgTaskbar, MSGFLT_ALLOW, NULL);
    }
    cp_tray(G.copilot);
    cp_hk_apply();
    if (!G.copilot)
    {
        if (g_cp_on)
            cp_to_main();
        else
            cp_hide();
    }
}

#endif /* LLAMAC_COPILOT_H */
