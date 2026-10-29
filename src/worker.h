/* worker.h - Streaming worker thread (SSE) */
#ifndef LLAMAC_WORKER_H
#define LLAMAC_WORKER_H

/* ---------- streaming worker ---------- */
typedef struct { char *body; char url[256]; char key[256]; } Job;
static void post_msg(int kind, const char *text) {
    Msg *m = (Msg *)malloc(sizeof *m); m->kind = kind; m->text = xstrdup(text);
    PostMessageW(hMain, WM_APP_MSG, 0, (LPARAM)m);
}
static void handle_line(char *ln) {
    while (*ln == ' ') ln++;
    if (strncmp(ln, "data:", 5)) return;
    ln += 5; while (*ln == ' ') ln++;
    if (!strncmp(ln, "[DONE]", 6)) return;
    J *r = json_parse(ln); if (!r) return;
    J *err = j_get(r, "error");
    if (err) { const char *m = j_str(j_get(err, "message")); post_msg(3, m ? m : "Server error"); j_free(r); return; }
    J *d = j_get(j_idx(j_get(r, "choices"), 0), "delta");
    const char *c = j_str(j_get(d, "content")), *rc = j_str(j_get(d, "reasoning_content"));
    if (rc && *rc) post_msg(1, rc);
    if (c && *c) post_msg(0, c);
    J *t = j_get(r, "timings");
    if (t) {
        char b[200];
        snprintf(b, sizeof b, "prompt %d tok @ %.1f t/s   |   generated %d tok @ %.1f t/s", (int)j_num(j_get(t, "prompt_n"), 0), j_num(j_get(t, "prompt_per_second"), 0),
                 (int)j_num(j_get(t, "predicted_n"), 0), j_num(j_get(t, "predicted_per_second"), 0));
        post_msg(2, b);
    }
    j_free(r);
}
static void stop_gen(void) {
    g_stop = 1;
    HINTERNET h = (HINTERNET)InterlockedExchangePointer((PVOID volatile *)&g_req, NULL);
    if (h) WinHttpCloseHandle(h);
}
static DWORD WINAPI worker(LPVOID arg) {
    Job *j = (Job *)arg; Url u; HINTERNET ses = NULL, con = NULL, req = NULL; SB buf = {0};
    wchar_t *hd = NULL; DWORD status = 0, sz = sizeof status, got; char tmp[8192]; size_t blen = strlen(j->body);
    if (!url_parse(j->url, "/v1/chat/completions", &u)) { post_msg(3, "Invalid server URL"); goto done; }
    ses = WinHttpOpen(L"llamac/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
    if (!ses) { post_msg(3, "WinHttpOpen failed"); goto done; }
    WinHttpSetTimeouts(ses, 5000, 5000, 30000, 0);
    con = WinHttpConnect(ses, u.host, u.port, 0);
    if (con) req = WinHttpOpenRequest(con, L"POST", u.path, NULL, NULL, NULL, u.secure ? WINHTTP_FLAG_SECURE : 0);
    if (!req) { post_msg(3, "Cannot connect to server"); goto done; }
    InterlockedExchangePointer((PVOID volatile *)&g_req, req);
    hd = auth_headers(j->key, 1);
    if (!WinHttpSendRequest(req, hd, (DWORD)-1, j->body, (DWORD)blen, (DWORD)blen, 0) || !WinHttpReceiveResponse(req, NULL)) {
        if (!g_stop) post_msg(3, "Connection to the server failed"); goto done;
    }
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &sz, NULL);
    while (WinHttpReadData(req, tmp, sizeof tmp, &got) && got) {
        sb_add(&buf, tmp, got);
        if (status != 200) continue;
        char *start = buf.p, *nl;
        while ((nl = (char *)memchr(start, '\n', (size_t)(buf.p + buf.n - start))) != NULL) { *nl = 0; handle_line(start); start = nl + 1; }
        size_t rest = (size_t)(buf.p + buf.n - start); memmove(buf.p, start, rest); buf.n = rest; buf.p[rest] = 0;
    }
    if (status == 200) { if (buf.n) handle_line(buf.p); }
    else if (!g_stop) {
        J *r = json_parse(sbs(&buf)); const char *m = r ? j_str(j_get(j_get(r, "error"), "message")) : NULL;
        char e[600]; snprintf(e, sizeof e, "HTTP %lu: %.500s", status, m ? m : sbs(&buf)); post_msg(3, e); j_free(r);
    }
done:
    {
        HINTERNET h = (HINTERNET)InterlockedExchangePointer((PVOID volatile *)&g_req, NULL);
        if (h) WinHttpCloseHandle(h);
    }
    if (con) WinHttpCloseHandle(con);
    if (ses) WinHttpCloseHandle(ses);
    free(hd); free(buf.p); free(j->body); free(j);
    post_msg(4, "");
    return 0;
}

#endif /* LLAMAC_WORKER_H */
