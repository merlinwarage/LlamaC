/* storage.h - Data dir, settings (API key protected with DPAPI) and per-chat persistence */
#ifndef LLAMAC_STORAGE_H
#define LLAMAC_STORAGE_H

/* ---------- persistence ---------- */
static void data_path(const wchar_t *file, wchar_t *out)
{
    _snwprintf(out, MAX_PATH, L"%ls\\%ls", dataDir, file);
    out[MAX_PATH - 1] = 0;
}
static int dir_writable(const wchar_t *dir)
{
    wchar_t t[MAX_PATH];
    _snwprintf(t, MAX_PATH, L"%ls\\.probe", dir);
    t[MAX_PATH - 1] = 0;
    HANDLE f = CreateFileW(t, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return 0;
    CloseHandle(f);
    return 1;
}
/* data folder: <exe folder>\data if writable, else %APPDATA%\llamac, else %TEMP%\llamac */
static void init_data_dir(void)
{
    wchar_t base[MAX_PATH], p[MAX_PATH];
    GetModuleFileNameW(NULL, base, MAX_PATH);
    wchar_t *s = wcsrchr(base, L'\\');
    if (s)
        *s = 0;
    _snwprintf(p, MAX_PATH, L"%ls\\data", base);
    p[MAX_PATH - 1] = 0;
    CreateDirectoryW(p, NULL);
    if (dir_writable(p))
    {
        wcscpy(dataDir, p);
        return;
    }
    if (GetEnvironmentVariableW(L"APPDATA", base, MAX_PATH))
    {
        _snwprintf(p, MAX_PATH, L"%ls\\llamac", base);
        p[MAX_PATH - 1] = 0;
        CreateDirectoryW(p, NULL);
        if (dir_writable(p))
        {
            wcscpy(dataDir, p);
            return;
        }
    }
    GetTempPathW(MAX_PATH, base);
    _snwprintf(p, MAX_PATH, L"%ls\\llamac", base);
    p[MAX_PATH - 1] = 0;
    CreateDirectoryW(p, NULL);
    wcscpy(dataDir, p);
}
static void save_fail(const char *what)
{
    char b[500];
    DWORD e = GetLastError();
    char *d = u_from(dataDir);
    snprintf(b, sizeof b, "Could not save %s (Windows error %lu) in %s", what, e, d);
    free(d);
    set_status(b);
}
/* write <path>.tmp, then replace <path>: a crash or full disk never leaves a half-written file */
static int write_atomic(const wchar_t *path, const char *d, size_t n)
{
    wchar_t tmp[MAX_PATH + 8];
    _snwprintf(tmp, MAX_PATH + 8, L"%ls.tmp", path);
    tmp[MAX_PATH + 7] = 0;
    if (!write_file(tmp, d, n))
    {
        DeleteFileW(tmp);
        return 0;
    }
    if (MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return 1;
    DeleteFileW(tmp);
    return 0;
}

/* ---------- API key: Windows DPAPI (bound to this Windows user; useless if the data folder is copied elsewhere) ---------- */
static const char DPAPI_ENT[] = "llamac-key-v1";
static char *dpapi_enc(const char *plain)
{
    DATA_BLOB in, ent, out;
    char *s;
    in.pbData = (BYTE *)plain;
    in.cbData = (DWORD)strlen(plain);
    ent.pbData = (BYTE *)DPAPI_ENT;
    ent.cbData = (DWORD)(sizeof DPAPI_ENT - 1);
    if (!CryptProtectData(&in, L"llamac API key", &ent, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return NULL;
    s = b64(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return s;
}
static int dpapi_dec(const char *text, char *dst, size_t cap)
{
    DWORD n = 0;
    BYTE *bin;
    DATA_BLOB in, ent, out;
    int ok;
    size_t m;
    if (!CryptStringToBinaryA(text, 0, CRYPT_STRING_BASE64, NULL, &n, NULL, NULL) || !n)
        return 0;
    bin = (BYTE *)malloc(n);
    if (!CryptStringToBinaryA(text, 0, CRYPT_STRING_BASE64, bin, &n, NULL, NULL))
    {
        free(bin);
        return 0;
    }
    in.pbData = bin;
    in.cbData = n;
    ent.pbData = (BYTE *)DPAPI_ENT;
    ent.cbData = (DWORD)(sizeof DPAPI_ENT - 1);
    ok = CryptUnprotectData(&in, NULL, &ent, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out);
    free(bin);
    if (!ok)
        return 0;
    m = out.cbData < cap - 1 ? out.cbData : cap - 1;
    memcpy(dst, out.pbData, m);
    dst[m] = 0;
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return 1;
}
static void cfg_save(void)
{
    wchar_t p[MAX_PATH];
    Cfg *g2 = (Cfg *)malloc(sizeof *g2);
    char *t;
    SB o = {0};
    data_path(L"settings.v2.txt", p);
    memcpy(g2, &G, sizeof *g2);
    g2->key[0] = 0; /* never written in clear text */
    t = cfg_serialize(g2);
    free(g2);
    sb_puts(&o, t);
    free(t);
    if (G.key[0])
    {
        char *e = dpapi_enc(G.key);
        if (e)
        {
            sb_puts(&o, "keyenc=");
            sb_puts(&o, e);
            sb_putc(&o, '\n');
            free(e);
        }
        else
            set_status("Could not encrypt the API key with Windows DPAPI - it was NOT saved");
    }
    if (!write_atomic(p, sbs(&o), o.n))
        save_fail("settings");
    free(o.p);
}

/* ---------- chats: one file per conversation (data\chats\<id>.json); only changed chats are rewritten ---------- */
static int g_legacy; /* chats.json (single-file format of <= 1.5.x) was loaded and still has to be migrated */
static void chats_dir(wchar_t *out)
{
    _snwprintf(out, MAX_PATH, L"%ls\\chats", dataDir);
    out[MAX_PATH - 1] = 0;
}
static void conv_file(const Conv *c, wchar_t *out)
{
    wchar_t id[48];
    int k = 0;
    for (const char *p = c->id; *p && k < 44; p++)
        id[k++] = (isalnum((unsigned char)*p) || *p == '-' || *p == '_') ? (wchar_t)*p : L'_';
    id[k] = 0;
    _snwprintf(out, MAX_PATH, L"%ls\\chats\\%ls.json", dataDir, id);
    out[MAX_PATH - 1] = 0;
}
static int save_one(Conv *c)
{
    wchar_t p[MAX_PATH], d[MAX_PATH];
    SB s = {0};
    int ok;
    chats_dir(d);
    CreateDirectoryW(d, NULL);
    conv_json(&s, c);
    conv_file(c, p);
    ok = write_atomic(p, sbs(&s), s.n);
    free(s.p);
    if (ok)
        c->dirty = 0;
    return ok;
}
static void conv_file_delete(const Conv *c)
{
    wchar_t p[MAX_PATH];
    conv_file(c, p);
    DeleteFileW(p);
}
/* The chat being viewed and the chat being generated are the only ones that can have changed. */
static void save_all(void)
{
    int fail = 0;
    if (cur)
        cur->dirty = 1;
    if (genConv)
        genConv->dirty = 1;
    for (int i = 0; i < nconv; i++)
        if (convs[i]->dirty && !save_one(convs[i]))
            fail = 1;
    if (fail)
    {
        save_fail("chats");
        return;
    }
    if (g_legacy)
    { /* every chat now has its own file: retire the old single file (kept as a backup) */
        wchar_t a[MAX_PATH], b[MAX_PATH];
        data_path(L"chats.json", a);
        data_path(L"chats.json.bak", b);
        if (MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING))
            g_legacy = 0;
    }
}
static void convs_add(Conv *c)
{
    convs = (Conv **)realloc(convs, (size_t)(nconv + 1) * sizeof *convs);
    convs[nconv++] = c;
    c->dirty = 1;
}
static void load_conv_json(const char *text)
{
    J *r = json_parse(text);
    Conv *c = r ? conv_from_json(r, 0) : NULL;
    if (c)
        convs_add(c);
    j_free(r);
}
static void load_all(void)
{
    wchar_t p[MAX_PATH], pat[MAX_PATH];
    size_t len;
    unsigned char *b;
    int migrate_key = 0;
    data_path(L"settings.v2.txt", p);
    if ((b = read_file(p, &len)) != NULL)
    {
        char enc[2048] = "";
        char *k = strstr((char *)b, "keyenc=");
        while (k && k != (char *)b && k[-1] != '\n')
            k = strstr(k + 1, "keyenc=");
        if (k)
        {
            size_t n = strcspn(k + 7, "\r\n");
            if (n < sizeof enc)
            {
                memcpy(enc, k + 7, n);
                enc[n] = 0;
            }
        }
        cfg_parse(&G, (char *)b); /* destroys the buffer: 'enc' was copied first */
        if (enc[0])
        {
            if (!dpapi_dec(enc, G.key, sizeof G.key))
                G.key[0] = 0; /* other Windows user or machine: the key has to be entered again */
        }
        else if (G.key[0])
            migrate_key = 1; /* plain-text key from an older version: re-save it encrypted */
        free(b);
    }
    if (migrate_key)
        cfg_save();

    chats_dir(pat);
    wcscat(pat, L"\\*.json");
    {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(pat, &fd);
        if (h != INVALID_HANDLE_VALUE)
        {
            do
            {
                wchar_t fp[MAX_PATH];
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    continue;
                _snwprintf(fp, MAX_PATH, L"%ls\\chats\\%ls", dataDir, fd.cFileName);
                fp[MAX_PATH - 1] = 0;
                if ((b = read_file(fp, &len)) != NULL)
                {
                    int before = nconv;
                    load_conv_json((char *)b);
                    if (nconv > before)
                        convs[nconv - 1]->dirty = 0;
                    free(b);
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
    data_path(L"chats.json", p);
    if (!nconv && (b = read_file(p, &len)) != NULL)
    { /* first start after updating from the single-file format */
        J *r = json_parse((char *)b);
        if (r && r->t == J_ARR)
        {
            for (J *e = r->child; e; e = e->next)
            {
                Conv *c = conv_from_json(e, 0);
                if (c)
                    convs_add(c); /* dirty: gets its own file on the first save */
            }
            g_legacy = 1;
        }
        j_free(r);
        free(b);
    }
}

#endif /* LLAMAC_STORAGE_H */
