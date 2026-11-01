/* storage.h - Data dir, settings and chat persistence */
#ifndef LLAMAC_STORAGE_H
#define LLAMAC_STORAGE_H

/* ---------- persistence ---------- */
static void data_path(const wchar_t *file, wchar_t *out) { _snwprintf(out, MAX_PATH, L"%ls\\%ls", dataDir, file); out[MAX_PATH - 1] = 0; }
static int dir_writable(const wchar_t *dir) {
    wchar_t t[MAX_PATH]; _snwprintf(t, MAX_PATH, L"%ls\\.probe", dir); t[MAX_PATH - 1] = 0;
    HANDLE f = CreateFileW(t, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    CloseHandle(f); return 1;
}
/* data folder: <exe folder>\data if writable, else %APPDATA%\llamac, else %TEMP%\llamac */
static void init_data_dir(void) {
    wchar_t base[MAX_PATH], p[MAX_PATH];
    GetModuleFileNameW(NULL, base, MAX_PATH);
    wchar_t *s = wcsrchr(base, L'\\'); if (s) *s = 0;
    _snwprintf(p, MAX_PATH, L"%ls\\data", base); p[MAX_PATH - 1] = 0;
    CreateDirectoryW(p, NULL);
    if (dir_writable(p)) { wcscpy(dataDir, p); return; }
    if (GetEnvironmentVariableW(L"APPDATA", base, MAX_PATH)) {
        _snwprintf(p, MAX_PATH, L"%ls\\llamac", base); p[MAX_PATH - 1] = 0;
        CreateDirectoryW(p, NULL);
        if (dir_writable(p)) { wcscpy(dataDir, p); return; }
    }
    GetTempPathW(MAX_PATH, base);
    _snwprintf(p, MAX_PATH, L"%ls\\llamac", base); p[MAX_PATH - 1] = 0;
    CreateDirectoryW(p, NULL);
    wcscpy(dataDir, p);
}
static void save_fail(const char *what) {
    char b[500]; DWORD e = GetLastError(); char *d = u_from(dataDir);
    snprintf(b, sizeof b, "Could not save %s (Windows error %lu) in %s", what, e, d);
    free(d); set_status(b);
}
static void cfg_save(void) {
    wchar_t p[MAX_PATH]; data_path(L"settings.v2.txt", p);
    char *t = cfg_serialize(&G);
    if (!write_file(p, t, strlen(t))) save_fail("settings");
    free(t);
}
static void save_all(void) {
    SB s = {0}; wchar_t p[MAX_PATH], tmp[MAX_PATH];
    sb_putc(&s, '[');
    for (int i = 0; i < nconv; i++) { if (i) sb_putc(&s, ','); conv_json(&s, convs[i]); }
    sb_putc(&s, ']');
    data_path(L"chats.json", p); data_path(L"chats.tmp", tmp);
    if (!write_file(tmp, sbs(&s), s.n) || !MoveFileExW(tmp, p, MOVEFILE_REPLACE_EXISTING)) {
        if (!write_file(p, sbs(&s), s.n)) save_fail("chats");
    }
    free(s.p);
}
static void convs_add(Conv *c) { convs = (Conv **)realloc(convs, (size_t)(nconv + 1) * sizeof *convs); convs[nconv++] = c; }
static void load_all(void) {
    wchar_t p[MAX_PATH]; size_t len; unsigned char *b;
    data_path(L"settings.v2.txt", p);
    if ((b = read_file(p, &len)) != NULL) { cfg_parse(&G, (char *)b); free(b); }
    data_path(L"chats.json", p);
    if ((b = read_file(p, &len)) != NULL) {
        J *r = json_parse((char *)b);
        if (r && r->t == J_ARR) for (J *e = r->child; e; e = e->next) { Conv *c = conv_from_json(e, 0); if (c) convs_add(c); }
        j_free(r); free(b);
    }
}

#endif /* LLAMAC_STORAGE_H */
