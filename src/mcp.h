/* mcp.h - MCP client core: server config (mcp.json), JSON-RPC, tool discovery and tool calls */
#ifndef LLAMAC_MCP_H
#define LLAMAC_MCP_H

#define MCP_MAX_SRV 32
typedef struct McpSrv
{
    char name[64];
    int kind; /* 0 = stdio, 1 = HTTP (Streamable HTTP) */
    int timeout_ms, auto_all;
    char *auto_list;       /* auto_list: "\nname1\nname2\n" */
    CRITICAL_SECTION lock; /* one request at a time per server */
    LONG next_id;
    char proto[24];
    /* stdio */
    HANDLE job, hproc, hin, hout, hrd, ev;
    CRITICAL_SECTION wlock;
    volatile LONG want_id, dead;
    char *volatile resp;
    wchar_t *cmdline, *cwd, *env;
    /* http */
    Url u;
    HINTERNET hses;
    char *hdrs;
    char sid[200];
} McpSrv;
static McpSrv *g_srv[MCP_MAX_SRV];
static volatile int g_nsrv;
static volatile LONG g_booting;

static char *mcp_stdio_exchange(McpSrv *s, long id, const char *msg, int timeout_ms, int cancel, char *err, size_t es);
static char *mcp_http_exchange(McpSrv *s, long id, const char *msg, int timeout_ms, int cancel, char *err, size_t es);
static int mcp_stdio_open(McpSrv *s, J *cfg, char *err, size_t es);
static int mcp_http_open(McpSrv *s, J *cfg, char *err, size_t es);
static void mcp_srv_stop(McpSrv *s);

/* one JSON-RPC request; returns the raw JSON-RPC response message (malloc) or NULL with err filled */
static char *mcp_rpc(McpSrv *s, const char *method, const char *params, int timeout_ms, int cancel, char *err, size_t es)
{
    SB m = {0};
    char *res;
    long id;
    EnterCriticalSection(&s->lock);
    id = (long)++s->next_id;
    err[0] = 0;
    sb_printf(&m, "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"method\":", id);
    sb_json(&m, method);
    if (params)
    {
        sb_puts(&m, ",\"params\":");
        sb_puts(&m, params);
    }
    sb_putc(&m, '}');
    res = s->kind == 0 ? mcp_stdio_exchange(s, id, sbs(&m), timeout_ms, cancel, err, es) : mcp_http_exchange(s, id, sbs(&m), timeout_ms, cancel, err, es);
    free(m.p);
    LeaveCriticalSection(&s->lock);
    return res;
}
static void mcp_notify(McpSrv *s, const char *method)
{
    SB m = {0};
    char err[128];
    char *r;
    EnterCriticalSection(&s->lock);
    sb_puts(&m, "{\"jsonrpc\":\"2.0\",\"method\":");
    sb_json(&m, method);
    sb_putc(&m, '}');
    r = s->kind == 0 ? mcp_stdio_exchange(s, 0, sbs(&m), 5000, 0, err, sizeof err) : mcp_http_exchange(s, 0, sbs(&m), 10000, 0, err, sizeof err);
    free(r);
    free(m.p);
    LeaveCriticalSection(&s->lock);
}
/* returns the response if it is a JSON-RPC response with this id (used for SSE / line parsing) */
static char *mcp_match_response(const char *json, long id)
{
    J *r = json_parse(json);
    char *out = NULL;
    if (r && r->t == J_OBJ && !j_get(r, "method"))
    {
        J *i = j_get(r, "id");
        if (i && i->t == J_NUM && (long)i->num == id)
            out = xstrdup(json);
    }
    j_free(r);
    return out;
}

static void mcp_sanitize_name(char *dst, size_t cap, const char *src)
{
    size_t n = 0;
    for (; *src && n + 1 < cap; src++)
        dst[n++] = (isalnum((unsigned char)*src) || *src == '_' || *src == '-') ? *src : '_';
    dst[n] = 0;
}
static int mcp_autoapproved(McpSrv *s, const char *tool)
{
    char key[200];
    if (s->auto_all)
        return 1;
    if (!s->auto_list)
        return 0;
    snprintf(key, sizeof key, "\n%s\n", tool);
    return strstr(s->auto_list, key) != NULL;
}

static int mcp_handshake(McpSrv *s, int idx, char *err, size_t es, int *ntools)
{
    char *raw = mcp_rpc(s, "initialize", "{\"protocolVersion\":\"2025-06-18\",\"capabilities\":{},\"clientInfo\":{\"name\":\"llamac\",\"version\":\"1.0\"}}", 60000, 0, err, es);
    J *r;
    char cursor[512] = "";
    if (!raw)
        return 0;
    r = json_parse(raw);
    free(raw);
    if (!r || j_get(r, "error"))
    {
        const char *m = r ? j_str(j_get(j_get(r, "error"), "message")) : NULL;
        snprintf(err, es, "initialize failed: %.200s", m ? m : "invalid response");
        j_free(r);
        return 0;
    }
    {
        const char *pv = j_str(j_get(j_get(r, "result"), "protocolVersion"));
        if (pv)
            strncpy(s->proto, pv, sizeof s->proto - 1);
    }
    j_free(r);
    mcp_notify(s, "notifications/initialized");
    for (int page = 0; page < 20; page++)
    {
        SB pr = {0};
        J *res, *arr;
        const char *nc;
        if (cursor[0])
        {
            sb_puts(&pr, "{\"cursor\":");
            sb_json(&pr, cursor);
            sb_putc(&pr, '}');
        }
        else
            sb_puts(&pr, "{}");
        raw = mcp_rpc(s, "tools/list", sbs(&pr), 30000, 0, err, es);
        free(pr.p);
        if (!raw)
            return 0;
        r = json_parse(raw);
        free(raw);
        if (!r || j_get(r, "error"))
        {
            const char *m = r ? j_str(j_get(j_get(r, "error"), "message")) : NULL;
            snprintf(err, es, "tools/list failed: %.200s", m ? m : "invalid response");
            j_free(r);
            return 0;
        }
        res = j_get(r, "result");
        arr = j_get(res, "tools");
        for (J *t = arr ? arr->child : NULL; t; t = t->next)
        {
            const char *nm = j_str(j_get(t, "name")), *ds = j_str(j_get(t, "description"));
            J *sc = j_get(t, "inputSchema");
            char sn[24], tn[80], full[72];
            SB sj = {0}, dsc = {0};
            if (!nm || !*nm)
                continue;
            mcp_sanitize_name(sn, sizeof sn, s->name);
            mcp_sanitize_name(tn, sizeof tn, nm);
            snprintf(full, sizeof full, "%.20s__%.40s", sn, tn);
            if (sc && sc->t == J_OBJ)
                j_dump(&sj, sc);
            sb_puts(&dsc, "[");
            sb_puts(&dsc, s->name);
            sb_puts(&dsc, "] ");
            sb_puts(&dsc, ds ? ds : "");
            tool_add(full, sbs(&dsc), sj.p ? sbs(&sj) : NULL, 1, idx, nm, 0, mcp_autoapproved(s, nm));
            free(sj.p);
            free(dsc.p);
            (*ntools)++;
        }
        nc = j_str(j_get(res, "nextCursor"));
        cursor[0] = 0;
        if (nc && *nc)
            strncpy(cursor, nc, sizeof cursor - 1);
        j_free(r);
        if (!cursor[0])
            break;
    }
    return 1;
}

static char *mcp_call_tool(int srv, const char *rname, J *args)
{
    McpSrv *s = (srv >= 0 && srv < g_nsrv) ? g_srv[srv] : NULL;
    char err[300] = "";
    SB pr = {0}, out = {0};
    char *raw;
    J *r, *res, *content;
    if (!s)
        return xstrdup("Error: MCP server is not available (try Settings > Reload MCP)");
    sb_puts(&pr, "{\"name\":");
    sb_json(&pr, rname);
    sb_puts(&pr, ",\"arguments\":");
    if (args)
        j_dump(&pr, args);
    else
        sb_puts(&pr, "{}");
    sb_putc(&pr, '}');
    raw = mcp_rpc(s, "tools/call", sbs(&pr), s->timeout_ms, 1, err, sizeof err);
    free(pr.p);
    if (!raw)
    {
        sb_puts(&out, "Error: MCP call failed: ");
        sb_puts(&out, err[0] ? err : "no response");
        return out.p;
    }
    r = json_parse(raw);
    free(raw);
    if (!r)
        return xstrdup("Error: invalid response from the MCP server");
    if (j_get(r, "error"))
    {
        J *e = j_get(r, "error");
        const char *m = j_str(j_get(e, "message"));
        char code[40];
        snprintf(code, sizeof code, "%d", (int)j_num(j_get(e, "code"), 0));
        sb_puts(&out, "Error: MCP error ");
        sb_puts(&out, code);
        sb_puts(&out, ": ");
        sb_puts(&out, m ? m : "");
        j_free(r);
        return out.p;
    }
    res = j_get(r, "result");
    content = j_get(res, "content");
    if (j_get(res, "isError") && j_get(res, "isError")->num != 0)
        sb_puts(&out, "Error: ");
    int first = 1;
    for (J *c = content ? content->child : NULL; c; c = c->next)
    {
        const char *ty = j_str(j_get(c, "type")), *tx = j_str(j_get(c, "text"));
        if (!first)
            sb_putc(&out, '\n');
        first = 0;
        if (ty && !strcmp(ty, "text") && tx)
            sb_puts(&out, tx);
        else if (ty && !strcmp(ty, "image"))
        {
            sb_puts(&out, "[image omitted: ");
            sb_puts(&out, j_str(j_get(c, "mimeType")) ? j_str(j_get(c, "mimeType")) : "unknown type");
            sb_putc(&out, ']');
        }
        else if (ty && !strcmp(ty, "audio"))
            sb_puts(&out, "[audio omitted]");
        else if (ty && !strcmp(ty, "resource"))
        {
            J *rs = j_get(c, "resource");
            const char *t2 = j_str(j_get(rs, "text")), *uri = j_str(j_get(rs, "uri"));
            if (t2)
                sb_puts(&out, t2);
            else
            {
                sb_puts(&out, "[resource: ");
                sb_puts(&out, uri ? uri : "?");
                sb_putc(&out, ']');
            }
        }
        else if (ty && !strcmp(ty, "resource_link"))
        {
            sb_puts(&out, "[resource link: ");
            sb_puts(&out, j_str(j_get(c, "uri")) ? j_str(j_get(c, "uri")) : "?");
            sb_putc(&out, ']');
        }
        else if (tx)
            sb_puts(&out, tx);
    }
    if ((!content || !content->child) && j_get(res, "structuredContent"))
        j_dump(&out, j_get(res, "structuredContent"));
    j_free(r);
    return out.p ? out.p : xstrdup("(no output)");
}

/* ---------- lifecycle ---------- */
static void mcp_srv_free(McpSrv *s)
{
    if (!s)
        return;
    mcp_srv_stop(s);
    if (s->hses)
        WinHttpCloseHandle(s->hses);
    free(s->cmdline);
    free(s->cwd);
    free(s->env);
    free(s->hdrs);
    free(s->auto_list);
    DeleteCriticalSection(&s->lock);
    DeleteCriticalSection(&s->wlock);
    free(s);
}
static void mcp_stop_all(void)
{
    int n = g_nsrv;
    g_nsrv = 0;
    for (int i = 0; i < n; i++)
    {
        mcp_srv_free(g_srv[i]);
        g_srv[i] = NULL;
    }
}
static char *mcp_connect_all(void)
{
    wchar_t p[MAX_PATH];
    size_t len;
    unsigned char *b;
    J *r, *srvs;
    SB fail = {0};
    int ok = 0, total = 0;
    char sum[1200];
    data_path(L"mcp.json", p);
    b = read_file(p, &len);
    if (!b)
        return xstrdup("MCP: no mcp.json yet (Settings > Edit MCP servers)");
    r = json_parse((char *)b);
    free(b);
    srvs = j_get(r, "mcpServers");
    if (!srvs)
        srvs = j_get(r, "servers");
    if (!srvs || srvs->t != J_OBJ)
    {
        j_free(r);
        return xstrdup("MCP: mcp.json is invalid or has no \"mcpServers\" object");
    }
    for (J *e = srvs->child; e && g_nsrv < MCP_MAX_SRV; e = e->next)
    {
        McpSrv *s;
        char err[300] = "";
        int idx, nt = 0, opened;
        J *dis = j_get(e, "disabled"), *aa = j_get(e, "autoApprove"), *to = j_get(e, "timeout");
        if (!e->key || e->t != J_OBJ || (dis && dis->num != 0))
            continue;
        s = (McpSrv *)calloc(1, sizeof *s);
        InitializeCriticalSection(&s->lock);
        InitializeCriticalSection(&s->wlock);
        strncpy(s->name, e->key, sizeof s->name - 1);
        s->timeout_ms = (to && to->t == J_NUM && to->num >= 1) ? (int)(to->num * 1000) : 60000;
        if (aa && aa->t == J_BOOL)
            s->auto_all = aa->num != 0;
        else if (aa && aa->t == J_ARR)
        {
            SB l = {0};
            sb_putc(&l, '\n');
            for (J *x = aa->child; x; x = x->next)
                if (j_str(x))
                {
                    sb_puts(&l, j_str(x));
                    sb_putc(&l, '\n');
                }
            s->auto_list = l.p;
        }
        idx = g_nsrv;
        g_srv[g_nsrv++] = s;
        if (j_str(j_get(e, "url")))
        {
            s->kind = 1;
            opened = mcp_http_open(s, e, err, sizeof err);
        }
        else if (j_str(j_get(e, "command")))
        {
            s->kind = 0;
            opened = mcp_stdio_open(s, e, err, sizeof err);
        }
        else
        {
            snprintf(err, sizeof err, "needs \"command\" (stdio) or \"url\" (HTTP)");
            opened = 0;
        }
        if (opened && mcp_handshake(s, idx, err, sizeof err, &nt))
        {
            ok++;
            total += nt;
        }
        else
        {
            tools_clear(1, idx);
            mcp_srv_stop(s);
            if (fail.n)
                sb_puts(&fail, "; ");
            sb_puts(&fail, s->name);
            sb_puts(&fail, ": ");
            sb_puts(&fail, err[0] ? err : "failed");
        }
    }
    j_free(r);
    snprintf(sum, sizeof sum, "MCP: %d server(s) connected, %d tool(s)%s%.700s", ok, total, fail.n ? "  |  failed: " : "", fail.n ? sbs(&fail) : "");
    free(fail.p);
    return xstrdup(sum);
}
static DWORD WINAPI mcp_boot(LPVOID arg)
{
    char *sum;
    (void)arg;
    mcp_stop_all();
    tools_clear(1, -1);
    sum = mcp_connect_all();
    post_msg(9, sum);
    free(sum);
    InterlockedExchange(&g_booting, 0);
    return 0;
}
static void mcp_start_async(void)
{
    HANDLE h;
    if (busy)
    {
        set_status("Stop the current generation before reloading MCP servers");
        return;
    }
    if (InterlockedExchange(&g_booting, 1))
        return;
    g_mcp_started = 1;
    set_status("MCP: connecting to servers ...");
    h = CreateThread(NULL, 0, mcp_boot, NULL, 0, NULL);
    if (h)
        CloseHandle(h);
    else
        g_booting = 0;
}
/* Settings button: create a template mcp.json when missing and open it in Notepad */
/* ---------- built-in mcp.json editor ---------- */
static HWND g_meWnd, g_meEdit;
static int g_meDone;
static HFONT g_meFont;
static wchar_t g_mePath[MAX_PATH];

static void mcp_ed_layout(HWND h)
{
    RECT r;
    GetClientRect(h, &r);
    MoveWindow(g_meEdit, 12, 12, r.right - 24, r.bottom - 12 - 62, TRUE);
    MoveWindow(GetDlgItem(h, IDOK), r.right - 190, r.bottom - 48, 80, 34, TRUE);
    MoveWindow(GetDlgItem(h, IDCANCEL), r.right - 100, r.bottom - 48, 80, 34, TRUE);
}

static int mcp_ed_save(HWND h)
{
    char *t = get_text(g_meEdit), *o;
    size_t n = 0, i;
    J *chk;
    if (!t)
        return 0;
    o = (char *)malloc(strlen(t) + 1);
    for (i = 0; t[i]; i++)
        if (t[i] != '\r')
            o[n++] = t[i]; /* store with plain \n */
    o[n] = 0;
    free(t);
    chk = json_parse(o);
    if (!chk && MessageBoxW(h, L"This is not valid JSON. Save anyway?", L"llamac - mcp.json", MB_YESNO | MB_ICONWARNING) != IDYES)
    {
        free(o);
        return 0;
    }
    j_free(chk);
    if (!write_file(g_mePath, o, n))
    {
        MessageBoxW(h, L"Could not write mcp.json.", L"llamac - mcp.json", MB_OK | MB_ICONERROR);
        free(o);
        return 0;
    }
    free(o);
    set_status("mcp.json saved - press Reload MCP in Settings to apply");
    return 1;
}

static LRESULT CALLBACK McpEdProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_SIZE:
        if (g_meEdit)
            mcp_ed_layout(h);
        return 0;
    case WM_ERASEBKGND:
    {
        RECT r;
        GetClientRect(h, &r);
        FillRect((HDC)w, &r, hbrPanel);
        return 1;
    }
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)w, cFg);
        SetBkColor((HDC)w, cSurf);
        return (LRESULT)hbrSurf;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        SetTextColor((HDC)w, cFg);
        SetBkColor((HDC)w, cPanel);
        return (LRESULT)hbrPanel;
    case WM_DRAWITEM:
        draw_btn((DRAWITEMSTRUCT *)l);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(w) == IDOK)
        {
            if (mcp_ed_save(h))
                DestroyWindow(h);
        }
        else if (LOWORD(w) == IDCANCEL)
            DestroyWindow(h);
        return 0;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        g_meDone = 1;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void mcp_edit_config(HWND owner)
{
    static int reg = 0;
    size_t len = 0;
    unsigned char *b;
    SB conv = {0};
    wchar_t *wt;
    RECT r;
    DWORD st = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MAXIMIZEBOX;
    MSG msg;
    HWND top = owner ? owner : hMain;

    data_path(L"mcp.json", g_mePath);
    if (GetFileAttributesW(g_mePath) == INVALID_FILE_ATTRIBUTES)
    {
        const char *tpl =
            "{\n  \"mcpServers\": {\n"
            "    \"example-stdio\": {\n      \"command\": \"npx\",\n      \"args\": [\"-y\", \"@modelcontextprotocol/server-everything\"],\n      \"env\": {},\n      \"autoApprove\": [],\n      \"disabled\": true\n    },\n"
            "    \"example-http\": {\n      \"url\": \"http://127.0.0.1:3000/mcp\",\n      \"headers\": { \"Authorization\": \"Bearer TOKEN\" },\n      \"disabled\": true\n    }\n"
            "  }\n}\n";
        write_file(g_mePath, tpl, strlen(tpl));
    }
    if (!reg)
    {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof wc);
        wc.cbSize = sizeof wc;
        wc.lpfnWndProc = McpEdProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.lpszClassName = L"LCMcpEd";
        RegisterClassExW(&wc);
        reg = 1;
        g_meFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, MONO);
    }
    GetWindowRect(top, &r);
    g_meDone = 0;
    g_meWnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"LCMcpEd", L"MCP servers (mcp.json)", st, r.left + 80, r.top + 60, 860, 620, top, NULL, GetModuleHandleW(NULL), NULL);
    apply_frame(g_meWnd);
    g_meEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN, 0, 0, 10, 10, g_meWnd, NULL, GetModuleHandleW(NULL), NULL);
    SendMessageW(g_meEdit, WM_SETFONT, (WPARAM)g_meFont, TRUE);
    SendMessageW(g_meEdit, EM_SETLIMITTEXT, 0x7FFFFFF0, 0);
    mkbtn(g_meWnd, L"Save", IDOK);
    mkbtn(g_meWnd, L"Cancel", IDCANCEL);
    SendMessageW(GetDlgItem(g_meWnd, IDOK), WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(GetDlgItem(g_meWnd, IDCANCEL), WM_SETFONT, (WPARAM)hFont, TRUE);

    b = read_file(g_mePath, &len);
    if (b)
    {
        for (size_t i = 0; i < len; i++)
        { /* \n -> \r\n for the edit control */
            if (b[i] == '\r')
                continue;
            if (b[i] == '\n')
                sb_putc(&conv, '\r');
            sb_putc(&conv, (char)b[i]);
        }
        wt = w_from(conv.p ? sbs(&conv) : "");
        SetWindowTextW(g_meEdit, wt);
        free(wt);
        free(conv.p);
        free(b);
    }
    mcp_ed_layout(g_meWnd);
    EnableWindow(top, FALSE);
    ShowWindow(g_meWnd, SW_SHOW);
    SetFocus(g_meEdit);
    SendMessageW(g_meEdit, EM_SETSEL, 0, 0);
    while (!g_meDone && GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        if (!IsDialogMessageW(g_meWnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(top, TRUE);
    SetForegroundWindow(top);
    g_meEdit = NULL;
    g_meWnd = NULL;
}

#endif /* LLAMAC_MCP_H */
