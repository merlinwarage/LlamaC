/* text_window.h - Read-only text window (Jinja template view) */
#ifndef LLAMAC_TEXT_WINDOW_H
#define LLAMAC_TEXT_WINDOW_H

/* ---------- simple read-only text window (used for the Jinja template view) ---------- */
static LRESULT CALLBACK TxtProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_SIZE: { HWND e = GetDlgItem(h, 1); if (e) MoveWindow(e, 0, 0, LOWORD(l), HIWORD(l), TRUE); return 0; }
    case WM_CTLCOLOREDIT: case WM_CTLCOLORSTATIC: SetTextColor((HDC)w, cFg); SetBkColor((HDC)w, cBg); return (LRESULT)hbrBg;
    case WM_CLOSE: DestroyWindow(h); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static void show_text(const wchar_t *title, const char *utf8) {
    static int reg = 0;
    if (!reg) {
        WNDCLASSEXW wc; memset(&wc, 0, sizeof wc); wc.cbSize = sizeof wc; wc.lpfnWndProc = TxtProc; wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW); wc.hbrBackground = hbrBg; wc.lpszClassName = L"LCTxt"; RegisterClassExW(&wc); reg = 1;
    }
    SB s = {0};
    for (const char *p = utf8; *p; p++) { if (*p == '\r') continue; if (*p == '\n') sb_putc(&s, '\r'); sb_putc(&s, *p); }
    wchar_t *w = w_from(sbs(&s)); free(s.p);
    RECT r; GetWindowRect(hMain, &r);
    HWND h = CreateWindowExW(0, L"LCTxt", title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, r.left + 80, r.top + 60, 900, 640, hMain, NULL, GetModuleHandleW(NULL), NULL);
    HWND e = CreateWindowExW(0, L"EDIT", w, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 0, 0, 900, 640, h, (HMENU)1, NULL, NULL);
    HFONT mf = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Consolas");
    SendMessageW(e, WM_SETFONT, (WPARAM)mf, TRUE); SetWindowTheme(e, G.dark ? L"DarkMode_Explorer" : L"Explorer", NULL);
    apply_frame(h); free(w);
    RECT cr; GetClientRect(h, &cr); MoveWindow(e, 0, 0, cr.right, cr.bottom, TRUE);
}
static void act_template(void) {
    if (!cur) return;
    char *body = build_template_body(cur, &G), *resp = http_req(L"POST", "/apply-template", body); SB o = {0};
    sb_puts(&o, "=== Rendered prompt (POST /apply-template, current branch) ===\n\n");
    if (resp) { J *r = json_parse(resp); const char *p = j_str(j_get(r, "prompt")); sb_puts(&o, p ? p : resp); j_free(r); }
    else sb_puts(&o, "(the server did not answer /apply-template - is it reachable and started with --jinja?)");
    sb_puts(&o, "\n\n=== Server chat template (Jinja, from /props) ===\n\n");
    sb_puts(&o, g_tmpl && g_tmpl[0] ? g_tmpl : "(not available - press the refresh button to query /props)");
    show_text(L"Jinja chat template", sbs(&o)); free(o.p); free(body); free(resp);
}

#endif /* LLAMAC_TEXT_WINDOW_H */
