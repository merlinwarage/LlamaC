/* mcp_stdio.h - MCP over stdio: child process, pipes, newline-delimited JSON-RPC */
#ifndef LLAMAC_MCP_STDIO_H
#define LLAMAC_MCP_STDIO_H

static void mcp_wq(SB *s, const char *a)
{ /* append one command-line argument, quoted for CreateProcess */
    size_t bs = 0, k;
    if (*a && !strpbrk(a, " \t\""))
    {
        sb_puts(s, a);
        return;
    }
    sb_putc(s, '"');
    for (; *a; a++)
    {
        if (*a == '\\')
        {
            bs++;
            continue;
        }
        if (*a == '"')
        {
            for (k = 0; k < bs * 2 + 1; k++)
                sb_putc(s, '\\');
            sb_putc(s, '"');
            bs = 0;
            continue;
        }
        for (k = 0; k < bs; k++)
            sb_putc(s, '\\');
        bs = 0;
        sb_putc(s, *a);
    }
    for (k = 0; k < bs * 2; k++)
        sb_putc(s, '\\');
    sb_putc(s, '"');
}
static void mcp_wadd(wchar_t **o, size_t *len, size_t *cap, const wchar_t *s, size_t n)
{
    if (*len + n + 1 > *cap)
    {
        while (*len + n + 1 > *cap)
            *cap *= 2;
        *o = (wchar_t *)realloc(*o, *cap * sizeof(wchar_t));
    }
    memcpy(*o + *len, s, n * sizeof(wchar_t));
    *len += n;
}
/* parent environment + the "env" object of the server config */
static wchar_t *mcp_envblock(J *env)
{
    wchar_t *keys[64], *o, *pe;
    size_t nk = 0, len = 0, cap = 8192;
    if (!env || env->t != J_OBJ || !env->child)
        return NULL;
    o = (wchar_t *)malloc(cap * sizeof(wchar_t));
    for (J *e = env->child; e && nk < 64; e = e->next)
        if (e->key && e->t == J_STR)
            keys[nk++] = w_from(e->key);
    pe = GetEnvironmentStringsW();
    if (pe)
    {
        for (wchar_t *q = pe; *q; q += wcslen(q) + 1)
        {
            int skip = 0;
            for (size_t i = 0; i < nk && !skip; i++)
            {
                size_t kl = wcslen(keys[i]);
                if (kl && !_wcsnicmp(q, keys[i], kl) && q[kl] == L'=')
                    skip = 1;
            }
            if (!skip)
                mcp_wadd(&o, &len, &cap, q, wcslen(q) + 1);
        }
        FreeEnvironmentStringsW(pe);
    }
    for (J *e = env->child; e; e = e->next)
    {
        if (!e->key || e->t != J_STR)
            continue;
        SB kv = {0};
        sb_puts(&kv, e->key);
        sb_putc(&kv, '=');
        sb_puts(&kv, e->str ? e->str : "");
        wchar_t *w = w_from(sbs(&kv));
        mcp_wadd(&o, &len, &cap, w, wcslen(w) + 1);
        free(w);
        free(kv.p);
    }
    mcp_wadd(&o, &len, &cap, L"", 1); /* block terminator */
    for (size_t i = 0; i < nk; i++)
        free(keys[i]);
    return o;
}

static int mcp_stdio_write(McpSrv *s, const char *json)
{
    DWORD w = 0;
    int ok;
    EnterCriticalSection(&s->wlock);
    ok = s->hin && WriteFile(s->hin, json, (DWORD)strlen(json), &w, NULL) && w == strlen(json) && WriteFile(s->hin, "\n", 1, &w, NULL);
    LeaveCriticalSection(&s->wlock);
    return ok;
}
static void mcp_on_line(McpSrv *s, const char *line)
{
    J *r = json_parse(line), *id, *meth;
    if (!r)
        return;
    id = j_get(r, "id");
    meth = j_get(r, "method");
    if (id && !meth)
    { /* response to one of our requests */
        if (id->t == J_NUM && (LONG)id->num == s->want_id)
        {
            char *old = (char *)InterlockedExchangePointer((PVOID volatile *)&s->resp, xstrdup(line));
            free(old);
            SetEvent(s->ev);
        }
    }
    else if (id && meth)
    { /* server -> client request: answer ping, refuse the rest */
        SB o = {0};
        const char *m = j_str(meth);
        sb_puts(&o, "{\"jsonrpc\":\"2.0\",\"id\":");
        j_dump(&o, id);
        if (m && !strcmp(m, "ping"))
            sb_puts(&o, ",\"result\":{}}");
        else
            sb_puts(&o, ",\"error\":{\"code\":-32601,\"message\":\"Method not supported by this client\"}}");
        mcp_stdio_write(s, sbs(&o));
        free(o.p);
    }
    j_free(r);
}
static DWORD WINAPI mcp_reader(LPVOID arg)
{
    McpSrv *s = (McpSrv *)arg;
    SB buf = {0};
    char tmp[4096];
    DWORD got;
    while (ReadFile(s->hout, tmp, sizeof tmp, &got, NULL) && got)
    {
        char *start, *nl;
        sb_add(&buf, tmp, got);
        start = buf.p;
        while ((nl = (char *)memchr(start, '\n', (size_t)(buf.p + buf.n - start))) != NULL)
        {
            *nl = 0;
            if (nl > start && nl[-1] == '\r')
                nl[-1] = 0;
            if (*start == '{')
                mcp_on_line(s, start);
            start = nl + 1;
        }
        {
            size_t rest = (size_t)(buf.p + buf.n - start);
            memmove(buf.p, start, rest);
            buf.n = rest;
            buf.p[rest] = 0;
        }
    }
    free(buf.p);
    InterlockedExchange(&s->dead, 1);
    SetEvent(s->ev);
    return 0;
}

static int mcp_stdio_open(McpSrv *s, J *cfg, char *err, size_t es)
{
    SECURITY_ATTRIBUTES sa;
    HANDLE inR = NULL, inW = NULL, outR = NULL, outW = NULL, nul = NULL;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    SB cl = {0};
    wchar_t *wcmd, found[MAX_PATH], *wcl;
    J *args = j_get(cfg, "args");
    const char *cmd = j_str(j_get(cfg, "command")), *cwd = j_str(j_get(cfg, "cwd"));
    int direct, ok;
    memset(&sa, 0, sizeof sa);
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    wcmd = w_from(cmd);
    {
        DWORD n = SearchPathW(NULL, wcmd, L".exe", MAX_PATH, found, NULL);
        direct = (n > 0 && n < MAX_PATH);
    }
    free(wcmd);
    if (direct)
        mcp_wq(&cl, cmd);
    else
    {
        sb_puts(&cl, "cmd.exe /d /s /c \"");
        mcp_wq(&cl, cmd);
    } /* npx & co. are .cmd files */
    for (J *a = args ? args->child : NULL; a; a = a->next)
        if (j_str(a))
        {
            sb_putc(&cl, ' ');
            mcp_wq(&cl, j_str(a));
        }
    if (!direct)
        sb_putc(&cl, '"');
    wcl = w_from(sbs(&cl));
    free(cl.p);
    s->env = mcp_envblock(j_get(cfg, "env"));
    if (cwd && *cwd)
        s->cwd = w_from(cwd);
    if (!CreatePipe(&inR, &inW, &sa, 0) || !CreatePipe(&outR, &outW, &sa, 0))
    {
        snprintf(err, es, "cannot create pipes");
        free(wcl);
        return 0;
    }
    SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inR;
    si.hStdOutput = outW;
    si.hStdError = nul;
    memset(&pi, 0, sizeof pi);
    ok = CreateProcessW(NULL, wcl, NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, s->env, s->cwd, &si, &pi);
    free(wcl);
    CloseHandle(inR);
    CloseHandle(outW);
    if (nul != INVALID_HANDLE_VALUE)
        CloseHandle(nul);
    if (!ok)
    {
        snprintf(err, es, "cannot start \"%.100s\" (error %lu)", cmd, GetLastError());
        CloseHandle(inW);
        CloseHandle(outR);
        return 0;
    }
    s->job = CreateJobObjectW(NULL, NULL); /* the whole process tree dies with the job / with llamac */
    if (s->job)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jl;
        memset(&jl, 0, sizeof jl);
        jl.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(s->job, JobObjectExtendedLimitInformation, &jl, sizeof jl);
        AssignProcessToJobObject(s->job, pi.hProcess);
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    s->hproc = pi.hProcess;
    s->hin = inW;
    s->hout = outR;
    s->ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    s->hrd = CreateThread(NULL, 0, mcp_reader, s, 0, NULL);
    return s->hrd != NULL;
}
static char *mcp_stdio_exchange(McpSrv *s, long id, const char *msg, int timeout_ms, int cancel, char *err, size_t es)
{
    DWORD t0;
    if (!s->hin || s->dead)
    {
        snprintf(err, es, "server process is not running");
        return NULL;
    }
    if (!id)
    {
        return mcp_stdio_write(s, msg) ? xstrdup("") : NULL;
    }
    {
        char *old = (char *)InterlockedExchangePointer((PVOID volatile *)&s->resp, NULL);
        free(old);
    }
    s->want_id = (LONG)id;
    if (!mcp_stdio_write(s, msg))
    {
        snprintf(err, es, "cannot write to the server");
        return NULL;
    }
    t0 = GetTickCount();
    for (;;)
    {
        char *r;
        WaitForSingleObject(s->ev, 100);
        r = (char *)InterlockedExchangePointer((PVOID volatile *)&s->resp, NULL);
        if (r)
            return r;
        if (s->dead)
        {
            snprintf(err, es, "server process exited");
            return NULL;
        }
        if (cancel && g_stop)
        {
            snprintf(err, es, "cancelled");
            return NULL;
        }
        if (GetTickCount() - t0 > (DWORD)timeout_ms)
        {
            snprintf(err, es, "timeout after %d s", timeout_ms / 1000);
            return NULL;
        }
    }
}
static void mcp_srv_stop(McpSrv *s)
{
    if (s->hin)
    {
        CloseHandle(s->hin);
        s->hin = NULL;
    } /* EOF on stdin = polite shutdown request */
    if (s->hproc)
    {
        WaitForSingleObject(s->hproc, 300);
        CloseHandle(s->hproc);
        s->hproc = NULL;
    }
    if (s->job)
    {
        CloseHandle(s->job);
        s->job = NULL;
    } /* kills anything still running */
    if (s->hrd)
    {
        WaitForSingleObject(s->hrd, 1500);
        CloseHandle(s->hrd);
        s->hrd = NULL;
    }
    if (s->hout)
    {
        CloseHandle(s->hout);
        s->hout = NULL;
    }
    if (s->ev)
    {
        CloseHandle(s->ev);
        s->ev = NULL;
    }
    free(InterlockedExchangePointer((PVOID volatile *)&s->resp, NULL));
}

#endif /* LLAMAC_MCP_STDIO_H */
