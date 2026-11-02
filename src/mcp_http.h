/* mcp_http.h - MCP over Streamable HTTP (WinHTTP, no third-party code) */
#ifndef LLAMAC_MCP_HTTP_H
#define LLAMAC_MCP_HTTP_H

/* "headers": {"Authorization": "Bearer X"} from mcp.json -> "Key: value\r\n..." */
static char *mcp_http_headers(J *cfg)
{
    J *h = j_get(cfg, "headers");
    SB o = {0};
    if (h && h->t == J_OBJ)
        for (J *e = h->child; e; e = e->next)
            if (e->key && e->t == J_STR && e->str)
            {
                sb_puts(&o, e->key);
                sb_puts(&o, ": ");
                sb_puts(&o, e->str);
                sb_puts(&o, "\r\n");
            }
    return o.p ? o.p : xstrdup("");
}

static int mcp_http_open(McpSrv *s, J *cfg, char *err, size_t es)
{
    const char *url = j_str(j_get(cfg, "url"));
    if (!url || !*url)
    {
        snprintf(err, es, "missing url");
        return 0;
    }
    if (!url_parse(url, "/", &s->u))
    {
        snprintf(err, es, "invalid url");
        return 0;
    }
    s->hses = WinHttpOpen(L"llamac-mcp/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!s->hses)
    {
        snprintf(err, es, "WinHttpOpen failed");
        return 0;
    }
    s->hdrs = mcp_http_headers(cfg);
    s->sid[0] = 0;
    return 1;
}

/* walk an SSE body, return the JSON-RPC response with this id */
static char *mcp_sse_find(const char *body, long id)
{
    SB data = {0};
    const char *p = body;
    char *out = NULL;
    while (p && *p && !out)
    {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len && p[len - 1] == '\r')
            len--;
        if (len == 0)
        {
            if (data.n)
            {
                out = mcp_match_response(sbs(&data), id);
                data.n = 0;
                if (data.p)
                    data.p[0] = 0;
            }
        }
        else if (len >= 5 && !strncmp(p, "data:", 5))
        {
            const char *v = p + 5;
            size_t vl = len - 5;
            if (vl && *v == ' ')
            {
                v++;
                vl--;
            }
            if (data.n)
                sb_putc(&data, '\n');
            sb_add(&data, v, vl);
        }
        p = nl ? nl + 1 : NULL;
    }
    if (!out && data.n)
        out = mcp_match_response(sbs(&data), id);
    free(data.p);
    return out;
}

static char *mcp_http_exchange(McpSrv *s, long id, const char *msg, int timeout_ms, int cancel, char *err, size_t es)
{
    HINTERNET con = NULL, req = NULL;
    SB hd = {0}, body = {0};
    wchar_t *whd = NULL;
    DWORD status = 0, sz = sizeof status, got;
    char tmp[8192];
    char *result = NULL;
    wchar_t buf[512], ct[128];
    DWORD bl;
    int sse = 0;
    size_t mlen = strlen(msg);

    if (!s->hses)
    {
        snprintf(err, es, "HTTP session not open");
        return NULL;
    }
    con = WinHttpConnect(s->hses, s->u.host, s->u.port, 0);
    if (!con)
    {
        snprintf(err, es, "cannot connect to server");
        goto done;
    }
    req = WinHttpOpenRequest(con, L"POST", s->u.path, NULL, NULL, NULL, s->u.secure ? WINHTTP_FLAG_SECURE : 0);
    if (!req)
    {
        snprintf(err, es, "cannot open request");
        goto done;
    }
    WinHttpSetTimeouts(req, 10000, 10000, timeout_ms, timeout_ms);

    sb_puts(&hd, "Content-Type: application/json\r\nAccept: application/json, text/event-stream\r\n");
    if (s->hdrs)
        sb_puts(&hd, s->hdrs);
    if (s->sid[0])
    {
        sb_puts(&hd, "Mcp-Session-Id: ");
        sb_puts(&hd, s->sid);
        sb_puts(&hd, "\r\n");
    }
    if (s->proto[0] && id)
    {
        sb_puts(&hd, "MCP-Protocol-Version: ");
        sb_puts(&hd, s->proto);
        sb_puts(&hd, "\r\n");
    }
    whd = w_from(sbs(&hd));

    if (!WinHttpSendRequest(req, whd, (DWORD)-1, (LPVOID)msg, (DWORD)mlen, (DWORD)mlen, 0) || !WinHttpReceiveResponse(req, NULL))
    {
        snprintf(err, es, (cancel && g_stop) ? "cancelled" : "request failed or timed out (error %lu)", GetLastError());
        goto done;
    }
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &sz, NULL);

    bl = sizeof buf;
    if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CUSTOM, L"Mcp-Session-Id", buf, &bl, WINHTTP_NO_HEADER_INDEX) && buf[0])
    {
        char *u8 = u_from(buf);
        if (u8)
        {
            strncpy(s->sid, u8, sizeof s->sid - 1);
            free(u8);
        }
    }
    bl = sizeof ct;
    if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX, ct, &bl, WINHTTP_NO_HEADER_INDEX))
        sse = wcsstr(ct, L"text/event-stream") != NULL;

    while (WinHttpReadData(req, tmp, sizeof tmp, &got) && got)
    {
        if (body.n < 32u * 1024 * 1024)
            sb_add(&body, tmp, got);
        if (cancel && g_stop)
        {
            snprintf(err, es, "cancelled");
            goto done;
        }
    }
    if (status >= 400)
    {
        snprintf(err, es, "HTTP %lu %.200s", status, body.n ? sbs(&body) : "");
        goto done;
    }
    if (!id)
    {
        result = xstrdup("");
        goto done;
    } /* notification: 202 Accepted, no body */
    if (!body.n)
    {
        snprintf(err, es, "empty response (HTTP %lu)", status);
        goto done;
    }
    result = sse ? mcp_sse_find(sbs(&body), id) : mcp_match_response(sbs(&body), id);
    if (!result)
        snprintf(err, es, "no matching JSON-RPC response in the reply");
done:
    free(whd);
    free(hd.p);
    free(body.p);
    if (req)
        WinHttpCloseHandle(req);
    if (con)
        WinHttpCloseHandle(con);
    return result;
}

#endif /* LLAMAC_MCP_HTTP_H */