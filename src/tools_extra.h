/* tools_extra.h - built-in tools that do not work on files: system_info, update_plan, ask_user, screenshot */
#ifndef LLAMAC_TOOLS_EXTRA_H
#define LLAMAC_TOOLS_EXTRA_H

/* ---------- system_info: what the model needs to write commands that work here (no user or computer names) ---------- */
static void reg_str(HKEY root, const wchar_t *key, const wchar_t *val, char *out, size_t cap)
{
    wchar_t buf[256];
    DWORD sz = sizeof buf;
    out[0] = 0;
    if (RegGetValueW(root, key, val, RRF_RT_REG_SZ, NULL, buf, &sz) == ERROR_SUCCESS)
    {
        char *u = u_from(buf);
        snprintf(out, cap, "%s", u);
        free(u);
    }
}
static char *sysinfo_call(void)
{
    SB o = {0};
    char prod[128], disp[64], build[32], cpu[160];
    DWORD ubr = 0, sz = sizeof ubr;
    MEMORYSTATUSEX ms;
    wchar_t drives[256];
    const wchar_t *NT = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    reg_str(HKEY_LOCAL_MACHINE, NT, L"ProductName", prod, sizeof prod);
    reg_str(HKEY_LOCAL_MACHINE, NT, L"DisplayVersion", disp, sizeof disp);
    reg_str(HKEY_LOCAL_MACHINE, NT, L"CurrentBuild", build, sizeof build);
    RegGetValueW(HKEY_LOCAL_MACHINE, NT, L"UBR", RRF_RT_REG_DWORD, NULL, &ubr, &sz);
    if (atoi(build) >= 22000 && !strncmp(prod, "Windows 10", 10)) /* the registry still says 10 on Windows 11 */
        prod[8] = '1', prod[9] = '1';
    sb_printf(&o, "OS: %s %s (build %s.%lu), %s\n", prod, disp, build, (unsigned long)ubr,
#ifdef _WIN64
              "64-bit"
#else
              "32-bit"
#endif
    );
    reg_str(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString", cpu, sizeof cpu);
    {
        char *c = cpu;
        size_t L;
        while (*c == ' ')
            c++;
        L = strlen(c);
        while (L && c[L - 1] == ' ') /* the registry pads the name */
            c[--L] = 0;
        sb_printf(&o, "CPU: %s, %lu logical processors\n", c, (unsigned long)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
    }
    ms.dwLength = sizeof ms;
    if (GlobalMemoryStatusEx(&ms))
        sb_printf(&o, "RAM: %.1f GB total, %.1f GB free\n", ms.ullTotalPhys / 1073741824.0, ms.ullAvailPhys / 1073741824.0);
    { /* graphics adapters (names only) */
        DISPLAY_DEVICEW dd;
        char seen[600] = "";
        sb_puts(&o, "GPU:");
        for (DWORD i = 0; i < 16; i++)
        {
            char *u;
            memset(&dd, 0, sizeof dd);
            dd.cb = sizeof dd;
            if (!EnumDisplayDevicesW(NULL, i, &dd, 0))
                break;
            u = u_from(dd.DeviceString);
            if (*u && !strstr(seen, u) && strlen(seen) + strlen(u) + 2 < sizeof seen)
            {
                sb_printf(&o, "%s %s", seen[0] ? "," : "", u);
                strcat(seen, u);
                strcat(seen, "|");
            }
            free(u);
        }
        sb_printf(&o, "\nScreen: %d x %d (primary)\n", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    }
    if (GetLogicalDriveStringsW(255, drives))
    {
        sb_puts(&o, "Drives:");
        for (wchar_t *d = drives; *d; d += wcslen(d) + 1)
        {
            ULARGE_INTEGER fr, tot;
            UINT t = GetDriveTypeW(d);
            if (t != DRIVE_FIXED && t != DRIVE_REMOVABLE)
                continue;
            if (GetDiskFreeSpaceExW(d, &fr, &tot, NULL))
                sb_printf(&o, " %c: %.0f of %.0f GB free;", (char)d[0], fr.QuadPart / 1073741824.0, tot.QuadPart / 1073741824.0);
        }
        sb_putc(&o, '\n');
    }
    {
        wchar_t loc[LOCALE_NAME_MAX_LENGTH];
        DYNAMIC_TIME_ZONE_INFORMATION tz;
        char *ul, *ut;
        GetUserDefaultLocaleName(loc, LOCALE_NAME_MAX_LENGTH);
        GetDynamicTimeZoneInformation(&tz);
        ul = u_from(loc);
        ut = u_from(tz.TimeZoneKeyName);
        sb_printf(&o, "Locale: %s, time zone: %s, ANSI code page %u, console OEM code page %u\n", ul, ut, GetACP(), GetOEMCP());
        free(ul);
        free(ut);
    }
    { /* developer tools found on PATH (not run, so no versions) */
        static const wchar_t *tools[] = {L"git.exe", L"node.exe", L"npm.cmd", L"python.exe", L"py.exe", L"pwsh.exe", L"powershell.exe",
                                         L"cmake.exe", L"cl.exe", L"gcc.exe", L"clang.exe", L"java.exe", L"dotnet.exe", L"cargo.exe",
                                         L"go.exe", L"docker.exe", L"wsl.exe", NULL};
        int any = 0;
        sb_puts(&o, "On PATH:");
        for (int i = 0; tools[i]; i++)
        {
            wchar_t full[MAX_PATH];
            if (SearchPathW(NULL, tools[i], NULL, MAX_PATH, full, NULL))
            {
                char *u = u_from(full);
                sb_printf(&o, "%s %s", any++ ? "," : "", u);
                free(u);
            }
        }
        sb_puts(&o, any ? "\n" : " (none of the usual tools)\n");
    }
    sb_puts(&o, "Shell for run_command: cmd.exe");
    if (G.fsroot[0])
    {
        sb_puts(&o, "; file tools root: ");
        sb_puts(&o, G.fsroot);
    }
    sb_putc(&o, '\n');
    return o.p;
}

/* ---------- update_plan: a checklist the model keeps for a multi-step task (shown in the chat) ---------- */
static char *plan_call(J *a)
{
    J *items = j_get(a, "items");
    int n = 0, done = 0;
    const char *now = NULL;
    SB o = {0};
    if (!items || items->t != J_ARR)
        return xstrdup("Error: 'items' must be an array of {text, status}");
    for (J *e = items->child; e; e = e->next)
    {
        const char *st = j_str(j_get(e, "status")), *tx = j_str(j_get(e, "text"));
        if (!tx || !*tx)
            continue;
        n++;
        if (st && !strcmp(st, "done"))
            done++;
        else if (st && !strcmp(st, "in_progress") && !now)
            now = tx;
    }
    if (!n)
        return xstrdup("Error: the plan has no items");
    sb_printf(&o, "Plan saved: %d of %d done.", done, n);
    if (now)
    {
        sb_puts(&o, " Now: ");
        sb_add(&o, now, tx_cut(now, strlen(now), 200));
        sb_putc(&o, '.');
    }
    sb_puts(&o, done < n ? " Update it as steps are finished." : " All steps are done.");
    return o.p;
}

/* ---------- ask_user: a question with optional choices, answered in a dialog ----------
   The tool runs on the runner thread; the dialog must run on the UI thread, so the request is sent there. */
typedef struct
{
    const char *question;
    const char *opts[8];
    int nopt;
    char *answer; /* malloc'ed; NULL = no answer */
} AskReq;
#define WM_APP_ASK (WM_APP + 3)
static char *ask_text(const wchar_t *title, const wchar_t *okLabel, const char *init); /* actions.h */
/* UI thread */
static void ask_user_ui(AskReq *r)
{
    HMODULE cc = GetModuleHandleW(L"comctl32.dll");
    PFN_TDI fn = cc ? (PFN_TDI)GetProcAddress(cc, "TaskDialogIndirect") : NULL;
    TASKDIALOG_BUTTON btn[9];
    wchar_t *labels[9];
    wchar_t *wq = w_from(r->question);
    int nb = 0, pressed = 0;
    r->answer = NULL;
    for (int i = 0; i < r->nopt; i++)
    {
        labels[nb] = w_from(r->opts[i]);
        btn[nb].nButtonID = 3000 + i;
        btn[nb].pszButtonText = labels[nb];
        nb++;
    }
    labels[nb] = w_from("Write my own answer...");
    btn[nb].nButtonID = 2999;
    btn[nb].pszButtonText = labels[nb];
    nb++;
    if (fn)
    {
        TASKDIALOGCONFIG tc;
        memset(&tc, 0, sizeof tc);
        tc.cbSize = sizeof tc;
        tc.hwndParent = hMain;
        tc.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
        tc.pszWindowTitle = L"llamac - the model asks";
        tc.pszMainInstruction = L"The model has a question";
        tc.pszContent = wq;
        tc.cButtons = (UINT)nb;
        tc.pButtons = btn;
        tc.nDefaultButton = r->nopt ? 3000 : 2999;
        tc.pszMainIcon = MAKEINTRESOURCEW(-3);
        if (FAILED(fn(&tc, &pressed, NULL, NULL)))
            pressed = 2999;
    }
    else
        pressed = 2999;
    if (pressed >= 3000 && pressed < 3000 + r->nopt)
        r->answer = xstrdup(r->opts[pressed - 3000]);
    else if (pressed == 2999)
    {
        SB t = {0};
        wchar_t title[200];
        _snwprintf(title, 200, L"Your answer to: %.150ls", wq);
        title[199] = 0;
        r->answer = ask_text(title, L"Send answer", sbs(&t));
        if (r->answer && !*r->answer)
        {
            free(r->answer);
            r->answer = NULL;
        }
    }
    for (int i = 0; i < nb; i++)
        free(labels[i]);
    free(wq);
}
static char *ask_call(J *a)
{
    AskReq r;
    J *op = j_get(a, "options");
    SB o = {0};
    memset(&r, 0, sizeof r);
    r.question = j_str(j_get(a, "question"));
    if (!r.question || !*r.question)
        return xstrdup("Error: 'question' is required");
    for (J *e = op && op->t == J_ARR ? op->child : NULL; e && r.nopt < 8; e = e->next)
        if (j_str(e) && *j_str(e))
            r.opts[r.nopt++] = j_str(e);
    SendMessageW(hMain, WM_APP_ASK, 0, (LPARAM)&r); /* blocks until the dialog is closed */
    if (!r.answer)
        return xstrdup("The user closed the question without answering. Continue with your best judgment and say what you assumed.");
    sb_puts(&o, "The user answered: ");
    sb_puts(&o, r.answer);
    free(r.answer);
    return o.p;
}

/* ---------- screenshot: the screen or one window, as an image for a vision model ----------
   The result starts with "\x01IMAGE <data url>\n": the runner keeps the image out of the text cap and the chat attaches
   it to the tool result; requests send it as a user message right after the tool results. */
/* The user approved one window by the text in its title. Nothing else is revealed: when no title matches, the
   titles of the other open windows are NOT listed to the model (they can name documents, sites, customers). */
typedef struct
{
    const char *want;
    HWND found;
} ShotFind;
static BOOL CALLBACK shot_enum(HWND h, LPARAM l)
{
    ShotFind *f = (ShotFind *)l;
    wchar_t t[256];
    char *u;
    if (f->found || !IsWindowVisible(h) || IsIconic(h) || GetWindow(h, GW_OWNER) || !GetWindowTextW(h, t, 256))
        return TRUE;
    u = u_from(t);
    if (StrStrIA(u, f->want))
        f->found = h; /* llamac's own window may match too: useful for questions about this app */
    free(u);
    return TRUE;
}
static char *shot_call(J *a)
{
    const char *want = j_str(j_get(a, "window"));
    int maxw = (int)j_num(j_get(a, "max_width"), 1280), w, h, ow, oh;
    HWND target = NULL;
    HDC sdc, mdc, sdc2;
    HBITMAP bm, scaled = NULL;
    char *png, what[300];
    SB o = {0};
    if (!g_vision)
        return xstrdup("Error: the loaded model cannot see images (the server reports no vision support), so a screenshot would be useless.");
    if (maxw < 320)
        maxw = 320;
    if (maxw > 2560)
        maxw = 2560;
    if (want && *want)
    {
        ShotFind f;
        memset(&f, 0, sizeof f);
        f.want = want;
        EnumWindows(shot_enum, (LPARAM)&f);
        if (!f.found)
            return xstrdup("Error: no visible window title contains that text. Ask the user for the exact window title; do not try other windows or the whole screen on your own.");
        target = f.found;
    }
    sdc = GetDC(NULL);
    mdc = CreateCompatibleDC(sdc);
    if (target)
    {
        RECT r;
        wchar_t t[200];
        char *u;
        GetWindowRect(target, &r);
        ow = r.right - r.left;
        oh = r.bottom - r.top;
        bm = CreateCompatibleBitmap(sdc, ow, oh);
        SelectObject(mdc, bm);
        if (!PrintWindow(target, mdc, 2 /* PW_RENDERFULLCONTENT */))
            BitBlt(mdc, 0, 0, ow, oh, sdc, r.left, r.top, SRCCOPY);
        GetWindowTextW(target, t, 200);
        u = u_from(t);
        snprintf(what, sizeof what, "the window \"%.200s\"", u);
        free(u);
    }
    else
    {
        ow = GetSystemMetrics(SM_CXSCREEN);
        oh = GetSystemMetrics(SM_CYSCREEN);
        bm = CreateCompatibleBitmap(sdc, ow, oh);
        SelectObject(mdc, bm);
        BitBlt(mdc, 0, 0, ow, oh, sdc, 0, 0, SRCCOPY | CAPTUREBLT);
        snprintf(what, sizeof what, "the primary screen");
    }
    w = ow;
    h = oh;
    if (ow > maxw)
    { /* smaller image, fewer image tokens */
        w = maxw;
        h = (int)((long long)oh * maxw / ow);
        sdc2 = CreateCompatibleDC(sdc);
        scaled = CreateCompatibleBitmap(sdc, w, h);
        SelectObject(sdc2, scaled);
        SetStretchBltMode(sdc2, HALFTONE);
        SetBrushOrgEx(sdc2, 0, 0, NULL);
        StretchBlt(sdc2, 0, 0, w, h, mdc, 0, 0, ow, oh, SRCCOPY);
        DeleteDC(sdc2);
    }
    DeleteDC(mdc);
    ReleaseDC(NULL, sdc);
    png = hbm_png(scaled ? scaled : bm);
    if (scaled)
        DeleteObject(scaled);
    DeleteObject(bm);
    if (!png)
        return xstrdup("Error: the screenshot could not be encoded");
    sb_puts(&o, "\x01IMAGE ");
    sb_puts(&o, png);
    sb_printf(&o, "\nScreenshot of %s, %d x %d pixels; the image follows.", what, w, h);
    free(png);
    return o.p;
}

static void extra_register(void)
{
    if (!G.tools_on)
        return;
    tool_add("system_info", "Windows version, CPU, RAM, GPU, drives, locale and the developer tools on PATH. Use before writing commands or diagnosing environment problems.",
             "{\"type\":\"object\",\"properties\":{}}", 0, 0, "system_info", 1, 0);
    tool_add("ask_user", "Ask the user a question and wait for the answer, with optional choices. Use when the task is ambiguous instead of guessing; not for small talk.",
             "{\"type\":\"object\",\"properties\":{\"question\":{\"type\":\"string\"},\"options\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},\"description\":\"Up to 8 short answers to choose from (optional)\"}},\"required\":[\"question\"]}", 0, 0, "ask_user", 1, 0);
    tool_add("update_plan", "Keep a checklist for a task with several steps: send the whole list each time, marking steps done as you finish them.",
             "{\"type\":\"object\",\"properties\":{\"items\":{\"type\":\"array\",\"items\":{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\"},\"status\":{\"type\":\"string\",\"enum\":[\"pending\",\"in_progress\",\"done\"]}},\"required\":[\"text\",\"status\"]}}},\"required\":[\"items\"]}", 0, 0, "update_plan", 1, 0);
    if (G.shot)
        tool_add("screenshot", "Take a screenshot of the screen or of one window (by title) to see it. Asks the user every time.",
                 "{\"type\":\"object\",\"properties\":{\"window\":{\"type\":\"string\",\"description\":\"Part of the window title; empty = the whole primary screen\"},\"max_width\":{\"type\":\"integer\",\"description\":\"Default 1280\"}}}", 0, 0, "screenshot", 0, 0);
}

#endif /* LLAMAC_TOOLS_EXTRA_H */
