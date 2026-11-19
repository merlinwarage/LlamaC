/* richtext.h - RichEdit output primitives */
#ifndef LLAMAC_RICHTEXT_H
#define LLAMAC_RICHTEXT_H

/* ---------- rich text output ---------- */
static COLORREF re_bg = CLR_INVALID;
static int re_pt = PT;
static int re_track;
static LONG re_s, re_e;
static LONG re_at = -1; /* >= 0: re_add() inserts here instead of at the end, and moves re_at past the inserted text */
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
    SendMessageW(hChat, EM_SETSEL, (WPARAM)re_at, (LPARAM)re_at); /* -1, -1 = the end */
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
        if (a > 0 && re_at < 0) /* after EM_REPLACESEL the caret is already behind the inserted text */
            SendMessageW(hChat, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
        SendMessageW(hChat, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        SendMessageW(hChat, EM_REPLACESEL, FALSE, (LPARAM)(w + a));
        w[b] = keep;
        a = b;
    }
    if (re_track || re_at >= 0)
    {
        CHARRANGE cr;
        SendMessageW(hChat, EM_EXGETSEL, 0, (LPARAM)&cr);
        re_e = cr.cpMax;
        if (re_at >= 0)
            re_at = cr.cpMax;
    }
    free(w);
}
/* ---------- chat scroll position ----------
   g_follow: the view sticks to the bottom while text streams in. Scrolling up (wheel, scrollbar, keys) clears it,
   scrolling back to the bottom sets it again. Every programmatic change of the chat goes through keep_begin() /
   keep_end(): following -> scroll to the bottom; otherwise the scroll position (and a text selection) is put back.
   Needed because a RichEdit that has the focus (the user clicked into the chat) scrolls to the caret on every
   EM_SETSEL / EM_REPLACESEL, even read-only. Msftedit's EM_GETSCROLLPOS / EM_SETSCROLLPOS are 32-bit (tested). */
static int g_follow = 1;
static int g_scroll_prog; /* > 0 while the app itself scrolls the chat: ChatSub leaves g_follow alone */
typedef struct
{
    POINT pos;
    CHARRANGE sel;
    int follow, frozen;
} ScrollKeep;
static int at_bottom(void)
{
    SCROLLINFO si;
    memset(&si, 0, sizeof si);
    si.cbSize = sizeof si;
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    GetScrollInfo(hChat, SB_VERT, &si);
    return si.nPos + (int)si.nPage >= si.nMax - 8;
}
static void keep_begin(ScrollKeep *k)
{
    k->follow = g_follow;
    SendMessageW(hChat, EM_GETSCROLLPOS, 0, (LPARAM)&k->pos);
    SendMessageW(hChat, EM_EXGETSEL, 0, (LPARAM)&k->sel);
    /* with the focus in the chat, each edit would scroll the view to the caret: no painting until it is put back */
    k->frozen = !k->follow && GetFocus() == hChat;
    if (k->frozen)
        SendMessageW(hChat, WM_SETREDRAW, FALSE, 0);
}
static void keep_end(ScrollKeep *k)
{
    POINT now;
    g_scroll_prog++;
    if (k->sel.cpMax > k->sel.cpMin && k->sel.cpMax <= GetWindowTextLengthW(hChat))
        SendMessageW(hChat, EM_EXSETSEL, 0, (LPARAM)&k->sel); /* the user's selection survives streaming */
    else
        SendMessageW(hChat, EM_SETSEL, (WPARAM)-1, (LPARAM)-1); /* never leave a range of our own selected */
    if (k->follow)
        SendMessageW(hChat, WM_VSCROLL, SB_BOTTOM, 0);
    else
    {
        SendMessageW(hChat, EM_GETSCROLLPOS, 0, (LPARAM)&now);
        if (now.x != k->pos.x || now.y != k->pos.y)
            SendMessageW(hChat, EM_SETSCROLLPOS, 0, (LPARAM)&k->pos);
    }
    g_scroll_prog--;
    if (k->frozen)
    {
        SendMessageW(hChat, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(hChat, NULL, FALSE);
    }
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
