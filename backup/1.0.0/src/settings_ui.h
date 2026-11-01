/* settings_ui.h - Settings dialog */
#ifndef LLAMAC_SETTINGS_UI_H
#define LLAMAC_SETTINGS_UI_H

/* ---------- settings window (values left empty = use llama-server's own) ---------- */
static HWND hSet, setCtl[NFIELDS]; static int setDone;
static LRESULT CALLBACK SetProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ERASEBKGND: { RECT r; GetClientRect(h, &r); FillRect((HDC)w, &r, hbrPanel); return 1; }
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN: SetTextColor((HDC)w, cFg); SetBkColor((HDC)w, cPanel); return (LRESULT)hbrPanel;
    case WM_CTLCOLOREDIT: SetTextColor((HDC)w, cFg); SetBkColor((HDC)w, cSurf); return (LRESULT)hbrSurf;
    case WM_DRAWITEM: draw_btn((DRAWITEMSTRUCT *)l); return TRUE;
    case WM_COMMAND:
        if (LOWORD(w) == IDC_RESET) {
            for (int i = 0; i < NFIELDS; i++) if (FIELDS[i].kind == 1 && setCtl[i]) SetWindowTextW(setCtl[i], L"");
        } else if (LOWORD(w) == IDOK) {
            for (int i = 0; i < NFIELDS; i++) {
                const Field *f = &FIELDS[i]; if (!f->label) continue;
                if (f->kind == 3) *(int *)((char *)&G + f->off) = (SendMessageW(setCtl[i], BM_GETCHECK, 0, 0) == BST_CHECKED);
                else { char *t = get_text(setCtl[i]); field_set(&G, f, t); free(t); }
            }
            cfg_save(); set_theme(); render_all(); DestroyWindow(h);
        } else if (LOWORD(w) == IDCANCEL) DestroyWindow(h);
        return 0;
    case DM_GETDEFID: return MAKELONG(IDOK, DC_HASDEFID);
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: setDone = 1; return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static void show_settings(void) {
    static int reg = 0;
    if (!reg) {
        WNDCLASSEXW wc; memset(&wc, 0, sizeof wc); wc.cbSize = sizeof wc; wc.lpfnWndProc = SetProc; wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW); wc.lpszClassName = L"LCSet"; RegisterClassExW(&wc); reg = 1;
    }
    const int colW = 450, labW = 170, rowH = 32; int rowsL = 0, rowsR = 0;
    for (int i = 0; i < NFIELDS; i++) if (FIELDS[i].label) { if (FIELDS[i].kind == 1) rowsR++; else rowsL++; }
    int rows = rowsL > rowsR ? rowsL : rowsR, CW = colW * 2 + 40, CH = 44 + rows * rowH + 80;
    RECT wr = { 0, 0, CW, CH }; DWORD st = WS_POPUP | WS_CAPTION | WS_SYSMENU; AdjustWindowRectEx(&wr, st, FALSE, WS_EX_DLGMODALFRAME);
    RECT r; GetWindowRect(hMain, &r);
    hSet = CreateWindowExW(WS_EX_DLGMODALFRAME, L"LCSet", L"Settings", st, r.left + 60, r.top + 40, wr.right - wr.left, wr.bottom - wr.top, hMain, NULL, GetModuleHandleW(NULL), NULL);
    apply_frame(hSet);
    HWND t1 = CreateWindowExW(0, L"STATIC", L"General  &&  chat template", WS_CHILD | WS_VISIBLE, 20, 14, 400, 22, hSet, NULL, NULL, NULL);
    HWND t2 = CreateWindowExW(0, L"STATIC", L"Sampling overrides  -  empty = use llama-server value", WS_CHILD | WS_VISIBLE, 20 + colW + 10, 14, 440, 22, hSet, NULL, NULL, NULL);
    SendMessageW(t1, WM_SETFONT, (WPARAM)hFontB, TRUE); SendMessageW(t2, WM_SETFONT, (WPARAM)hFontB, TRUE);
    int rl = 0, rr = 0;
    for (int i = 0; i < NFIELDS; i++) {
        const Field *f = &FIELDS[i]; if (!f->label) continue;
        int col = (f->kind == 1), x = 20 + col * (colW + 10), y = 44 + (col ? rr++ : rl++) * rowH; wchar_t *lb = w_from(f->label);
        if (f->kind == 3) {
            setCtl[i] = CreateWindowExW(0, L"BUTTON", lb, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, x, y, colW - 10, 24, hSet, NULL, NULL, NULL);
            SetWindowTheme(setCtl[i], L"", L"");
            SendMessageW(setCtl[i], BM_SETCHECK, *(int *)((char *)&G + f->off) ? BST_CHECKED : BST_UNCHECKED, 0);
        } else {
            CreateWindowExW(0, L"STATIC", lb, WS_CHILD | WS_VISIBLE | SS_NOPREFIX, x, y + 4, labW, 20, hSet, NULL, NULL, NULL);
            SB v = {0}; field_get(&G, f, &v); wchar_t *wv = w_from(sbs(&v));
            setCtl[i] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", wv, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, x + labW, y, colW - labW - 10, 24, hSet, NULL, NULL, NULL);
            if (f->kind == 1) {
                char cue[80]; snprintf(cue, sizeof cue, "%s%s", srvdef[i][0] ? "server: " : "server default", srvdef[i]);
                wchar_t *wc = w_from(cue); SendMessageW(setCtl[i], EM_SETCUEBANNER, TRUE, (LPARAM)wc); free(wc);
            }
            free(wv); free(v.p);
        }
        free(lb);
    }
    mkbtn(hSet, L"Reset to server defaults", IDC_RESET);
    mkbtn(hSet, L"OK", IDOK); mkbtn(hSet, L"Cancel", IDCANCEL);
    MoveWindow(GetDlgItem(hSet, IDC_RESET), 20 + colW + 10, CH - 56, 200, 34, TRUE);
    MoveWindow(GetDlgItem(hSet, IDOK), CW - 190, CH - 56, 80, 34, TRUE);
    MoveWindow(GetDlgItem(hSet, IDCANCEL), CW - 100, CH - 56, 80, 34, TRUE);
    for (HWND c = GetWindow(hSet, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) if (c != t1 && c != t2) SendMessageW(c, WM_SETFONT, (WPARAM)hFont, TRUE);
    setDone = 0; EnableWindow(hMain, FALSE); ShowWindow(hSet, SW_SHOW);
    MSG msg;
    while (!setDone && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hSet, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(hMain, TRUE); SetForegroundWindow(hMain);
}

#endif /* LLAMAC_SETTINGS_UI_H */
