/* checkpoint.h - File checkpoints tied to the chat tree.
   Before a write tool changes a file, its previous content (or the fact that it did not exist) is saved under
   data\checkpoints\<chat id>\, keyed by the tool call id. Editing an earlier message (going back in the chat)
   can then restore every file the abandoned part of the conversation changed. */
#ifndef LLAMAC_CHECKPOINT_H
#define LLAMAC_CHECKPOINT_H

#define CP_MAX_FILE (20u * 1024 * 1024) /* larger files are not checkpointed */
/* set by the tool runner for the call being executed (write tools run one at a time) */
static char g_cp_conv[40], g_cp_tcid[96];
static CRITICAL_SECTION g_cp_cs;
static int g_cp_init;
static void cp_lock(void)
{
    if (!g_cp_init)
    {
        InitializeCriticalSection(&g_cp_cs);
        g_cp_init = 1;
    }
    EnterCriticalSection(&g_cp_cs);
}
static void cp_dir(const char *conv_id, wchar_t *out)
{
    wchar_t id[48];
    int k = 0;
    for (const char *p = conv_id; *p && k < 44; p++)
        id[k++] = (isalnum((unsigned char)*p) || *p == '-' || *p == '_') ? (wchar_t)*p : L'_';
    id[k] = 0;
    _snwprintf(out, MAX_PATH, L"%ls\\checkpoints\\%ls", dataDir, id);
    out[MAX_PATH - 1] = 0;
}
/* call before changing `path` (absolute). No-op outside a tool call. */
static void cp_record(const wchar_t *path)
{
    wchar_t dir[MAX_PATH], bin[MAX_PATH], idx[MAX_PATH], base[MAX_PATH];
    static unsigned seq;
    int existed;
    size_t len = 0;
    unsigned char *b = NULL;
    char name[64], *up;
    SB line = {0};
    if (!g_cp_conv[0] || !g_cp_tcid[0])
        return;
    existed = GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES && !(GetFileAttributesW(path) & FILE_ATTRIBUTE_DIRECTORY);
    if (existed)
    {
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (GetFileAttributesExW(path, GetFileExInfoStandard, &fa) && (fa.nFileSizeHigh || fa.nFileSizeLow > CP_MAX_FILE))
            return;
        b = read_file(path, &len);
        if (!b)
            return;
    }
    cp_lock();
    _snwprintf(base, MAX_PATH, L"%ls\\checkpoints", dataDir);
    base[MAX_PATH - 1] = 0;
    CreateDirectoryW(base, NULL);
    cp_dir(g_cp_conv, dir);
    CreateDirectoryW(dir, NULL);
    snprintf(name, sizeof name, "%llx-%x.bin", (unsigned long long)time(NULL), ++seq);
    {
        wchar_t *wn = w_from(name);
        _snwprintf(bin, MAX_PATH, L"%ls\\%ls", dir, wn);
        bin[MAX_PATH - 1] = 0;
        free(wn);
    }
    if (existed && !write_file(bin, (const char *)b, len))
    {
        LeaveCriticalSection(&g_cp_cs);
        free(b);
        return;
    }
    up = u_from(path);
    sb_puts(&line, g_cp_tcid);
    sb_putc(&line, '\t');
    sb_puts(&line, name);
    sb_printf(&line, "\t%d\t", existed);
    sb_puts(&line, up);
    sb_putc(&line, '\n');
    free(up);
    _snwprintf(idx, MAX_PATH, L"%ls\\index.txt", dir);
    idx[MAX_PATH - 1] = 0;
    {
        HANDLE f = CreateFileW(idx, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD w;
        if (f != INVALID_HANDLE_VALUE)
        {
            WriteFile(f, line.p, (DWORD)line.n, &w, NULL);
            CloseHandle(f);
        }
    }
    LeaveCriticalSection(&g_cp_cs);
    free(line.p);
    free(b);
}
typedef struct
{
    char *tcid, *bin, *path;
    int existed;
} CpEnt;
static int cp_load(const char *conv_id, CpEnt **out, char **raw)
{
    wchar_t dir[MAX_PATH], idx[MAX_PATH];
    size_t len;
    int n = 0, cap = 0;
    CpEnt *e = NULL;
    char *b;
    *out = NULL;
    *raw = NULL;
    cp_dir(conv_id, dir);
    _snwprintf(idx, MAX_PATH, L"%ls\\index.txt", dir);
    idx[MAX_PATH - 1] = 0;
    b = (char *)read_file(idx, &len);
    if (!b)
        return 0;
    for (char *ln = b; ln && *ln;)
    {
        char *nl = strchr(ln, '\n'), *f[4];
        int k = 0;
        if (nl)
            *nl = 0;
        f[0] = ln;
        for (char *q = ln; *q && k < 3; q++)
            if (*q == '\t')
            {
                *q = 0;
                f[++k] = q + 1;
            }
        if (k == 3)
        {
            if (n == cap)
            {
                cap = cap ? cap * 2 : 16;
                e = (CpEnt *)realloc(e, (size_t)cap * sizeof(CpEnt));
            }
            e[n].tcid = f[0];
            e[n].bin = f[1];
            e[n].existed = atoi(f[2]);
            e[n].path = f[3];
            n++;
        }
        ln = nl ? nl + 1 : NULL;
    }
    *out = e;
    *raw = b;
    return n;
}
static int cp_in(const char *tcid, char **ids, int nids)
{
    for (int i = 0; i < nids; i++)
        if (!strcmp(ids[i], tcid))
            return 1;
    return 0;
}
/* number of distinct files checkpointed by these tool calls */
static int cp_count(const char *conv_id, char **ids, int nids)
{
    CpEnt *e;
    char *raw;
    int n = cp_load(conv_id, &e, &raw), cnt = 0;
    for (int i = 0; i < n; i++)
        if (cp_in(e[i].tcid, ids, nids))
        {
            int dup = 0;
            for (int j = 0; j < i; j++)
                if (cp_in(e[j].tcid, ids, nids) && !_stricmp(e[j].path, e[i].path))
                    dup = 1;
            cnt += !dup;
        }
    free(e);
    free(raw);
    return cnt;
}
/* Restores the files changed by these tool calls (newest change undone first, so every file ends at its oldest
   saved state) and drops their entries. Returns the number of restore steps that failed. */
static int cp_restore(const char *conv_id, char **ids, int nids, int *done)
{
    wchar_t dir[MAX_PATH], idx[MAX_PATH];
    CpEnt *e;
    char *raw;
    int n = cp_load(conv_id, &e, &raw), bad = 0;
    SB keep = {0};
    *done = 0;
    cp_dir(conv_id, dir);
    for (int i = n - 1; i >= 0; i--)
    {
        wchar_t *wp, bin[MAX_PATH];
        if (!cp_in(e[i].tcid, ids, nids))
            continue;
        wp = w_from(e[i].path);
        {
            wchar_t *wb = w_from(e[i].bin);
            _snwprintf(bin, MAX_PATH, L"%ls\\%ls", dir, wb);
            bin[MAX_PATH - 1] = 0;
            free(wb);
        }
        if (e[i].existed)
        {
            size_t len;
            unsigned char *d = read_file(bin, &len);
            wchar_t par[MAX_PATH];
            wcsncpy(par, wp, MAX_PATH - 1);
            par[MAX_PATH - 1] = 0;
            {
                wchar_t *sl = wcsrchr(par, L'\\');
                if (sl)
                {
                    *sl = 0;
                    for (wchar_t *q = par + 3; *q; q++) /* create missing parents */
                        if (*q == L'\\')
                        {
                            *q = 0;
                            CreateDirectoryW(par, NULL);
                            *q = L'\\';
                        }
                    CreateDirectoryW(par, NULL);
                }
            }
            if (d && write_file(wp, (const char *)d, len))
                (*done)++;
            else
                bad++;
            free(d);
        }
        else if (DeleteFileW(wp) || GetLastError() == ERROR_FILE_NOT_FOUND)
            (*done)++;
        else
            bad++;
        DeleteFileW(bin);
        free(wp);
    }
    for (int i = 0; i < n; i++)
        if (!cp_in(e[i].tcid, ids, nids))
            sb_printf(&keep, "%s\t%s\t%d\t%s\n", e[i].tcid, e[i].bin, e[i].existed, e[i].path);
    _snwprintf(idx, MAX_PATH, L"%ls\\index.txt", dir);
    idx[MAX_PATH - 1] = 0;
    if (keep.n)
        write_atomic(idx, keep.p, keep.n);
    else
        DeleteFileW(idx);
    free(keep.p);
    free(e);
    free(raw);
    return bad;
}
/* chat deleted: drop its checkpoints */
static void cp_delete_conv(const char *conv_id)
{
    wchar_t dir[MAX_PATH], pat[MAX_PATH], f[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    cp_dir(conv_id, dir);
    _snwprintf(pat, MAX_PATH, L"%ls\\*", dir);
    pat[MAX_PATH - 1] = 0;
    h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            _snwprintf(f, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName);
            f[MAX_PATH - 1] = 0;
            DeleteFileW(f);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir);
}

#endif /* LLAMAC_CHECKPOINT_H */
