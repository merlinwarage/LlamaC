/* render.h - Thinking blocks, streaming flush, render_all */
#ifndef LLAMAC_RENDER_H
#define LLAMAC_RENDER_H

static COLORREF blend(COLORREF a, COLORREF b, int pct)
{
    return RGB(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * pct / 100, GetGValue(a) + (GetGValue(b) - GetGValue(a)) * pct / 100,
               GetBValue(a) + (GetBValue(b) - GetBValue(a)) * pct / 100);
}
/* ---------- collapsible blocks: thinking, tool results ----------
   "> label" header (a click toggles it, think_toggle in codeblocks.h; the open state is the node's think_open), then
   the body, hidden while collapsed. code: body in the code font (tool output); note: an extra italic line in the body. */
static void render_fold(const char *label, const char *txt, size_t n, int code, const char *note)
{
    int open = (g_rnode >= 0 && cur && g_rnode < cur->cnt && cur->n[g_rnode].think_open);
    char hdr[240];
    LONG a;
    snprintf(hdr, sizeof hdr, "%s %s", open ? "\xE2\x96\xBE" : "\xE2\x96\xB8", label);
    if (nthink == capthink)
    {
        capthink = capthink ? capthink * 2 : 8;
        thinks = (Think *)realloc(thinks, (size_t)capthink * sizeof(Think));
    }
    int id = nthink++;
    Think *t = &thinks[id];
    t->node = g_rnode;
    t->collapsed = !open;
    re_track = 1;
    re_add(hdr, strlen(hdr), cDim, CFE_BOLD, FACE);
    re_track = 0;
    t->hs = re_s;
    link_new(re_s, re_e, id, 2);
    re_nl();
    re_track = 1;
    if (code)
        re_code(txt, n, cDim, 0);
    else
        re_add(txt, n, cDim, CFE_ITALIC, FACE);
    a = re_s;
    if (note)
        re_add(note, strlen(note), cDim, CFE_ITALIC, FACE);
    re_track = 0;
    t->a = a;
    t->b = re_e;
    re_nl();
    if (t->collapsed)
        set_hidden(t->a, t->b + 1, 1);
    re_nl();
}
static void render_think(const char *txt, size_t n)
{
    char label[64];
    if (!n)
        return;
    snprintf(label, sizeof label, "Thinking (%u chars)", (unsigned)n);
    render_fold(label, txt, n, 0, NULL);
}

/* ---------- streaming: coalesce tokens, flush on a timer ---------- */
static SB g_pend;
static int g_pend_kind = -1, g_timer_on;
static size_t gl[2], gc[2];
static void app_amort(char **d, size_t *len, size_t *cap, const char *s)
{
    size_t b = strlen(s);
    if (*len + b + 1 > *cap)
    {
        size_t c = *cap ? *cap : 64;
        while (c < *len + b + 1)
            c *= 2;
        *d = (char *)realloc(*d, c);
        *cap = c;
    }
    memcpy(*d + *len, s, b + 1);
    *len += b;
}
/* offset of the end of the chat text (where the next re_add() writes) */
static LONG re_len(void)
{
    CHARRANGE cr;
    SendMessageW(hChat, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    SendMessageW(hChat, EM_EXGETSEL, 0, (LPARAM)&cr);
    return cr.cpMin;
}

/* ---------- fade-in ----------
   New text is inserted in the background colour and then recoloured towards the text colour in a few steps.
   Only the last ~240 ms of text is touched, so the cost stays small and constant. */
#define FADE_STEPS 6
#define FADE_MS 240
#define FADE_MAX 48
typedef struct
{
    LONG a, b;
    DWORD t0;
    int step;
} Fade;
static Fade g_fade[FADE_MAX];
static int g_nfade, g_fade_timer;
static COLORREF fade_col(int step) /* 0 = background, FADE_STEPS = text colour; ease-out */
{
    int f = FADE_STEPS * FADE_STEPS - (FADE_STEPS - step) * (FADE_STEPS - step), d = FADE_STEPS * FADE_STEPS;
    int r = GetRValue(cBg) + (GetRValue(cFg) - GetRValue(cBg)) * f / d;
    int g = GetGValue(cBg) + (GetGValue(cFg) - GetGValue(cBg)) * f / d;
    int b = GetBValue(cBg) + (GetBValue(cFg) - GetBValue(cBg)) * f / d;
    return RGB(r, g, b);
}
static void fade_apply(LONG a, LONG b, COLORREF col)
{
    CHARFORMAT2W cf;
    memset(&cf, 0, sizeof cf);
    cf.cbSize = sizeof cf;
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = col;
    SendMessageW(hChat, EM_SETSEL, (WPARAM)a, (LPARAM)b);
    SendMessageW(hChat, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}
static void fade_clear(void) { g_nfade = 0; }
static void fade_add(LONG a, LONG b)
{
    if (b <= a)
        return;
    if (g_nfade == FADE_MAX)
    { /* too many at once: show the oldest one fully */
        fade_apply(g_fade[0].a, g_fade[0].b, cFg);
        memmove(g_fade, g_fade + 1, (FADE_MAX - 1) * sizeof(Fade));
        g_nfade--;
    }
    g_fade[g_nfade].a = a;
    g_fade[g_nfade].b = b;
    g_fade[g_nfade].t0 = GetTickCount();
    g_fade[g_nfade].step = 0;
    g_nfade++;
}
/* advances every fade; returns 1 while any is still running */
static int fade_tick(void)
{
    DWORD now = GetTickCount();
    int n = 0;
    ScrollKeep k;
    if (!g_nfade)
        return 0;
    keep_begin(&k);
    for (int i = 0; i < g_nfade; i++)
    {
        Fade f = g_fade[i];
        int step = (int)((now - f.t0) * FADE_STEPS / FADE_MS);
        if (step > FADE_STEPS)
            step = FADE_STEPS;
        if (step != f.step)
        {
            fade_apply(f.a, f.b, step >= FADE_STEPS ? cFg : fade_col(step));
            f.step = step;
        }
        if (step < FADE_STEPS)
            g_fade[n++] = f;
    }
    g_nfade = n;
    keep_end(&k);
    return n > 0;
}

/* ---------- live markdown while streaming ----------
   The answer first appears as plain text (fast). As soon as a unit is complete - a line, a closed ``` code block,
   a finished table - the plain text of that unit is replaced by its final markdown rendering. Only the open
   unit at the end stays plain, so there is no big jump when the answer finishes. */
static SB g_live;            /* answer text shown so far (since live tracking began) */
static size_t g_live_done;   /* bytes of g_live already rendered as markdown */
static LONG g_live_off = -1; /* chat offset where the plain tail starts; < 0 = not tracking yet */
static int g_live_stop;      /* <think> tags in the answer: leave everything to the final render */
static void live_reset(void)
{
    g_live.n = 0;
    if (g_live.p)
        g_live.p[0] = 0;
    g_live_done = 0;
    g_live_off = -1;
    g_live_stop = 0;
    fade_clear();
}
static void live_commit(void)
{
    size_t end;
    char *seg;
    if (g_live_stop || g_live_off < 0 || !g_live.n)
        return;
    end = tx_block_end(g_live.p, g_live_done, g_live.n);
    if (end <= g_live_done)
        return;
    seg = xstrndup(g_live.p + g_live_done, end - g_live_done);
    if (strstr(seg, "think>"))
    { /* render_content() has to see a <think> block as a whole */
        g_live_stop = 1;
        free(seg);
        return;
    }
    fade_clear(); /* the plain tail is replaced: its ranges are gone */
    SendMessageW(hChat, WM_SETREDRAW, FALSE, 0);
    SendMessageW(hChat, EM_SETSEL, (WPARAM)g_live_off, (LPARAM)-1);
    SendMessageW(hChat, EM_REPLACESEL, FALSE, (LPARAM)L""); /* drop the plain tail */
    md_render(seg, cFg);
    g_live_off = re_len();
    if (g_live.n > end)
        re_add(g_live.p + end, g_live.n - end, cFg, 0, FACE); /* the open unit stays plain */
    g_live_done = end;
    SendMessageW(hChat, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hChat, NULL, FALSE);
    free(seg);
}

/* ---------- live thinking block (while the model reasons, before the answer starts) ----------
   An animated header line "<spinner> Thinking... 12s" at the end of the chat: a cycling glyph with a pulsing colour
   and a highlight sweeping over the word (timer 4). With "Show reasoning blocks" on, a fixed-height box of LT_LINES
   visual lines follows it: the reasoning text is appended at its end and whole lines are cut from its top, so the
   text scrolls inside the box while the box keeps its height. When the answer starts, the block is replaced by the
   ordinary collapsed thinking block (or just removed when reasoning is hidden).
   Layout: [ha,hb) header text, "\r", [ta,tb) reasoning tail, [tb,pe) padding "\r"s. Nothing follows pe while on. */
#define LT_LINES 6
#define LT_TICK 110
static struct
{
    int on, box, frame, nl; /* nl: the box text ends with a newline (or is empty) */
    LONG ha, hb, ta, tb, pe;
} g_lt;
static int g_gen_live;   /* a generation is streaming (set by start_gen_px, cleared when it ends) */
static DWORD g_think_t0; /* tick of the running generation's first reasoning text, 0 = none yet */
static int g_lt_timer;
static void lt_del(LONG a, LONG b)
{
    if (b <= a)
        return;
    SendMessageW(hChat, EM_SETSEL, (WPARAM)a, (LPARAM)b);
    SendMessageW(hChat, EM_REPLACESEL, FALSE, (LPARAM)L"");
}
static void lt_color(LONG a, LONG b, COLORREF col)
{
    CHARFORMAT2W cf;
    memset(&cf, 0, sizeof cf);
    cf.cbSize = sizeof cf;
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = col;
    SendMessageW(hChat, EM_SETSEL, (WPARAM)a, (LPARAM)b);
    SendMessageW(hChat, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}
/* (re)writes the header; the text after it moves by the change in length */
static void lt_header(void)
{
    static const wchar_t frames[] = L"\x2722\x2733\x2736\x273B\x273D\x273B\x2736\x2733"; /* spinner, back and forth */
    static const char word[] = "Thinking\xE2\x80\xA6";                                    /* 9 characters on screen */
    int f = g_lt.frame, nfr = (int)(sizeof frames / sizeof frames[0]) - 1, tri, sweep;
    char gl[8], tm[40];
    wchar_t gw[2];
    LONG old = g_lt.hb, ws;
    DWORD secs = g_think_t0 ? (GetTickCount() - g_think_t0) / 1000 : 0;
    gw[0] = frames[f % nfr];
    gw[1] = 0;
    WideCharToMultiByte(CP_UTF8, 0, gw, -1, gl, sizeof gl, NULL, NULL);
    if (secs >= 60)
        snprintf(tm, sizeof tm, "   %lum %02lus", (unsigned long)(secs / 60), (unsigned long)(secs % 60));
    else
        snprintf(tm, sizeof tm, "   %lus", (unsigned long)secs);
    tri = f % 16 < 8 ? f % 16 : 16 - f % 16; /* 0..8..0: the glyph's colour pulses */
    lt_del(g_lt.ha, g_lt.hb);
    re_at = g_lt.ha;
    re_add(gl, strlen(gl), blend(cDim, cAccent, 40 + tri * 60 / 8), CFE_BOLD, L"Segoe UI Symbol");
    re_add(" ", 1, cDim, 0, FACE);
    ws = re_at;
    re_add(word, strlen(word), cDim, CFE_BOLD, FACE);
    re_add(tm, strlen(tm), cDim, 0, FACE);
    g_lt.hb = re_at;
    re_at = -1;
    sweep = f % 14 - 3; /* a highlight moves over the word, with a pause between sweeps */
    for (int i = 0; i < 9; i++)
    {
        int d = i > sweep ? i - sweep : sweep - i, pct = d >= 3 ? 0 : 100 - d * 40;
        if (pct)
            lt_color(ws + i, ws + i + 1, blend(cDim, cFg, pct));
    }
    g_lt.ta += g_lt.hb - old;
    g_lt.tb += g_lt.hb - old;
    g_lt.pe += g_lt.hb - old;
}
/* keeps the box at LT_LINES visual lines: cut whole lines from the top, pad below the text */
static void lt_fit(void)
{
    LONG l0 = (LONG)SendMessageW(hChat, EM_EXLINEFROMCHAR, 0, (LPARAM)g_lt.ta);
    LONG l1 = (LONG)SendMessageW(hChat, EM_EXLINEFROMCHAR, 0, (LPARAM)g_lt.tb);
    LONG lines = l1 - l0 + 1, want, have;
    if (lines > LT_LINES)
    {
        LONG cut = (LONG)SendMessageW(hChat, EM_LINEINDEX, (WPARAM)(l0 + lines - LT_LINES), 0);
        if (cut > g_lt.ta && cut <= g_lt.tb)
        {
            lt_del(g_lt.ta, cut);
            g_lt.tb -= cut - g_lt.ta;
            g_lt.pe -= cut - g_lt.ta;
            lines = LT_LINES;
        }
    }
    want = LT_LINES - lines + 1; /* the first "\r" ends the text's last line, each further one adds an empty line */
    if (want < 1)
        want = 1;
    have = g_lt.pe - g_lt.tb;
    if (want > have)
    {
        SB pad = {0};
        for (LONG i = have; i < want; i++)
            sb_putc(&pad, '\n');
        re_at = g_lt.pe;
        re_add(pad.p, pad.n, cDim, 0, FACE);
        re_at = -1;
        free(pad.p);
    }
    else if (want < have)
        lt_del(g_lt.tb + want, g_lt.pe);
    g_lt.pe = g_lt.tb + want;
}
static void lt_append(const char *s, size_t n)
{
    LONG was;
    if (!g_lt.on || !g_lt.box)
        return;
    if (g_lt.ta == g_lt.tb)
        while (n && (*s == '\n' || *s == '\r' || *s == ' '))
            s++, n--; /* no empty lines at the top of the box */
    if (n > 4000)
    { /* only the tail can be visible */
        s += n - 4000;
        n = 4000;
        while (n && ((unsigned char)*s & 0xC0) == 0x80)
            s++, n--;
    }
    if (!n)
        return;
    { /* runs of newlines become one: blank lines would waste the box's few lines */
        SB f = {0};
        for (size_t i = 0; i < n; i++)
        {
            if (s[i] == '\r' || (s[i] == '\n' && g_lt.nl))
                continue;
            g_lt.nl = s[i] == '\n';
            sb_putc(&f, s[i]);
        }
        if (f.n)
        {
            was = g_lt.tb;
            re_at = g_lt.tb;
            re_add(f.p, f.n, cDim, CFE_ITALIC, FACE);
            g_lt.tb = re_at;
            re_at = -1;
            g_lt.pe += g_lt.tb - was;
            lt_fit();
        }
        free(f.p);
    }
}
/* starts the block at the end of the chat; text = reasoning received so far (shown in the box) */
static void lt_begin(const char *text)
{
    if (g_lt.on)
        return;
    memset(&g_lt, 0, sizeof g_lt);
    g_lt.on = 1;
    g_lt.nl = 1;
    g_lt.box = G.thoughts;
    g_lt.ha = g_lt.hb = re_len();
    re_nl();
    g_lt.ta = g_lt.tb = g_lt.pe = re_len();
    if (g_lt.box)
    {
        re_nl();
        g_lt.pe = re_len();
    }
    lt_header();
    if (g_lt.box)
    {
        lt_fit();
        if (text && *text)
            lt_append(text, strlen(text));
    }
    genPhase = 1;
    if (!g_lt_timer)
    {
        SetTimer(hMain, 4, LT_TICK, NULL);
        g_lt_timer = 1;
    }
}
/* the answer starts (or an error / the end arrives): drop the live block, show the normal thinking block instead */
static void lt_end(void)
{
    if (!g_lt.on)
        return;
    g_lt.on = 0;
    if (g_lt.pe != re_len())
        return; /* something was written after it: leave it to the next render_all() */
    lt_del(g_lt.ha, g_lt.pe);
    if (g_lt.box && cur && cur == genConv && genNode < cur->cnt)
    {
        g_rnode = genNode;
        render_think(cur->n[genNode].reasoning, strlen(cur->n[genNode].reasoning));
    }
}
/* timer 4: animate the header; returns 0 when the block is gone */
static int lt_tick(void)
{
    ScrollKeep k;
    if (!g_lt.on)
        return 0;
    keep_begin(&k);
    g_lt.frame++;
    lt_header();
    keep_end(&k);
    return 1;
}

/* show at most maxb bytes of the pending text (never splits a UTF-8 character); the rest waits for the next tick */
static void flush_stream_n(size_t maxb)
{
    size_t n = g_pend.n, emit = n;
    int show = (n && cur && cur == genConv);
    if (emit > maxb)
    {
        emit = maxb;
        while (emit < n && ((unsigned char)g_pend.p[emit] & 0xC0) == 0x80)
            emit++;
    }
    if (show)
    {
        ScrollKeep k;
        keep_begin(&k);
        if (g_pend_kind == 1)
        {
            if (!g_lt.on && genPhase != 2)
                lt_begin(NULL);
            if (g_lt.on)
                lt_append(g_pend.p, emit);
            else
                re_add(g_pend.p, emit, cDim, CFE_ITALIC, FACE); /* reasoning after the answer began: rare */
        }
        else
        {
            int fade = G.fade && emit && emit < 600; /* a big catch-up chunk just appears */
            lt_end();
            genPhase = 2;
            if (g_live_off < 0)
                g_live_off = re_len();
            re_track = 1;
            re_add(g_pend.p, emit, fade ? cBg : cFg, 0, FACE);
            re_track = 0;
            if (fade)
                fade_add(re_s, re_e);
            sb_add(&g_live, g_pend.p, emit);
            live_commit();
        }
        keep_end(&k);
    }
    else
        emit = n; /* not the visible chat: drop the pending text */
    if (emit >= g_pend.n)
        g_pend.n = 0;
    else
    {
        memmove(g_pend.p, g_pend.p + emit, g_pend.n - emit);
        g_pend.n -= emit;
    }
    if (g_pend.p)
        g_pend.p[g_pend.n] = 0;
}
static void flush_stream(void) { flush_stream_n((size_t)-1); } /* show everything now */

/* tick scheduler: spaces ticks by the measured cost of a flush so the UI thread never saturates */
static double g_flush_ms;
static UINT flush_interval(void)
{
    double t = g_flush_ms * 3;
    return (UINT)(t < 16 ? 16 : t > 250 ? 250
                                        : t);
}
static double g_rate, g_gap_ms = 300, g_last_arr, g_credit, g_prev_step;
static double qpc_ms(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}
/* call whenever new text was added to g_pend: reveal the backlog over about one arrival gap */
static void pace_arrival(void)
{
    double now = qpc_ms(), T;
    if (g_last_arr > 0)
    {
        double gap = now - g_last_arr;
        if (gap > 700)
            gap = 700;
        g_gap_ms = g_gap_ms * 0.7 + gap * 0.3;
    }
    g_last_arr = now;
    T = g_gap_ms < 120 ? 120 : g_gap_ms > 700 ? 700
                                              : g_gap_ms;
    g_rate = (double)g_pend.n * 1000.0 / T; /* bytes per second, held until the next arrival */
}
static void flush_step(void)
{
    UINT iv = flush_interval();
    double t0 = qpc_ms(), dt = g_prev_step ? t0 - g_prev_step : (double)iv, ms;
    size_t q;
    g_prev_step = t0;
    if (dt > 100)
        dt = 100;
    g_credit += g_rate * dt / 1000.0;
    q = (size_t)g_credit;
    if (q > g_pend.n)
        q = g_pend.n;
    if (g_pend.n > 4096 && q < g_pend.n / 4)
        q = g_pend.n / 4; /* never fall far behind */
    g_credit -= (double)q;
    if (g_credit < 0 || q >= g_pend.n)
        g_credit = 0;
    if (q)
        flush_stream_n(q);
    ms = qpc_ms() - t0;
    g_flush_ms = g_flush_ms * 0.8 + ms * 0.2;
    if (g_nfade && !g_fade_timer)
    {
        SetTimer(hMain, 3, iv > 33 ? iv : 33, NULL);
        g_fade_timer = 1;
    }
#ifdef LLAMAC_PROFILE
    {
        wchar_t t[160];
        _snwprintf(t, 160, L"tick dt %.0f ms | flush %.1f | paint %.1f | backlog %u B | rate %.0f B/s | gap %.0f ms",
                   dt, ms, g_paint_ms, (unsigned)g_pend.n, g_rate, g_gap_ms);
        SetWindowTextW(hMain, t);
    }
#endif
}

/* <think>...</think> inside the content (reasoning_format = none) is shown as a dim thinking block */
static void render_content(const char *s)
{
    const char *p = s;
    for (;;)
    {
        const char *a = strstr(p, "<think>");
        if (!a)
        {
            md_render(p, cFg);
            return;
        }
        if (a > p)
        {
            char *seg = xstrndup(p, (size_t)(a - p));
            md_render(seg, cFg);
            free(seg);
        }
        const char *b = a + 7, *e = strstr(b, "</think>");
        size_t tl = e ? (size_t)(e - b) : strlen(b);
        if (G.thoughts && tl)
            render_think(b, tl);
        if (!e)
            return;
        p = e + 8;
        while (*p == '\n' || *p == '\r')
            p++;
    }
}
/* ---------- token confidence ----------
   Uncertain tokens (from logprobs) are shaded: the less likely, the stronger. The answer is rendered as markdown,
   so token byte offsets do not map to screen positions directly; each token is searched in the rendered text,
   in order, within the distance its byte offset allows. Tokens that cannot be placed are skipped. */
static int tok_skip(const char *s, int n)
{
    for (int i = 0; i < n; i++)
        if (!strchr(" \t\r\n*_`#|>~-", s[i]))
            return 0;
    return 1; /* whitespace or markdown syntax: not visible as such */
}
static void shade_tokens(int node, LONG a, LONG b)
{
    Node *m = &cur->n[node];
    TEXTRANGEW tr;
    wchar_t *txt;
    LONG len = b - a, pos = 0;
    int prev_end = 0;
    CHARFORMAT2W cf;
    if (!m->ntk || len <= 0)
        return;
    txt = (wchar_t *)malloc(((size_t)len + 1) * sizeof(wchar_t));
    tr.chrg.cpMin = a;
    tr.chrg.cpMax = b;
    tr.lpstrText = txt;
    len = (LONG)SendMessageW(hChat, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
    memset(&cf, 0, sizeof cf);
    cf.cbSize = sizeof cf;
    cf.dwMask = CFM_BACKCOLOR;
    for (int i = 0; i < m->ntk; i++)
    {
        TokInfo *t = &m->tk[i];
        const char *s;
        int n, wn;
        wchar_t w[64];
        LONG lim, hit = -1;
        if (t->p >= 0.5f || t->off + t->len > (int)strlen(m->content))
            continue;
        s = m->content + t->off;
        n = t->len;
        while (n && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r'))
            s++, n--;
        while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\n' || s[n - 1] == '\r'))
            n--;
        if (n <= 0 || tok_skip(s, n))
            continue;
        wn = MultiByteToWideChar(CP_UTF8, 0, s, n, w, 63);
        if (wn <= 0)
            continue;
        lim = pos + (t->off - prev_end) + wn + 8; /* rendered text is never longer than the source */
        if (lim > len)
            lim = len;
        for (LONG k = pos; k + wn <= lim; k++)
            if (!wmemcmp(txt + k, w, (size_t)wn))
            {
                hit = k;
                break;
            }
        if (hit < 0)
            continue;
        cf.crBackColor = blend(cBg, RGB(245, 158, 11), t->p < 0.2f ? 45 : 13); /* < 20%: strong, 20-50%: faint */
        SendMessageW(hChat, EM_SETSEL, (WPARAM)(a + hit), (LPARAM)(a + hit + wn));
        SendMessageW(hChat, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        link_insert(a + hit, a + hit + wn, node, 4, i);
        pos = hit + wn;
        prev_end = t->off + t->len;
    }
    free(txt);
}
/* ---------- tool calls and tool results ---------- */
static const char *role_label(int r) { return r == ROLE_USER ? "You" : r == ROLE_ASSIST ? "Assistant"
                                                                   : r == ROLE_TOOL     ? "Tool"
                                                                                        : "System"; }
/* a tool result: a collapsible block like thinking (render_node skips tool nodes while reasoning blocks are hidden) */
static void render_tool_result(Node *m)
{
    char h[240];
    size_t full = strlen(m->content), n = full;
    const char *nm = m->tname && *m->tname ? m->tname : "tool";
    if (!full)
    {
        snprintf(h, sizeof h, "\xE2\x96\xB8 %.150s result (empty)\n", nm);
        re_add(h, strlen(h), cDim, CFE_BOLD, FACE);
        re_nl();
        return;
    }
    snprintf(h, sizeof h, "%.150s result (%u chars)%s", nm, (unsigned)full, m->elided ? "   - shortened in requests to save context" : "");
    if (n > 4000)
    {
        n = 4000;
        while (n && ((unsigned char)m->content[n] & 0xC0) == 0x80)
            n--;
    }
    render_fold(h, m->content, n, 1, n < full ? "\n... (shortened in this view; the model received the full result)" : NULL);
}
static void render_tool_calls(const char *json)
{
    J *a = json_parse(json);
    for (J *c = a ? a->child : NULL; c; c = c->next)
    {
        J *fn = j_get(c, "function");
        const char *nm = j_str(j_get(fn, "name")), *ar = j_str(j_get(fn, "arguments"));
        SB s = {0};
        size_t al = ar ? strlen(ar) : 0;
        if (al > 300)
        {
            al = 300;
            while (al && ((unsigned char)ar[al] & 0xC0) == 0x80)
                al--;
        }
        if (nm && !strcmp(nm, "update_plan") && ar)
        { /* the plan as a checklist */
            J *pa = json_parse(ar), *items = j_get(pa, "items");
            if (items && items->t == J_ARR)
            {
                re_add("\xE2\x96\xB8 Plan\n", strlen("\xE2\x96\xB8 Plan\n"), cAccent, CFE_BOLD, FACE);
                for (J *it = items->child; it; it = it->next)
                {
                    const char *st = j_str(j_get(it, "status")), *tx = j_str(j_get(it, "text"));
                    int done = st && !strcmp(st, "done"), now = st && !strcmp(st, "in_progress");
                    SB l = {0};
                    if (!tx)
                        continue;
                    sb_puts(&l, done ? "   \xE2\x98\x91  " : now ? "   \xE2\x96\xB6  " : "   \xE2\x98\x90  ");
                    sb_puts(&l, tx);
                    sb_putc(&l, '\n');
                    re_add(l.p, l.n, done ? cDim : now ? cFg : cDim, now ? CFE_BOLD : 0, FACE);
                    free(l.p);
                }
                j_free(pa);
                free(s.p);
                continue;
            }
            j_free(pa);
        }
        sb_puts(&s, "\xE2\x96\xB8 Tool call: ");
        sb_puts(&s, nm ? nm : "?");
        sb_putc(&s, '(');
        if (ar)
            sb_add(&s, ar, al);
        if (ar && al < strlen(ar))
            sb_puts(&s, "...");
        sb_puts(&s, ")\n");
        re_add(sbs(&s), s.n, cAccent, 0, FACE);
        free(s.p);
    }
    j_free(a);
}
/* ---------- incremental chat view ----------
   Only the last rendered message can change while a chat is open (it streams, then gets Markdown-rendered), so
   render_all() keeps the offset of every rendered message and re-renders from the first message that differs. */
typedef struct
{
    int id;         /* node index */
    LONG off;       /* RichEdit character offset where this message starts */
    int nb, nl, nt; /* code blocks / links / think blocks that existed before it */
} RdNode;
static RdNode *g_rd;
static int g_rdn, g_rdcap, g_rd_key = -1;
static int g_rd_base; /* first path index shown: 0, or in the copilot panel the latest user message */
static void blocks_trunc(int nb, int nl, int nt)
{
    for (int i = nb; i < nblk; i++)
        free(blks[i].text);
    if (nb < nblk)
        nblk = nb;
    for (int i = nl; i < nlinks; i++)
        free(links[i].url);
    if (nl < nlinks)
        nlinks = nl;
    if (nt < nthink)
        nthink = nt;
}
/* tool results are reasoning-like detail: hidden together with the thinking blocks (Show reasoning blocks off) */
static int rd_shown(int node) { return !(cur->n[node].role == ROLE_TOOL && !G.thoughts); }
/* base: first path index shown (render_all). The separator line goes BEFORE a message when a shown one precedes it,
   so a message depends only on what is above it - hidden tool results in between need no re-render of earlier ones */
static void render_node(const int *p, int i, int n, int base)
{
    Node *m = &cur->n[p[i]];
    char hdr[96];
    if (!rd_shown(p[i]))
        return;
    for (int k = i - 1; k >= base; k--)
        if (rd_shown(p[k]))
        {
            re_rule(cBorder);
            break;
        }
    Node *pn = &cur->n[m->parent];
    int pos = 0;
    for (int k = 0; k < pn->nk; k++)
        if (pn->kids[k] == p[i])
            pos = k;
    int summ = (m->role == ROLE_SYS && i == 0); /* compression summary: always the first node */
    snprintf(hdr, sizeof hdr, "\xE2\x97\x8F  %s", summ ? "Summary of earlier messages" : role_label(m->role));
    if (pn->nk > 1)
        snprintf(hdr + strlen(hdr), sizeof hdr - strlen(hdr), "     \xE2\x80\xB9 %d / %d \xE2\x80\xBA", pos + 1, pn->nk);
    re_add(hdr, strlen(hdr), m->role == ROLE_USER ? cUser : m->role == ROLE_TOOL || summ ? cDim
                                                                                         : cAsst,
           CFE_BOLD, FACE);
    re_nl();
    if (summ)
    {
        const char *note = pn->nk > 1 ? "Older messages were compressed into this summary. Previous branch (\xE2\x80\xB9) shows the originals.\n"
                                      : "Older messages were compressed into this summary.\n";
        re_add(note, strlen(note), cDim, CFE_ITALIC, FACE);
    }
    for (int k = 0; k < m->na; k++)
    {
        char b[300];
        snprintf(b, sizeof b, "\xE2\x96\xAB %s\n", m->att[k].name);
        re_add(b, strlen(b), cDim, 0, FACE);
    }
    g_rnode = p[i];
    if (g_gen_live && busy && cur == genConv && p[i] == genNode)
    { /* the generating message: still reasoning -> the live block (it is the last node, so the block ends the chat) */
        int reasoning = m->reasoning[0] && !m->content[0] && !(m->tcalls && *m->tcalls);
        genPhase = reasoning ? 1 : m->content[0] ? 2 : 0;
        if (reasoning)
        {
            lt_begin(m->reasoning);
            return;
        }
    }
    /* a summary node keeps the original prompt in `reasoning` (for the chat-list "Copy original prompt"); never shown here */
    if (!summ && G.thoughts)
        render_think(m->reasoning, strlen(m->reasoning));
    if (m->role == ROLE_TOOL)
        render_tool_result(m);
    else
    {
        LONG ca = re_len();
        render_content(m->content);
        if (m->ntk && !(busy && cur == genConv && p[i] == genNode))
            shade_tokens(p[i], ca, re_len());
        if (m->tcalls && *m->tcalls)
            render_tool_calls(m->tcalls);
    }
    if (!(busy && cur == genConv && p[i] == genNode))
        re_nl();
}
static void render_all(void)
{
    int *p = NULL, n = 0, start = 0, base = 0;
    int key = (G.dark ? 1 : 0) | (G.thoughts ? 2 : 0) | (g_cp_on ? 4 : 0) | (g_colw << 3);
    ScrollKeep k;
    g_pend.n = 0;
    if (g_pend.p)
        g_pend.p[0] = 0;
    live_reset(); /* whatever was streamed is re-rendered from the node below */
    g_lt.on = 0;  /* the live thinking block is part of the last message: removed with it, rebuilt by render_node */
    if (cur)
        n = conv_path(cur, &p);
    if (g_cp_on) /* the copilot panel shows only the latest exchange: the last user message and what followed it */
        for (int i = n - 1; i >= 0; i--)
            if (cur->n[p[i]].role == ROLE_USER)
            {
                base = i;
                break;
            }
    if (cur && cur == g_rd_conv && key == g_rd_key && base == g_rd_base && n > 0 && g_rdn > 0)
    {
        int d = 0;
        while (d < n && d < g_rdn && g_rd[d].id == p[d])
            d++;
        start = d;
        if (start > n - 1)
            start = n - 1;
        if (start > g_rdn - 1)
            start = g_rdn - 1;
        if (start < 0)
            start = 0;
        if (start < base)
            start = 0; /* a change before the shown exchange: rebuild */
    }
    keep_begin(&k);
    SendMessageW(hChat, WM_SETREDRAW, FALSE, 0);
    if (start == 0)
    {
        SetWindowTextW(hChat, L"");
        blocks_clear();
        g_rdn = 0;
        {
            PARAFORMAT2 pf;
            memset(&pf, 0, sizeof pf);
            pf.cbSize = sizeof pf;
            pf.dwMask = PFM_LINESPACING;
            pf.bLineSpacingRule = 5;
            pf.dyLineSpacing = 24;
            SendMessageW(hChat, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
        }
        if (cur && n == 0 && !g_cp_on)
        {
            int sv = re_pt;
            re_pt = 20;
            re_nl();
            re_nl();
            re_add(" LlamaC\n", strlen("  LlamaC\n"), cAccent, CFE_BOLD, FACE);
            re_pt = 11;
            re_add("  Ask anything. Drop images or text files to attach them.\n", strlen("  Ask anything. Drop images or text files to attach them.\n"), cDim, 0, FACE);
            re_pt = sv;
        }
    }
    else
    {
        const RdNode *rn = &g_rd[start];
        SendMessageW(hChat, EM_SETSEL, (WPARAM)rn->off, (LPARAM)-1);
        SendMessageW(hChat, EM_REPLACESEL, FALSE, (LPARAM)L"");
        blocks_trunc(rn->nb, rn->nl, rn->nt);
        g_rdn = start;
    }
    if (n > g_rdcap)
    {
        g_rdcap = n + 16;
        g_rd = (RdNode *)realloc(g_rd, (size_t)g_rdcap * sizeof(RdNode));
    }
    for (int i = start; i < n; i++)
    {
        g_rd[i].id = p[i];
        g_rd[i].off = re_len();
        g_rd[i].nb = nblk;
        g_rd[i].nl = nlinks;
        g_rd[i].nt = nthink;
        if (i >= base) /* the nodes before base keep offset 0: nothing of them is shown */
            render_node(p, i, n, base);
        g_rdn = i + 1;
    }
    g_rd_conv = cur;
    g_rd_key = key;
    g_rd_base = base;
    free(p);
    SendMessageW(hChat, WM_SETREDRAW, TRUE, 0);
    if (start == 0)
    { /* another chat, or a full rebuild: start at the bottom, following */
        g_follow = 1;
        k.follow = 1;
        k.sel.cpMin = k.sel.cpMax = 0;
    }
    k.frozen = 0; /* redraw is already back on, the repaint follows */
    keep_end(&k); /* the same chat: bottom while following, else the view stays where the user scrolled */
    InvalidateRect(hChat, NULL, TRUE);
    InvalidateRect(hMain, &rcHead, FALSE);
}

#endif /* LLAMAC_RENDER_H */
