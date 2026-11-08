/* settings_ui.h - Settings dialog */
#ifndef LLAMAC_SETTINGS_UI_H
#define LLAMAC_SETTINGS_UI_H

/* ---------- settings window (values left empty = use llama-server's own) ---------- */
static HWND hSet, setCtl[NFIELDS];
static int setDone;
/* stored text uses \n; the edit control needs \r\n */
static wchar_t *sys_to_edit(const char *u)
{
    SB c = {0};
    wchar_t *w;
    for (; *u; u++)
    {
        if (*u == '\r')
            continue;
        if (*u == '\n')
            sb_putc(&c, '\r');
        sb_putc(&c, *u);
    }
    w = w_from(c.p ? sbs(&c) : "");
    free(c.p);
    return w;
}
static void strip_cr(char *s)
{
    char *d = s;
    for (; *s; s++)
        if (*s != '\r')
            *d++ = *s;
    *d = 0;
}

/* ---------- saved system prompts: data\prompts.json = [{"name":..,"text":..}, ...] ---------- */
typedef struct
{
    char name[80];
    char *text;
} SysPrompt;
static SysPrompt *sysP;
static int nSysP;
static HWND hSysEdit, hSysCombo;

static void sysp_free(void)
{
    for (int i = 0; i < nSysP; i++)
        free(sysP[i].text);
    free(sysP);
    sysP = NULL;
    nSysP = 0;
}
static void sysp_load(void)
{
    wchar_t p[MAX_PATH];
    size_t n;
    sysp_free();
    data_path(L"prompts.json", p);
    char *d = (char *)read_file(p, &n);
    if (!d)
        return;
    J *a = json_parse(d), *c;
    free(d);
    if (a && a->t == J_ARR)
    {
        int cnt = 0;
        for (c = a->child; c; c = c->next)
            cnt++;
        sysP = (SysPrompt *)calloc(cnt ? cnt : 1, sizeof *sysP);
        for (c = a->child; c; c = c->next)
        {
            const char *nm = j_str(j_get(c, "name")), *tx = j_str(j_get(c, "text"));
            if (!nm || !*nm)
                continue;
            snprintf(sysP[nSysP].name, sizeof sysP[nSysP].name, "%s", nm);
            sysP[nSysP].text = xstrdup(tx ? tx : "");
            nSysP++;
        }
    }
    j_free(a);
}
static void sysp_save(void)
{
    wchar_t p[MAX_PATH];
    SB o = {0};
    sb_puts(&o, "[\n");
    for (int i = 0; i < nSysP; i++)
    {
        sb_puts(&o, "  {\"name\": ");
        sb_json(&o, sysP[i].name);
        sb_puts(&o, ", \"text\": ");
        sb_json(&o, sysP[i].text);
        sb_puts(&o, i + 1 < nSysP ? "},\n" : "}\n");
    }
    sb_puts(&o, "]\n");
    data_path(L"prompts.json", p);
    if (!write_atomic(p, o.p, strlen(o.p)))
        save_fail("prompts.json");
    free(o.p);
}
static int sysp_find(const char *name)
{
    for (int i = 0; i < nSysP; i++)
        if (!_stricmp(sysP[i].name, name))
            return i;
    return -1;
}
/* combo box text, trimmed (caller frees) */
static char *sysp_combo_name(void)
{
    char *t = get_text(hSysCombo), *s = t, *e;
    while (*s == ' ' || *s == '\t')
        s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
        *--e = 0;
    memmove(t, s, strlen(s) + 1);
    return t;
}
static void sysp_fill_combo(const char *sel)
{
    SendMessageW(hSysCombo, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < nSysP; i++)
    {
        wchar_t *w = w_from(sysP[i].name);
        SendMessageW(hSysCombo, CB_ADDSTRING, 0, (LPARAM)w);
        free(w);
    }
    int k = sel ? sysp_find(sel) : -1;
    if (k >= 0)
        SendMessageW(hSysCombo, CB_SETCURSEL, (WPARAM)k, 0);
    else
    {
        wchar_t *w = w_from(sel ? sel : "");
        SetWindowTextW(hSysCombo, w);
        free(w);
    }
}
static void sysp_on_select(void)
{
    int k = (int)SendMessageW(hSysCombo, CB_GETCURSEL, 0, 0);
    if (k < 0 || k >= nSysP)
        return;
    wchar_t *w = sys_to_edit(sysP[k].text);
    SetWindowTextW(hSysEdit, w);
    free(w);
}
static void sysp_on_save(HWND h)
{
    char *nm = sysp_combo_name();
    if (!*nm)
    {
        MessageBoxW(h, L"Type a name for this prompt in the box first.", L"Save prompt", MB_OK | MB_ICONINFORMATION);
        SetFocus(hSysCombo);
        free(nm);
        return;
    }
    char *tx = get_text(hSysEdit);
    strip_cr(tx);
    int k = sysp_find(nm);
    if (k < 0)
    {
        sysP = (SysPrompt *)realloc(sysP, (nSysP + 1) * sizeof *sysP);
        k = nSysP++;
        sysP[k].text = NULL;
    }
    snprintf(sysP[k].name, sizeof sysP[k].name, "%s", nm);
    free(sysP[k].text);
    sysP[k].text = tx;
    sysp_save();
    sysp_fill_combo(sysP[k].name);
    free(nm);
}
static void sysp_on_delete(HWND h)
{
    char *nm = sysp_combo_name();
    int k = sysp_find(nm);
    free(nm);
    if (k < 0)
        return;
    wchar_t q[160], *wn = w_from(sysP[k].name);
    _snwprintf(q, 160, L"Delete the saved prompt \"%ls\"?", wn);
    q[159] = 0;
    free(wn);
    if (MessageBoxW(h, q, L"Delete prompt", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;
    free(sysP[k].text);
    memmove(sysP + k, sysP + k + 1, (nSysP - k - 1) * sizeof *sysP);
    nSysP--;
    sysp_save();
    sysp_fill_combo("");
}

/* ---------- dialog layout: two columns of sections; '#' = section header, '!mcp' = MCP buttons,
   half = two fields side by side. Fields not listed here are not shown. ---------- */
typedef struct
{
    const char *key, *label, *tip;
    int half;
} SItem;
static const SItem SLAY_L[] = {
    {"#", "Connection", NULL, 0},
    {"url", NULL, "Address of llama-server, e.g. http://127.0.0.1:8080. Models come from /v1/models; the context size and the server's sampling defaults from /props.", 0},
    {"key", NULL, "Sent as 'Authorization: Bearer <key>'. Only needed when llama-server was started with --api-key.", 0},
    {"#", "Chat template", NULL, 0},
    {"sys", NULL, "Sent as the system message at the start of every request. Save / Delete keep named prompts in data\\prompts.json.", 0},
    {"kwargs", "Template kwargs", "JSON object passed to the server's Jinja chat template as chat_template_kwargs, e.g. {\"enable_thinking\": false}. Ignored unless it is a valid JSON object. Also sent with the compression summary request.", 0},
    {"rfmt", "Reasoning format", "Sent as reasoning_format: auto, none or deepseek. Empty = server setting. Decides whether the model's thinking arrives separately (shown as a Thinking block) or inline in the answer.", 0},
    {"stop", "Stop strings", "Generation stops when one of these appears. Separate several with |, e.g.  </answer>|###", 0},
    {"custom", "Extra request JSON", "Extra fields added to every chat request, written as a JSON object, e.g. {\"n_probs\": 0}. For server options that have no setting here.", 0},
    {"#", "Display", NULL, 0},
    {"dark", NULL, "Dark colour theme.", 0},
    {"thoughts", NULL, "Show the model's reasoning as a collapsible Thinking block above the answer.", 0},
    {"stats", NULL, "Status bar shows prompt / generated tokens, speed and context use while generating.", 0},
    {"fade", NULL, "Streamed text fades in instead of appearing at once.", 0},
    {"smartpaste", NULL, "Pasting more than 2000 characters into the input adds them as an attachment (pasted-text.txt) instead of filling the box.", 0},
    {NULL, NULL, NULL, 0}};
static const SItem SLAY_R[] = {
    {"#", "Sampling  -  empty = server value", NULL, 0},
    {"temp", NULL, "Randomness. Lower = focused and repeatable, higher = more varied. Empty = server value (shown grey in the box).", 1},
    {"top_k", NULL, "Sample only from the K most likely tokens. 0 = off.", 1},
    {"top_p", NULL, "Sample from the smallest set of tokens whose probabilities add up to P. 1 = off.", 1},
    {"min_p", NULL, "Drop tokens less likely than P x the most likely token. 0 = off.", 1},
    {"rep", "Repeat pen.", "Repeat penalty: lowers the chance of recently used tokens. 1 = off.", 1},
    {"pres", "Presence pen.", "Presence penalty: lowers the chance of any token that already appeared. 0 = off.", 1},
    {"freq", "Frequency pen.", "Frequency penalty: grows with how often a token appeared. 0 = off.", 1},
    {"dry", "DRY mult.", "DRY multiplier: penalizes repeating whole sequences. 0 = off.", 1},
    {"maxtok", NULL, "Most tokens generated per reply. Empty, 0 or negative = not sent (server value applies).", 1},
    {"seed", NULL, "Random seed for repeatable output. Empty or negative = not sent (random).", 1},
    {"#", "Tools & memory", NULL, 0},
    {"tools", "Enable tools (files, memory, MCP)", "Lets the model call tools: file tools (need a root folder), memory and MCP servers. Read-only tools run without asking.", 0},
    {"fsroot", "Root folder", "File tools work only inside this folder. Empty = no file tools.", 0},
    {"fswrite", NULL, "Adds write_file, edit_file, create_directory, move_path and delete_file. Each call asks for approval unless auto-approve is on.", 0},
    {"shell", NULL, "Adds run_command: runs cmd.exe commands starting in the root folder. A command can reach anything your Windows account can.", 0},
    {"autoappr", NULL, "Runs every tool call without asking, including writes, deletes and shell commands.", 0},
    {"toolmax", "Max tool rounds", "How many tool-call rounds the model may do for one message before it is stopped. Empty or 0 = 10.", 0},
    {"mem", NULL, "Saved facts about you are added to the system prompt. With tools on, the model can save and search them (memory_save / memory_search).", 0},
    {"!mcp", NULL, NULL, 0},
    {"#", "Context & compression", NULL, 0},
    {"ctxlimit", "Context limit", "Context size in tokens that compression works with. 0 = the server's n_ctx (from /props). Set it lower to compress earlier. It does not change the server or trim requests.", 0},
    {"autocompact", NULL, "When the conversation nears the context limit, the model summarizes the older messages into one summary message. The originals stay in the previous branch (<).", 0},
    {"compactpct", "Compress at %", "Start compressing when the prompt reaches this % of the context limit (30-95). 0 = auto: keeps max(15%, 4096 tokens) free for the reply.", 0},
    {NULL, NULL, NULL, 0}};
static const SItem *const SLAYOUT[2] = {SLAY_L, SLAY_R};
static HWND setHdr[12];
static int nSetHdr;
static int is_set_hdr(HWND hw)
{
    for (int i = 0; i < nSetHdr; i++)
        if (setHdr[i] == hw)
            return 1;
    return 0;
}
static void tip_add(HWND tt, HWND ctl, const char *tip)
{
    if (!tt || !ctl || !tip)
        return;
    wchar_t *w = w_from(tip);
    TOOLINFOW ti;
    memset(&ti, 0, sizeof ti);
    ti.cbSize = TTTOOLINFOW_V2_SIZE; /* accepted by Common Controls v5 and v6; sizeof ti only by v6 */
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = hSet;
    ti.uId = (UINT_PTR)ctl;
    ti.lpszText = w;
    SendMessageW(tt, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    free(w); /* the tooltip keeps its own copy */
}

static LRESULT CALLBACK SetProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_ERASEBKGND:
    {
        RECT r;
        GetClientRect(h, &r);
        FillRect((HDC)w, &r, hbrPanel);
        return 1;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        SetTextColor((HDC)w, is_set_hdr((HWND)l) ? cAccent : cFg);
        SetBkColor((HDC)w, cPanel);
        return (LRESULT)hbrPanel;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetTextColor((HDC)w, cFg);
        SetBkColor((HDC)w, cSurf);
        return (LRESULT)hbrSurf;
    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT *d = (DRAWITEMSTRUCT *)l;
        UINT oid = d->CtlID;
        if (oid == IDC_MCPEDIT || oid == IDC_MCPRELOAD || oid == IDC_SYSSAVE || oid == IDC_SYSDEL)
            d->CtlID = IDC_RESET; /* same look as the Reset button */
        draw_btn(d);
        d->CtlID = oid;
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(w) == IDC_SYSLIST)
        {
            if (HIWORD(w) == CBN_SELCHANGE)
                sysp_on_select();
        }
        else if (LOWORD(w) == IDC_SYSSAVE)
            sysp_on_save(h);
        else if (LOWORD(w) == IDC_SYSDEL)
            sysp_on_delete(h);
        else if (LOWORD(w) == IDC_MCPEDIT)
        {
            mcp_edit_config(h);
        }
        else if (LOWORD(w) == IDC_MCPRELOAD)
        {
            mcp_start_async();
        }
        else if (LOWORD(w) == IDC_RESET)
        {
            for (int i = 0; i < NFIELDS; i++)
                if (FIELDS[i].kind == 1 && setCtl[i])
                    SetWindowTextW(setCtl[i], L"");
        }
        else if (LOWORD(w) == IDOK)
        {
            for (int i = 0; i < NFIELDS; i++)
            {
                const Field *f = &FIELDS[i];
                if (!f->label || !setCtl[i]) /* not in the layout: keep the stored value */
                    continue;
                if (f->kind == 3)
                    *(int *)((char *)&G + f->off) = (SendMessageW(setCtl[i], BM_GETCHECK, 0, 0) == BST_CHECKED);
                else
                {
                    char *t = get_text(setCtl[i]);
                    if (!strcmp(f->key, "sys"))
                        strip_cr(t);
                    if (f->kind <= 1) /* the edit box holds the real text; \\ and \n escaping is only for the file */
                        snprintf((char *)&G + f->off, f->size, "%s", t);
                    else
                        field_set(&G, f, t);
                    free(t);
                }
            }
            {
                char *nm = sysp_combo_name();
                snprintf(G.sysname, sizeof G.sysname, "%s", nm);
                free(nm);
            }
            cfg_save();
            tools_apply_settings();
            set_theme();
            render_all();
            DestroyWindow(h);
        }
        else if (LOWORD(w) == IDCANCEL)
            DestroyWindow(h);
        return 0;
    case DM_GETDEFID:
        return MAKELONG(IDOK, DC_HASDEFID);
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        sysp_free();
        hSysEdit = hSysCombo = NULL;
        setDone = 1;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static void show_settings(void)
{
    static int reg = 0;
    if (!reg)
    {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof wc);
        wc.cbSize = sizeof wc;
        wc.lpfnWndProc = SetProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.lpszClassName = L"LCSet";
        RegisterClassExW(&wc);
        reg = 1;
    }
    const int colW = 450, labW = 150, rowH = 30, hdrH = 34, top = 14;
    const int sysExtra = 110, sysRow = 32; /* textarea extra height, saved-prompt picker row */
    const int halfW = (colW - 10) / 2, halfLab = 104;
    sysp_load();
    /* height of each column of the layout (same walk as the creation loop below) */
    int colH[2] = {0, 0};
    for (int c = 0; c < 2; c++)
    {
        int y = 0, half = 0;
        for (const SItem *it = SLAYOUT[c]; it->key; it++)
        {
            int fi = it->key[0] == '#' || it->key[0] == '!' ? -1 : fidx(it->key);
            if (half && !(fi >= 0 && it->half))
                y += rowH, half = 0; /* close an unfinished pair */
            if (it->key[0] == '#')
                y += hdrH;
            else if (it->key[0] == '!')
                y += rowH + 4;
            else if (fi < 0 || !FIELDS[fi].label)
                continue;
            else if (it->half)
            {
                if (half)
                    y += rowH;
                half = !half;
            }
            else if (!strcmp(it->key, "sys"))
                y += rowH + sysRow + sysExtra;
            else
                y += rowH;
        }
        if (half)
            y += rowH;
        colH[c] = y;
    }
    int CW = colW * 2 + 50, CH = top + (colH[0] > colH[1] ? colH[0] : colH[1]) + 70;
    RECT wr = {0, 0, CW, CH};
    DWORD st = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRectEx(&wr, st, FALSE, WS_EX_DLGMODALFRAME);
    RECT r;
    GetWindowRect(hMain, &r);
    hSet = CreateWindowExW(WS_EX_DLGMODALFRAME, L"LCSet", L"Settings", st, r.left + 60, r.top + 40, wr.right - wr.left, wr.bottom - wr.top, hMain, NULL, GetModuleHandleW(NULL), NULL);
    apply_frame(hSet);
    nSetHdr = 0;
    memset(setCtl, 0, sizeof setCtl);
    /* tooltips: hover a label or its control for the explanation */
    HWND tt = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                              CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hSet, NULL, NULL, NULL);
    if (tt)
    {
        SendMessageW(tt, TTM_SETMAXTIPWIDTH, 0, 380);
        SendMessageW(tt, TTM_SETDELAYTIME, TTDT_AUTOPOP, 30000);
        SendMessageW(tt, TTM_SETDELAYTIME, TTDT_INITIAL, 400);
        if (G.dark)
            SetWindowTheme(tt, L"DarkMode_Explorer", NULL);
    }
    for (int c = 0; c < 2; c++)
    {
        int x0 = 20 + c * (colW + 10), y = top, half = 0;
        for (const SItem *it = SLAYOUT[c]; it->key; it++)
        {
            int fi = it->key[0] == '#' || it->key[0] == '!' ? -1 : fidx(it->key);
            if (half && !(fi >= 0 && it->half))
                y += rowH, half = 0;
            if (it->key[0] == '#')
            { /* section header */
                wchar_t *wt = w_from(it->label);
                HWND hh = CreateWindowExW(0, L"STATIC", wt, WS_CHILD | WS_VISIBLE | SS_NOPREFIX, x0, y + 8, colW, 22, hSet, NULL, NULL, NULL);
                free(wt);
                SendMessageW(hh, WM_SETFONT, (WPARAM)hFontB, TRUE);
                if (nSetHdr < (int)(sizeof setHdr / sizeof setHdr[0]))
                    setHdr[nSetHdr++] = hh;
                y += hdrH;
                continue;
            }
            if (!strcmp(it->key, "!mcp"))
            { /* MCP server buttons, part of the Tools section */
                HWND be = mkbtn(hSet, L"Edit MCP servers...", IDC_MCPEDIT), br = mkbtn(hSet, L"Reload MCP", IDC_MCPRELOAD);
                MoveWindow(be, x0, y + 2, 170, 28, FALSE);
                MoveWindow(br, x0 + 180, y + 2, 110, 28, FALSE);
                tip_add(tt, be, "Edit the MCP server list (data\\mcp.json): local programs (stdio) or HTTP servers whose tools the model can call.");
                tip_add(tt, br, "Restart the MCP servers and load their tools again, e.g. after editing the list.");
                y += rowH + 4;
                continue;
            }
            if (fi < 0 || !FIELDS[fi].label)
                continue;
            const Field *f = &FIELDS[fi];
            wchar_t *lb = w_from(it->label ? it->label : f->label);
            int x = x0, w = colW, lw = labW;
            if (it->half)
            {
                x = x0 + (half ? halfW + 10 : 0);
                w = halfW;
                lw = halfLab;
            }
            if (f->kind == 3)
            {
                setCtl[fi] = CreateWindowExW(0, L"BUTTON", lb, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, x, y, w - 10, 24, hSet, NULL, NULL, NULL);
                SetWindowTheme(setCtl[fi], L"", L"");
                SendMessageW(setCtl[fi], BM_SETCHECK, *(int *)((char *)&G + f->off) ? BST_CHECKED : BST_UNCHECKED, 0);
                tip_add(tt, setCtl[fi], it->tip);
            }
            else
            {
                int isSys = !strcmp(f->key, "sys");
                HWND hl = CreateWindowExW(0, L"STATIC", lb, WS_CHILD | WS_VISIBLE | SS_NOPREFIX | SS_NOTIFY, x, y + 4, lw - 4, 20, hSet, NULL, NULL, NULL);
                SB v = {0};
                if (f->kind <= 1)
                    sb_puts(&v, (const char *)&G + f->off); /* real text, not the file's escaped form */
                else
                    field_get(&G, f, &v);
                wchar_t *wv = isSys ? sys_to_edit(G.sys) : w_from(sbs(&v));
                if (isSys)
                {
                    /* saved-prompt picker: [ name combo v ] [Save] [Delete] */
                    const int fw = w - lw - 10, bw1 = 54, bw2 = 62, cw = fw - bw1 - bw2 - 8;
                    hSysCombo = CreateWindowExW(0, L"COMBOBOX", L"",
                                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL,
                                                x + lw, y, cw, 240, hSet, (HMENU)(INT_PTR)IDC_SYSLIST, NULL, NULL);
                    SetWindowTheme(hSysCombo, G.dark ? L"DarkMode_CFD" : L"CFD", NULL);
                    SendMessageW(hSysCombo, CB_LIMITTEXT, (WPARAM)(sizeof G.sysname - 1), 0);
                    SendMessageW(hSysCombo, CB_SETCUEBANNER, 0, (LPARAM)L"Saved prompts / new name");
                    sysp_fill_combo(G.sysname);
                    HWND bs = mkbtn(hSet, L"Save", IDC_SYSSAVE), bd = mkbtn(hSet, L"Delete", IDC_SYSDEL);
                    MoveWindow(bs, x + lw + cw + 4, y, bw1, 26, FALSE);
                    MoveWindow(bd, x + lw + cw + 8 + bw1, y, bw2, 26, FALSE);
                    tip_add(tt, hSysCombo, "Pick a saved prompt, or type a new name and press Save to store the text below under it.");
                    hSysEdit = setCtl[fi] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", wv,
                                                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                                                            x + lw, y + sysRow, fw, 24 + sysExtra, hSet, NULL, NULL, NULL);
                    SendMessageW(setCtl[fi], EM_SETLIMITTEXT, (WPARAM)(sizeof G.sys - 1), 0);
                    SendMessageW(setCtl[fi], EM_SETCUEBANNER, TRUE, (LPARAM)L"Instructions sent to the model at the start of every chat");
                    y += sysRow + sysExtra;
                }
                else
                    setCtl[fi] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", wv, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, x + lw, y, w - lw - 10, 24, hSet, NULL, NULL, NULL);
                if (f->kind == 1)
                {
                    char cue[80];
                    snprintf(cue, sizeof cue, "%s%s", srvdef[fi][0] ? "server: " : "server default", srvdef[fi]);
                    wchar_t *wc = w_from(cue);
                    SendMessageW(setCtl[fi], EM_SETCUEBANNER, TRUE, (LPARAM)wc);
                    free(wc);
                }
                tip_add(tt, hl, it->tip);
                tip_add(tt, setCtl[fi], it->tip);
                free(wv);
                free(v.p);
            }
            free(lb);
            if (it->half)
            {
                if (half)
                    y += rowH;
                half = !half;
            }
            else
                y += rowH;
        }
    }
    mkbtn(hSet, L"Reset sampling", IDC_RESET);
    mkbtn(hSet, L"OK", IDOK);
    mkbtn(hSet, L"Cancel", IDCANCEL);
    MoveWindow(GetDlgItem(hSet, IDC_RESET), 20, CH - 52, 150, 34, TRUE);
    MoveWindow(GetDlgItem(hSet, IDOK), CW - 190, CH - 52, 80, 34, TRUE);
    MoveWindow(GetDlgItem(hSet, IDCANCEL), CW - 100, CH - 52, 80, 34, TRUE);
    tip_add(tt, GetDlgItem(hSet, IDC_RESET), "Clear all sampling overrides, so every value comes from llama-server again. Takes effect with OK.");
    for (HWND c = GetWindow(hSet, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        if (!is_set_hdr(c))
            SendMessageW(c, WM_SETFONT, (WPARAM)hFont, TRUE);
    setDone = 0;
    EnableWindow(hMain, FALSE);
    ShowWindow(hSet, SW_SHOW);
    MSG msg;
    while (!setDone && GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        if (!IsDialogMessageW(hSet, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(hMain, TRUE);
    SetForegroundWindow(hMain);
}

#endif /* LLAMAC_SETTINGS_UI_H */
