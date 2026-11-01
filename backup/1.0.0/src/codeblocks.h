/* codeblocks.h - Code blocks with Copy/Download links, link hit-testing */
#ifndef LLAMAC_CODEBLOCKS_H
#define LLAMAC_CODEBLOCKS_H

static const char *ext_for(const char *lang) {
    static const struct { const char *l, *e; } M[] = {
        {"python", "py"}, {"py", "py"}, {"javascript", "js"}, {"js", "js"}, {"jsx", "jsx"}, {"typescript", "ts"}, {"ts", "ts"}, {"tsx", "tsx"},
        {"c", "c"}, {"h", "h"}, {"cpp", "cpp"}, {"c++", "cpp"}, {"cc", "cpp"}, {"hpp", "hpp"}, {"cs", "cs"}, {"csharp", "cs"}, {"java", "java"},
        {"go", "go"}, {"golang", "go"}, {"rust", "rs"}, {"rs", "rs"}, {"kotlin", "kt"}, {"swift", "swift"}, {"php", "php"}, {"ruby", "rb"},
        {"bash", "sh"}, {"sh", "sh"}, {"shell", "sh"}, {"zsh", "sh"}, {"powershell", "ps1"}, {"ps1", "ps1"}, {"json", "json"}, {"yaml", "yml"},
        {"yml", "yml"}, {"toml", "toml"}, {"sql", "sql"}, {"html", "html"}, {"xml", "xml"}, {"css", "css"}, {"markdown", "md"}, {"md", "md"},
        {"dockerfile", "Dockerfile"}, {"lua", "lua"}, {"dart", "dart"}, {"scala", "scala"},
    };
    for (size_t i = 0; i < sizeof M / sizeof M[0]; i++) if (!strcmp(M[i].l, lang)) return M[i].e;
    return "txt";
}


/* ---------- code blocks: header with Copy / Download links ---------- */
typedef struct { char *text; char lang[24]; } CodeBlk;
static CodeBlk *blks; static int nblk, capblk;
typedef struct { LONG a, b; int blk, act; } LinkR;
static LinkR *links; static int nlinks, caplinks;
typedef struct { LONG a, b, hs; int collapsed, node; } Think;
static Think *thinks; static int nthink, capthink;
static int g_rnode = -1;
static void blocks_clear(void) {
    for (int i = 0; i < nblk; i++) free(blks[i].text);
    nblk = 0; nlinks = 0; nthink = 0;
}
static void re_link(const char *txt, int blk, int act) {
    re_track = 1; re_bg = cCodeHdr; re_add(txt, strlen(txt), cAccent, CFE_BOLD | CFE_UNDERLINE, MONO); re_bg = CLR_INVALID; re_track = 0;
    if (nlinks == caplinks) { caplinks = caplinks ? caplinks * 2 : 16; links = (LinkR *)realloc(links, (size_t)caplinks * sizeof(LinkR)); }
    links[nlinks].a = re_s; links[nlinks].b = re_e; links[nlinks].blk = blk; links[nlinks].act = act; nlinks++;
}
static int code_cols(void) {
    static int dpi_c, cols_c, colw_c;
    HDC dc = GetDC(hChat); int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    if (dpi != dpi_c || g_colw != colw_c) {
        HFONT f = CreateFontW(-MulDiv(PT, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, MONO);
        HGDIOBJ old = SelectObject(dc, f); SIZE sz = {0};
        GetTextExtentPoint32W(dc, L"0000000000", 10, &sz);
        SelectObject(dc, old); DeleteObject(f);
        double cwid = sz.cx > 0 ? sz.cx / 10.0 : 8.0;
        cols_c = (int)((g_colw - 16) / cwid) - 3;      /* -3 = the two pad spaces + safety */
        if (cols_c < 30) cols_c = 30;
        dpi_c = dpi; colw_c = g_colw;
    }
    ReleaseDC(hChat, dc);
    return cols_c;
}
static char *pad_code(const char *s, size_t n, size_t *outn, size_t *outw) {
	size_t w = (size_t)code_cols(), c = 0, col = 0;
    for (size_t i = 0; i <= n; i++) {
        if (i == n || s[i] == '\n') { if (c > w) w = c; c = 0; }
        else if (s[i] == '\r') { }
        else if (s[i] == '\t') c += 4;
        else if (((unsigned char)s[i] & 0xC0) != 0x80) c++;
    }
    SB o = {0}; sb_putc(&o, ' ');
    for (size_t i = 0; i <= n; i++) {
        if (i == n || s[i] == '\n') {
            for (; col < w; col++) sb_putc(&o, ' ');
            sb_putc(&o, ' ');
            if (i < n) { sb_putc(&o, '\n'); sb_putc(&o, ' '); }
            col = 0;
        } else if (s[i] == '\r') { }
        else if (s[i] == '\t') { sb_puts(&o, "    "); col += 4; }
        else { sb_putc(&o, s[i]); if (((unsigned char)s[i] & 0xC0) != 0x80) col++; }
    }
    *outn = o.n; *outw = w; return o.p;
}
static void code_block(const char *lang, const char *code, size_t n) {
    char hl[64]; size_t pn, w;
    if (nblk == capblk) { capblk = capblk ? capblk * 2 : 8; blks = (CodeBlk *)realloc(blks, (size_t)capblk * sizeof(CodeBlk)); }
    int id = nblk++;
    blks[id].text = xstrndup(code, n); strncpy(blks[id].lang, lang, 23); blks[id].lang[23] = 0;
    char *pc = pad_code(code, n, &pn, &w);
    snprintf(hl, sizeof hl, "  %s", lang[0] ? lang : "code");
    size_t used = strlen(hl) + 6 + 2 + 10, tot = w + 2, gap = tot > used + 2 ? tot - used : 2;
    char *sp = (char *)malloc(gap + 1); memset(sp, ' ', gap); sp[gap] = 0;
    re_code_bg(hl, strlen(hl), cDim, CFE_ITALIC, cCodeHdr); re_code_bg(sp, gap, cDim, 0, cCodeHdr); free(sp);
    re_link(" Copy ", id, 0); re_code_bg("  ", 2, cDim, 0, cCodeHdr); re_link(" Download ", id, 1); re_nl();
    highlight(pc, pn, lang); re_nl(); free(pc);
}
static void blk_copy(int b) {
    if (b < 0 || b >= nblk) return;
    SB s = {0};
    for (const char *p = blks[b].text; *p; p++) { if (*p == '\r') continue; if (*p == '\n') sb_putc(&s, '\r'); sb_putc(&s, *p); }
    wchar_t *w = w_from(sbs(&s)); size_t sz = (wcslen(w) + 1) * 2; HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, sz);
    if (g && OpenClipboard(hMain)) {
        memcpy(GlobalLock(g), w, sz); GlobalUnlock(g); EmptyClipboard(); SetClipboardData(CF_UNICODETEXT, g); CloseClipboard();
        set_status("Code copied to clipboard");
    } else if (g) GlobalFree(g);
    free(w); free(s.p);
}
static void blk_save(int b) {
    if (b < 0 || b >= nblk) return;
    wchar_t f[MAX_PATH]; const char *ext = ext_for(blks[b].lang);
    if (!strcmp(ext, "Dockerfile")) wcscpy(f, L"Dockerfile");
    else { wchar_t *we = w_from(ext); _snwprintf(f, MAX_PATH, L"snippet.%ls", we); f[MAX_PATH - 1] = 0; free(we); }
    OPENFILENAMEW o; memset(&o, 0, sizeof o);
    o.lStructSize = sizeof o; o.hwndOwner = hMain; o.lpstrFile = f; o.nMaxFile = MAX_PATH; o.lpstrFilter = L"All files\0*.*\0"; o.Flags = OFN_OVERWRITEPROMPT;
    if (GetSaveFileNameW(&o)) { if (write_file(f, blks[b].text, strlen(blks[b].text))) set_status("Code saved"); else set_status("Could not write file"); }
}
static void set_hidden(LONG a, LONG b, int hide) {
    CHARFORMAT2W cf; memset(&cf, 0, sizeof cf); cf.cbSize = sizeof cf;
    cf.dwMask = CFM_HIDDEN; cf.dwEffects = hide ? CFE_HIDDEN : 0;
    SendMessageW(hChat, EM_SETSEL, (WPARAM)a, (LPARAM)b);
    SendMessageW(hChat, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}
static void think_toggle(int id) {
    if (id < 0 || id >= nthink) return;
    Think *t = &thinks[id]; CHARRANGE sv;
    SendMessageW(hChat, EM_EXGETSEL, 0, (LPARAM)&sv);
    SendMessageW(hChat, WM_SETREDRAW, FALSE, 0);
    t->collapsed = !t->collapsed;
    set_hidden(t->a, t->b + 1, t->collapsed);
    SendMessageW(hChat, EM_SETSEL, (WPARAM)t->hs, (LPARAM)(t->hs + 1));
    SendMessageW(hChat, EM_REPLACESEL, FALSE, (LPARAM)(t->collapsed ? L"\x25B8" : L"\x25BE"));
    SendMessageW(hChat, EM_EXSETSEL, 0, (LPARAM)&sv);
    SendMessageW(hChat, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hChat, NULL, FALSE);
    if (cur && t->node >= 0 && t->node < cur->cnt) cur->n[t->node].think_open = !t->collapsed;
}
static int link_at(int x, int y) {
    if (!nlinks) return -1;
    POINTL pt; pt.x = x; pt.y = y;
    LONG ch = (LONG)SendMessageW(hChat, EM_CHARFROMPOS, 0, (LPARAM)&pt);
    int lo = 0, hi = nlinks - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (ch < links[mid].a) hi = mid - 1;
        else if (ch >= links[mid].b) lo = mid + 1;
        else {
            POINTL p1, p2;
            SendMessageW(hChat, EM_POSFROMCHAR, (WPARAM)&p1, (LPARAM)links[mid].a);
            SendMessageW(hChat, EM_POSFROMCHAR, (WPARAM)&p2, (LPARAM)links[mid].b);
            return (y >= p1.y && y < p1.y + 26 && x >= p1.x && x <= p2.x) ? mid : -1;
        }
    }
    return -1;
}
static LRESULT CALLBACK ChatSub(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    if (m == WM_SETCURSOR) {
        POINT pt; GetCursorPos(&pt); ScreenToClient(h, &pt);
        if (link_at(pt.x, pt.y) >= 0) { SetCursor(LoadCursorW(NULL, IDC_HAND)); return TRUE; }
    } else if (m == WM_LBUTTONUP) {
        LRESULT r = DefSubclassProc(h, m, w, l);
        int k = link_at(GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (k >= 0) { if (links[k].act == 0) blk_copy(links[k].blk); else if (links[k].act == 1) blk_save(links[k].blk); else think_toggle(links[k].blk); }
        return r;
    }
    return DefSubclassProc(h, m, w, l);
}

#endif /* LLAMAC_CODEBLOCKS_H */
