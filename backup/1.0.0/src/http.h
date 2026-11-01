/* http.h - WinHTTP URL parsing and requests */
#ifndef LLAMAC_HTTP_H
#define LLAMAC_HTTP_H

/* ---------- HTTP ---------- */
typedef struct { wchar_t host[256], path[1024]; INTERNET_PORT port; int secure; } Url;
static int url_parse(const char *base, const char *ep, Url *u) {
    char full[700]; size_t bl = strlen(base);
    while (bl && base[bl - 1] == '/') bl--;
    snprintf(full, sizeof full, "%s%.*s%s", strstr(base, "://") ? "" : "http://", (int)bl, base, ep);
    wchar_t *w = w_from(full); URL_COMPONENTS uc; memset(&uc, 0, sizeof uc);
    uc.dwStructSize = sizeof uc; uc.lpszHostName = u->host; uc.dwHostNameLength = 256; uc.lpszUrlPath = u->path; uc.dwUrlPathLength = 1024;
    BOOL ok = WinHttpCrackUrl(w, 0, 0, &uc); free(w);
    if (!ok) return 0;
    u->port = uc.nPort; u->secure = (uc.nScheme == INTERNET_SCHEME_HTTPS); return 1;
}
static wchar_t *auth_headers(const char *key, int json) {
    SB h = {0};
    if (json) sb_puts(&h, "Content-Type: application/json\r\n");
    if (key[0]) { sb_puts(&h, "Authorization: Bearer "); sb_puts(&h, key); sb_puts(&h, "\r\n"); }
    wchar_t *w = w_from(sbs(&h)); free(h.p); return w;
}
static char *http_req(const wchar_t *method, const char *ep, const char *body) {
    Url u; if (!url_parse(G.url, ep, &u)) return NULL;
    HINTERNET ses = WinHttpOpen(L"llamac/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0), con = NULL, req = NULL;
    SB out = {0}; int ok = 0; wchar_t *hd = auth_headers(G.key, body != NULL); DWORD bl = body ? (DWORD)strlen(body) : 0;
    if (!ses) { free(hd); return NULL; }
    WinHttpSetTimeouts(ses, 1500, 1500, 5000, 15000);
    con = WinHttpConnect(ses, u.host, u.port, 0);
    if (con) req = WinHttpOpenRequest(con, method, u.path, NULL, NULL, NULL, u.secure ? WINHTTP_FLAG_SECURE : 0);
    if (req && WinHttpSendRequest(req, hd, (DWORD)-1, (LPVOID)body, bl, bl, 0) && WinHttpReceiveResponse(req, NULL)) {
        DWORD st = 0, sz = sizeof st, got; char tmp[4096];
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &st, &sz, NULL);
        while (WinHttpReadData(req, tmp, sizeof tmp, &got) && got) sb_add(&out, tmp, got);
        ok = (st == 200);
    }
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses); free(hd);
    if (!ok) { free(out.p); return NULL; }
    if (!out.p) return xstrdup("");
    return out.p;
}
static void refresh_models(void) {
    char *b; J *r; int sel = -1, cnt = 0; char info[600];
    SendMessageW(hModel, CB_RESETCONTENT, 0, 0);
    memset(srvdef, 0, sizeof srvdef);
    b = http_req(L"GET", "/v1/models", NULL);
    if (!b) { set_status("Server not reachable - check Settings > Server URL (and API key)"); return; }
    r = json_parse(b); free(b);
    J *d = j_get(r, "data");
    for (J *m = d ? d->child : NULL; m; m = m->next) {
        const char *id = j_str(j_get(m, "id")); if (!id) continue;
        wchar_t *w = w_from(id); int idx = (int)SendMessageW(hModel, CB_ADDSTRING, 0, (LPARAM)w); free(w);
        if (!strcmp(id, G.model)) sel = idx;
        cnt++;
    }
    if (sel < 0 && cnt > 0) {
        sel = 0; wchar_t w[160]; SendMessageW(hModel, CB_GETLBTEXT, 0, (LPARAM)w);
        char *u = u_from(w); strncpy(G.model, u, sizeof G.model - 1); free(u);
    }
    if (sel >= 0) SendMessageW(hModel, CB_SETCURSEL, (WPARAM)sel, 0);
    j_free(r);
    snprintf(info, sizeof info, "Connected  |  %d model(s)", cnt);
    b = http_req(L"GET", "/props", NULL);
    if (b) {
        r = json_parse(b);
        double nctx = j_num(j_get(j_get(r, "default_generation_settings"), "n_ctx"), 0);
        J *mod = j_get(r, "modalities");
        int vis = mod && j_get(mod, "vision") && j_get(mod, "vision")->num != 0;
        int aud = mod && j_get(mod, "audio") && j_get(mod, "audio")->num != 0;
        const char *tp = j_str(j_get(r, "chat_template")); setstr(&g_tmpl, tp ? tp : "");
        srv_parse(r, srvdef);
        snprintf(info + strlen(info), sizeof info - strlen(info), "  |  n_ctx %d%s%s", (int)nctx, vis ? "  |  vision" : "", aud ? "  |  audio" : "");
        snprintf(info + strlen(info), sizeof info - strlen(info), "  |  server defaults: temp %s, top_k %s, top_p %s, min_p %s, repeat %s",
                 srvdef[fidx("temp")][0] ? srvdef[fidx("temp")] : "?", srvdef[fidx("top_k")][0] ? srvdef[fidx("top_k")] : "?",
                 srvdef[fidx("top_p")][0] ? srvdef[fidx("top_p")] : "?", srvdef[fidx("min_p")][0] ? srvdef[fidx("min_p")] : "?",
                 srvdef[fidx("rep")][0] ? srvdef[fidx("rep")] : "?");
        j_free(r); free(b);
    }
    set_status(info);
}

#endif /* LLAMAC_HTTP_H */
