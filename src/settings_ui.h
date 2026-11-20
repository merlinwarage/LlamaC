/* settings_ui.h - Settings dialog */
#ifndef LLAMAC_SETTINGS_UI_H
#define LLAMAC_SETTINGS_UI_H

static void layout(void); /* main_window.h */
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

/* ---------- profiles (saved prompts): data\prompts.json = [{"name":..,"text":..,"remind":..,"toolset":..,"temp":..}, ...]
   Besides the system prompt a profile keeps the settings that suit a kind of work and a model: the reminder, the
   tool set, sampling values, the reasoning budget and the template kwargs. Entries of older versions have only
   name and text; picking them changes only the prompt. */
static const char *const PROF_KEYS[] = {"remind", "toolset", "temp", "top_k", "top_p", "min_p", "rbud", "kwargs", "tooloff"};
static char s_tooloff[1024]; /* the dialog's copy of G.tooloff (Built-in tools... button, profiles); applied with OK */
#define NPK ((int)(sizeof PROF_KEYS / sizeof PROF_KEYS[0]))
typedef struct
{
    char name[80];
    char *text;
    char *vals[NPK]; /* NULL = not stored in this profile */
} SysPrompt;
static SysPrompt *sysP;
static int nSysP;
static HWND hSysEdit, hSysCombo;

static void sysp_free(void)
{
    for (int i = 0; i < nSysP; i++)
    {
        free(sysP[i].text);
        for (int k = 0; k < NPK; k++)
            free(sysP[i].vals[k]);
    }
    free(sysP);
    sysP = NULL;
    nSysP = 0;
}
/* the value a settings control shows (combo: its index), malloc'ed */
static char *ctl_value(int fi)
{
    char b[16];
    if (fi >= 0 && !strcmp(FIELDS[fi].key, "tooloff"))
        return xstrdup(s_tooloff);
    if (fi < 0 || !setCtl[fi])
        return NULL;
    if (!strcmp(FIELDS[fi].key, "toolset"))
    {
        int s = (int)SendMessageW(setCtl[fi], CB_GETCURSEL, 0, 0);
        snprintf(b, sizeof b, "%d", s < 0 ? 0 : s);
        return xstrdup(b);
    }
    return get_text(setCtl[fi]);
}
static void ctl_set(int fi, const char *v)
{
    if (fi >= 0 && v && !strcmp(FIELDS[fi].key, "tooloff"))
    {
        snprintf(s_tooloff, sizeof s_tooloff, "%s", v);
        return;
    }
    if (fi < 0 || !setCtl[fi] || !v)
        return;
    if (!strcmp(FIELDS[fi].key, "toolset"))
        SendMessageW(setCtl[fi], CB_SETCURSEL, (WPARAM)atoi(v), 0);
    else
    {
        wchar_t *w = w_from(v);
        SetWindowTextW(setCtl[fi], w);
        free(w);
    }
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
            for (int k = 0; k < NPK; k++)
            {
                J *v = j_get(c, PROF_KEYS[k]);
                char nb[40];
                sysP[nSysP].vals[k] = NULL;
                if (v && v->t == J_STR)
                    sysP[nSysP].vals[k] = xstrdup(v->str);
                else if (v && v->t == J_NUM)
                {
                    fmt_num(nb, v->num);
                    sysP[nSysP].vals[k] = xstrdup(nb);
                }
            }
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
        for (int k = 0; k < NPK; k++)
            if (sysP[i].vals[k])
            {
                sb_printf(&o, ", \"%s\": ", PROF_KEYS[k]);
                sb_json(&o, sysP[i].vals[k]);
            }
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
    for (int i = 0; i < NPK; i++) /* the profile's other settings (only the ones it stores) */
        ctl_set(fidx(PROF_KEYS[i]), sysP[k].vals[i]);
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
        memset(&sysP[k], 0, sizeof sysP[k]);
    }
    snprintf(sysP[k].name, sizeof sysP[k].name, "%s", nm);
    free(sysP[k].text);
    sysP[k].text = tx;
    for (int i = 0; i < NPK; i++)
    {
        free(sysP[k].vals[i]);
        sysP[k].vals[i] = ctl_value(fidx(PROF_KEYS[i]));
    }
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
    for (int i = 0; i < NPK; i++)
        free(sysP[k].vals[i]);
    memmove(sysP + k, sysP + k + 1, (nSysP - k - 1) * sizeof *sysP);
    nSysP--;
    sysp_save();
    sysp_fill_combo("");
}

/* Windows folder dialog; path is the start folder on entry and the choice on return. 1 = chosen. */
static int pick_folder(HWND owner, const wchar_t *title, wchar_t *path, size_t cap)
{
    IFileOpenDialog *d = NULL;
    IShellItem *it = NULL;
    int ok = 0;
    HRESULT hc = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileOpenDialog, (void **)&d)))
    {
        DWORD o = 0;
        IFileOpenDialog_GetOptions(d, &o);
        IFileOpenDialog_SetOptions(d, o | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        IFileOpenDialog_SetTitle(d, title);
        if (path[0])
        {
            IShellItem *f = NULL;
            if (SUCCEEDED(SHCreateItemFromParsingName(path, NULL, &IID_IShellItem, (void **)&f)))
            {
                IFileOpenDialog_SetFolder(d, f);
                IShellItem_Release(f);
            }
        }
        if (SUCCEEDED(IFileOpenDialog_Show(d, owner)) && SUCCEEDED(IFileOpenDialog_GetResult(d, &it)))
        {
            PWSTR s = NULL;
            if (SUCCEEDED(IShellItem_GetDisplayName(it, SIGDN_FILESYSPATH, &s)))
            {
                wcsncpy(path, s, cap - 1);
                path[cap - 1] = 0;
                CoTaskMemFree(s);
                ok = 1;
            }
            IShellItem_Release(it);
        }
        IFileOpenDialog_Release(d);
    }
    if (SUCCEEDED(hc))
        CoUninitialize();
    return ok;
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
    {"#", "Chat template & profiles", NULL, 0},
    {"sys", NULL, "Sent as the system message at the start of every request. Save / Delete keep named profiles in data\\prompts.json: the prompt together with the reminder, tool set, temperature, top-k/p, min-p, reasoning budget and template kwargs.", 0},
    {"remind", "Reminder", "A few words added to the end of your latest message when it is sent (not stored, not shown), e.g. 'Answer in Hungarian. Keep it short.' Small models follow instructions at the end of the context better than ones at the start. Costs a few tokens; the prompt cache stays valid.", 0},
    {"toolset", "Tool set", "Which tools the model gets. All: file, shell, memory, guides, MCP. Chat: date, memory, guides and MCP only - fewer tool definitions (often 1-2k tokens less per request) and fewer wrong tool choices. None: no tools at all (needed for token confidence shading).", 0},
    {"kwargs", "Template kwargs", "JSON object passed to the server's Jinja chat template as chat_template_kwargs, e.g. {\"enable_thinking\": false}. Ignored unless it is a valid JSON object. Also sent with the compression summary request.", 0},
    {"rfmt", "Reasoning format", "Sent as reasoning_format: auto, none or deepseek. Empty = server setting. Decides whether the model's thinking arrives separately (shown as a Thinking block) or inline in the answer.", 0},
    {"stop", "Stop strings", "Generation stops when one of these appears. Separate several with |, e.g.  </answer>|###", 0},
    {"custom", "Extra request JSON", "Extra fields added to every chat request, written as a JSON object, e.g. {\"n_probs\": 0}. For server options that have no setting here.", 0},
    {"#", "Context & compression", NULL, 0},
    {"ctxlimit", "Context limit", "Context size in tokens that compression and fitting work with. 0 = the server's n_ctx (from /props). Set it lower to compress earlier.", 0},
    {"autocompact", NULL, "When the conversation nears the context limit: first old tool results are shortened (instant), and only if that is not enough the model summarizes the older messages. The originals stay in the previous branch (<).", 0},
    {"compactpct", "Compress at %", "Start compressing when the prompt reaches this % of the context limit (30-95). 0 = auto: keeps max(15%, 4096 tokens) free for the reply.", 0},
    {"precompact", NULL, "After a reply, when the context is above 85% of the compression point, compress while you read instead of when you send. Sending a message that still fits cancels it.", 0},
    {"sumbud", "Summary budget", "Most tokens a reasoning model may think before writing the compression summary (reasoning_budget_tokens; needs a recent llama-server). Makes compression much faster. 0 = no limit.", 0},
    {"budget", NULL, "If a request would not fit, old tool results are shortened, large ones clipped and, as a last resort, early messages left out of that request - instead of a server error. Nothing is deleted from the chat.", 0},
    {"kvslot", NULL, "Saves the server's KV cache when you switch chats and restores it when you come back, so a long chat is not processed again (seconds instead of minutes). Start llama-server with --slot-save-path <folder>. Uses slot 0.", 0},
    {"kvdir", "KV slot folder", "Optional: the server's --slot-save-path folder when llama-server runs on this PC: a full path such as I:\\AI\\llama-server\\slot-save, or one relative to the folder of llamac.exe such as llama-server\\slot-save. Then a chat's file is checked before restoring, deleted with the chat, and old files are removed above the limit. Only llamac-*.bin files are deleted.", 0},
    {"kvmax", "KV folder limit (GB)", "Most space the saved KV caches (llamac-*.bin) may use in the KV slot folder; the oldest are deleted first. A 27B model needs about 0.5-1 GB per chat. 0 = no limit.", 0},
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
    {"rbud", "Think budget", "Reasoning budget: most tokens a reasoning model may think per reply (reasoning_budget_tokens, needs a recent llama-server). Empty = not sent.", 1},
    {"#", "Tools & memory", NULL, 0},
    {"tools", "Enable tools (files, memory, MCP)", "Lets the model call tools: file tools (need a root folder), memory and MCP servers. Read-only tools run without asking.", 0},
    {"fsroot", "Root folder", "File tools work only inside this folder. Empty = no file tools.", 0},
    {"fswrite", NULL, "Adds write_file, edit_file, create_directory, move_path and delete_file. Each call asks for approval unless auto-approve is on.", 0},
    {"shell", NULL, "Adds run_command: runs cmd.exe commands starting in the root folder. A command can reach anything your Windows account can.", 0},
    {"autoappr", NULL, "Runs every tool call without asking, including writes, deletes and shell commands.", 0},
    {"toolmax", "Max tool rounds", "How many tool-call rounds the model may do for one message before it is stopped. Empty or 0 = 10.", 0},
    {"mem", NULL, "Saved facts about you are added to the system prompt - the ones that match the chat's first message first. With tools on, the model can save and search them (memory_save / memory_search).", 0},
    {"shot", NULL, "Adds the screenshot tool: the model can ask to see the screen or a window (needs a vision model). Every screenshot asks first, even with auto-approve, because the screen can show anything, including personal or confidential data.", 0},
    {"verify", "Verify command", "Run after every tool round that changed files, e.g. build.bat or npm test (cmd.exe, in the root folder, 5 min limit). Its result is added to the tool result, so the model sees build errors at once. Runs without asking: it is your own command.", 0},
    {"!mcp", NULL, NULL, 0},
    {"#", "Display", NULL, 0},
    {"dark", NULL, "Dark colour theme.", 0},
    {"thoughts", NULL, "Show the model's reasoning as a collapsible Thinking block above the answer, and tool results as collapsible blocks (click to open). Off hides both; the tool calls stay visible.", 0},
    {"stats", NULL, "Status bar shows prompt / generated tokens, speed and context use while generating.", 0},
    {"fade", NULL, "Streamed text fades in instead of appearing at once.", 0},
    {"smartpaste", NULL, "Pasting more than 2000 characters into the input adds them as an attachment (pasted-text.txt) instead of filling the box.", 0},
    {"logprobs", NULL, "Shades words the model was unsure about; click one to see what else it considered and continue the answer from another choice. llama-server allows this only in requests without tools: set Tool set to None (e.g. in a profile).", 0},
    {"copilot", NULL, "LlamaC lives in the tray. The hotkey (or a click on the tray icon) shows a small floating panel with the input box; it shows only the latest exchange and grows with the answer. Open app in the panel or the tray menu brings back this window. Closing this window keeps LlamaC in the tray; Exit is in the tray menu.", 0},
    {"cphk", "Hotkey", "Shows / hides the copilot panel from anywhere, e.g. Ctrl+Shift+Space, Win+Alt+L or F9. Modifiers: Ctrl, Alt, Shift, Win; keys: A-Z, 0-9, F1-F24, Space, Insert, Home, End, PageUp, PageDown, Pause. Avoid Ctrl+Alt combinations on keyboards with AltGr.", 0},
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

static void cp_apply(void); /* copilot.h */
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
        if (oid == IDC_MCPEDIT || oid == IDC_MCPRELOAD || oid == IDC_SYSSAVE || oid == IDC_SYSDEL || oid == IDC_BROWSE_FS || oid == IDC_BROWSE_KV || oid == IDC_TOOLSDLG)
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
        else if (LOWORD(w) == IDC_TOOLSDLG)
        { /* show the dialog for the tool set and Enable tools as chosen here, not yet applied */
            int fs = fidx("toolset"), fo = fidx("tools"), set = G.toolset, on = G.tools_on;
            if (fs >= 0 && setCtl[fs])
            {
                int s = (int)SendMessageW(setCtl[fs], CB_GETCURSEL, 0, 0);
                set = s < 0 ? 0 : s;
            }
            if (fo >= 0 && setCtl[fo])
                on = SendMessageW(setCtl[fo], BM_GETCHECK, 0, 0) == BST_CHECKED;
            tools_dialog(h, s_tooloff, sizeof s_tooloff, set, on);
        }
        else if (LOWORD(w) == IDC_BROWSE_FS || LOWORD(w) == IDC_BROWSE_KV)
        {
            int fi = fidx(LOWORD(w) == IDC_BROWSE_FS ? "fsroot" : "kvdir");
            if (fi >= 0 && setCtl[fi])
            {
                char *cv = get_text(setCtl[fi]);
                wchar_t path[MAX_PATH] = L"";
                path_clean(cv);
                if (*cv)
                { /* start in the current folder; a relative one is taken from the exe folder */
                    wchar_t *wc = w_from(cv), base[MAX_PATH], tmp[MAX_PATH * 2];
                    if (PathIsRelativeW(wc))
                    {
                        GetModuleFileNameW(NULL, base, MAX_PATH);
                        if (wcsrchr(base, L'\\'))
                            *wcsrchr(base, L'\\') = 0;
                        _snwprintf(tmp, MAX_PATH * 2, L"%ls\\%ls", base, wc);
                        tmp[MAX_PATH * 2 - 1] = 0;
                        GetFullPathNameW(tmp, MAX_PATH, path, NULL);
                    }
                    else
                        GetFullPathNameW(wc, MAX_PATH, path, NULL);
                    free(wc);
                }
                free(cv);
                if (pick_folder(h, LOWORD(w) == IDC_BROWSE_FS ? L"Root folder for the file tools" : L"KV slot folder (llama-server --slot-save-path)", path, MAX_PATH))
                    SetWindowTextW(setCtl[fi], path);
            }
        }
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
                else if (!strcmp(f->key, "toolset"))
                {
                    int s = (int)SendMessageW(setCtl[i], CB_GETCURSEL, 0, 0);
                    G.toolset = s < 0 ? 0 : s;
                }
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
            path_clean(G.fsroot);
            path_clean(G.kvdir);
            snprintf(G.tooloff, sizeof G.tooloff, "%s", s_tooloff);
            cfg_save();
            tools_apply_settings();
            cp_apply(); /* copilot mode: tray icon and hotkey on / off */
            layout(); /* the MCP and Tools buttons in the input box come and go with "Enable tools" */
            mcp_btn_update();
            tools_btn_update();
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
    snprintf(s_tooloff, sizeof s_tooloff, "%s", G.tooloff);
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
                HWND be = mkbtn(hSet, L"Edit MCP servers...", IDC_MCPEDIT), br = mkbtn(hSet, L"Reload MCP", IDC_MCPRELOAD), btd = mkbtn(hSet, L"Built-in tools...", IDC_TOOLSDLG);
                MoveWindow(btd, x0 + 300, y + 2, 140, 28, FALSE);
                tip_add(tt, btd, "Switch single built-in tools off (or on) to save tokens and avoid needless tool calls. Also stored in a profile when you Save it.");
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
                else if (!strcmp(f->key, "toolset"))
                {
                    setCtl[fi] = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                                                 x + lw, y, w - lw - 10, 200, hSet, (HMENU)(INT_PTR)IDC_TOOLSET, NULL, NULL);
                    SetWindowTheme(setCtl[fi], G.dark ? L"DarkMode_CFD" : L"CFD", NULL);
                    SendMessageW(setCtl[fi], CB_ADDSTRING, 0, (LPARAM)L"All tools");
                    SendMessageW(setCtl[fi], CB_ADDSTRING, 0, (LPARAM)L"Chat: date, memory, guides, MCP");
                    SendMessageW(setCtl[fi], CB_ADDSTRING, 0, (LPARAM)L"None");
                    SendMessageW(setCtl[fi], CB_SETCURSEL, (WPARAM)G.toolset, 0);
                }
                else if (!strcmp(f->key, "fsroot") || !strcmp(f->key, "kvdir"))
                { /* folder: edit box + "..." button that opens the folder picker */
                    int bid = !strcmp(f->key, "fsroot") ? IDC_BROWSE_FS : IDC_BROWSE_KV;
                    HWND bb;
                    setCtl[fi] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", wv, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, x + lw, y, w - lw - 10 - 40, 24, hSet, NULL, NULL, NULL);
                    bb = mkbtn(hSet, L"...", bid);
                    MoveWindow(bb, x + w - 10 - 36, y, 36, 24, FALSE);
                    tip_add(tt, bb, "Choose the folder");
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
