/* main_window.h - Main window procedure, layout, WinMain */
#ifndef LLAMAC_MAIN_WINDOW_H
#define LLAMAC_MAIN_WINDOW_H

/* ---------- main window ---------- */
static LRESULT CALLBACK InputProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CHAR) {
        if (w == L'\r' && !(GetKeyState(VK_SHIFT) & 0x8000)) { if (!busy) PostMessageW(hMain, WM_COMMAND, IDC_SEND, 0); return 0; }
        if (w == 1) { SendMessageW(h, EM_SETSEL, 0, (LPARAM)-1); return 0; }
    }
    if (m == WM_PASTE && G.smartpaste && OpenClipboard(h)) {
        HANDLE g = GetClipboardData(CF_UNICODETEXT);
        if (g) {
            const wchar_t *t = (const wchar_t *)GlobalLock(g);
            if (t && wcslen(t) > 2000) { char *u = u_from(t); pend_add(ATT_TEXT, "pasted-text.txt", u, "text/plain"); free(u); GlobalUnlock(g); CloseClipboard(); return 0; }
            if (t) GlobalUnlock(g);
        }
        CloseClipboard();
    }
    return CallWindowProcW(oldInput, h, m, w, l);
}
static void layout(void) {
    RECT rc; GetClientRect(hMain, &rc); int W = rc.right, H = rc.bottom;
    if (W < 100 || H < 100) return;
    int sw = sbVisible ? 272 : 0, x0 = sw, Wr = W - sw;
    SetRect(&rcSide, 0, 0, sw, H);
    int vis = sbVisible ? SW_SHOW : SW_HIDE;
    ShowWindow(hSearch, vis); ShowWindow(hList, vis); ShowWindow(B(IDC_NEW), vis);
    if (sbVisible) {
        MoveWindow(B(IDC_NEW), 14, 58, sw - 28, 40, TRUE);
        SetRect(&rcSearch, 14, 108, sw - 14, 144);
        MoveWindow(hSearch, rcSearch.left + 14, rcSearch.top + 8, sw - 28 - 28, 22, TRUE);
        MoveWindow(hList, 6, 154, sw - 12, H - 154 - 8, TRUE);
    }
    SetRect(&rcHead, x0, 0, W, 60);
    { const int ids[] = { IDC_REFRESH, IDC_SET, IDC_TEMPLATE, IDC_IMP, IDC_EXP }; const int wd[] = { 38, 88, 84, 76, 76 }; int x = W - 14;
      for (int i = 0; i < 5; i++) { x -= wd[i]; MoveWindow(B(ids[i]), x, 12, wd[i], 36, TRUE); x -= 6; }
      x -= 250; MoveWindow(hModel, x, 17, 250, 300, TRUE);
      SetRect(&rcTitle, x0 + 22, 0, x - 12, 60); }
    int cw = Wr - 48; if (cw < 300) cw = 300;
    int left = x0 + (Wr - cw) / 2; g_colw = cw;
    int boxH = 84, statusH = 22, actH = 36;
    int yStatus = H - statusH - 8, yBox = yStatus - boxH - 6, yAct = yBox - actH - 2;
    MoveWindow(hChat, x0, 60, Wr, yAct - 64, TRUE);
    { int m = (Wr - cw) / 2; SendMessageW(hChat, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(m, m)); }
    { const int ids[] = { IDC_REGEN, IDC_EDIT, IDC_DELM, IDC_PREV, IDC_NEXT, IDC_COPY, IDC_DELC }; const int wd[] = { 100, 92, 100, 36, 36, 62, 100 }; int x = left;
      for (int i = 0; i < 7; i++) { MoveWindow(B(ids[i]), x, yAct + 2, wd[i], 32, TRUE); x += wd[i] + 4; } }
    SetRect(&rcBox, left, yBox, left + cw, yBox + boxH);
    MoveWindow(B(IDC_ATTACH), left + 10, yBox + (boxH - 38) / 2, 38, 38, TRUE);
    MoveWindow(hInput, left + 60, yBox + 14, cw - 60 - 100, boxH - 28, TRUE);
    MoveWindow(B(IDC_SEND), left + cw - 88, yBox + (boxH - 38) / 2, 78, 38, TRUE);
    MoveWindow(hStatus, left, yStatus, cw, 20, TRUE);
    RedrawWindow(hMain, NULL, NULL, RDW_INVALIDATE | RDW_ERASE);
}
static void paint_main(HWND h) {
    PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT r;
    SetBkMode(dc, TRANSPARENT);
    if (sbVisible) {
        FillRect(dc, &rcSide, hbrPanel);
        RECT line = { rcSide.right - 1, 0, rcSide.right, rcSide.bottom }; HBRUSH lb = CreateSolidBrush(cBorder); FillRect(dc, &line, lb);
        SelectObject(dc, hFontT); SetTextColor(dc, cFg); RECT t = { 18, 16, rcSide.right - 10, 50 };
        DrawTextW(dc, L"llama.cpp", -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, hFont); SetTextColor(dc, cAccent); RECT t2 = { 124, 22, rcSide.right - 10, 50 };
        DrawTextW(dc, L"  native", -1, &t2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        rrect(dc, &rcSearch, 16, cSurf, cBorder);
        DeleteObject(lb);
    }
    r = rcHead; r.top = r.bottom - 1; { HBRUSH lb = CreateSolidBrush(cBorder); FillRect(dc, &r, lb); DeleteObject(lb); }
    if (cur) {
        wchar_t *w = w_from(cur->name); SelectObject(dc, hFontB); SetTextColor(dc, cFg);
        DrawTextW(dc, w, -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX); free(w);
    }
    rrect(dc, &rcBox, 26, cSurf, cBorder);
    EndPaint(h, &ps);
}
static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        hMain = h; HINSTANCE hi = GetModuleHandleW(NULL);
        hFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        hFontB = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        hFontT = CreateFontW(-24, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        hSearch = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10, h, (HMENU)(INT_PTR)IDC_SEARCH, hi, NULL);
        SendMessageW(hSearch, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search chats  (Ctrl+K)");
        hList = CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS, 0, 0, 10, 10, h, (HMENU)(INT_PTR)IDC_LIST, hi, NULL);
        hChat = CreateWindowExW(0, L"RICHEDIT50W", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 0, 0, 10, 10, h, (HMENU)(INT_PTR)IDC_CHAT, hi, NULL);
        SendMessageW(hChat, EM_EXLIMITTEXT, 0, 0x7FFFFFF0); SendMessageW(hChat, EM_SETBKGNDCOLOR, 0, (LPARAM)cBg);
        SetWindowSubclass(hChat, ChatSub, 2, 0);
        SendMessageW(hChat, EM_SETUNDOLIMIT, 0, 0);
        hInput = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, 0, 0, 10, 10, h, (HMENU)(INT_PTR)IDC_INPUT, hi, NULL);
        oldInput = (WNDPROC)SetWindowLongPtrW(hInput, GWLP_WNDPROC, (LONG_PTR)InputProc);
        hStatus = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS | SS_NOPREFIX, 0, 0, 10, 10, h, (HMENU)(INT_PTR)IDC_STATUS, hi, NULL);
        hModel = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWNLIST, 0, 0, 10, 300, h, (HMENU)(INT_PTR)IDC_MODEL, hi, NULL);
        SendMessageW(hList, LB_SETITEMHEIGHT, 0, 40);
        { HWND c[] = { hSearch, hList, hInput, hStatus, hModel }; for (int i = 0; i < 5; i++) SendMessageW(c[i], WM_SETFONT, (WPARAM)hFont, TRUE); }
        SendMessageW(hSearch, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(2, 2));
        SendMessageW(hInput, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(4, 4));
        mkbtn(h, L"+  New chat", IDC_NEW); mkbtn(h, L"Export", IDC_EXP); mkbtn(h, L"Import", IDC_IMP); mkbtn(h, L"Template", IDC_TEMPLATE);
        mkbtn(h, L"Settings", IDC_SET); mkbtn(h, L"\u21BB", IDC_REFRESH);
        mkbtn(h, L"Regenerate", IDC_REGEN); mkbtn(h, L"Edit last", IDC_EDIT); mkbtn(h, L"Delete msg", IDC_DELM);
        mkbtn(h, L"\u25C0", IDC_PREV); mkbtn(h, L"\u25B6", IDC_NEXT); mkbtn(h, L"Copy", IDC_COPY); mkbtn(h, L"Delete chat", IDC_DELC);
        mkbtn(h, L"+", IDC_ATTACH); mkbtn(h, L"Send", IDC_SEND);
        DragAcceptFiles(h, TRUE);
        set_theme();
        PostMessageW(h, WM_APP_INIT, 0, 0);
		{ wchar_t t[MAX_PATH + 40]; _snwprintf(t, MAX_PATH + 40, L"llama.cpp  -  data: %ls", dataDir); SetWindowTextW(h, t); }
        return 0;
    }
    case WM_APP_INIT:
        layout();
        set_status("Connecting...");
        if (!nconv) convs_add(conv_new());
        qsort(convs, (size_t)nconv, sizeof(Conv *), cmp_mod);
        select_conv(convs[0]);
        refresh_models(); SetFocus(hInput);
        return 0;
    case WM_SIZE: if (B(IDC_SEND)) layout(); return 0;
	case WM_EXITSIZEMOVE: render_all(); return 0;
    case WM_ERASEBKGND: { RECT r; GetClientRect(h, &r); FillRect((HDC)w, &r, hbrBg); return 1; }
    case WM_PAINT: paint_main(h); return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO *)l)->ptMinTrackSize.x = 940; ((MINMAXINFO *)l)->ptMinTrackSize.y = 560; return 0;
    case WM_MEASUREITEM: if (((MEASUREITEMSTRUCT *)l)->CtlType == ODT_LISTBOX) { ((MEASUREITEMSTRUCT *)l)->itemHeight = 40; return TRUE; } break;
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *d = (DRAWITEMSTRUCT *)l;
        if (d->CtlType == ODT_LISTBOX) draw_list_item(d); else if (d->CtlType == ODT_BUTTON) draw_btn(d);
        return TRUE;
    }
    case WM_CTLCOLOREDIT: SetTextColor((HDC)w, cFg); SetBkColor((HDC)w, cSurf); return (LRESULT)hbrSurf;
    case WM_CTLCOLORLISTBOX:
        if ((HWND)l == hList) { SetTextColor((HDC)w, cFg); SetBkColor((HDC)w, cPanel); return (LRESULT)hbrPanel; }
        SetTextColor((HDC)w, cFg); SetBkColor((HDC)w, cSurf); return (LRESULT)hbrSurf;
    case WM_CTLCOLORSTATIC: SetTextColor((HDC)w, cDim); SetBkColor((HDC)w, cBg); return (LRESULT)hbrBg;
    case WM_DROPFILES: {
        HDROP d = (HDROP)w; UINT n = DragQueryFileW(d, 0xFFFFFFFF, NULL, 0);
        for (UINT i = 0; i < n; i++) { wchar_t f[MAX_PATH]; DragQueryFileW(d, i, f, MAX_PATH); add_file(f); }
        DragFinish(d); return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(w), nt = HIWORD(w);
        if (id == IDC_LIST && nt == LBN_SELCHANGE) {
            int s = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
            if (s >= 0 && s < nmap && convs[lmap[s]] != cur) { cur = convs[lmap[s]]; render_all(); }
            return 0;
        }
        if (id == IDC_SEARCH && nt == EN_CHANGE) { SetTimer(h, 2, 200, NULL); return 0; }
        if (id == IDC_INPUT) return 0;
        if (id == IDC_MODEL && nt == CBN_SELCHANGE) {
            wchar_t t[160]; int s = (int)SendMessageW(hModel, CB_GETCURSEL, 0, 0);
            if (s >= 0) { SendMessageW(hModel, CB_GETLBTEXT, (WPARAM)s, (LPARAM)t); char *u = u_from(t); strncpy(G.model, u, sizeof G.model - 1); free(u); cfg_save(); }
            return 0;
        }
        switch (id) {
        case IDC_NEW: new_chat(); break;
        case IDC_DELC: act_delconv(); break;
        case IDC_EXP: act_export(); break;
        case IDC_IMP: act_import(); break;
        case IDC_SET: show_settings(); break;
        case IDC_TEMPLATE: act_template(); break;
        case IDC_REFRESH: refresh_models(); break;
        case IDC_ATTACH: {
            wchar_t f[MAX_PATH] = L""; OPENFILENAMEW o; memset(&o, 0, sizeof o);
            o.lStructSize = sizeof o; o.hwndOwner = h; o.lpstrFile = f; o.nMaxFile = MAX_PATH; o.Flags = OFN_FILEMUSTEXIST;
            o.lpstrFilter = L"All supported\0*.png;*.jpg;*.jpeg;*.gif;*.webp;*.mp3;*.wav;*.txt;*.md;*.json;*.csv;*.log;*.c;*.h;*.cpp;*.py;*.ts;*.js;*.html;*.css;*.xml;*.yaml;*.yml\0All files\0*.*\0";
            if (GetOpenFileNameW(&o)) add_file(f);
            break;
        }
        case IDC_REGEN: act_regen(); break;
        case IDC_EDIT: act_edit(); break;
        case IDC_DELM: act_delmsg(); break;
        case IDC_PREV: act_nav(-1); break;
        case IDC_NEXT: act_nav(1); break;
        case IDC_COPY: act_copy(); break;
        case IDC_SEND: do_send(); break;
        case IDC_FIND: if (!sbVisible) { sbVisible = 1; layout(); } SetFocus(hSearch); break;
        case IDC_SIDEBAR: sbVisible = !sbVisible; layout(); break;
        }
        return 0;
    }
    case WM_APP_MSG: {
        Msg *g = (Msg *)l;
        if (g->kind <= 1 && genConv && genNode < genConv->cnt) {
            Node *n = &genConv->n[genNode];
            app_amort(g->kind ? &n->reasoning : &n->content, &gl[g->kind], &gc[g->kind], g->text);
            if (cur == genConv && (g->kind == 0 || G.thoughts)) {
                if (g_pend.n && g_pend_kind != g->kind) flush_stream();
                g_pend_kind = g->kind; sb_puts(&g_pend, g->text);
                if (!g_timer_on) { SetTimer(h, 1, 33, NULL); g_timer_on = 1; }
            }
        } else {
            flush_stream();
            if (g->kind == 2) {
                if (G.stats) set_status(g->text);
            } else if (g->kind == 3) {
                char b[700]; snprintf(b, sizeof b, "\n[Error] %s\n", g->text);
                if (cur == genConv) re_add(b, strlen(b), cErr, 0, FACE);
                set_status(g->text);
            } else if (g->kind == 4) {
                if (g_timer_on) { KillTimer(h, 1); g_timer_on = 0; }
                set_busy(0);
                if (genConv && genNode < genConv->cnt) {
                    Node *n = &genConv->n[genNode];
                    if (!n->content[0] && !n->reasoning[0]) {
                        Node *pn = &genConv->n[n->parent];
                        for (int k = 0; k < pn->nk; k++) if (pn->kids[k] == genNode) { memmove(&pn->kids[k], &pn->kids[k + 1], (size_t)(pn->nk - k - 1) * sizeof(int)); pn->nk--; break; }
                        if (genConv->leaf == genNode) genConv->leaf = n->parent;
                    }
                }
                if (genConv) genConv->mod = now_ms();
                refresh_list(); render_all(); save_all();
            }
        }
        free(g->text); free(g); return 0;
    }
    case WM_TIMER:
        if (w == 1) { KillTimer(h, 1); g_timer_on = 0; flush_stream(); }
        else if (w == 2) { KillTimer(h, 2); refresh_list(); }
        return 0;
    case WM_CLOSE:
        if (busy) stop_gen();
        save_all(); cfg_save(); DestroyWindow(h); return 0;
	case WM_ENDSESSION: if (w) { save_all(); cfg_save(); } return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE hp, PWSTR cl, int show) {
    (void)hp; (void)cl;
    LoadLibraryW(L"Msftedit.dll");
    cfg_defaults(&G);
	init_data_dir();
    load_all();
    set_theme();
    WNDCLASSEXW wc; memset(&wc, 0, sizeof wc); wc.cbSize = sizeof wc; wc.lpfnWndProc = MainProc; wc.hInstance = hi;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW); wc.hbrBackground = hbrBg; wc.lpszClassName = L"LlamaC";
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);
    HWND w = CreateWindowExW(0, L"LlamaC", L"llama.cpp", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 820, NULL, NULL, hi, NULL);
    if (!w) return 1;
    ACCEL ac[] = {
        { FVIRTKEY | FCONTROL | FSHIFT, 'N', IDC_NEW }, { FVIRTKEY | FCONTROL | FSHIFT, 'D', IDC_DELC },
        { FVIRTKEY | FCONTROL, 'K', IDC_FIND }, { FVIRTKEY | FCONTROL, 'B', IDC_SIDEBAR },
    };
    hAcc = CreateAcceleratorTableW(ac, 4);
    ShowWindow(w, show); UpdateWindow(w);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!TranslateAcceleratorW(w, hAcc, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    return (int)msg.wParam;
}

#endif /* LLAMAC_MAIN_WINDOW_H */
