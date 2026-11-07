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
        SetTextColor((HDC)w, cFg);
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
                if (!f->label)
                    continue;
                if (f->kind == 3)
                    *(int *)((char *)&G + f->off) = (SendMessageW(setCtl[i], BM_GETCHECK, 0, 0) == BST_CHECKED);
                else
                {
                    char *t = get_text(setCtl[i]);
                    if (!strcmp(f->key, "sys"))
                        strip_cr(t);
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
    const int colW = 450, labW = 170, rowH = 32;
    int rowsL = 0, rowsR = 0;
    const int sysExtra = 110, sysRow = 32; /* textarea extra height, saved-prompt picker row */
    int sysShift = 0;                      /* extra height for the system prompt box */
    sysp_load();
    for (int i = 0; i < NFIELDS; i++)
        if (FIELDS[i].label)
        {
            if (FIELDS[i].kind == 1)
                rowsR++;
            else
                rowsL++;
        }
    int rows = rowsL > rowsR ? rowsL : rowsR, CW = colW * 2 + 40, CH = 44 + rows * rowH + 80 + sysExtra + sysRow;
    RECT wr = {0, 0, CW, CH};
    DWORD st = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRectEx(&wr, st, FALSE, WS_EX_DLGMODALFRAME);
    RECT r;
    GetWindowRect(hMain, &r);
    hSet = CreateWindowExW(WS_EX_DLGMODALFRAME, L"LCSet", L"Settings", st, r.left + 60, r.top + 40, wr.right - wr.left, wr.bottom - wr.top, hMain, NULL, GetModuleHandleW(NULL), NULL);
    apply_frame(hSet);
    HWND t1 = CreateWindowExW(0, L"STATIC", L"General  &&  chat template", WS_CHILD | WS_VISIBLE, 20, 14, 400, 22, hSet, NULL, NULL, NULL);
    HWND t2 = CreateWindowExW(0, L"STATIC", L"Sampling overrides  -  empty = use llama-server value", WS_CHILD | WS_VISIBLE, 20 + colW + 10, 14, 440, 22, hSet, NULL, NULL, NULL);
    SendMessageW(t1, WM_SETFONT, (WPARAM)hFontB, TRUE);
    SendMessageW(t2, WM_SETFONT, (WPARAM)hFontB, TRUE);
    int rl = 0, rr = 0;
    for (int i = 0; i < NFIELDS; i++)
    {
        const Field *f = &FIELDS[i];
        if (!f->label)
            continue;
        int col = (f->kind == 1), x = 20 + col * (colW + 10), y = 44 + (col ? rr++ : rl++) * rowH + (col ? 0 : sysShift);
        wchar_t *lb = w_from(f->label);
        if (f->kind == 3)
        {
            setCtl[i] = CreateWindowExW(0, L"BUTTON", lb, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, x, y, colW - 10, 24, hSet, NULL, NULL, NULL);
            SetWindowTheme(setCtl[i], L"", L"");
            SendMessageW(setCtl[i], BM_SETCHECK, *(int *)((char *)&G + f->off) ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        else
        {
            int isSys = !strcmp(f->key, "sys");
            CreateWindowExW(0, L"STATIC", lb, WS_CHILD | WS_VISIBLE | SS_NOPREFIX, x, y + 4, labW, 20, hSet, NULL, NULL, NULL);
            SB v = {0};
            field_get(&G, f, &v);
            wchar_t *wv = isSys ? sys_to_edit(G.sys) : w_from(sbs(&v));
            if (isSys)
            {
                /* saved-prompt picker: [ name combo v ] [Save] [Delete] */
                const int fw = colW - labW - 10, bw1 = 54, bw2 = 62, cw = fw - bw1 - bw2 - 8;
                hSysCombo = CreateWindowExW(0, L"COMBOBOX", L"",
                                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL,
                                            x + labW, y, cw, 240, hSet, (HMENU)(INT_PTR)IDC_SYSLIST, NULL, NULL);
                SetWindowTheme(hSysCombo, G.dark ? L"DarkMode_CFD" : L"CFD", NULL);
                SendMessageW(hSysCombo, CB_LIMITTEXT, (WPARAM)(sizeof G.sysname - 1), 0);
                SendMessageW(hSysCombo, CB_SETCUEBANNER, 0, (LPARAM)L"Saved prompts / new name");
                sysp_fill_combo(G.sysname);
                HWND bs = mkbtn(hSet, L"Save", IDC_SYSSAVE), bd = mkbtn(hSet, L"Delete", IDC_SYSDEL);
                MoveWindow(bs, x + labW + cw + 4, y, bw1, 26, FALSE);
                MoveWindow(bd, x + labW + cw + 8 + bw1, y, bw2, 26, FALSE);
                hSysEdit = setCtl[i] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", wv,
                                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                                                       x + labW, y + sysRow, fw, 24 + sysExtra, hSet, NULL, NULL, NULL);
                SendMessageW(setCtl[i], EM_SETLIMITTEXT, (WPARAM)(sizeof G.sys - 1), 0);
                SendMessageW(setCtl[i], EM_SETCUEBANNER, TRUE, (LPARAM)L"Instructions sent to the model at the start of every chat");
                sysShift += sysExtra + sysRow;
            }
            else
            {
                setCtl[i] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", wv, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, x + labW, y, colW - labW - 10, 24, hSet, NULL, NULL, NULL);
            }
            if (f->kind == 1)
            {
                char cue[80];
                snprintf(cue, sizeof cue, "%s%s", srvdef[i][0] ? "server: " : "server default", srvdef[i]);
                wchar_t *wc = w_from(cue);
                SendMessageW(setCtl[i], EM_SETCUEBANNER, TRUE, (LPARAM)wc);
                free(wc);
            }
            free(wv);
            free(v.p);
            free(lb);
        }
    }
    mkbtn(hSet, L"Reset to server defaults", IDC_RESET);
    mkbtn(hSet, L"Edit MCP servers...", IDC_MCPEDIT);
    mkbtn(hSet, L"Reload MCP", IDC_MCPRELOAD);
    mkbtn(hSet, L"OK", IDOK);
    mkbtn(hSet, L"Cancel", IDCANCEL);
    MoveWindow(GetDlgItem(hSet, IDC_RESET), 20 + colW + 10, CH - 56, 200, 34, TRUE);
    MoveWindow(GetDlgItem(hSet, IDC_MCPEDIT), 20, CH - 56, 170, 34, TRUE);
    MoveWindow(GetDlgItem(hSet, IDC_MCPRELOAD), 200, CH - 56, 110, 34, TRUE);
    MoveWindow(GetDlgItem(hSet, IDOK), CW - 190, CH - 56, 80, 34, TRUE);
    MoveWindow(GetDlgItem(hSet, IDCANCEL), CW - 100, CH - 56, 80, 34, TRUE);
    for (HWND c = GetWindow(hSet, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        if (c != t1 && c != t2)
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
