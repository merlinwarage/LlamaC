/* main_window.h - Main window procedure, layout, WinMain */
#ifndef LLAMAC_MAIN_WINDOW_H
#define LLAMAC_MAIN_WINDOW_H

/* ---------- main window ---------- */
static int g_gauge_last = -1;
static LRESULT CALLBACK InputProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_CHAR)
    {
        if (w == L'\r' && !(GetKeyState(VK_SHIFT) & 0x8000))
        { /* while the model is busy, Enter queues the message instead of stopping */
            PostMessageW(hMain, WM_COMMAND, busy ? IDC_QUEUE : IDC_SEND, 0);
            return 0;
        }
        if (w == 1)
        {
            SendMessageW(h, EM_SETSEL, 0, (LPARAM)-1);
            return 0;
        }
    }
    if (m == WM_DROPFILES)
    { /* dropped on the input box itself: the main window handler attaches the files */
        SendMessageW(hMain, WM_DROPFILES, w, l);
        return 0;
    }
    if (m == WM_PASTE && paste_special())
        return 0; /* copied files or an image became attachments */
    if (m == WM_PASTE && G.smartpaste && OpenClipboard(h))
    {
        HANDLE g = GetClipboardData(CF_UNICODETEXT);
        if (g)
        {
            const wchar_t *t = (const wchar_t *)GlobalLock(g);
            if (t && wcslen(t) > 2000)
            {
                char *u = u_from(t);
                pend_add(ATT_TEXT, "pasted-text.txt", u, "text/plain");
                free(u);
                GlobalUnlock(g);
                CloseClipboard();
                return 0;
            }
            if (t)
                GlobalUnlock(g);
        }
        CloseClipboard();
    }
    return CallWindowProcW(oldInput, h, m, w, l);
}
static void layout(void)
{
    RECT rc;
    GetClientRect(hMain, &rc);
    int W = rc.right, H = rc.bottom;
    if (W < 100 || H < 100)
        return;
    int sw = sbVisible ? 272 : 0, x0 = sw, Wr = W - sw;
    SetRect(&rcSide, 0, 0, sw, H);
    int vis = sbVisible ? SW_SHOW : SW_HIDE;
    ShowWindow(hSearch, vis);
    ShowWindow(hList, vis);
    ShowWindow(B(IDC_NEW), vis);
    if (sbVisible)
    {
        MoveWindow(B(IDC_NEW), 14, 58, sw - 28, 40, TRUE);
        SetRect(&rcSearch, 14, 108, sw - 14, 144);
        MoveWindow(hSearch, rcSearch.left + 14, rcSearch.top + 8, sw - 28 - 28, 22, TRUE);
        MoveWindow(hList, 6, 154, sw - 12, H - 154 - 8, TRUE);
    }
    SetRect(&rcHead, x0, 0, W, 60);
    {
        const int ids[] = {IDC_REFRESH, IDC_SET, IDC_TEMPLATE, IDC_IMP, IDC_EXP};
        const int wd[] = {38, 88, 84, 76, 76};
        int x = W - 14;
        for (int i = 0; i < 5; i++)
        {
            x -= wd[i];
            MoveWindow(B(ids[i]), x, 12, wd[i], 36, TRUE);
            x -= 6;
        }
        x -= 250;
        MoveWindow(hModel, x, 17, 250, 300, TRUE);
        SetRect(&rcTitle, x0 + 22, 0, x - 12, 60);
    }
    int cw = Wr - 48;
    if (cw < 300)
        cw = 300;
    int left = x0 + (Wr - cw) / 2;
    g_colw = cw;
    int boxH = 84, statusH = 22, actH = 36;
    int yStatus = H - statusH - 8, yBox = yStatus - boxH - 6, yAct = yBox - actH - 2;
    MoveWindow(hChat, x0, 60, Wr, yAct - 64, TRUE);
    {
        int m = (Wr - cw) / 2;
        SendMessageW(hChat, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(m, m));
    }
    {
        const int ids[] = {IDC_REGEN, IDC_EDIT, IDC_EDITREPLY, IDC_DELM, IDC_PREV, IDC_NEXT, IDC_COPY, IDC_DELC};
        const int wd[] = {96, 84, 88, 92, 36, 36, 56, 92};
        int x = left;
        for (int i = 0; i < 8; i++)
        {
            MoveWindow(B(ids[i]), x, yAct + 2, wd[i], 32, TRUE);
            x += wd[i] + 4;
        }
    }
    SetRect(&rcBox, left, yBox, left + cw, yBox + boxH);
    MoveWindow(B(IDC_ATTACH), left + 10, yBox + (boxH - 38) / 2, 38, 38, TRUE);
    if (G.tools_on)
    { /* MCP server switch next to the attach button */
        MoveWindow(B(IDC_TOOLSBTN), left + 52, yBox + (boxH - 38) / 2, 86, 38, TRUE);
        MoveWindow(B(IDC_MCPBTN), left + 142, yBox + (boxH - 38) / 2, 74, 38, TRUE);
        MoveWindow(hInput, left + 224, yBox + 14, cw - 224 - 100, boxH - 28, TRUE);
    }
    else
        MoveWindow(hInput, left + 60, yBox + 14, cw - 60 - 100, boxH - 28, TRUE);
    MoveWindow(B(IDC_SEND), left + cw - 88, yBox + (boxH - 38) / 2, 78, 38, TRUE);
    MoveWindow(hStatus, left, yStatus, cw - 186, 20, TRUE);
    SetRect(&rcGauge, left + cw - 176, yStatus + 1, left + cw, yStatus + 19);
    RedrawWindow(hMain, NULL, NULL, RDW_INVALIDATE | RDW_ERASE);
}
/* how full the context is: measured tokens of the last request (plus anything added since) / context size */
static void paint_gauge(HDC dc)
{
    int lim = ctx_lim(), used = (cur && cur == g_meas_conv) ? est_tokens(cur) : -1;
    RECT r = rcGauge, f;
    wchar_t t[64];
    COLORREF col = cAccent;
    if (cur && cur->leaf == cur->root && !(busy && cur == genConv))
        used = 0; /* empty chat */
    if (used < 0 && cur && cur == g_meas_conv)
        used = g_gauge_last; /* between the start of a request and its first timings */
    if (cur == g_meas_conv && used >= 0)
        g_gauge_last = used;
    FillRect(dc, &rcGauge, hbrBg);
    rrect(dc, &r, 8, cSurf, cBorder);
    if (lim > 0 && used >= 0)
    {
        int trig = ctx_trigger(lim), w = (int)((long long)(r.right - r.left - 4) * (used > lim ? lim : used) / lim);
        if (used > trig)
            col = cErr;
        else if (used > trig * 85 / 100)
            col = RGB(245, 158, 11);
        f = r;
        InflateRect(&f, -2, -2);
        f.right = f.left + w;
        if (w > 0)
            rrect(dc, &f, 6, col, col);
        _snwprintf(t, 64, L"ctx %d.%dk / %dk", used / 1000, used % 1000 / 100, lim / 1000);
    }
    else
        _snwprintf(t, 64, lim > 0 ? L"ctx ? / %dk" : L"ctx ?", lim / 1000);
    t[63] = 0;
    SelectObject(dc, hFont);
    SetTextColor(dc, cFg);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, t, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}
static void paint_main(HWND h)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT r;
    SetBkMode(dc, TRANSPARENT);
    if (sbVisible)
    {
        FillRect(dc, &rcSide, hbrPanel);
        RECT line = {rcSide.right - 1, 0, rcSide.right, rcSide.bottom};
        HBRUSH lb = CreateSolidBrush(cBorder);
        FillRect(dc, &line, lb);
        SelectObject(dc, hFontT);
        SetTextColor(dc, cFg);
        RECT t = {18, 16, rcSide.right - 10, 50};
        DrawTextW(dc, L"LlamaC", -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, hFont);
        SetTextColor(dc, cAccent);
        RECT t2 = {104, 22, rcSide.right - 10, 50};
        DrawTextW(dc, L"native", -1, &t2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, hFont);
        SetTextColor(dc, cDim);
        RECT tv = {14, 22, rcSide.right - 16, 50};
        DrawTextW(dc, L"v" LLAMAC_VERSION, -1, &tv, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        rrect(dc, &rcSearch, 16, cSurf, cBorder);
        DeleteObject(lb);
    }
    r = rcHead;
    r.top = r.bottom - 1;
    {
        HBRUSH lb = CreateSolidBrush(cBorder);
        FillRect(dc, &r, lb);
        DeleteObject(lb);
    }
    FillRect(dc, &rcTitle, hbrBg);
    if (cur)
    {
        wchar_t *w = w_from(cur->name);
        SelectObject(dc, hFontB);
        SetTextColor(dc, cFg);
        DrawTextW(dc, w, -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        free(w);
    }
    rrect(dc, &rcBox, 26, cSurf, cBorder);
    paint_gauge(dc);
    EndPaint(h, &ps);
}
static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_CREATE:
    {
        hMain = h;
        HINSTANCE hi = GetModuleHandleW(NULL);
        hFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        hFontB = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        hFontT = CreateFontW(-24, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        hSearch = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10, h, (HMENU)(INT_PTR)IDC_SEARCH, hi, NULL);
        SendMessageW(hSearch, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search chats  (Ctrl+K)");
        hList = CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS, 0, 0, 10, 10, h, (HMENU)(INT_PTR)IDC_LIST, hi, NULL);
        hChat = CreateWindowExW(0, L"RICHEDIT50W", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 0, 0, 10, 10, h, (HMENU)(INT_PTR)IDC_CHAT, hi, NULL);
        SendMessageW(hChat, EM_EXLIMITTEXT, 0, 0x7FFFFFF0);
        SendMessageW(hChat, EM_SETBKGNDCOLOR, 0, (LPARAM)cBg);
        SetWindowSubclass(hChat, ChatSub, 2, 0);
        SendMessageW(hChat, EM_SETUNDOLIMIT, 0, 0);
        hInput = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, 0, 0, 10, 10, h, (HMENU)(INT_PTR)IDC_INPUT, hi, NULL);
        oldInput = (WNDPROC)SetWindowLongPtrW(hInput, GWLP_WNDPROC, (LONG_PTR)InputProc);
        hStatus = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS | SS_NOPREFIX, 0, 0, 10, 10, h, (HMENU)(INT_PTR)IDC_STATUS, hi, NULL);
        hModel = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWNLIST, 0, 0, 10, 300, h, (HMENU)(INT_PTR)IDC_MODEL, hi, NULL);
        SendMessageW(hList, LB_SETITEMHEIGHT, 0, 40);
        {
            HWND c[] = {hSearch, hList, hInput, hStatus, hModel};
            for (int i = 0; i < 5; i++)
                SendMessageW(c[i], WM_SETFONT, (WPARAM)hFont, TRUE);
        }
        SendMessageW(hSearch, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(2, 2));
        SendMessageW(hInput, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(4, 4));
        mkbtn(h, L"+  New chat", IDC_NEW);
        mkbtn(h, L"Export", IDC_EXP);
        mkbtn(h, L"Import", IDC_IMP);
        mkbtn(h, L"Template", IDC_TEMPLATE);
        mkbtn(h, L"Settings", IDC_SET);
        mkbtn(h, L"\u21BB", IDC_REFRESH);
        mkbtn(h, L"Regenerate", IDC_REGEN);
        mkbtn(h, L"Edit last", IDC_EDIT);
        mkbtn(h, L"Edit reply", IDC_EDITREPLY);
        mkbtn(h, L"Delete msg", IDC_DELM);
        mkbtn(h, L"\u25C0", IDC_PREV);
        mkbtn(h, L"\u25B6", IDC_NEXT);
        mkbtn(h, L"Copy", IDC_COPY);
        mkbtn(h, L"Delete chat", IDC_DELC);
        mkbtn(h, L"+", IDC_ATTACH);
        mkbtn(h, L"MCP", IDC_MCPBTN);
        mkbtn(h, L"Tools", IDC_TOOLSBTN);
        mkbtn(h, L"Send", IDC_SEND);
        /* drops also work when the app runs elevated (UIPI would otherwise block them from Explorer) */
        ChangeWindowMessageFilterEx(h, WM_DROPFILES, MSGFLT_ALLOW, NULL);
        ChangeWindowMessageFilterEx(h, 0x0049 /* WM_COPYGLOBALDATA */, MSGFLT_ALLOW, NULL);
        DragAcceptFiles(h, TRUE);
        DragAcceptFiles(hInput, TRUE);
        RevokeDragDrop(hChat); /* the RichEdit registers its own OLE drop target, which swallows file drops */
        DragAcceptFiles(hChat, TRUE);
        set_theme();
        PostMessageW(h, WM_APP_INIT, 0, 0);
        {
            wchar_t t[MAX_PATH + 40];
            _snwprintf(t, MAX_PATH + 40, L"LlamaC  -  data: %ls", dataDir);
            SetWindowTextW(h, t);
        }
        return 0;
    }
    case WM_APP_ASK: /* ask_user from the tool runner thread (it waits in SendMessage) */
        ask_user_ui((AskReq *)l);
        return 0;
    case WM_APP_INIT:
        layout();
        mcp_btn_update();
        tools_startup();
        tools_btn_update();
        set_status("Connecting...");
        if (!nconv)
            convs_add(conv_new());
        qsort(convs, (size_t)nconv, sizeof(Conv *), cmp_mod);
        select_conv(convs[0]);
        refresh_models();
        SetFocus(hInput);
        return 0;
    case WM_SIZE:
        if (B(IDC_SEND))
            layout();
        return 0;
    case WM_EXITSIZEMOVE:
        render_all();
        return 0;
    case WM_ERASEBKGND:
    {
        RECT r;
        GetClientRect(h, &r);
        FillRect((HDC)w, &r, hbrBg);
        return 1;
    }
    case WM_PAINT:
        paint_main(h);
        return 0;
    case WM_LBUTTONUP:
    {
        POINT pt = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        if (PtInRect(&rcGauge, pt))
            act_context();
        return 0;
    }
    case WM_SETCURSOR:
    {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(h, &pt);
        if ((HWND)w == h && PtInRect(&rcGauge, pt))
        {
            SetCursor(LoadCursorW(NULL, IDC_HAND));
            return TRUE;
        }
        break;
    }
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)l)->ptMinTrackSize.x = 1000;
        ((MINMAXINFO *)l)->ptMinTrackSize.y = 560;
        return 0;
    case WM_MEASUREITEM:
        if (((MEASUREITEMSTRUCT *)l)->CtlType == ODT_LISTBOX)
        {
            ((MEASUREITEMSTRUCT *)l)->itemHeight = 40;
            return TRUE;
        }
        break;
    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT *d = (DRAWITEMSTRUCT *)l;
        if (d->CtlType == ODT_LISTBOX)
            draw_list_item(d);
        else if (d->CtlType == ODT_BUTTON)
            draw_btn(d);
        return TRUE;
    }
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)w, cFg);
        SetBkColor((HDC)w, cSurf);
        return (LRESULT)hbrSurf;
    case WM_CTLCOLORLISTBOX:
        if ((HWND)l == hList)
        {
            SetTextColor((HDC)w, cFg);
            SetBkColor((HDC)w, cPanel);
            return (LRESULT)hbrPanel;
        }
        SetTextColor((HDC)w, cFg);
        SetBkColor((HDC)w, cSurf);
        return (LRESULT)hbrSurf;
    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)w, cDim);
        SetBkColor((HDC)w, cBg);
        return (LRESULT)hbrBg;
    case WM_CONTEXTMENU:
        if ((HWND)w == hList)
        {
            POINT pt;
            pt.x = (short)LOWORD(l);
            pt.y = (short)HIWORD(l);
            if (l == -1)
            { /* keyboard (Shift+F10 / menu key): at the selected item */
                RECT ir;
                int s = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
                if (s < 0 || SendMessageW(hList, LB_GETITEMRECT, (WPARAM)s, (LPARAM)&ir) == LB_ERR)
                    return 0;
                pt.x = ir.left + 20;
                pt.y = ir.bottom;
                ClientToScreen(hList, &pt);
                conv_menu(s, pt.x, pt.y);
            }
            else
            {
                POINT cp = pt;
                ScreenToClient(hList, &cp);
                DWORD r = (DWORD)SendMessageW(hList, LB_ITEMFROMPOINT, 0, MAKELPARAM(cp.x, cp.y));
                if (HIWORD(r) == 0 && (int)LOWORD(r) < nmap)
                    conv_menu((int)LOWORD(r), pt.x, pt.y);
            }
            return 0;
        }
        break;
    case WM_DROPFILES:
    {
        HDROP d = (HDROP)w;
        UINT n = DragQueryFileW(d, 0xFFFFFFFF, NULL, 0);
        for (UINT i = 0; i < n; i++)
        {
            wchar_t f[MAX_PATH];
            DragQueryFileW(d, i, f, MAX_PATH);
            add_file(f);
        }
        DragFinish(d);
        SetFocus(hInput);
        return 0;
    }
    case WM_COMMAND:
    {
        int id = LOWORD(w), nt = HIWORD(w);
        if (id == IDC_LIST && nt == LBN_SELCHANGE)
        {
            int s = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
            if (s >= 0 && s < nmap && convs[lmap[s]] != cur)
            {
                cur = convs[lmap[s]];
                render_all();
                stats_reset();
            }
            return 0;
        }
        if (id == IDC_SEARCH && nt == EN_CHANGE)
        {
            SetTimer(h, 2, 200, NULL);
            return 0;
        }
        if (id == IDC_INPUT)
            return 0;
        if (id == IDC_MODEL && nt == CBN_SELCHANGE)
        {
            int s = (int)SendMessageW(hModel, CB_GETCURSEL, 0, 0);
            if (s >= 0)
            {
                model_from_combo(s);
                cfg_save();
            }
            return 0;
        }
        switch (id)
        {
        case IDC_NEW:
            new_chat();
            break;
        case IDC_DELC:
            act_delconv();
            break;
        case IDC_EXP:
            act_export();
            break;
        case IDC_IMP:
            act_import();
            break;
        case IDC_SET:
            show_settings();
            break;
        case IDC_TEMPLATE:
            act_template();
            break;
        case IDC_REFRESH:
            refresh_models();
            break;
        case IDC_ATTACH:
        {
            wchar_t f[MAX_PATH] = L"";
            OPENFILENAMEW o;
            memset(&o, 0, sizeof o);
            o.lStructSize = sizeof o;
            o.hwndOwner = h;
            o.lpstrFile = f;
            o.nMaxFile = MAX_PATH;
            o.Flags = OFN_FILEMUSTEXIST;
            o.lpstrFilter = L"All supported\0*.png;*.jpg;*.jpeg;*.gif;*.webp;*.mp3;*.wav;*.txt;*.md;*.json;*.csv;*.log;*.c;*.h;*.cpp;*.py;*.ts;*.js;*.html;*.css;*.xml;*.yaml;*.yml\0All files\0*.*\0";
            if (GetOpenFileNameW(&o))
                add_file(f);
            break;
        }
        case IDC_REGEN:
            act_regen();
            break;
        case IDC_EDIT:
            act_edit();
            break;
        case IDC_EDITREPLY:
            act_edit_reply();
            break;
        case IDC_MCPBTN:
            mcp_menu();
            break;
        case IDC_TOOLSBTN:
            act_tools();
            break;
        case IDC_QUEUE:
            act_queue();
            break;
        case IDC_CONTEXT:
            act_context();
            break;
        case IDC_DELM:
            act_delmsg();
            break;
        case IDC_PREV:
            act_nav(-1);
            break;
        case IDC_NEXT:
            act_nav(1);
            break;
        case IDC_COPY:
            act_copy();
            break;
        case IDC_SEND:
            do_send();
            break;
        case IDC_FIND:
            if (!sbVisible)
            {
                sbVisible = 1;
                layout();
            }
            SetFocus(hSearch);
            break;
        case IDC_SIDEBAR:
            sbVisible = !sbVisible;
            layout();
            break;
        }
        return 0;
    }
    case WM_APP_MSG:
    {
        Msg *g = (Msg *)l;
        if (g->kind <= 1 && genConv && genNode < genConv->cnt)
        {
            Node *n = &genConv->n[genNode];
            app_amort(g->kind ? &n->reasoning : &n->content, &gl[g->kind], &gc[g->kind], g->text);
            if (cur == genConv && (g->kind == 0 || G.thoughts))
            {
                if (g_pend.n && g_pend_kind != g->kind)
                    flush_stream();
                g_pend_kind = g->kind;
                sb_puts(&g_pend, g->text);
                pace_arrival();
                if (!g_timer_on)
                {
                    SetTimer(h, 1, flush_interval(), NULL);
                    g_timer_on = 1;
                }
            }
        }
        else
        {
            if (g->kind != 2 && g->kind < 11)
                flush_stream(); /* stats, token info and progress arrive while streaming; they must not force a flush */
            if (g->kind == 2)
            {
                if (G.stats)
                    set_status(g->text);
                InvalidateRect(hMain, &rcGauge, FALSE);
            }
            else if (g->kind == 11)
            { /* uncertain token of the answer: "off\tlen\tp\talternatives" */
                if (genConv && genNode < genConv->cnt)
                {
                    char *a = g->text, *b = strchr(a, '\t'), *c = b ? strchr(b + 1, '\t') : NULL, *d = c ? strchr(c + 1, '\t') : NULL;
                    int off = atoi(a), len = d ? atoi(b + 1) : 0;
                    if (d && off >= 0 && len >= 0 && (size_t)off + (size_t)len <= strlen(genConv->n[genNode].content))
                        tk_add(&genConv->n[genNode], off, len, (float)atof(c + 1), d + 1);
                }
            }
            else if (g->kind == 12)
            { /* prompt processing progress: "processed\ttotal\tcache\tms" */
                int pr = 0, tot = 0, ca = 0, ms = 0;
                char st[200];
                sscanf(g->text, "%d\t%d\t%d\t%d", &pr, &tot, &ca, &ms);
                if (tot > 0 && pr < tot)
                {
                    snprintf(st, sizeof st, "Reading the prompt: %d%%  (%d of %d tokens, %d from cache)%s", (int)((long long)pr * 100 / tot), pr, tot, ca,
                             tot - ca > 2000 ? "  -  a long prompt takes a while" : "");
                    set_status(st);
                }
            }
            else if (g->kind == 13)
            {
                if (genConv && genNode < genConv->cnt)
                    genConv->n[genNode].trunc = 1;
            }
            else if (g->kind == 14)
                kv_mark(g->text); /* this chat's KV cache is now saved on the server */
            else if (g->kind == 16)
                mcp_btn_update(); /* MCP servers started / stopped */
            else if (g->kind == 15)
                kv_forget(g->text); /* its saved KV cache could not be restored */
            else if (g->kind == 3)
            {
                char b[700];
                snprintf(b, sizeof b, "\n[Error] %s\n", g->text);
                if (cur == genConv)
                    re_add(b, strlen(b), cErr, 0, FACE);
                set_status(g->text);
            }
            else if (g->kind == 4)
            {
                int cont = 0;
                if (g_timer_on)
                {
                    KillTimer(h, 1);
                    g_timer_on = 0;
                }
                if (genConv && genNode < genConv->cnt)
                {
                    Node *n = &genConv->n[genNode];
                    int maxr = atoi(G.toolmax);
                    if (!g_stop)
                        ms_note_gen(g_last_prompt_n, g_job_chars, n->trunc);
                    if (maxr <= 0)
                        maxr = 10;
                    if (n->tcalls && *n->tcalls)
                    {
                        if (g_stop)
                        {
                            free(n->tcalls);
                            n->tcalls = NULL;
                        }
                        else if (g_toolround >= maxr)
                        {
                            free(n->tcalls);
                            n->tcalls = NULL;
                            set_status("Tool round limit reached (Settings > Max tool rounds)");
                        }
                        else
                            cont = 1;
                    }

                    {
                        int lim = ctx_lim(), used = g_last_prompt_n + g_last_pred_n;
                        if (G.autocompact && lim > 0 && !g_stop)
                        {
                            int trig = ctx_trigger(lim);
                            if (used > trig)
                                compact_maybe(genConv, used, trig, 0);
                            else if (!cont && G.precompact && used > trig * 85 / 100)
                                compact_maybe(genConv, used, trig * 85 / 100, 1); /* idle now: prepare early */
                        }
                    }
                    g_cpt = ms_cpt();

                    if (!cont && !n->content[0] && !n->reasoning[0])
                    {
                        Node *pn = &genConv->n[n->parent];
                        for (int k = 0; k < pn->nk; k++)
                            if (pn->kids[k] == genNode)
                            {
                                memmove(&pn->kids[k], &pn->kids[k + 1], (size_t)(pn->nk - k - 1) * sizeof(int));
                                pn->nk--;
                                break;
                            }
                        if (genConv->leaf == genNode)
                            genConv->leaf = n->parent;
                    }
                }
                if (genConv)
                    genConv->mod = now_ms();
                if (cont)
                {
                    int lim = ctx_lim();
                    size_t cap = 60000;
                    g_toolround++;
                    set_status("Running tools...");
                    if (lim > 0)
                    { /* each result may use at most ~40% of the free context, shared between the calls */
                        long long fr = (long long)lim - g_last_prompt_n - g_last_pred_n - reply_reserve(&G, lim);
                        int nc = tc_count(genConv->n[genNode].tcalls);
                        cap = fr > 0 ? (size_t)(fr * 0.4 * g_cpt / (nc > 0 ? nc : 1)) : 4000;
                    }
                    tool_run_start(genConv->n[genNode].tcalls, cap, genConv->id);
                } /* stays busy until the tools finish */
                else
                    set_busy(0);
                refresh_list();
                render_all();
                save_all();
                ms_save();
                if (!cont && genConv && genNode < genConv->cnt && genConv->n[genNode].trunc && !g_stop)
                    set_status("The reply was cut at Max tokens. Edit reply -> Continue from here lets the model go on.");
                if (!busy)
                {
                    if (g_stop && g_queue_text)
                    { /* stopped: the queued message goes back to the input box */
                        wchar_t *qw = w_from(g_queue_text);
                        SetWindowTextW(hInput, qw);
                        free(qw);
                        for (int i = 0; i < g_queue_natt && npend < 16; i++)
                            pend[npend++] = g_queue_att[i];
                        g_queue_natt = 0;
                        free(g_queue_text);
                        g_queue_text = NULL;
                    }
                    else
                        queue_flush();
                }
                InvalidateRect(hMain, &rcGauge, FALSE);
            }
            else if (g->kind == 5)
            { /* tool_calls of the assistant message just streamed */
                if (genConv && genNode < genConv->cnt)
                    setstr(&genConv->n[genNode].tcalls, g->text);
            }
            else if (g->kind == 6)
            { /* tool result: "id\nname\nresult" */
                char *id = g->text, *nm = strchr(id, '\n'), *res = NULL;
                if (nm)
                {
                    *nm++ = 0;
                    res = strchr(nm, '\n');
                }
                if (res && genConv)
                {
                    *res++ = 0;
                    int t = node_add(genConv, genConv->leaf, ROLE_TOOL);
                    Node *tn = &genConv->n[t];
                    if (!strncmp(res, "\x01IMAGE ", 7) && strchr(res, '\n'))
                    { /* screenshot: the image is attached to the tool result */
                        char *nl2 = strchr(res, '\n');
                        *nl2 = 0;
                        att_add(tn, ATT_IMAGE, "screenshot.png", res + 7, "image/png");
                        res = nl2 + 1;
                    }
                    setstr(&tn->content, res);
                    setstr(&tn->tcid, id);
                    setstr(&tn->tname, nm);
                    genConv->leaf = t;
                    if (cur == genConv)
                        render_all();
                }
            }
            else if (g->kind == 8)
            { /* all tools of this round finished */
                if (g_stop || !genConv)
                {
                    set_busy(0);
                    refresh_list();
                    render_all();
                    save_all();
                    if (g_queue_text)
                    { /* stopped: the queued message goes back to the input box */
                        wchar_t *qw = w_from(g_queue_text);
                        SetWindowTextW(hInput, qw);
                        free(qw);
                        for (int i = 0; i < g_queue_natt && npend < 16; i++)
                            pend[npend++] = g_queue_att[i];
                        g_queue_natt = 0;
                        free(g_queue_text);
                        g_queue_text = NULL;
                    }
                }
                else
                {
                    if (cur != genConv)
                        select_conv(genConv);
                    save_all();
                    start_gen_ex(genConv->leaf, 1);
                }
            }
            else if (g->kind == 9)
            {
                set_status(g->text);
            }
            else if (g->kind == 10 && !strcmp(g->text, "!cancelled"))
            { /* Stop, or the user's message went first: not a failure, may run again later */
                g_compacting = 0;
                g_clast_fail = 0;
                set_status(g_compact_early ? "Early compression cancelled: your message went first." : "Compression cancelled.");
                if (g_wait_gen)
                { /* cancelled while a generation waited for it: send with the uncompressed chat (fitting keeps it inside the context) */
                    int alive = 0;
                    g_wait_gen = 0;
                    for (int i = 0; i < nconv; i++)
                        if (convs[i] == g_wait_conv)
                            alive = 1;
                    if (alive && g_wait_parent < g_wait_conv->cnt && busy)
                    {
                        char *pf = g_wait_prefill;
                        g_wait_prefill = NULL;
                        if (cur != g_wait_conv)
                            select_conv(g_wait_conv);
                        start_gen_px(g_wait_parent, g_wait_cont, pf);
                        free(pf);
                    }
                    else
                        set_busy(0);
                }
                queue_flush();
                free(g->text);
                free(g);
                return 0;
            }
            else if (g->kind == 10)
            {
                int ok = 0;
                const char *why = g->text && g->text[0] == '!' ? g->text + 1 : "the conversation changed while summarizing";
                if (g->text && *g->text && g->text[0] != '!')
                {
                    char *nl1 = strchr(g->text, '\n'), *nl2 = nl1 ? strchr(nl1 + 1, '\n') : NULL;
                    if (nl2)
                    {
                        Conv *tc = (Conv *)(size_t)strtoull(g->text, NULL, 10);
                        int keep = atoi(nl1 + 1), alive = 0;
                        for (int i = 0; i < nconv; i++)
                            if (convs[i] == tc)
                                alive = 1;
                        if (alive && keep > 0 && keep < tc->cnt && tc->n[keep].parent >= 0)
                        {
                            int s = node_add(tc, tc->root, ROLE_SYS);
                            Node *kn;
                            Node *op;
                            setstr(&tc->n[s].content, nl2 + 1);
                            { /* keep the original prompt: the summary node's `reasoning` field holds it (inherited when
                                 an earlier summary is compressed again) */
                                int t = keep;
                                while (tc->n[t].parent >= 0 && tc->n[t].parent != tc->root)
                                    t = tc->n[t].parent;
                                const char *orig = tc->n[t].role == ROLE_SYS    ? tc->n[t].reasoning
                                                   : tc->n[t].role == ROLE_USER ? tc->n[t].content
                                                                                : "";
                                SB o = {0};
                                compact_clip(&o, orig, 20000);
                                setstr(&tc->n[s].reasoning, o.p ? o.p : "");
                                free(o.p);
                            }
                            kn = &tc->n[keep];
                            op = &tc->n[kn->parent];
                            for (int k = 0; k < op->nk; k++)
                                if (op->kids[k] == keep)
                                {
                                    memmove(op->kids + k, op->kids + k + 1, (size_t)(op->nk - k - 1) * sizeof(int));
                                    op->nk--;
                                    break;
                                }
                            kn->parent = s;
                            kid_add(tc, s, keep);
                            tc->mod = now_ms();
                            tc->dirty = 1;
                            if (cur == tc)
                                render_all();
                            save_all();
                            ok = 1;
                        }
                    }
                }
                g_compacting = 0;
                g_clast_fail = !ok;
                if (ok)
                    g_meas_conv = NULL; /* the prompt shrank: the old measurement no longer applies */
                if (ok)
                    set_status("Context compressed.");
                else
                {
                    char sb2[520];
                    snprintf(sb2, sizeof sb2, "Context compression failed: %.420s - will retry after a few more messages.", why);
                    set_status(sb2);
                }
                if (g_wait_gen)
                { /* resume the generation that waited for the compression */
                    int alive = 0;
                    g_wait_gen = 0;
                    for (int i = 0; i < nconv; i++)
                        if (convs[i] == g_wait_conv)
                            alive = 1;
                    if (alive && g_wait_parent < g_wait_conv->cnt)
                    {
                        char *pf = g_wait_prefill;
                        g_wait_prefill = NULL;
                        if (cur != g_wait_conv)
                            select_conv(g_wait_conv);
                        start_gen_px(g_wait_parent, g_wait_cont, pf);
                        free(pf);
                    }
                    else
                        set_busy(0);
                }
                queue_flush();
                InvalidateRect(hMain, &rcGauge, FALSE);
                free(g->text);
                free(g);
                return 0;
            }
        }

        free(g->text);
        free(g);
        return 0;
    }
    case WM_TIMER:
        if (w == 1)
        {
            flush_step();
            if (g_pend.n)
                SetTimer(h, 1, flush_interval(), NULL); /* more waiting: next tick, spaced by the measured cost */
            else
            {
                KillTimer(h, 1);
                g_timer_on = 0;
            }
        }
        else if (w == 3)
        {
            if (!fade_tick())
            {
                KillTimer(h, 3);
                g_fade_timer = 0;
            }
        }
        else if (w == 2)
        {
            KillTimer(h, 2);
            refresh_list();
        }
        return 0;
    case WM_CLOSE:
        if (busy)
            stop_gen();
        compact_cancel();
        save_all();
        cfg_save();
        ms_save();
        DestroyWindow(h);
        return 0;
    case WM_ENDSESSION:
        if (w)
        {
            save_all();
            cfg_save();
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE hp, PWSTR cl, int show)
{
    (void)hp;
    (void)cl;
    LoadLibraryW(L"Msftedit.dll");
    cfg_defaults(&G);
    init_data_dir();
    load_all();
    set_theme();
#ifdef LLAMAC_TOOLTEST
    /* developer test mode (not in release builds): llamac.exe --tool <name> <args.json> <out.txt>
       runs one built-in tool with the saved settings, without approval dialogs, and writes its result */
    {
        int argc = 0;
        wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argc == 5 && !wcscmp(argv[1], L"--tool"))
        {
            size_t n = 0;
            char *name = u_from(argv[2]), *args = (char *)read_file(argv[3], &n), *res;
            G.autoappr = 1;
            fs_register();
            mem_register();
            date_register();
            guides_register();
            extra_register();
            refresh_models(); /* context size, vision, slots */
            if (!strcmp(name, "screenshot"))
            { /* bypasses the approval dialog that the tool always shows */
                J *a = json_parse_strict(args ? args : "{}"); /* strict, like tool_execute: bad JSON must not mean "whole screen" */
                g_vision = 1;
                res = a ? shot_call(a) : xstrdup("Error: invalid JSON arguments");
                j_free(a);
            }
            else
                res = tool_execute(name, args ? args : "{}");
            write_file(argv[4], res, strlen(res));
            return 0;
        }
    }
#endif
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = hbrBg;
    wc.lpszClassName = L"LlamaC";
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);
    HWND w = CreateWindowExW(0, L"LlamaC", L"LlamaC", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 820, NULL, NULL, hi, NULL);
    if (!w)
        return 1;
    ACCEL ac[] = {
        {FVIRTKEY | FCONTROL | FSHIFT, 'N', IDC_NEW},
        {FVIRTKEY | FCONTROL | FSHIFT, 'D', IDC_DELC},
        {FVIRTKEY | FCONTROL, 'K', IDC_FIND},
        {FVIRTKEY | FCONTROL, 'B', IDC_SIDEBAR},
    };
    hAcc = CreateAcceleratorTableW(ac, 4);
    ShowWindow(w, show);
    UpdateWindow(w);
    MSG msg;
    timeBeginPeriod(1);
    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        if (!TranslateAcceleratorW(w, hAcc, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    timeEndPeriod(1);
    return (int)msg.wParam;
}

#endif /* LLAMAC_MAIN_WINDOW_H */
