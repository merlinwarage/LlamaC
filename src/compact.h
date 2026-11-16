/* compact.h - automatic session compression when free context is low
   Tier 1 (instant, no model call): old tool results - before the latest user message - are replaced by short stubs
   in requests (Node.elided). Tier 2 (only if tier 1 was not enough): the model summarizes the older part of the
   conversation. Both run once at the trigger point, so llama-server's prompt cache is invalidated once, not on
   every turn. With "Prepare compression early" the work starts while the user is idle, a bit before the limit;
   such an early summary is cancelled when the user sends a message that still fits. */
#ifndef LLAMAC_COMPACT_H
#define LLAMAC_COMPACT_H

/* The transcript is snapshotted on the UI thread (c->n may be realloc'ed by the UI while a
   background thread runs), so the worker thread never touches the Conv. */
typedef struct
{
    Conv *c;
    int keep; /* first node that stays; it is re-parented under the summary */
    char *txt;
} CJob;

/* last attempt: after a failure the same conversation is not retried until its path grows by
   CRETRY nodes, so a server that cannot summarize is not hit after every turn */
#define CRETRY 6
static Conv *g_clast_conv;
static int g_clast_n, g_clast_fail;
/* g_compact_early (the running summary was started early) and g_ccancel are declared in actions.h */
static volatile HINTERNET g_creq; /* the summary request, closed to cancel it */
static void compact_clip(SB *o, const char *s, size_t cap)
{
    size_t n = strlen(s), h, t, ts;
    if (n <= cap)
    {
        sb_add(o, s, n);
        return;
    }
    h = cap * 2 / 3;
    t = cap - h;
    while (h && ((unsigned char)s[h] & 0xC0) == 0x80)
        h--;
    ts = n - t;
    while (ts < n && ((unsigned char)s[ts] & 0xC0) == 0x80)
        ts++;
    sb_add(o, s, h);
    sb_puts(o, "\n[... clipped ...]\n");
    sb_add(o, s + ts, n - ts);
}
static void compact_transcript(SB *o, Conv *c, const int *ids, int k, size_t cap)
{
    for (int i = 0; i < k; i++)
    {
        Node *m = &c->n[ids[i]];
        if (m->role == ROLE_ROOT)
            continue;
        sb_puts(o, m->role == ROLE_SYS ? "earlier summary" : role_name(m->role));
        sb_puts(o, ": ");
        compact_clip(o, m->content, m->role == ROLE_TOOL && m->elided ? 400 : cap);
        sb_putc(o, '\n');
        if (m->tcalls && *m->tcalls)
        {
            sb_puts(o, "[tool calls: ");
            compact_clip(o, m->tcalls, 600);
            sb_puts(o, "]\n");
        }
        for (int a = 0; a < m->na; a++)
            if (m->att[a].kind == ATT_TEXT)
            {
                sb_puts(o, "[file ");
                sb_puts(o, m->att[a].name);
                sb_puts(o, "]\n");
                compact_clip(o, m->att[a].data, cap);
                sb_putc(o, '\n');
            }
    }
}
/* Tier 1: stub the tool results that come before the latest user message. Returns the characters saved. */
static size_t compact_elide(Conv *c)
{
    int *p, n = conv_path(c, &p), lastu = -1;
    size_t saved = 0;
    for (int i = 0; i < n; i++)
        if (c->n[p[i]].role == ROLE_USER)
            lastu = i;
    for (int i = 0; i < lastu; i++)
    {
        Node *m = &c->n[p[i]];
        size_t L = strlen(m->content);
        if (m->role == ROLE_TOOL && !m->elided && L >= 600)
        {
            m->elided = 1;
            saved += L - 250;
        }
    }
    free(p);
    if (saved)
        c->dirty = 1;
    return saved;
}
static const char *SUMMARY_PROMPT =
    "You compress a conversation so it can continue in a fresh context. Write a summary with exactly these sections:\n"
    "## Goal\nWhat the user wants overall.\n"
    "## Current state\nWhat is done, what is in progress, the latest results.\n"
    "## Decisions and constraints\nChoices made, requirements, preferences, things to avoid.\n"
    "## Facts and identifiers\nFile paths, names, commands, numbers, versions, error messages - exact.\n"
    "## Open questions and next steps\n"
    "If the transcript starts with an earlier summary, merge it in and keep its facts. Dense and factual, under 600 words, "
    "bullet points, no commentary, do not invent anything.";
/* One streaming completion without tools, used for summaries (compression, summarize_file). Streaming, so closing
   the request stops generation on the server and progress can be shown. Closing *hslot cancels it; *cancel tells the
   caller it was cancelled. id_slot >= 0 pins a server slot. Returns the text, or NULL with the reason in err. */
static char *llm_complete(const char *system, const char *user, int max_tokens, int id_slot, volatile HINTERNET *hslot,
                          volatile LONG *cancel, const char *what, char *err, size_t es)
{
    SB body = {0}, content = {0}, buf = {0};
    Url u;
    HINTERNET ses = NULL, con = NULL, req = NULL;
    wchar_t *hd = NULL;
    DWORD status = 0, sz = sizeof status, got, last = GetTickCount();
    char tmp[8192], *out = NULL;
    long ntok = 0, nreason = 0;
    char fin[32] = "";
    err[0] = 0;
    sb_puts(&body, "{\"messages\":[{\"role\":\"system\",\"content\":");
    sb_json(&body, system);
    sb_puts(&body, "},{\"role\":\"user\",\"content\":");
    sb_json(&body, user);
    /* Reasoning models think before the summary: measured ~2100 reasoning + ~700 summary tokens for an 8k-token
       transcript (Qwen3 27B). A reasoning budget (llama-server reasoning_budget_tokens) caps the thinking; thinking
       is not forced off, since some models answer enable_thinking:false with an empty reply. */
    sb_printf(&body, "}],\"stream\":true,\"max_tokens\":%d,\"temperature\":0.2", max_tokens);
    if (id_slot >= 0)
        sb_printf(&body, ",\"id_slot\":%d", id_slot);
    if (G.sumbud > 0)
        sb_printf(&body, ",\"reasoning_budget_tokens\":%d", G.sumbud);
    if (G.kwargs[0])
    {
        J *k = json_parse_strict(G.kwargs);
        if (k && k->t == J_OBJ)
            sb_printf(&body, ",\"chat_template_kwargs\":%s", G.kwargs);
        j_free(k);
    }
    if (G.model[0])
    {
        sb_puts(&body, ",\"model\":");
        sb_json(&body, G.model);
    }
    sb_putc(&body, '}');
    if (!url_parse(G.url, "/v1/chat/completions", &u))
    {
        snprintf(err, es, "invalid server URL");
        free(body.p);
        return NULL;
    }
    ses = WinHttpOpen(L"llamac/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
    if (ses)
    {
        WinHttpSetTimeouts(ses, 5000, 5000, 30000, 900000); /* a slow model can think for minutes before the first summary token */
        con = WinHttpConnect(ses, u.host, u.port, 0);
    }
    if (con)
        req = WinHttpOpenRequest(con, L"POST", u.path, NULL, NULL, NULL, u.secure ? WINHTTP_FLAG_SECURE : 0);
    if (!req)
    {
        snprintf(err, es, "cannot connect to %s", G.url);
        goto done;
    }
    InterlockedExchangePointer((PVOID volatile *)hslot, req);
    if (*cancel)
        goto done;
    hd = auth_headers(G.key, 1);
    if (!WinHttpSendRequest(req, hd, (DWORD)-1, body.p, (DWORD)body.n, (DWORD)body.n, 0) || !WinHttpReceiveResponse(req, NULL))
    {
        if (!*cancel)
            snprintf(err, es, GetLastError() == ERROR_WINHTTP_TIMEOUT ? "no reply in time (timeout)" : "cannot connect to %s (server not running?)", G.url);
        goto done;
    }
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &sz, NULL);
    while (WinHttpReadData(req, tmp, sizeof tmp, &got) && got)
    {
        char *start, *nl;
        sb_add(&buf, tmp, got);
        if (status != 200)
            continue;
        start = buf.p;
        while ((nl = (char *)memchr(start, '\n', (size_t)(buf.p + buf.n - start))) != NULL)
        {
            char *ln = start;
            *nl = 0;
            start = nl + 1;
            while (*ln == ' ')
                ln++;
            if (strncmp(ln, "data:", 5))
                continue;
            ln += 5;
            while (*ln == ' ')
                ln++;
            {
                J *r = json_parse(ln), *ch = j_idx(j_get(r, "choices"), 0), *d = j_get(ch, "delta");
                const char *c = j_str(j_get(d, "content")), *rc = j_str(j_get(d, "reasoning_content")), *fr = j_str(j_get(ch, "finish_reason"));
                const char *em = j_str(j_get(j_get(r, "error"), "message"));
                if (em)
                    snprintf(err, es, "%.300s", em);
                if (c && *c)
                    sb_puts(&content, c), ntok++;
                if (rc && *rc)
                    nreason++;
                if (fr)
                    snprintf(fin, sizeof fin, "%s", fr);
                j_free(r);
            }
        }
        {
            size_t rest = (size_t)(buf.p + buf.n - start);
            memmove(buf.p, start, rest);
            buf.n = rest;
            buf.p[rest] = 0;
        }
        if (GetTickCount() - last > 1500)
        {
            char st[200];
            last = GetTickCount();
            snprintf(st, sizeof st, "%s: %ld thinking + %ld answer tokens so far ...", what, nreason, ntok);
            post_msg(9, st);
        }
    }
    if (status != 200 && !*cancel)
    {
        J *r = json_parse(sbs(&buf));
        const char *m = j_str(j_get(j_get(r, "error"), "message"));
        snprintf(err, es, "HTTP %lu: %.300s", (unsigned long)status, m ? m : sbs(&buf));
        j_free(r);
    }
done:
    {
        HINTERNET h = (HINTERNET)InterlockedExchangePointer((PVOID volatile *)hslot, NULL);
        if (h)
            WinHttpCloseHandle(h);
    }
    if (con)
        WinHttpCloseHandle(con);
    if (ses)
        WinHttpCloseHandle(ses);
    free(hd);
    free(body.p);
    free(buf.p);
    if (*cancel)
    {
        snprintf(err, es, "cancelled");
        free(content.p);
        return NULL;
    }
    {
        const char *s = sbs(&content), *e = strstr(s, "</think>"); /* reasoning_format "none": inline thinking */
        if (e)
            s = e + 8;
        while (*s && isspace((unsigned char)*s))
            s++;
        if (*s)
            out = xstrdup(s);
        else if (!err[0])
            snprintf(err, es, nreason ? "the model used its whole token budget on reasoning (finish_reason %s)" : "the model returned an empty summary (finish_reason %s)", fin[0] ? fin : "?");
    }
    free(content.p);
    return out;
}
static char *compact_summarize(const char *transcript, char *err, size_t es)
{
    SB t = {0};
    char *r;
    sb_puts(&t, "--- CONVERSATION ---\n");
    sb_puts(&t, transcript);
    sb_puts(&t, "\n--- END ---\nWrite the summary now.");
    r = llm_complete(SUMMARY_PROMPT, sbs(&t), 6144, -1, &g_creq, &g_ccancel, "Compressing older messages", err, es);
    free(t.p);
    return r;
}
static DWORD WINAPI compact_worker(LPVOID arg)
{
    CJob *j = (CJob *)arg;
    char err[400] = "";
    char *sum = compact_summarize(j->txt, err, sizeof err);
    if (sum)
    {
        SB m = {0};
        sb_printf(&m, "%llu\n%d\n", (unsigned long long)(size_t)j->c, j->keep);
        sb_puts(&m, sum);
        post_msg(10, sbs(&m));
        free(m.p);
        free(sum);
    }
    else
    { /* "!reason" = failed */
        char m[420];
        snprintf(m, sizeof m, "!%s", err[0] ? err : "unknown error");
        post_msg(10, m);
    }
    free(j->txt);
    free(j);
    return 0;
}
/* Stop, or a message sent while an early summary runs: close the request (the server stops generating) */
static void compact_cancel(void)
{
    HINTERNET h;
    if (!g_compacting)
        return;
    InterlockedExchange(&g_ccancel, 1);
    h = (HINTERNET)InterlockedExchangePointer((PVOID volatile *)&g_creq, NULL);
    if (h)
        WinHttpCloseHandle(h);
}
static void compact_start(Conv *c, int early)
{
    CJob *j;
    HANDLE h;
    int *p, n, cut, k;
    SB t = {0};
    size_t limit;
    if (g_compacting || !G.autocompact || !g_nctx)
        return;
    n = conv_path(c, &p);
    if (n < 8 || (g_clast_fail && c == g_clast_conv && n < g_clast_n + CRETRY))
    {
        free(p);
        return;
    }
    /* Cut before a user message so assistant tool_calls never get separated from their results.
       A long agent run may have no user message after the cut point: then cut before an assistant
       message instead - its tool results follow it, so the pairing still holds. */
    cut = n * 7 / 10;
    for (k = cut; k < n && c->n[p[k]].role != ROLE_USER; k++)
        ;
    if (k >= n)
        for (k = cut; k < n && c->n[p[k]].role != ROLE_ASSIST; k++)
            ;
    if (k >= n || k < 4)
    {
        free(p);
        return;
    }
    /* keep the request well inside the server context (half the window) */
    limit = (size_t)((G.ctx_limit > 0 ? G.ctx_limit : g_nctx) * g_cpt / 2);
    compact_transcript(&t, c, p, k, 6000);
    if (t.n > limit)
    {
        free(t.p);
        memset(&t, 0, sizeof t);
        compact_transcript(&t, c, p, k, 800);
    }
    if (t.n > limit)
    { /* many short messages: clip the whole transcript (keeps its start and its most recent part) */
        SB t2 = {0};
        compact_clip(&t2, t.p, limit);
        free(t.p);
        t = t2;
    }
    g_clast_conv = c;
    g_clast_n = n;
    j = (CJob *)calloc(1, sizeof *j);
    j->c = c;
    j->keep = p[k];
    j->txt = t.p ? t.p : xstrdup("");
    free(p);
    g_compacting = 1;
    g_compact_early = early;
    InterlockedExchange(&g_ccancel, 0);
    set_status(early ? "Preparing context compression while idle (sending a message cancels it) ..."
                     : "Compressing older messages to free context ...");
    h = CreateThread(NULL, 0, compact_worker, j, 0, NULL);
    if (h)
        CloseHandle(h);
    else
    {
        g_compacting = 0;
        free(j->txt);
        free(j);
    }
}
/* The context passed `threshold` tokens (est = estimated prompt size): tier 1 first; tier 2 only if the
   estimate is still above it. Returns 1 when a summary was started. */
static int compact_maybe(Conv *c, int est, int threshold, int early)
{
    size_t saved;
    if (g_compacting || !G.autocompact)
        return 0;
    saved = compact_elide(c);
    if (saved)
    {
        int tok = (int)(saved / g_cpt);
        char st[200];
        est -= tok;
        if (c == g_meas_conv && g_last_prompt_n > tok)
            g_last_prompt_n -= tok; /* keep est_tokens() in line with what the next request will send */
        snprintf(st, sizeof st, "Context: old tool results shortened (about %d tokens saved).", tok);
        set_status(st);
        save_all();
    }
    if (est <= threshold)
        return 0;
    compact_start(c, early);
    return g_compacting;
}

#endif /* LLAMAC_COMPACT_H */
