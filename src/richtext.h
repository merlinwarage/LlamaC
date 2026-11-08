/* richtext.h - RichEdit output primitives */
#ifndef LLAMAC_RICHTEXT_H
#define LLAMAC_RICHTEXT_H

/* ---------- rich text output ---------- */
static COLORREF re_bg = CLR_INVALID;
static int re_pt = PT;
static int re_track;
static LONG re_s, re_e;
/* chars the text font usually lacks (emoji, symbols, CJK): RichEdit substitutes a fallback font for them,
   and keeps using a different font for the rest of the same insertion (seen with Consolas: the text after an
   emoji came out in Segoe UI). Such runs are inserted separately so the following run gets its own font back. */
static int re_special(const wchar_t *w, int i)
{
    wchar_t c = w[i];
    return (c >= 0xD800 && c <= 0xDFFF) || (c >= 0x2600 && c <= 0x27BF) || (c >= 0x2B00 && c <= 0x2BFF) ||
           (c >= 0x2E80 && c <= 0xD7FF) || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFE00 && c <= 0xFE0F) ||
           c == 0x200D || c == 0x20E3;
}
static void re_add(const char *s, size_t len, COLORREF col, DWORD eff, const wchar_t *face)
{
    if (!len)
        return;
    int n = MultiByteToWideChar(CP_UTF8, 0, s, (int)len, NULL, 0);
    wchar_t *w = (wchar_t *)malloc((size_t)(n + 1) * 2);
    MultiByteToWideChar(CP_UTF8, 0, s, (int)len, w, n);
    int k = 0;
    for (int i = 0; i < n; i++)
    {
        if (w[i] == L'\r')
            continue;
        w[k++] = (w[i] == L'\n') ? L'\r' : w[i];
    }
    w[k] = 0;
    CHARFORMAT2W cf;
    memset(&cf, 0, sizeof cf);
    cf.cbSize = sizeof cf;
    cf.dwMask = CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_STRIKEOUT | CFM_FACE | CFM_SIZE | CFM_BACKCOLOR | CFM_HIDDEN;
    cf.dwEffects = eff;
    cf.crTextColor = col;
    cf.yHeight = re_pt * 20;
    if (re_bg == CLR_INVALID)
        cf.dwEffects |= CFE_AUTOBACKCOLOR;
    else
        cf.crBackColor = re_bg;
    wcsncpy(cf.szFaceName, face, LF_FACESIZE - 1);
    SendMessageW(hChat, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    if (re_track)
    {
        CHARRANGE cr;
        SendMessageW(hChat, EM_EXGETSEL, 0, (LPARAM)&cr);
        re_s = cr.cpMin;
    }
    for (int a = 0; a < k;)
    { /* runs of ordinary / special chars, each inserted with the format set again */
        int b = a, sp = re_special(w, a);
        while (b < k && re_special(w, b) == sp)
            b++;
        wchar_t keep = w[b];
        w[b] = 0;
        if (a > 0)
            SendMessageW(hChat, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
        SendMessageW(hChat, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        SendMessageW(hChat, EM_REPLACESEL, FALSE, (LPARAM)(w + a));
        w[b] = keep;
        a = b;
    }
    if (re_track)
    {
        CHARRANGE cr;
        SendMessageW(hChat, EM_EXGETSEL, 0, (LPARAM)&cr);
        re_e = cr.cpMax;
    }
    free(w);
}
static void re_nl(void) { re_add("\n", 1, cFg, 0, FACE); }
static void re_code(const char *s, size_t n, COLORREF fg, DWORD eff)
{
    re_bg = cCodeBg;
    re_add(s, n, fg, eff, MONO);
    re_bg = CLR_INVALID;
}
static void re_code_bg(const char *s, size_t n, COLORREF fg, DWORD eff, COLORREF bg)
{
    re_bg = bg;
    re_add(s, n, fg, eff, MONO);
    re_bg = CLR_INVALID;
}
static void re_rule(COLORREF col)
{
    int n = (g_colw - 40) / 4;
    if (n < 10)
        n = 10;
    SB s = {0};
    for (int i = 0; i < n; i++)
        sb_puts(&s, HBAR);
    int sv = re_pt;
    re_pt = 5;
    re_add(sbs(&s), s.n, col, 0, MONO);
    re_nl();
    re_pt = sv;
    free(s.p);
}

#endif /* LLAMAC_RICHTEXT_H */
