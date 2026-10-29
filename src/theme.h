/* theme.h - Theme colours, DWM frame, owner-drawn buttons/list */
#ifndef LLAMAC_THEME_H
#define LLAMAC_THEME_H

/* ---------- theme / modern look ---------- */
static COLORREF adj(COLORREF c, int d) {
    int r = GetRValue(c) + d, g = GetGValue(c) + d, b = GetBValue(c) + d;
    r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b;
    return RGB(r, g, b);
}
static void apply_frame(HWND h) {
    BOOL d = G.dark; int corner = 2; COLORREF cap = cPanel, tx = cFg, bd = cBorder;
    DwmSetWindowAttribute(h, 20, &d, sizeof d);
    DwmSetWindowAttribute(h, 33, &corner, sizeof corner);
    DwmSetWindowAttribute(h, 34, &bd, sizeof bd);
    DwmSetWindowAttribute(h, 35, &cap, sizeof cap);
    DwmSetWindowAttribute(h, 36, &tx, sizeof tx);
}
static void set_theme(void) {
    HBRUSH o1 = hbrBg, o2 = hbrPanel, o3 = hbrSurf;
    if (G.dark) {
        cBg = RGB(15, 15, 18); cPanel = RGB(24, 24, 28); cSurf = RGB(36, 36, 42); cBorder = RGB(58, 58, 68);
        cFg = RGB(236, 236, 240); cDim = RGB(150, 150, 165); cAccent = RGB(99, 102, 241);
        cUser = RGB(165, 180, 252); cAsst = RGB(110, 231, 183); cCode = RGB(253, 224, 140); cCodeBg = RGB(30, 30, 38); cCodeHdr = RGB(44, 44, 58); cType = RGB(45, 212, 191);
        cKw = RGB(192, 132, 252); cStr = RGB(134, 239, 172); cCom = RGB(120, 120, 140); cNum = RGB(251, 146, 60); cFn = RGB(125, 211, 252);
    } else {
        cBg = RGB(255, 255, 255); cPanel = RGB(244, 244, 247); cSurf = RGB(240, 241, 245); cBorder = RGB(214, 216, 224);
        cFg = RGB(24, 24, 32); cDim = RGB(110, 112, 128); cAccent = RGB(79, 70, 229);
        cUser = RGB(67, 56, 202); cAsst = RGB(5, 122, 85); cCode = RGB(154, 52, 18); cCodeBg = RGB(243, 244, 248); cCodeHdr = RGB(228, 231, 240); cType = RGB(15, 118, 110);
        cKw = RGB(126, 34, 206); cStr = RGB(21, 128, 61); cCom = RGB(113, 113, 130); cNum = RGB(194, 65, 12); cFn = RGB(3, 105, 161);
    }
    hbrBg = CreateSolidBrush(cBg); hbrPanel = CreateSolidBrush(cPanel); hbrSurf = CreateSolidBrush(cSurf);
    if (hMain) {
        const wchar_t *t = G.dark ? L"DarkMode_Explorer" : L"Explorer";
        SetClassLongPtrW(hMain, GCLP_HBRBACKGROUND, (LONG_PTR)hbrBg);
        SendMessageW(hChat, EM_SETBKGNDCOLOR, 0, (LPARAM)cBg);
        SetWindowTheme(hList, t, NULL); SetWindowTheme(hChat, t, NULL); SetWindowTheme(hInput, t, NULL);
        SetWindowTheme(hModel, G.dark ? L"DarkMode_CFD" : L"CFD", NULL);
        apply_frame(hMain);
        RedrawWindow(hMain, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }
    if (o1) DeleteObject(o1);
    if (o2) DeleteObject(o2);
    if (o3) DeleteObject(o3);
}
static void rrect(HDC dc, const RECT *r, int rad, COLORREF fill, COLORREF line) {
    HBRUSH b = CreateSolidBrush(fill); HPEN p = CreatePen(PS_SOLID, 1, line);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, r->left, r->top, r->right, r->bottom, rad, rad);
    SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(b); DeleteObject(p);
}
static void draw_btn(DRAWITEMSTRUCT *d) {
    int id = (int)d->CtlID; HDC dc = d->hDC; RECT r = d->rcItem;
    int pressed = (d->itemState & ODS_SELECTED) != 0, hot = (d->hwndItem == hHot);
    int primary = (id == IDC_NEW || id == IDC_SEND || id == IDOK), danger = (id == IDC_SEND && busy);
    COLORREF par = (id == IDC_ATTACH || id == IDC_SEND) ? cSurf : (id == IDC_NEW || id == IDOK || id == IDCANCEL || id == IDC_RESET) ? cPanel : cBg;
    COLORREF bg, fg, ln;
    if (primary) { bg = danger ? RGB(220, 38, 38) : cAccent; if (hot) bg = adj(bg, 22); if (pressed) bg = adj(bg, -28); fg = RGB(255, 255, 255); ln = bg; }
    else { bg = par; fg = cDim; ln = par; if (hot) { bg = adj(cSurf, G.dark ? 14 : -6); fg = cFg; ln = bg; } if (pressed) bg = adj(bg, G.dark ? 14 : -10); if (id == IDCANCEL || id == IDC_RESET) { ln = cBorder; } }
    HBRUSH pb = CreateSolidBrush(par); FillRect(dc, &r, pb); DeleteObject(pb);
    rrect(dc, &r, 14, bg, ln);
    wchar_t t[64]; GetWindowTextW(d->hwndItem, t, 64);
    SelectObject(dc, id == IDC_NEW || id == IDC_SEND ? hFontB : hFont); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, fg);
    DrawTextW(dc, t, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}
static void draw_list_item(DRAWITEMSTRUCT *d) {
    if ((int)d->itemID < 0 || (int)d->itemID >= nmap) { FillRect(d->hDC, &d->rcItem, hbrPanel); return; }
    Conv *c = convs[lmap[d->itemID]]; HDC dc = d->hDC; RECT r = d->rcItem;
    FillRect(dc, &r, hbrPanel);
    int sel = (d->itemState & ODS_SELECTED) != 0;
    RECT pill = { r.left + 6, r.top + 2, r.right - 6, r.bottom - 2 };
    if (sel) { rrect(dc, &pill, 12, cSurf, cSurf); RECT bar = { pill.left + 2, pill.top + 9, pill.left + 5, pill.bottom - 9 }; HBRUSH ab = CreateSolidBrush(cAccent); FillRect(dc, &bar, ab); DeleteObject(ab); }
    wchar_t *w = w_from(c->name); RECT tr = { pill.left + 16, pill.top, pill.right - 10, pill.bottom };
    SelectObject(dc, hFont); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, sel ? cFg : cDim);
    DrawTextW(dc, w, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX); free(w);
}
static LRESULT CALLBACK BtnSub(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    if (m == WM_MOUSEMOVE && hHot != h) { hHot = h; TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 }; TrackMouseEvent(&t); InvalidateRect(h, NULL, FALSE); }
    else if (m == WM_MOUSELEAVE) { hHot = NULL; InvalidateRect(h, NULL, FALSE); }
    else if (m == WM_SETCURSOR) { SetCursor(LoadCursorW(NULL, IDC_HAND)); return TRUE; }
    return DefSubclassProc(h, m, w, l);
}
static HWND mkbtn(HWND par, const wchar_t *t, int id) {
    HWND b = CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, par, (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessageW(b, WM_SETFONT, (WPARAM)hFont, TRUE); SetWindowSubclass(b, BtnSub, 1, 0); return b;
}

#endif /* LLAMAC_THEME_H */
