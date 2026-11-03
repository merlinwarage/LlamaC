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
static char *mcp_call_tool(int srv, const char *rname, J *args); /* mcp.h */
static char *mem_call(const char *tool, J *args);
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

/* "tools" array for the chat completion request (NULL = send no tools) */
static char *tools_schema_build(void)
{
    if (!G.tools_on)
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
        if (i)
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
    if (G.autoappr || t->ro || t->allow)
        return 1;
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
    if (t.kind == 0 && !strncmp(t.rname, "memory_", 7))
        res = mem_call(t.rname, a);
    else
        res = t.kind == 0 ? fs_call(t.rname, a) : mcp_call_tool(t.srv, t.rname, a);
    if (!res)
        res = xstrdup("Error: unknown tool");
    j_free(a);
    return res ? res : xstrdup("");
}

/* ---------- runner thread: executes the calls of one assistant message, posts results to the UI thread ---------- */
typedef struct
{
    char *calls;
} TRun;
static DWORD WINAPI tool_runner(LPVOID arg)
{
    TRun *r = (TRun *)arg;
    J *arr = json_parse(r->calls);
    for (J *c = arr ? arr->child : NULL; c && !g_stop; c = c->next)
    {
        const char *id = j_str(j_get(c, "id")), *nm = j_str(j_get(j_get(c, "function"), "name")), *ar = j_str(j_get(j_get(c, "function"), "arguments"));
        char st[200], *res;
        SB m = {0};
        if (!nm)
            nm = "";
        snprintf(st, sizeof st, "Running tool: %.150s ...", nm);
        post_msg(9, st);
        res = tool_execute(nm, ar ? ar : "{}");
        if (strlen(res) > TOOL_MAX_RESULT)
        {
            size_t n = TOOL_MAX_RESULT;
            while (n && ((unsigned char)res[n] & 0xC0) == 0x80)
                n--;
            res[n] = 0;
            res = (char *)realloc(res, n + 64);
            strcat(res, "\n[output truncated]");
        }
        utf8_sanitize(res);
        sb_puts(&m, id ? id : "");
        sb_putc(&m, '\n');
        sb_puts(&m, nm);
        sb_putc(&m, '\n');
        sb_puts(&m, res);
        post_msg(6, sbs(&m));
        free(m.p);
        free(res);
    }
    j_free(arr);
    free(r->calls);
    free(r);
    post_msg(8, "");
    return 0;
}
static void tool_run_start(const char *calls)
{
    TRun *r = (TRun *)calloc(1, sizeof *r);
    r->calls = xstrdup(calls);
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

static void tools_apply_settings(void)
{
    fs_register();
    mem_register();
    if (G.tools_on && !g_mcp_started)
        mcp_start_async();
}
static void tools_startup(void)
{
    fs_register();
    mem_register();
    if (G.tools_on)
        mcp_start_async();
}

#endif /* LLAMAC_TOOLS_H */
