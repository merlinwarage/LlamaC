/* richtext.h - RichEdit output primitives */
#ifndef LLAMAC_RICHTEXT_H
#define LLAMAC_RICHTEXT_H

/* ---------- rich text output ---------- */
static COLORREF re_bg = CLR_INVALID;
static int re_pt = PT;
static int re_track; static LONG re_s, re_e;
static void re_add(const char *s, size_t len, COLORREF col, DWORD eff, const wchar_t *face) {
    if (!len) return;
    int n = MultiByteToWideChar(CP_UTF8, 0, s, (int)len, NULL, 0);
    wchar_t *w = (wchar_t *)malloc((size_t)(n + 1) * 2); MultiByteToWideChar(CP_UTF8, 0, s, (int)len, w, n);
    int k = 0;
    for (int i = 0; i < n; i++) { if (w[i] == L'\r') continue; w[k++] = (w[i] == L'\n') ? L'\r' : w[i]; }
    w[k] = 0;
    CHARFORMAT2W cf; memset(&cf, 0, sizeof cf); cf.cbSize = sizeof cf;
    cf.dwMask = CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_STRIKEOUT | CFM_FACE | CFM_SIZE | CFM_BACKCOLOR | CFM_HIDDEN;
    cf.dwEffects = eff; cf.crTextColor = col; cf.yHeight = re_pt * 20;
    if (re_bg == CLR_INVALID) cf.dwEffects |= CFE_AUTOBACKCOLOR; else cf.crBackColor = re_bg;
    wcsncpy(cf.szFaceName, face, LF_FACESIZE - 1);
    SendMessageW(hChat, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    if (re_track) { CHARRANGE cr; SendMessageW(hChat, EM_EXGETSEL, 0, (LPARAM)&cr); re_s = cr.cpMin; }
    SendMessageW(hChat, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessageW(hChat, EM_REPLACESEL, FALSE, (LPARAM)w);
    if (re_track) { CHARRANGE cr; SendMessageW(hChat, EM_EXGETSEL, 0, (LPARAM)&cr); re_e = cr.cpMax; }
    free(w);
}
static void re_nl(void) { re_add("\n", 1, cFg, 0, FACE); }
static void re_code(const char *s, size_t n, COLORREF fg, DWORD eff) { re_bg = cCodeBg; re_add(s, n, fg, eff, MONO); re_bg = CLR_INVALID; }
static void re_code_bg(const char *s, size_t n, COLORREF fg, DWORD eff, COLORREF bg) { re_bg = bg; re_add(s, n, fg, eff, MONO); re_bg = CLR_INVALID; }
static void re_rule(COLORREF col) {
    int n = (g_colw - 40) / 4; if (n < 10) n = 10;
    SB s = {0}; for (int i = 0; i < n; i++) sb_puts(&s, HBAR);
    int sv = re_pt; re_pt = 5; re_add(sbs(&s), s.n, col, 0, MONO); re_nl(); re_pt = sv; free(s.p);
}

#endif /* LLAMAC_RICHTEXT_H */
