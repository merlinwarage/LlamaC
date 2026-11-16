/* worker.h - Streaming worker thread (SSE) */
#ifndef LLAMAC_WORKER_H
#define LLAMAC_WORKER_H

/* ---------- streaming worker ---------- */
typedef struct
{
    char *body;
    char url[256];
    char key[256];
    int base_off;                     /* bytes already in the answer (prefill): token offsets start here */
    char save_id[40], restore_id[40]; /* KV cache per chat: save the slot for one chat, restore it for another */
} Job;
static int g_w_off;    /* worker: bytes of answer content received so far (for token offsets) */
#define TOK_KEEP 0.9f /* tokens at least this likely are not kept (they are never shaded) */

static int g_last_prompt_n, g_last_pred_n; /* file-scope in worker.h */
static void post_msg(int kind, const char *text)
{
    Msg *m = (Msg *)malloc(sizeof *m);
    m->kind = kind;
    m->text = xstrdup(text);
    PostMessageW(hMain, WM_APP_MSG, 0, (LPARAM)m);
}
static void handle_line(char *ln)
{
    while (*ln == ' ')
        ln++;
    if (strncmp(ln, "data:", 5))
        return;
    ln += 5;
    while (*ln == ' ')
        ln++;
    if (!strncmp(ln, "[DONE]", 6))
        return;
    J *r = json_parse(ln);
    if (!r)
        return;
    J *err = j_get(r, "error");
    if (err)
    {
        const char *m = j_str(j_get(err, "message"));
        post_msg(3, m ? m : "Server error");
        j_free(r);
        return;
    }
    J *ch0 = j_idx(j_get(r, "choices"), 0), *d = j_get(ch0, "delta");
    const char *c = j_str(j_get(d, "content")), *rc = j_str(j_get(d, "reasoning_content"));
    {
        J *pp = j_get(r, "prompt_progress"); /* return_progress: one update per prompt batch */
        if (pp)
        {
            char b[80];
            snprintf(b, sizeof b, "%d\t%d\t%d\t%d", (int)j_num(j_get(pp, "processed"), 0), (int)j_num(j_get(pp, "total"), 0),
                     (int)j_num(j_get(pp, "cache"), 0), (int)j_num(j_get(pp, "time_ms"), 0));
            post_msg(12, b);
        }
    }
    {
        const char *fr = j_str(j_get(ch0, "finish_reason"));
        if (fr && !strcmp(fr, "length"))
            post_msg(13, "");
    }
    if (rc && *rc)
        post_msg(1, rc);
    if (c && *c)
    {
        J *lp = j_get(j_get(ch0, "logprobs"), "content");
        post_msg(0, c);
        if (lp && lp->t == J_ARR)
        { /* token probabilities ride on the delta the token produced; offsets count from the answer start */
            int off = g_w_off;
            for (J *t = lp->child; t; t = t->next)
            {
                const char *tok = j_str(j_get(t, "token"));
                double l = j_num(j_get(t, "logprob"), 0), pr = l < -50 ? 0 : exp(l);
                int len = tok ? (int)strlen(tok) : 0;
                if (tok && pr < TOK_KEEP)
                {
                    SB m = {0};
                    sb_printf(&m, "%d\t%d\t%.4f\t", off, len, pr);
                    for (J *a = j_get(t, "top_logprobs") ? j_get(t, "top_logprobs")->child : NULL; a; a = a->next)
                    {
                        const char *at = j_str(j_get(a, "token"));
                        double al = j_num(j_get(a, "logprob"), -99);
                        if (!at || strchr(at, '\x1E') || strchr(at, '\x1F'))
                            continue;
                        sb_puts(&m, at);
                        sb_printf(&m, "\x1F%.4f\x1E", al < -50 ? 0 : exp(al));
                    }
                    post_msg(11, sbs(&m));
                    free(m.p);
                }
                off += len;
            }
        }
        g_w_off += (int)strlen(c);
    }
    {
        J *tcs = j_get(d, "tool_calls");
        if (tcs && tcs->t == J_ARR)
            tc_delta(tcs);
    }
    J *t = j_get(r, "timings");
    if (t)
    {
        char b[300];
        int pn = (int)j_num(j_get(t, "prompt_n"), 0);
        int cn = (int)j_num(j_get(t, "cache_n"), 0); // reused from KV cache
        int gn = (int)j_num(j_get(t, "predicted_n"), 0);
        int total_prompt = pn + cn;
        if (g_nctx > 0)
        {
            int used = total_prompt + gn, free_ctx = g_nctx - used;
            if (free_ctx < 0)
                free_ctx = 0;
            snprintf(b, sizeof b,
                     "prompt %d tok (%d new, %d cached) @ %.1f t/s   |   generated %d tok @ %.1f t/s   |   ctx %d / %d (%d free)",
                     total_prompt, pn, cn, j_num(j_get(t, "prompt_per_second"), 0),
                     gn, j_num(j_get(t, "predicted_per_second"), 0),
                     used, g_nctx, free_ctx);
        }
        else
        {
            snprintf(b, sizeof b,
                     "prompt %d tok (%d new, %d cached) @ %.1f t/s   |   generated %d tok @ %.1f t/s",
                     total_prompt, pn, cn, j_num(j_get(t, "prompt_per_second"), 0),
                     gn, j_num(j_get(t, "predicted_per_second"), 0));
        }
        {
            int dn = (int)j_num(j_get(t, "draft_n"), 0), da = (int)j_num(j_get(t, "draft_n_accepted"), 0);
            if (dn > 0) /* speculative decoding is on: how many drafted tokens the model accepted */
                snprintf(b + strlen(b), sizeof b - strlen(b), "   |   draft %d%% accepted", da * 100 / dn);
        }
        g_last_prompt_n = total_prompt;
        g_last_pred_n = gn;
        post_msg(2, b);
    }
    j_free(r);
}
/* KV cache per chat (llama-server --slot-save-path): save slot 0 under one chat's name, load another's.
   Only chats saved before are restored (the UI keeps that list), so the server is not asked for missing files.
   A failed action is reported; the prompt is then simply processed as usual. Returns 1 on success. */
static int slot_action(const char *action, const char *conv_id)
{
    char ep[80], body[120], err[300], st[400];
    char *r;
    int ok;
    snprintf(ep, sizeof ep, "/slots/0?action=%s", action);
    snprintf(body, sizeof body, "{\"filename\":\"llamac-");
    for (const char *p = conv_id; *p && strlen(body) < 100; p++)
    {
        size_t L = strlen(body);
        body[L] = (isalnum((unsigned char)*p) || *p == '-') ? *p : '_';
        body[L + 1] = 0;
    }
    strcat(body, ".bin\"}");
    r = http_req_x(L"POST", ep, body, 180000, err, sizeof err);
    ok = r != NULL;
    if (r)
    {
        J *j = json_parse(r);
        int n = (int)j_num(j_get(j, !strcmp(action, "save") ? "n_saved" : "n_restored"), -1);
        double ms = j_num(j_get(j_get(j, "timings"), !strcmp(action, "save") ? "save_ms" : "restore_ms"), 0);
        snprintf(st, sizeof st, !strcmp(action, "save") ? "KV cache of the previous chat saved (%d tokens, %.1f s)" : "KV cache of this chat restored (%d tokens, %.1f s)", n, ms / 1000);
        post_msg(9, st);
        j_free(j);
        free(r);
    }
    else if (strstr(err, "501"))
        post_msg(9, "KV cache per chat: the server was not started with --slot-save-path");
    else
    {
        snprintf(st, sizeof st, "KV cache per chat: %s failed (%.300s) - the prompt is processed normally", action, err);
        post_msg(9, st);
    }
    return ok;
}
static void stop_gen(void)
{
    g_stop = 1;
    HINTERNET h = (HINTERNET)InterlockedExchangePointer((PVOID volatile *)&g_req, NULL);
    if (h)
        WinHttpCloseHandle(h);
}
static DWORD WINAPI worker(LPVOID arg)
{
    Job *j = (Job *)arg;
    Url u;
    HINTERNET ses = NULL, con = NULL, req = NULL;
    SB buf = {0};
    wchar_t *hd = NULL;
    DWORD status = 0, sz = sizeof status, got;
    char tmp[8192];
    size_t blen = strlen(j->body);
    tc_reset();
    g_w_off = j->base_off;
    if (j->save_id[0])
    {
        post_msg(9, "Saving the KV cache of the previous chat ...");
        if (slot_action("save", j->save_id))
            post_msg(14, j->save_id); /* the UI remembers that this chat can be restored */
    }
    if (j->restore_id[0] && !g_stop)
    {
        post_msg(9, "Restoring this chat's KV cache ...");
        if (!slot_action("restore", j->restore_id))
            post_msg(15, j->restore_id); /* file gone (e.g. deleted by hand): stop asking for it */
    }
    if (!url_parse(j->url, "/v1/chat/completions", &u))
    {
        post_msg(3, "Invalid server URL");
        goto done;
    }
    ses = WinHttpOpen(L"llamac/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
    if (!ses)
    {
        post_msg(3, "WinHttpOpen failed");
        goto done;
    }
    WinHttpSetTimeouts(ses, 5000, 5000, 30000, 0);
    con = WinHttpConnect(ses, u.host, u.port, 0);
    if (con)
        req = WinHttpOpenRequest(con, L"POST", u.path, NULL, NULL, NULL, u.secure ? WINHTTP_FLAG_SECURE : 0);
    if (!req)
    {
        post_msg(3, "Cannot connect to server");
        goto done;
    }
    InterlockedExchangePointer((PVOID volatile *)&g_req, req);
    hd = auth_headers(j->key, 1);
    if (!WinHttpSendRequest(req, hd, (DWORD)-1, j->body, (DWORD)blen, (DWORD)blen, 0) || !WinHttpReceiveResponse(req, NULL))
    {
        if (!g_stop)
            post_msg(3, "Connection to the server failed");
        goto done;
    }
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &sz, NULL);
    while (WinHttpReadData(req, tmp, sizeof tmp, &got) && got)
    {
        sb_add(&buf, tmp, got);
        if (status != 200)
            continue;
        char *start = buf.p, *nl;
        while ((nl = (char *)memchr(start, '\n', (size_t)(buf.p + buf.n - start))) != NULL)
        {
            *nl = 0;
            handle_line(start);
            start = nl + 1;
        }
        size_t rest = (size_t)(buf.p + buf.n - start);
        memmove(buf.p, start, rest);
        buf.n = rest;
        buf.p[rest] = 0;
    }
    if (status == 200)
    {
        if (buf.n)
            handle_line(buf.p);
    }
    else if (!g_stop)
    {
        J *r = json_parse(sbs(&buf));
        const char *m = r ? j_str(j_get(j_get(r, "error"), "message")) : NULL;
        char e[600];
        snprintf(e, sizeof e, "HTTP %lu: %.500s", status, m ? m : sbs(&buf));
        post_msg(3, e);
        j_free(r);
    }
done:
    {
        HINTERNET h = (HINTERNET)InterlockedExchangePointer((PVOID volatile *)&g_req, NULL);
        if (h)
            WinHttpCloseHandle(h);
    }
    if (con)
        WinHttpCloseHandle(con);
    if (ses)
        WinHttpCloseHandle(ses);
    free(hd);
    free(buf.p);
    free(j->body);
    free(j);
    {
        char *tj = g_stop ? NULL : tc_build();
        if (tj)
        {
            post_msg(5, tj);
            free(tj);
        }
    }
    post_msg(4, "");
    return 0;
}

#endif /* LLAMAC_WORKER_H */
