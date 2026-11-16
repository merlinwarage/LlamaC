/* tools.h - Tool registry, approval dialog, executor and the tool-runner thread */
#ifndef LLAMAC_TOOLS_H
#define LLAMAC_TOOLS_H

/* kind: 0 built-in file tool, 1 MCP tool.  ro: read-only built-in (runs without asking).  allow: approved without asking. */
typedef struct
{
    char name[72];
    char *desc, *schema;
    int kind, srv, ro, allow;
    char rname[128];
} Tool;
static Tool *g_tools;
static int g_ntools, g_captools;
static CRITICAL_SECTION g_tl;
static int g_tl_init;
#define TOOL_MAX_RESULT 60000

static char *fs_call(const char *tool, J *args);                 /* tools_fs.h */
static char *fs_preview(const char *tool, J *args);              /* tools_fs.h: diff for the approval dialog */
static char *fs_run_cmd(const char *cmd, const char *cwdrel, int tmo, int need_shell); /* tools_fs.h */
static char *guide_call(J *args);                                /* below */
static char *sysinfo_call(void);                                 /* tools_extra.h */
static char *plan_call(J *a);
static char *ask_call(J *a);
static char *shot_call(J *a);
static void extra_register(void);
static char *mcp_call_tool(int srv, const char *rname, J *args); /* mcp.h */
static char *mem_call(const char *tool, J *args);
static char *date_call(void);      /* below */
static void mem_register(void);    /* memory.h */
static void fs_register(void);     /* tools_fs.h */
static void mcp_start_async(void); /* mcp.h */
static int g_mcp_started;

static void tl_enter(void)
{
    if (!g_tl_init)
    {
        InitializeCriticalSection(&g_tl);
        g_tl_init = 1;
    }
    EnterCriticalSection(&g_tl);
}
static void tl_leave(void) { LeaveCriticalSection(&g_tl); }

static void tool_add(const char *name, const char *desc, const char *schema, int kind, int srv, const char *rname, int ro, int allow)
{
    tl_enter();
    for (int i = 0; i < g_ntools; i++)
        if (!strcmp(g_tools[i].name, name))
        {
            tl_leave();
            return;
        }
    if (g_ntools == g_captools)
    {
        g_captools = g_captools ? g_captools * 2 : 32;
        g_tools = (Tool *)realloc(g_tools, (size_t)g_captools * sizeof(Tool));
    }
    Tool *t = &g_tools[g_ntools++];
    memset(t, 0, sizeof *t);
    strncpy(t->name, name, sizeof t->name - 1);
    t->desc = xstrdup(desc ? desc : "");
    t->schema = xstrdup(schema && *schema ? schema : "{\"type\":\"object\",\"properties\":{}}");
    t->kind = kind;
    t->srv = srv;
    t->ro = ro;
    t->allow = allow;
    strncpy(t->rname, rname ? rname : name, sizeof t->rname - 1);
    tl_leave();
}
/* srv < 0: remove every tool of this kind; otherwise only tools of that MCP server */
static void tools_clear(int kind, int srv)
{
    tl_enter();
    int k = 0;
    for (int i = 0; i < g_ntools; i++)
    {
        if (g_tools[i].kind == kind && (srv < 0 || g_tools[i].srv == srv))
        {
            free(g_tools[i].desc);
            free(g_tools[i].schema);
        }
        else
            g_tools[k++] = g_tools[i];
    }
    g_ntools = k;
    tl_leave();
}
static int tools_count(int kind)
{
    int n = 0;
    tl_enter();
    for (int i = 0; i < g_ntools; i++)
        if (g_tools[i].kind == kind)
            n++;
    tl_leave();
    return n;
}

/* name lists "|a|b|" (built-in tools switched off by the user) */
static int name_in_list(const char *list, const char *name)
{
    char k[96];
    snprintf(k, sizeof k, "|%s|", name);
    return strstr(list, k) != NULL;
}
static void name_list_set(char *list, size_t cap, const char *name, int in)
{
    char k[96], *at;
    snprintf(k, sizeof k, "|%s|", name);
    while ((at = strstr(list, k)) != NULL) /* remove, keeping the leading '|' */
        memmove(at, at + strlen(k) - 1, strlen(at + strlen(k) - 1) + 1);
    if (!strcmp(list, "|"))
        list[0] = 0;
    if (in && strlen(list) + strlen(k) < cap)
    {
        if (!list[0])
            strcpy(list, k);
        else
            strcat(list, k + 1);
    }
}
/* Tool set 1 ("chat"): no file or shell tools - fewer, cheaper tool definitions and fewer wrong tool choices for
   small models. MCP tools stay, since the user configured them on purpose. Built-in tools the user switched off in
   the Tools dialog are never sent. */
static int tool_in_set(const Tool *t)
{
    if (G.toolset == 2)
        return 0;
    if (t->kind == 0 && name_in_list(G.tooloff, t->name))
        return 0;
    if (G.toolset == 1 && t->kind == 0)
        return !strcmp(t->rname, "get_datetime") || !strncmp(t->rname, "memory_", 7) || !strcmp(t->rname, "read_guide") || !strcmp(t->rname, "ask_user");
    return 1;
}
/* "tools" array for the chat completion request (NULL = send no tools) */
static char *tools_schema_build(void)
{
    int any = 0;
    if (!G.tools_on || G.toolset == 2)
        return NULL;
    tl_enter();
    if (!g_ntools)
    {
        tl_leave();
        return NULL;
    }
    SB s = {0};
    sb_putc(&s, '[');
    for (int i = 0; i < g_ntools; i++)
    {
        Tool *t = &g_tools[i];
        if (!tool_in_set(t))
            continue;
        if (any++)
            sb_putc(&s, ',');
        sb_puts(&s, "{\"type\":\"function\",\"function\":{\"name\":");
        sb_json(&s, t->name);
        sb_puts(&s, ",\"description\":");
        sb_json(&s, t->desc);
        sb_puts(&s, ",\"parameters\":");
        sb_puts(&s, t->schema);
        sb_puts(&s, "}}");
    }
    sb_putc(&s, ']');
    tl_leave();
    if (!any)
    {
        free(s.p);
        return NULL;
    }
    return s.p;
}

/* ---------- approval ---------- */
typedef HRESULT(WINAPI *PFN_TDI)(const TASKDIALOGCONFIG *, int *, int *, BOOL *);
static int ask_approval(const Tool *t, const char *args, int *always)
{
    SB c = {0};
    size_t al = strlen(args);
    int allow = 0, done = 0;
    *always = 0;
    sb_puts(&c, "Tool: ");
    sb_puts(&c, t->name);
    if (t->kind)
        sb_puts(&c, "   (MCP server)");
    else if (!strcmp(t->rname, "run_command"))
        sb_puts(&c, "   (RUNS A SHELL COMMAND AS YOU - not sandboxed)");
    else
        sb_puts(&c, "   (file access)");
    if (!t->kind && !strcmp(t->rname, "run_command"))
    {
        J *aj = json_parse(args);
        const char *cm = j_str(j_get(aj, "command")), *cw = j_str(j_get(aj, "cwd"));
        sb_puts(&c, "\n\nCommand:\n");
        sb_puts(&c, cm ? cm : "(missing)");
        sb_puts(&c, "\n\nStarts in (relative to the file-tools root): ");
        sb_puts(&c, cw && *cw ? cw : ".");
        j_free(aj);
    }
    else if (!t->kind && !strcmp(t->rname, "screenshot"))
    {
        J *aj = json_parse(args);
        const char *wn = j_str(j_get(aj, "window"));
        sb_puts(&c, "\n\nThe model wants to see ");
        if (wn && *wn)
        {
            sb_puts(&c, "the window whose title contains \"");
            sb_puts(&c, wn);
            sb_puts(&c, "\".");
        }
        else
            sb_puts(&c, "your whole primary screen.");
        sb_puts(&c, "\n\nThe image is sent to the model and saved in this chat. Check that nothing confidential is visible.");
        j_free(aj);
    }
    else if (!t->kind && (!strcmp(t->rname, "edit_file") || !strcmp(t->rname, "write_file") || !strcmp(t->rname, "delete_file") || !strcmp(t->rname, "multi_edit")))
    { /* show what would change instead of raw JSON */
        J *aj = json_parse(args);
        const char *pth = j_str(j_get(aj, "path"));
        char *pv = aj ? fs_preview(t->rname, aj) : NULL;
        sb_puts(&c, "\n\nFile: ");
        sb_puts(&c, pth ? pth : "(missing)");
        sb_puts(&c, "\n\n");
        sb_puts(&c, pv ? pv : "(no preview)");
        free(pv);
        j_free(aj);
    }
    else
    {
        sb_puts(&c, "\n\nArguments:\n");
        if (al > 1200)
        {
            al = 1200;
            while (al && ((unsigned char)args[al] & 0xC0) == 0x80)
                al--;
        }
        sb_add(&c, args, al);
        if (al < strlen(args))
            sb_puts(&c, "\n...");
    }
    wchar_t *wc = w_from(sbs(&c));
    free(c.p);
    HMODULE cc = GetModuleHandleW(L"comctl32.dll");
    PFN_TDI fn = cc ? (PFN_TDI)GetProcAddress(cc, "TaskDialogIndirect") : NULL;
    if (fn)
    {
        TASKDIALOG_BUTTON btn[3] = {{1001, L"Allow once"}, {1002, L"Always allow this tool (until restart)"}, {1003, L"Deny"}};
        TASKDIALOGCONFIG tc;
        int pressed = 0;
        memset(&tc, 0, sizeof tc);
        tc.cbSize = sizeof tc;
        tc.hwndParent = hMain;
        tc.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
        tc.pszWindowTitle = L"llamac - tool call";
        tc.pszMainInstruction = L"The model wants to run a tool";
        tc.pszContent = wc;
        tc.cButtons = 3;
        tc.pButtons = btn;
        tc.nDefaultButton = 1003;
        tc.pszMainIcon = MAKEINTRESOURCEW(-3);
        if (SUCCEEDED(fn(&tc, &pressed, NULL, NULL)))
        {
            done = 1;
            if (pressed == 1001)
                allow = 1;
            else if (pressed == 1002)
            {
                allow = 1;
                *always = 1;
            }
        }
    }
    if (!done)
        allow = (MessageBoxW(hMain, wc, L"llamac - allow this tool call?", MB_YESNO | MB_ICONQUESTION | MB_TOPMOST) == IDYES);
    free(wc);
    return allow;
}
static int tool_allowed(const Tool *t, const char *args)
{
    int always = 0;
    if (t->allow || ((G.autoappr || t->ro) && !(t->kind == 0 && !strcmp(t->rname, "screenshot"))))
        return 1; /* a screenshot asks even with auto-approve: the screen can show anything */
    if (!ask_approval(t, args, &always))
        return 0;
    if (always)
    {
        tl_enter();
        for (int i = 0; i < g_ntools; i++)
            if (!strcmp(g_tools[i].name, t->name))
                g_tools[i].allow = 1;
        tl_leave();
    }
    return 1;
}

/* ---------- executor ---------- */
static char *tool_execute(const char *name, const char *args)
{
    Tool t;
    int found = 0;
    J *a;
    char *res;
    tl_enter();
    for (int i = 0; i < g_ntools; i++)
        if (!strcmp(g_tools[i].name, name))
        {
            t = g_tools[i];
            found = 1;
            break;
        }
    tl_leave();
    if (!found)
        return xstrdup("Error: unknown tool");
    if (!G.tools_on)
        return xstrdup("Error: tools are disabled in Settings");
    if (t.kind == 0 && name_in_list(G.tooloff, t.name))
        return xstrdup("Error: the user has switched this tool off. Do not call it again; continue without it.");
    if (!tool_allowed(&t, args))
        return xstrdup("The user denied this tool call.");
    a = (args && *args) ? json_parse_strict(args) : NULL;
    if (a && a->t != J_OBJ)
    {
        j_free(a);
        a = NULL;
    }
    if (!a && args && *args && strcmp(args, "{}"))
    {
        wchar_t debug_path[MAX_PATH];
        ms_note_tool(t.rname, NULL, 1);
        size_t n = strlen(args);
        char message[400];

        data_path(L"last_invalid_tool_args.json", debug_path);

        if (write_file(debug_path, args, n))
        {
            snprintf(message, sizeof message,
                     "Error: tool arguments failed JSON validation (%llu bytes). "
                     "No file was changed. Raw arguments saved to "
                     "last_invalid_tool_args.json in the app data folder.",
                     (unsigned long long)n);
        }
        else
        {
            snprintf(message, sizeof message,
                     "Error: tool arguments failed JSON validation (%llu bytes). "
                     "No file was changed. Could not save the diagnostic file.",
                     (unsigned long long)n);
        }

        return xstrdup(message);
    }
    if (t.kind == 0 && !strcmp(t.rname, "get_datetime"))
        res = date_call();
    else if (t.kind == 0 && !strncmp(t.rname, "memory_", 7))
        res = mem_call(t.rname, a);
    else if (t.kind == 0 && !strcmp(t.rname, "read_guide"))
        res = guide_call(a);
    else if (t.kind == 0 && !strcmp(t.rname, "system_info"))
        res = sysinfo_call();
    else if (t.kind == 0 && !strcmp(t.rname, "update_plan"))
        res = plan_call(a);
    else if (t.kind == 0 && !strcmp(t.rname, "ask_user"))
        res = ask_call(a);
    else if (t.kind == 0 && !strcmp(t.rname, "screenshot"))
        res = shot_call(a);
    else
        res = t.kind == 0 ? fs_call(t.rname, a) : mcp_call_tool(t.srv, t.rname, a);
    if (!res)
        res = xstrdup("Error: unknown tool");
    ms_note_tool(t.rname, res, 0);
    j_free(a);
    return res ? res : xstrdup("");
}
static int tool_is_ro(const char *name)
{
    int ro = 0;
    tl_enter();
    for (int i = 0; i < g_ntools; i++)
        if (!strcmp(g_tools[i].name, name))
            ro = g_tools[i].kind == 0 && g_tools[i].ro;
    tl_leave();
    return ro;
}
static int tool_changes_files(const char *name)
{
    static const char *w[] = {"write_file", "edit_file", "multi_edit", "move_path", "delete_file", "create_directory", NULL};
    for (int i = 0; w[i]; i++)
        if (!strcmp(name, w[i]))
            return 1;
    return 0;
}

/* ---------- runner thread: executes the calls of one assistant message, posts results to the UI thread ---------- */
typedef struct
{
    char *calls;
    size_t cap;    /* most characters one result may have (sized to the free context) */
    char conv[40]; /* chat id, for file checkpoints */
} TRun;
typedef struct
{
    const char *nm, *ar;
    char *res;
} TPar;
static DWORD WINAPI tool_par_thread(LPVOID p)
{
    TPar *t = (TPar *)p;
    t->res = tool_execute(t->nm, t->ar);
    return 0;
}
/* keeps a result within cap: command output keeps its end (errors are there), everything else its beginning */
static char *tool_cap(char *res, size_t cap, const char *name)
{
    size_t n = strlen(res);
    SB o = {0};
    if (n <= cap)
        return res;
    if (!strcmp(name, "run_command"))
    {
        size_t h = tx_cut(res, n, cap / 3), t = n - (cap - h);
        while (t < n && ((unsigned char)res[t] & 0xC0) == 0x80)
            t++;
        sb_add(&o, res, h);
        sb_printf(&o, "\n[... %llu characters omitted to fit the context ...]\n", (unsigned long long)(t - h));
        sb_puts(&o, res + t);
    }
    else
    {
        sb_add(&o, res, tx_cut(res, n, cap));
        sb_printf(&o, "\n[output cut at %llu of %llu characters to fit the free context; request a smaller part (offset/limit, a narrower pattern or path)]",
                  (unsigned long long)cap, (unsigned long long)n);
    }
    free(res);
    return o.p;
}
static DWORD WINAPI tool_runner(LPVOID arg)
{
    TRun *r = (TRun *)arg;
    J *arr = json_parse(r->calls);
    int n = 0, allro = 1, lastw = -1;
    const char **ids, **nms, **ars;
    char **res;
    for (J *c = arr ? arr->child : NULL; c; c = c->next)
        n++;
    ids = (const char **)calloc((size_t)n + 1, sizeof(char *));
    nms = (const char **)calloc((size_t)n + 1, sizeof(char *));
    ars = (const char **)calloc((size_t)n + 1, sizeof(char *));
    res = (char **)calloc((size_t)n + 1, sizeof(char *));
    {
        int i = 0;
        for (J *c = arr ? arr->child : NULL; c; c = c->next, i++)
        {
            ids[i] = j_str(j_get(c, "id"));
            nms[i] = j_str(j_get(j_get(c, "function"), "name"));
            ars[i] = j_str(j_get(j_get(c, "function"), "arguments"));
            if (!nms[i])
                nms[i] = "";
            if (!ars[i])
                ars[i] = "{}";
            if (!tool_is_ro(nms[i]))
                allro = 0;
        }
    }
    if (n > 1 && allro)
    { /* read-only calls do not depend on each other and never ask: run them at the same time */
        TPar *tp = (TPar *)calloc((size_t)n, sizeof(TPar));
        HANDLE *th = (HANDLE *)calloc((size_t)n, sizeof(HANDLE));
        char st[120];
        snprintf(st, sizeof st, "Running %d read-only tools in parallel ...", n);
        post_msg(9, st);
        for (int i = 0; i < n; i++)
        {
            tp[i].nm = nms[i];
            tp[i].ar = ars[i];
            th[i] = CreateThread(NULL, 0, tool_par_thread, &tp[i], 0, NULL);
            if (!th[i])
                tool_par_thread(&tp[i]);
        }
        for (int i = 0; i < n; i++)
            if (th[i])
            {
                WaitForSingleObject(th[i], INFINITE);
                CloseHandle(th[i]);
            }
        for (int i = 0; i < n; i++)
            res[i] = tp[i].res;
        free(tp);
        free(th);
    }
    else
    {
        snprintf(g_cp_conv, sizeof g_cp_conv, "%s", r->conv);
        for (int i = 0; i < n && !g_stop; i++)
        {
            char st[200];
            snprintf(st, sizeof st, "Running tool: %.150s ...", nms[i]);
            post_msg(9, st);
            snprintf(g_cp_tcid, sizeof g_cp_tcid, "%s", ids[i] ? ids[i] : "");
            res[i] = tool_execute(nms[i], ars[i]);
            g_cp_tcid[0] = 0;
            if (tool_changes_files(nms[i]) && strncmp(res[i], "Error", 5) && strncmp(res[i], "The user denied", 15))
                lastw = i;
        }
        g_cp_conv[0] = 0;
    }
    if (lastw >= 0 && G.verify[0] && G.fsroot[0] && !g_stop)
    { /* the user's verify command (build, tests, lint) checks the change; its output goes back to the model */
        char *out, st[300];
        SB m = {0};
        snprintf(st, sizeof st, "Verifying: %.250s ...", G.verify);
        post_msg(9, st);
        out = fs_run_cmd(G.verify, NULL, 300, 0);
        sb_puts(&m, res[lastw]);
        sb_puts(&m, "\n\n[Automatic check after this change - ");
        sb_puts(&m, G.verify);
        sb_puts(&m, "]\n");
        {
            size_t L = strlen(out);
            if (L > 6000)
            { /* errors are at the end */
                size_t h = tx_cut(out, L, 1500), t = L - 4500;
                while (t < L && ((unsigned char)out[t] & 0xC0) == 0x80)
                    t++;
                sb_add(&m, out, h);
                sb_puts(&m, "\n[...]\n");
                sb_puts(&m, out + t);
            }
            else
                sb_puts(&m, out);
        }
        free(out);
        free(res[lastw]);
        res[lastw] = m.p;
    }
    for (int i = 0; i < n; i++)
    {
        SB m = {0};
        if (!res[i])
            break; /* stopped before this call ran */
        char *img = NULL;
        if (!strncmp(res[i], "\x01IMAGE ", 7) && strchr(res[i], '\n'))
        { /* an image result: the image is kept apart, only the text is capped */
            char *nl = strchr(res[i], '\n'), *tx = xstrdup(nl + 1);
            img = xstrndup(res[i], (size_t)(nl - res[i]));
            free(res[i]);
            res[i] = tx;
        }
        res[i] = tool_cap(res[i], r->cap, nms[i]);
        utf8_sanitize(res[i]);
        sb_puts(&m, ids[i] ? ids[i] : "");
        sb_putc(&m, '\n');
        sb_puts(&m, nms[i]);
        sb_putc(&m, '\n');
        if (img)
        {
            sb_puts(&m, img);
            sb_putc(&m, '\n');
            free(img);
        }
        sb_puts(&m, res[i]);
        post_msg(6, sbs(&m));
        free(m.p);
        free(res[i]);
    }
    free((void *)ids);
    free((void *)nms);
    free((void *)ars);
    free(res);
    j_free(arr);
    free(r->calls);
    free(r);
    post_msg(8, "");
    return 0;
}
/* cap: most characters per result; conv_id: chat the calls belong to (file checkpoints) */
static void tool_run_start(const char *calls, size_t cap, const char *conv_id)
{
    TRun *r = (TRun *)calloc(1, sizeof *r);
    r->calls = xstrdup(calls);
    r->cap = cap > TOOL_MAX_RESULT ? TOOL_MAX_RESULT : cap < 4000 ? 4000 : cap;
    snprintf(r->conv, sizeof r->conv, "%s", conv_id ? conv_id : "");
    HANDLE h = CreateThread(NULL, 0, tool_runner, r, 0, NULL);
    if (h)
        CloseHandle(h);
    else
    {
        free(r->calls);
        free(r);
        post_msg(8, "");
    }
}

/* ---------- get_datetime: current local date/time (models cannot know it; read-only, runs without asking) ---------- */
static char *date_call(void)
{
    static const char *wd[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    SYSTEMTIME lt, ut;
    TIME_ZONE_INFORMATION tz;
    DWORD r = GetTimeZoneInformation(&tz);
    LONG bias = tz.Bias + (r == TIME_ZONE_ID_DAYLIGHT ? tz.DaylightBias : tz.StandardBias); /* minutes, UTC = local + bias */
    char out[300], sign = bias <= 0 ? '+' : '-';
    GetLocalTime(&lt);
    GetSystemTime(&ut);
    if (bias < 0)
        bias = -bias;
    snprintf(out, sizeof out, "Local date: %04d-%02d-%02d (%s)\nLocal time: %02d:%02d:%02d\nUTC offset: %c%02ld:%02ld\nISO 8601: %04d-%02d-%02dT%02d:%02d:%02d%c%02ld:%02ld\nUTC: %04d-%02d-%02dT%02d:%02d:%02dZ",
             lt.wYear, lt.wMonth, lt.wDay, wd[lt.wDayOfWeek % 7], lt.wHour, lt.wMinute, lt.wSecond,
             sign, bias / 60, bias % 60,
             lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond, sign, bias / 60, bias % 60,
             ut.wYear, ut.wMonth, ut.wDay, ut.wHour, ut.wMinute, ut.wSecond);
    return xstrdup(out);
}
static void date_register(void)
{
    if (!G.tools_on)
        return;
    tool_add("get_datetime", "Get the current local date, weekday, time and UTC offset from the user's computer. Call it whenever the answer depends on today's date or the time; never guess the date.",
             "{\"type\":\"object\",\"properties\":{}}", 0, 0, "get_datetime", 1, 0);
}

/* ---------- guides: data\guides\*.md, read on demand ----------
   Long instructions (coding rules, style guides, project notes) cost tokens in every request when they sit in the
   system prompt. As guides only their names and one-line descriptions are sent; the model reads one when needed. */
static int guide_name_ok(const char *n)
{
    if (!n || !*n || strlen(n) > 60)
        return 0;
    for (const char *p = n; *p; p++)
        if (!(isalnum((unsigned char)*p) || *p == '-' || *p == '_' || *p == ' ' || *p == '.'))
            return 0;
    return strstr(n, "..") == NULL;
}
static char *guide_call(J *a)
{
    const char *nm = j_str(j_get(a, "name"));
    wchar_t rel[120], p[MAX_PATH];
    wchar_t *wn;
    size_t len;
    char *b;
    if (!guide_name_ok(nm))
        return xstrdup("Error: unknown guide name");
    wn = w_from(nm);
    _snwprintf(rel, 120, L"guides\\%ls.md", wn);
    rel[119] = 0;
    free(wn);
    data_path(rel, p);
    b = (char *)read_file(p, &len);
    if (!b)
        return xstrdup("Error: no guide with that name");
    return b;
}
static void guides_register(void)
{
    wchar_t pat[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    SB d = {0}, en = {0};
    int n = 0;
    if (!G.tools_on)
        return;
    data_path(L"guides\\*.md", pat);
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    sb_puts(&d, "Read one of the user's guides: longer instructions to follow for a kind of task. Read the matching guide before starting such a task. Available:");
    do
    {
        char *nm, *b, *dot;
        wchar_t p[MAX_PATH];
        size_t len;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        nm = u_from(fd.cFileName);
        dot = strrchr(nm, '.');
        if (dot)
            *dot = 0;
        if (!guide_name_ok(nm))
        {
            free(nm);
            continue;
        }
        {
            wchar_t rel[MAX_PATH];
            _snwprintf(rel, MAX_PATH, L"guides\\%ls", fd.cFileName);
            rel[MAX_PATH - 1] = 0;
            data_path(rel, p);
        }
        b = (char *)read_file(p, &len);
        sb_puts(&d, "\n- ");
        sb_puts(&d, nm);
        if (b)
        { /* first non-empty line, without heading marks, is the description */
            char *s = b, *e;
            while (*s == '\n' || *s == '\r' || *s == ' ' || *s == '#')
                s++;
            e = s;
            while (*e && *e != '\n' && *e != '\r')
                e++;
            if (e > s)
            {
                sb_puts(&d, ": ");
                sb_add(&d, s, tx_cut(s, (size_t)(e - s), 160));
            }
            free(b);
        }
        sb_puts(&en, n++ ? "," : "");
        sb_json(&en, nm);
        free(nm);
    } while (FindNextFileW(h, &fd) && n < 40);
    FindClose(h);
    if (n)
    {
        SB sc = {0};
        sb_puts(&sc, "{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\",\"enum\":[");
        sb_puts(&sc, sbs(&en));
        sb_puts(&sc, "]}},\"required\":[\"name\"]}");
        tool_add("read_guide", sbs(&d), sbs(&sc), 0, 0, "read_guide", 1, 0);
        free(sc.p);
    }
    free(d.p);
    free(en.p);
}

static void tools_apply_settings(void)
{
    fs_register();
    mem_register();
    date_register();
    guides_register();
    extra_register();
    if (G.tools_on && !g_mcp_started)
        mcp_start_async();
}
static void tools_startup(void)
{
    fs_register();
    mem_register();
    date_register();
    guides_register();
    extra_register();
    if (G.tools_on)
        mcp_start_async();
}

#endif /* LLAMAC_TOOLS_H */
