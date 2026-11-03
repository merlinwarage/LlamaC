/* tools_fs.h - File tools limited to one root folder: list, read, search, grep, glob, write, edit, move, delete, mkdir;
   plus the optional run_command shell tool (NOT sandboxed, always asks unless auto-approve is on) */
#ifndef LLAMAC_TOOLS_FS_H
#define LLAMAC_TOOLS_FS_H

static char *fs_err(const char *m)
{
    SB s = {0};
    sb_puts(&s, "Error: ");
    sb_puts(&s, m);
    return s.p;
}

static int fs_final(const wchar_t *p, wchar_t *out, DWORD cap)
{
    HANDLE h = CreateFileW(p, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    DWORD n = GetFinalPathNameByHandleW(h, out, cap, VOLUME_NAME_DOS);
    CloseHandle(h);
    if (!n || n >= cap)
        return 0;
    if (!wcsncmp(out, L"\\\\?\\UNC\\", 8))
    {
        wmemmove(out + 2, out + 8, wcslen(out + 8) + 1);
        out[0] = out[1] = L'\\';
    }
    else if (!wcsncmp(out, L"\\\\?\\", 4))
        wmemmove(out, out + 4, wcslen(out + 4) + 1);
    return 1;
}

static int fs_inside(const wchar_t *root, const wchar_t *p)
{
    size_t rl = wcslen(root);
    if (_wcsnicmp(root, p, rl) != 0)
        return 0;
    return p[rl] == 0 || p[rl] == L'\\' || (rl && root[rl - 1] == L'\\');
}

static void fs_trim(wchar_t *p)
{
    while (wcslen(p) > 3 && p[wcslen(p) - 1] == L'\\')
        p[wcslen(p) - 1] = 0;
}

static int fs_root(wchar_t *rfull, wchar_t *rfinal)
{
    wchar_t *w = w_from(G.fsroot);
    DWORD n = GetFullPathNameW(w, MAX_PATH, rfull, NULL);
    free(w);
    if (!n || n >= MAX_PATH)
        return 0;
    fs_trim(rfull);
    if (!fs_final(rfull, rfinal, MAX_PATH))
        return 0;
    fs_trim(rfinal);
    return 1;
}

static const char *fs_resolve(const char *rel, wchar_t *out)
{
    wchar_t rfull[MAX_PATH], rfinal[MAX_PATH], joined[MAX_PATH * 2], full[MAX_PATH], tmp[MAX_PATH], fin[MAX_PATH];
    if (!G.fsroot[0])
        return "the file tools root folder is not configured (Settings)";
    if (!fs_root(rfull, rfinal))
        return "the configured root folder does not exist";
    wchar_t *wp = w_from(rel && *rel ? rel : ".");
    if (PathIsRelativeW(wp))
        _snwprintf(joined, MAX_PATH * 2, L"%ls\\%ls", rfull, wp);
    else
        _snwprintf(joined, MAX_PATH * 2, L"%ls", wp);
    joined[MAX_PATH * 2 - 1] = 0;
    free(wp);
    DWORD n = GetFullPathNameW(joined, MAX_PATH, full, NULL);
    if (!n || n >= MAX_PATH)
        return "invalid or too long path";
    if (!fs_inside(rfull, full))
        return "path is outside the allowed root folder";
    wcscpy(tmp, full);
    for (;;)
    {
        if (fs_final(tmp, fin, MAX_PATH))
            break;
        size_t L = wcslen(tmp);
        wchar_t *sl = wcsrchr(tmp, L'\\');
        if (L <= 3 || !sl)
            return "path is not accessible";
        if (sl - tmp <= 2)
            tmp[3] = 0;
        else
            *sl = 0;
    }
    fs_trim(fin);
    if (!fs_inside(rfinal, fin))
        return "path resolves outside the allowed root folder (link)";
    wcscpy(out, full);
    return NULL;
}

static char *fs_rel(const wchar_t *full)
{
    wchar_t rfull[MAX_PATH], rfinal[MAX_PATH];
    const wchar_t *p = full;
    if (fs_root(rfull, rfinal) && fs_inside(rfull, full))
    {
        p = full + wcslen(rfull);
        while (*p == L'\\')
            p++;
    }
    if (!*p)
        return xstrdup(".");
    return u_from(p);
}

static void fs_mkdirs(wchar_t *d)
{
    for (wchar_t *p = d + 3; *p; p++)
        if (*p == L'\\')
        {
            *p = 0;
            CreateDirectoryW(d, NULL);
            *p = L'\\';
        }
    CreateDirectoryW(d, NULL);
}

static int fs_isdir(const wchar_t *p)
{
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static int fs_isfile(const wchar_t *p)
{
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static int fs_binary(const unsigned char *b, size_t len)
{
    for (size_t i = 0; i < len && i < 8192; i++)
        if (!b[i])
            return 1;
    return 0;
}

static char *fs_list(J *a)
{
    wchar_t p[MAX_PATH], pat[MAX_PATH + 4];
    const char *e = fs_resolve(j_str(j_get(a, "path")), p);
    if (e)
        return fs_err(e);
    if (!fs_isdir(p))
        return fs_err("not a directory");
    _snwprintf(pat, MAX_PATH + 4, L"%ls\\*", p);
    pat[MAX_PATH + 3] = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    SB s = {0};
    int cnt = 0;
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
                continue;
            if (cnt++ >= 500)
            {
                sb_puts(&s, "... (more entries not shown)\n");
                break;
            }
            char *n = u_from(fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                sb_puts(&s, "[dir] ");
                sb_puts(&s, n);
                sb_putc(&s, '\n');
            }
            else
            {
                char sz[48];
                snprintf(sz, sizeof sz, " (%llu bytes)\n", ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow);
                sb_puts(&s, "[file] ");
                sb_puts(&s, n);
                sb_puts(&s, sz);
            }
            free(n);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (!cnt)
        sb_puts(&s, "(empty directory)");
    return s.p ? s.p : xstrdup("");
}

static char *fs_read(J *a)
{
    wchar_t p[MAX_PATH];
    const char *e = fs_resolve(j_str(j_get(a, "path")), p);
    if (e)
        return fs_err(e);
    if (!fs_isfile(p))
        return fs_err("not a file");
    size_t len;
    unsigned char *b = read_file(p, &len);
    if (!b)
        return fs_err("cannot read the file (or it is larger than 100 MB)");
    if (fs_binary(b, len))
    {
        free(b);
        return fs_err("binary files are not supported");
    }
    char *t = (char *)b;
    if (len >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF)
        t += 3;
    int off = (int)j_num(j_get(a, "offset"), 1), lim = (int)j_num(j_get(a, "limit"), 0);
    const char *st = t, *en;
    if (off > 1)
    {
        int ln = 1;
        while (*st && ln < off)
        {
            if (*st == '\n')
                ln++;
            st++;
        }
    }
    if (lim > 0)
    {
        int k = 0;
        en = st;
        while (*en && k < lim)
        {
            if (*en == '\n')
                k++;
            en++;
        }
    }
    else
        en = st + strlen(st);
    size_t n = (size_t)(en - st);
    int trunc = 0;
    const size_t CAP = 100000;
    if (n > CAP)
    {
        n = CAP;
        trunc = 1;
        while (n && ((unsigned char)st[n] & 0xC0) == 0x80)
            n--;
    }
    SB s = {0};
    if (j_get(a, "line_numbers") && j_get(a, "line_numbers")->num != 0)
        tx_numbered(&s, st, n, off > 1 ? off : 1);
    else
        sb_add(&s, st, n);
    if (trunc)
        sb_puts(&s, "\n[truncated at 100000 characters; use offset/limit to read further]");
    else if (lim > 0 && *en)
        sb_puts(&s, "\n[more lines follow; use offset/limit to read further]");
    free(b);
    return s.p ? s.p : xstrdup("(empty file)");
}

typedef struct
{
    SB out;
    int matches, maxm, files, content;
    const char *q;
    const wchar_t *pat;
    wchar_t root[MAX_PATH];
} FsSearch;

static void fs_walk(FsSearch *S, const wchar_t *dir, int depth)
{
    wchar_t pat[MAX_PATH + 4];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    if (depth > 10 || S->matches >= S->maxm || S->files > 20000)
        return;
    _snwprintf(pat, MAX_PATH + 4, L"%ls\\*", dir);
    pat[MAX_PATH + 3] = 0;
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do
    {
        wchar_t full[MAX_PATH];
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            continue;
        if (_snwprintf(full, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName) < 0)
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (!_wcsicmp(fd.cFileName, L".git") || !_wcsicmp(fd.cFileName, L"node_modules"))
                continue;
            fs_walk(S, full, depth + 1);
            continue;
        }
        S->files++;
        if (!PathMatchSpecW(fd.cFileName, S->pat))
            continue;
        {
            char *rel = fs_rel(full);
            if (!S->content)
            {
                sb_puts(&S->out, rel);
                sb_putc(&S->out, '\n');
                S->matches++;
            }
            else if (fd.nFileSizeHigh == 0 && fd.nFileSizeLow <= 2u * 1024 * 1024)
            {
                size_t len;
                unsigned char *b = read_file(full, &len);
                if (b && !fs_binary(b, len))
                {
                    char *ln = (char *)b;
                    int no = 1;
                    while (*ln && S->matches < S->maxm)
                    {
                        char *nl = strchr(ln, '\n');
                        size_t L = nl ? (size_t)(nl - ln) : strlen(ln);
                        if (nl)
                            *nl = 0;
                        if (L && ln[L - 1] == '\r')
                            ln[L - 1] = 0;
                        if (StrStrIA(ln, S->q))
                        {
                            char nb[24];
                            size_t cut = strlen(ln);
                            if (cut > 200)
                            {
                                cut = 200;
                                while (cut && ((unsigned char)ln[cut] & 0xC0) == 0x80)
                                    cut--;
                            }
                            snprintf(nb, sizeof nb, ":%d: ", no);
                            sb_puts(&S->out, rel);
                            sb_puts(&S->out, nb);
                            sb_add(&S->out, ln, cut);
                            sb_putc(&S->out, '\n');
                            S->matches++;
                        }
                        if (!nl)
                            break;
                        ln = nl + 1;
                        no++;
                    }
                }
                free(b);
            }
            free(rel);
        }
    } while (FindNextFileW(h, &fd) && S->matches < S->maxm);
    FindClose(h);
}

static char *fs_search(J *a)
{
    wchar_t p[MAX_PATH], rfinal[MAX_PATH];
    const char *e = fs_resolve(j_str(j_get(a, "path")), p);
    if (e)
        return fs_err(e);
    if (!fs_isdir(p))
        return fs_err("not a directory");
    FsSearch S;
    memset(&S, 0, sizeof S);
    const char *pat = j_str(j_get(a, "pattern")), *q = j_str(j_get(a, "query"));
    wchar_t *wpat = w_from(pat && *pat ? pat : "*");
    S.pat = wpat;
    S.q = q;
    S.content = (q && *q);
    S.maxm = (int)j_num(j_get(a, "max_results"), 100);
    if (S.maxm < 1)
        S.maxm = 1;
    if (S.maxm > 500)
        S.maxm = 500;
    fs_root(S.root, rfinal);
    fs_walk(&S, p, 0);
    if (S.matches >= S.maxm)
        sb_puts(&S.out, "[result limit reached; narrow the search]\n");
    free(wpat);
    if (!S.out.n)
    {
        free(S.out.p);
        return xstrdup("No matches.");
    }
    return S.out.p;
}

#pragma comment(lib, "advapi32.lib")
static void fs_diag(const wchar_t *p, SB *o)
{
    HANDLE tok = NULL, h;
    DWORD len = 0, il = 0, perr = 0;
    TOKEN_ELEVATION el;
    wchar_t user[128] = L"?", dir[MAX_PATH], probe[MAX_PATH + 40];
    DWORD ul = 128;
    char *u8, b[300];
    int probe_ok;
    memset(&el, 0, sizeof el);
    GetUserNameW(user, &ul);
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok))
    {
        DWORD rl = 0;
        GetTokenInformation(tok, TokenElevation, &el, sizeof el, &rl);
        GetTokenInformation(tok, TokenIntegrityLevel, NULL, 0, &len);
        if (len)
        {
            TOKEN_MANDATORY_LABEL *tl = (TOKEN_MANDATORY_LABEL *)malloc(len);
            if (tl && GetTokenInformation(tok, TokenIntegrityLevel, tl, len, &len))
                il = *GetSidSubAuthority(tl->Label.Sid, (DWORD)(*GetSidSubAuthorityCount(tl->Label.Sid) - 1));
            free(tl);
        }
        CloseHandle(tok);
    }
    wcscpy(dir, p);
    {
        wchar_t *sl = wcsrchr(dir, L'\\');
        if (sl)
            *sl = 0;
    }
    _snwprintf(probe, MAX_PATH + 40, L"%ls\\~llamac_probe.tmp", dir);
    probe[MAX_PATH + 39] = 0;
    h = CreateFileW(probe, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    probe_ok = (h != INVALID_HANDLE_VALUE);
    if (probe_ok)
        CloseHandle(h);
    else
        perr = GetLastError();
    u8 = u_from(user);
    snprintf(b, sizeof b, " [diag: user=%s elevated=%lu integrity=0x%lX probe_file_in_folder=%s(%lu) target_attrs=0x%lX]",
             u8, (unsigned long)el.TokenIsElevated, (unsigned long)il, probe_ok ? "ok" : "FAILED", (unsigned long)perr, (unsigned long)GetFileAttributesW(p));
    free(u8);
    sb_puts(o, b);
}
static char *fs_write(J *a)
{
    const char *path = j_str(j_get(a, "path")), *content = j_str(j_get(a, "content"));
    wchar_t p[MAX_PATH], d[MAX_PATH];
    const char *e;
    int app = j_get(a, "append") && j_get(a, "append")->num != 0, ok;
    if (!path || !content)
        return fs_err("'path' and 'content' are required");
    if ((e = fs_resolve(path, p)) != NULL)
        return fs_err(e);
    if (fs_isdir(p))
    {
        char m[400];
        char *rel = fs_rel(p);
        snprintf(m, sizeof m, "path is a directory (%.250s). Append a filename, e.g. %.250s\\file.txt", rel, rel);
        free(rel);
        return fs_err(m);
    }
    wcscpy(d, p);
    {
        wchar_t *sl = wcsrchr(d, L'\\');
        if (sl)
        {
            *sl = 0;
            fs_mkdirs(d);
        }
    }
    if (app)
    {
        HANDLE f = CreateFileW(p, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD w = 0;
        ok = f != INVALID_HANDLE_VALUE && WriteFile(f, content, (DWORD)strlen(content), &w, NULL) && w == strlen(content);
        if (f != INVALID_HANDLE_VALUE)
            CloseHandle(f);
    }
    else
    {
        HANDLE f = CreateFileW(p, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD w = 0;
        ok = f != INVALID_HANDLE_VALUE && WriteFile(f, content, (DWORD)strlen(content), &w, NULL) && w == strlen(content);
        if (f != INVALID_HANDLE_VALUE)
            CloseHandle(f);
    }
    if (!ok)
    {
        DWORD err = GetLastError();
        SB m = {0};
        char t[120], *r;
        snprintf(t, sizeof t, "could not write the file (Win32 error %lu)", (unsigned long)err);
        sb_puts(&m, t);
        fs_diag(p, &m);
        r = fs_err(sbs(&m));
        free(m.p);
        return r;
    }
    {
        char *rel = fs_rel(p), msg[400];
        snprintf(msg, sizeof msg, "%s %llu bytes to %.250s", app ? "Appended" : "Wrote", (unsigned long long)strlen(content), rel);
        free(rel);
        return xstrdup(msg);
    }
}

static char *fs_crlf(const char *s)
{
    SB o = {0};
    for (; *s; s++)
    {
        if (*s == '\n' && (o.n == 0 || o.p[o.n - 1] != '\r'))
            sb_putc(&o, '\r');
        sb_putc(&o, *s);
    }
    return o.p ? o.p : xstrdup("");
}

static int fs_count(const char *h, const char *n)
{
    int c = 0;
    size_t nl = strlen(n);
    for (const char *p = h; (p = strstr(p, n)) != NULL; p += nl)
        c++;
    return c;
}

static char *fs_edit(J *a)
{
    const char *path = j_str(j_get(a, "path")), *oldt = j_str(j_get(a, "old_text")), *newt = j_str(j_get(a, "new_text"));
    wchar_t p[MAX_PATH];
    const char *e;
    int all = j_get(a, "replace_all") && j_get(a, "replace_all")->num != 0;
    if (!path || !oldt || !newt)
        return fs_err("'path', 'old_text' and 'new_text' are required");
    if (!*oldt)
        return fs_err("'old_text' must not be empty (use write_file to create files)");
    if ((e = fs_resolve(path, p)) != NULL)
        return fs_err(e);
    if (!fs_isfile(p))
        return fs_err("not a file");
    size_t len;
    unsigned char *b = read_file(p, &len);
    if (!b)
        return fs_err("cannot read the file");
    if (fs_binary(b, len))
    {
        free(b);
        return fs_err("binary files are not supported");
    }
    char *o2 = NULL, *n2 = NULL;
    const char *so = oldt, *sn = newt;
    int cnt = fs_count((char *)b, so);
    if (!cnt && strstr((char *)b, "\r\n") && strchr(oldt, '\n'))
    {
        o2 = fs_crlf(oldt);
        n2 = fs_crlf(newt);
        so = o2;
        sn = n2;
        cnt = fs_count((char *)b, so);
    }
    char *res = NULL;
    if (!cnt)
    {
        int hint = tx_first_line_hint((char *)b, oldt);
        char m[420];
        if (hint)
            snprintf(m, sizeof m, "old_text was not found. Its first line does occur at line %d, so a later line differs (check whitespace, indentation and line endings). Read that region with read_file and copy the text exactly.", hint);
        else
            snprintf(m, sizeof m, "old_text was not found: not even its first line exists in the file. Re-read the file with read_file (or use grep) and copy the text exactly, including whitespace and indentation.");
        res = fs_err(m);
    }
    else if (cnt > 1 && !all)
    {
        char m[200];
        snprintf(m, sizeof m, "old_text matches %d places; add more surrounding context or set replace_all", cnt);
        res = fs_err(m);
    }
    else
    {
        SB o = {0};
        size_t ol = strlen(so);
        const char *q = (char *)b, *hit;
        size_t first_off = (size_t)-1;
        while ((hit = strstr(q, so)) != NULL)
        {
            sb_add(&o, q, (size_t)(hit - q));
            if (first_off == (size_t)-1)
                first_off = o.n;
            sb_puts(&o, sn);
            q = hit + ol;
            if (!all)
                break;
        }
        sb_puts(&o, q);
        if (write_atomic(p, sbs(&o), o.n) || write_file(p, sbs(&o), o.n))
        {
            SB m = {0};
            char *rel = fs_rel(p);
            int line = tx_line_of(sbs(&o), first_off == (size_t)-1 ? 0 : first_off), added = 1;
            for (const char *z = sn; *z; z++)
                if (*z == '\n')
                    added++;
            if (added > 20)
                added = 20;
            sb_printf(&m, "Replaced %d occurrence(s) in ", all ? cnt : 1);
            sb_puts(&m, rel);
            sb_printf(&m, ". Result around line %d:\n", line);
            tx_snippet(&m, sbs(&o), line - 2, line + added + 1);
            free(rel);
            res = m.p;
        }
        else
        {
            DWORD err = GetLastError();
            char m[300];
            snprintf(m, sizeof m, "could not write the file (Win32 error %lu)", err);
            res = fs_err(m);
        }
        free(o.p);
    }
    free(b);
    free(o2);
    free(n2);
    return res;
}

static char *fs_mkdir(J *a)
{
    const char *path = j_str(j_get(a, "path"));
    wchar_t p[MAX_PATH];
    const char *e;
    if (!path)
        return fs_err("'path' is required");
    if ((e = fs_resolve(path, p)) != NULL)
        return fs_err(e);
    fs_mkdirs(p);
    return fs_isdir(p) ? xstrdup("Directory is available") : fs_err("could not create the directory");
}

/* ---------- recursive file walker, grep, glob ---------- */
typedef int (*FsFileCb)(const wchar_t *full, const WIN32_FIND_DATAW *fd, void *ctx);
static int fs_skipdir(const wchar_t *n)
{
    static const wchar_t *skip[] = {L".git", L".hg", L".svn", L"node_modules", L".vs", L"__pycache__"};
    for (size_t i = 0; i < sizeof skip / sizeof skip[0]; i++)
        if (!_wcsicmp(n, skip[i]))
            return 1;
    return 0;
}
/* calls cb for every regular file below dir; returns 0 when cb asked to stop or the file budget ran out */
static int fs_each(const wchar_t *dir, int depth, FsFileCb cb, void *ctx, int *budget)
{
    wchar_t pat[MAX_PATH + 4];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int go = 1;
    if (depth > 12)
        return 1;
    _snwprintf(pat, MAX_PATH + 4, L"%ls\\*", dir);
    pat[MAX_PATH + 3] = 0;
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 1;
    do
    {
        wchar_t full[MAX_PATH];
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            continue;
        if (_snwprintf(full, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName) < 0)
            continue;
        full[MAX_PATH - 1] = 0;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (fs_skipdir(fd.cFileName))
                continue;
            go = fs_each(full, depth + 1, cb, ctx, budget);
        }
        else if (--*budget < 0)
            go = 0;
        else
            go = cb(full, &fd, ctx);
    } while (go && FindNextFileW(h, &fd));
    FindClose(h);
    return go;
}

typedef struct
{
    SB out;
    Rx rx;
    wchar_t *inc;
    int ctx, maxm, matches, nfiles;
} FsGrep;
static int fs_grep_cb(const wchar_t *full, const WIN32_FIND_DATAW *fd, void *c)
{
    FsGrep *S = (FsGrep *)c;
    size_t len;
    unsigned char *b;
    if (g_stop)
        return 0;
    if (S->inc && !PathMatchSpecW(fd->cFileName, S->inc))
        return 1;
    if (fd->nFileSizeHigh || fd->nFileSizeLow > 2u * 1024 * 1024)
        return 1;
    b = read_file(full, &len);
    if (!b)
        return 1;
    if (!fs_binary(b, len))
    {
        char *rel = fs_rel(full);
        const char *t = (const char *)b;
        int m;
        if (len >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF)
        {
            t += 3;
            len -= 3;
        }
        m = tx_grep_buf(&S->out, rel, t, len, &S->rx, S->ctx, S->maxm - S->matches);
        if (m)
        {
            S->matches += m;
            S->nfiles++;
        }
        free(rel);
    }
    free(b);
    return S->matches < S->maxm;
}
static char *fs_grep(J *a)
{
    wchar_t p[MAX_PATH];
    const char *e, *pat = j_str(j_get(a, "pattern")), *inc = j_str(j_get(a, "include"));
    int lit = j_get(a, "literal") && j_get(a, "literal")->num != 0, ic = j_get(a, "ignore_case") && j_get(a, "ignore_case")->num != 0;
    int budget = 50000;
    FsGrep S;
    char *res, *esc = NULL;
    const char *use = pat;
    if (!pat || !*pat)
        return fs_err("'pattern' is required");
    if ((e = fs_resolve(j_str(j_get(a, "path")), p)) != NULL)
        return fs_err(e);
    if (!fs_isdir(p))
        return fs_err("not a directory");
    memset(&S, 0, sizeof S);
    if (lit)
    {
        SB x = {0};
        for (const char *q = pat; *q; q++)
        {
            if (strchr("\\.^$*+?()[]{}|", *q))
                sb_putc(&x, '\\');
            sb_putc(&x, *q);
        }
        esc = x.p;
        use = esc ? esc : "";
    }
    if (!rx_compile(&S.rx, use, ic))
    {
        char m[200];
        snprintf(m, sizeof m, "invalid pattern: %.120s", S.rx.err);
        rx_free(&S.rx);
        free(esc);
        return fs_err(m);
    }
    free(esc);
    S.inc = (inc && *inc) ? w_from(inc) : NULL;
    S.ctx = (int)j_num(j_get(a, "context"), 0);
    S.maxm = (int)j_num(j_get(a, "max_results"), 100);
    if (S.maxm < 1)
        S.maxm = 1;
    if (S.maxm > 500)
        S.maxm = 500;
    fs_each(p, 0, fs_grep_cb, &S, &budget);
    if (S.matches >= S.maxm)
        sb_puts(&S.out, "[result limit reached; narrow the pattern, path or include]\n");
    if (!S.out.n)
        res = xstrdup("No matches.");
    else
    {
        SB h = {0};
        sb_printf(&h, "%d matching line(s) in %d file(s)\n", S.matches, S.nfiles);
        sb_puts(&h, sbs(&S.out));
        res = h.p;
    }
    free(S.out.p);
    free(S.inc);
    rx_free(&S.rx);
    return res;
}

typedef struct
{
    SB out;
    char pat[512];
    wchar_t base[MAX_PATH];
    int n, maxm;
} FsGlob;
static int fs_glob_cb(const wchar_t *full, const WIN32_FIND_DATAW *fd, void *c)
{
    FsGlob *S = (FsGlob *)c;
    const wchar_t *r = full + wcslen(S->base);
    char *u;
    (void)fd;
    if (g_stop)
        return 0;
    while (*r == L'\\')
        r++;
    u = u_from(r);
    for (char *q = u; *q; q++)
        if (*q == '\\')
            *q = '/';
    if (glob_match(S->pat, u))
    {
        char *rel = fs_rel(full);
        sb_puts(&S->out, rel);
        sb_putc(&S->out, '\n');
        free(rel);
        S->n++;
    }
    free(u);
    return S->n < S->maxm;
}
static char *fs_glob(J *a)
{
    wchar_t p[MAX_PATH];
    const char *e, *pat = j_str(j_get(a, "pattern"));
    int budget = 50000;
    FsGlob S;
    char *res;
    if (!pat || !*pat)
        return fs_err("'pattern' is required");
    if (strlen(pat) >= sizeof S.pat)
        return fs_err("pattern is too long");
    if ((e = fs_resolve(j_str(j_get(a, "path")), p)) != NULL)
        return fs_err(e);
    if (!fs_isdir(p))
        return fs_err("not a directory");
    memset(&S, 0, sizeof S);
    strcpy(S.pat, pat);
    for (char *q = S.pat; *q; q++)
        if (*q == '\\')
            *q = '/';
    wcscpy(S.base, p);
    S.maxm = (int)j_num(j_get(a, "max_results"), 200);
    if (S.maxm < 1)
        S.maxm = 1;
    if (S.maxm > 500)
        S.maxm = 500;
    fs_each(p, 0, fs_glob_cb, &S, &budget);
    if (S.n >= S.maxm)
        sb_puts(&S.out, "[result limit reached; narrow the pattern]\n");
    if (!S.out.n)
    {
        free(S.out.p);
        return xstrdup("No matching files.");
    }
    res = S.out.p;
    return res;
}

/* ---------- move / delete ---------- */
static char *fs_move(J *a)
{
    const char *from = j_str(j_get(a, "from")), *to = j_str(j_get(a, "to")), *e;
    wchar_t pf[MAX_PATH], pt[MAX_PATH], d[MAX_PATH];
    if (!from || !to)
        return fs_err("'from' and 'to' are required");
    if ((e = fs_resolve(from, pf)) != NULL || (e = fs_resolve(to, pt)) != NULL)
        return fs_err(e);
    if (GetFileAttributesW(pf) == INVALID_FILE_ATTRIBUTES)
        return fs_err("source does not exist");
    if (GetFileAttributesW(pt) != INVALID_FILE_ATTRIBUTES)
        return fs_err("destination already exists");
    wcscpy(d, pt);
    {
        wchar_t *sl = wcsrchr(d, L'\\');
        if (sl)
        {
            *sl = 0;
            fs_mkdirs(d);
        }
    }
    if (!MoveFileExW(pf, pt, MOVEFILE_COPY_ALLOWED))
    {
        char m[120];
        snprintf(m, sizeof m, "could not move (Win32 error %lu)", (unsigned long)GetLastError());
        return fs_err(m);
    }
    return xstrdup("Moved.");
}
static char *fs_delete(J *a)
{
    const char *path = j_str(j_get(a, "path")), *e;
    wchar_t p[MAX_PATH];
    if (!path)
        return fs_err("'path' is required");
    if ((e = fs_resolve(path, p)) != NULL)
        return fs_err(e);
    if (!fs_isfile(p))
        return fs_err("not a file (folders cannot be deleted with this tool)");
    if (!DeleteFileW(p))
    {
        char m[120];
        snprintf(m, sizeof m, "could not delete (Win32 error %lu)", (unsigned long)GetLastError());
        return fs_err(m);
    }
    return xstrdup("Deleted.");
}

/* ---------- run_command ---------- */
static void fs_drain(HANDLE rd, SB *raw)
{
    DWORD avail = 0, got;
    char buf[8192];
    while (PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL) && avail)
    {
        if (!ReadFile(rd, buf, sizeof buf, &got, NULL) || !got)
            break;
        if (raw->n < 8u * 1024 * 1024) /* keep reading so the child never blocks, but cap memory */
            sb_add(raw, buf, got);
    }
}
static char *fs_run(J *a)
{
    const char *cmd = j_str(j_get(a, "command")), *e;
    wchar_t cwd[MAX_PATH], sys[MAX_PATH], exe[MAX_PATH], *wc, *cl;
    int tmo = (int)j_num(j_get(a, "timeout_seconds"), 60), timed_out = 0, cancelled = 0, utf8ok;
    SECURITY_ATTRIBUTES sa;
    HANDLE rd = NULL, wr = NULL, nul, job;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    SB raw = {0}, res = {0};
    DWORD t0, code = 0;
    size_t L;
    char *text;
    if (!G.shell)
        return fs_err("the shell tool is disabled in Settings");
    if (!cmd || !*cmd)
        return fs_err("'command' is required");
    if (strlen(cmd) > 8000)
        return fs_err("command is too long");
    if ((e = fs_resolve(j_str(j_get(a, "cwd")), cwd)) != NULL)
        return fs_err(e);
    if (!fs_isdir(cwd))
        return fs_err("cwd is not a directory");
    if (tmo < 1)
        tmo = 1;
    if (tmo > 600)
        tmo = 600;
    if (!GetSystemDirectoryW(sys, MAX_PATH))
        return fs_err("cannot locate the system folder");
    _snwprintf(exe, MAX_PATH, L"%ls\\cmd.exe", sys);
    exe[MAX_PATH - 1] = 0;
    wc = w_from(cmd);
    L = wcslen(wc) + 40;
    cl = (wchar_t *)malloc(L * sizeof(wchar_t));
    _snwprintf(cl, L, L"cmd.exe /d /s /c \"%ls\"", wc);
    cl[L - 1] = 0;
    free(wc);
    memset(&sa, 0, sizeof sa);
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&rd, &wr, &sa, 0))
    {
        free(cl);
        return fs_err("cannot create a pipe");
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = nul != INVALID_HANDLE_VALUE ? nul : NULL;
    si.hStdOutput = wr;
    si.hStdError = wr;
    memset(&pi, 0, sizeof pi);
    if (!CreateProcessW(exe, cl, NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, NULL, cwd, &si, &pi))
    {
        char m[160];
        snprintf(m, sizeof m, "cannot start cmd.exe (Win32 error %lu)", (unsigned long)GetLastError());
        CloseHandle(rd);
        CloseHandle(wr);
        if (nul != INVALID_HANDLE_VALUE)
            CloseHandle(nul);
        free(cl);
        return fs_err(m);
    }
    free(cl);
    job = CreateJobObjectW(NULL, NULL); /* the whole process tree dies with the job */
    if (job)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jl;
        memset(&jl, 0, sizeof jl);
        jl.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &jl, sizeof jl);
        AssignProcessToJobObject(job, pi.hProcess);
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(wr);
    if (nul != INVALID_HANDLE_VALUE)
        CloseHandle(nul);
    t0 = GetTickCount();
    for (;;)
    {
        int alive = WaitForSingleObject(pi.hProcess, 0) == WAIT_TIMEOUT;
        fs_drain(rd, &raw);
        if (!alive)
            break;
        if (g_stop)
        {
            cancelled = 1;
            break;
        }
        if (GetTickCount() - t0 > (DWORD)tmo * 1000u)
        {
            timed_out = 1;
            break;
        }
        Sleep(20);
    }
    if (timed_out || cancelled)
    {
        if (job)
            TerminateJobObject(job, 1);
        else
            TerminateProcess(pi.hProcess, 1);
    }
    WaitForSingleObject(pi.hProcess, 2000);
    fs_drain(rd, &raw);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    if (job)
        CloseHandle(job);
    CloseHandle(rd);

    /* console programs usually write the OEM code page; keep the bytes if they already are valid UTF-8 */
    utf8ok = raw.n == 0 || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw.p, (int)raw.n, NULL, 0) > 0;
    if (utf8ok)
        text = raw.p ? raw.p : xstrdup("");
    else
    {
        int wn = MultiByteToWideChar(CP_OEMCP, 0, raw.p, (int)raw.n, NULL, 0);
        wchar_t *w = (wchar_t *)malloc(((size_t)wn + 1) * sizeof(wchar_t));
        MultiByteToWideChar(CP_OEMCP, 0, raw.p, (int)raw.n, w, wn);
        w[wn] = 0;
        text = u_from(w);
        free(w);
        free(raw.p);
    }
    if (timed_out)
        sb_printf(&res, "[killed: timeout after %d s]\n", tmo);
    else if (cancelled)
        sb_puts(&res, "[killed: cancelled by the user]\n");
    sb_printf(&res, "exit code: %lu\n", (unsigned long)code);
    L = strlen(text);
    if (L > 52000)
    { /* build and test errors are at the end: keep the head and a long tail */
        size_t h = tx_cut(text, L, 10000), t = L - 40000;
        while (t < L && ((unsigned char)text[t] & 0xC0) == 0x80)
            t++;
        sb_add(&res, text, h);
        sb_printf(&res, "\n[... %llu bytes omitted ...]\n", (unsigned long long)(t - h));
        sb_puts(&res, text + t);
    }
    else
        sb_puts(&res, text);
    free(text);
    return res.p;
}

static char *fs_call(const char *tool, J *a)
{
    if (!strcmp(tool, "list_directory"))
        return fs_list(a);
    if (!strcmp(tool, "read_file"))
        return fs_read(a);
    if (!strcmp(tool, "search_files"))
        return fs_search(a);
    if (!strcmp(tool, "write_file"))
        return fs_write(a);
    if (!strcmp(tool, "edit_file"))
        return fs_edit(a);
    if (!strcmp(tool, "create_directory"))
        return fs_mkdir(a);
    if (!strcmp(tool, "grep"))
        return fs_grep(a);
    if (!strcmp(tool, "glob"))
        return fs_glob(a);
    if (!strcmp(tool, "move_path"))
        return fs_move(a);
    if (!strcmp(tool, "delete_file"))
        return fs_delete(a);
    if (!strcmp(tool, "run_command"))
        return fs_run(a);
    return fs_err("unknown file tool");
}

static void fs_register(void)
{
    tools_clear(0, -1);
    if (!G.tools_on || !G.fsroot[0])
        return;
    tool_add("list_directory", "List the files and folders in a directory inside the allowed root folder.",
             "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Directory relative to the root folder. Default: the root.\"}}}", 0, 0, "list_directory", 1, 0);
    tool_add("read_file", "Read a text file inside the allowed root folder. Optionally read only a range of lines. Set line_numbers to get numbered lines (never copy the numbers into edit_file).",
             "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"File path relative to the root folder\"},\"offset\":{\"type\":\"integer\",\"description\":\"First line to read (1-based)\"},\"limit\":{\"type\":\"integer\",\"description\":\"Maximum number of lines\"},\"line_numbers\":{\"type\":\"boolean\",\"description\":\"Prefix every line with its line number\"}},\"required\":[\"path\"]}", 0, 0, "read_file", 1, 0);
    tool_add("search_files", "Find files by name pattern and optionally search their text for a string (case-insensitive). For regular expressions use grep.",
             "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Start directory relative to the root folder. Default: the root.\"},\"pattern\":{\"type\":\"string\",\"description\":\"File name wildcard, e.g. *.c or *.h;*.c. Default: *\"},\"query\":{\"type\":\"string\",\"description\":\"Text to look for inside files. Omit to just list matching files.\"},\"max_results\":{\"type\":\"integer\",\"description\":\"Default 100, max 500\"}}}", 0, 0, "search_files", 1, 0);
    tool_add("grep", "Search file contents with a regular expression (supports . ^ $ * + ? {n,m} | ( ) [a-z] \\d \\w \\s \\b; no back-references). Returns path:line: text, with optional context lines. Skips .git and node_modules.",
             "{\"type\":\"object\",\"properties\":{\"pattern\":{\"type\":\"string\",\"description\":\"Regular expression\"},\"path\":{\"type\":\"string\",\"description\":\"Start directory relative to the root folder. Default: the root.\"},\"include\":{\"type\":\"string\",\"description\":\"File name wildcard(s), e.g. *.c;*.h\"},\"ignore_case\":{\"type\":\"boolean\",\"description\":\"Case-insensitive match. Default false.\"},\"literal\":{\"type\":\"boolean\",\"description\":\"Treat pattern as plain text, not a regex\"},\"context\":{\"type\":\"integer\",\"description\":\"Lines of context around each match, 0-5. Default 0.\"},\"max_results\":{\"type\":\"integer\",\"description\":\"Maximum matching lines. Default 100, max 500.\"}},\"required\":[\"pattern\"]}", 0, 0, "grep", 1, 0);
    tool_add("glob", "Find files by path pattern: * and ? match inside one folder name, ** matches any number of folders. Example: src/**/*.h. A pattern without / matches file names at any depth, e.g. *.c.",
             "{\"type\":\"object\",\"properties\":{\"pattern\":{\"type\":\"string\",\"description\":\"Glob pattern\"},\"path\":{\"type\":\"string\",\"description\":\"Start directory relative to the root folder. Default: the root.\"},\"max_results\":{\"type\":\"integer\",\"description\":\"Default 200, max 500\"}},\"required\":[\"pattern\"]}", 0, 0, "glob", 1, 0);
    if (G.fswrite)
    {
    tool_add("write_file", "Create or overwrite a text file (parent folders are created). Set append to add to the end instead. Prefer edit_file for changes to existing files.",
             "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"content\":{\"type\":\"string\"},\"append\":{\"type\":\"boolean\"}},\"required\":[\"path\",\"content\"]}", 0, 0, "write_file", 0, 0);
    tool_add("edit_file", "Replace an exact piece of text in an existing file. old_text must match exactly once (include enough surrounding lines to make it unique) unless replace_all is true. Returns the edited region so you can verify it.",
             "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"old_text\":{\"type\":\"string\"},\"new_text\":{\"type\":\"string\"},\"replace_all\":{\"type\":\"boolean\"}},\"required\":[\"path\",\"old_text\",\"new_text\"]}", 0, 0, "edit_file", 0, 0);
    tool_add("create_directory", "Create a directory (and missing parents) inside the root folder.",
             "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},\"required\":[\"path\"]}", 0, 0, "create_directory", 0, 0);
    tool_add("move_path", "Move or rename a file or folder inside the root folder. Fails if the destination exists.",
             "{\"type\":\"object\",\"properties\":{\"from\":{\"type\":\"string\"},\"to\":{\"type\":\"string\"}},\"required\":[\"from\",\"to\"]}", 0, 0, "move_path", 0, 0);
    tool_add("delete_file", "Delete one file inside the root folder (not folders).",
             "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},\"required\":[\"path\"]}", 0, 0, "delete_file", 0, 0);
    }
    if (G.shell)
    tool_add("run_command", "Run a Windows shell command (cmd.exe) in a folder inside the root folder and return its exit code and output. Use it to build, run tests, run git, etc. The command can reach outside the root folder. Long output keeps its beginning and its end.",
             "{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\",\"description\":\"Command line, e.g. cargo test or git status\"},\"cwd\":{\"type\":\"string\",\"description\":\"Start folder relative to the root folder. Default: the root.\"},\"timeout_seconds\":{\"type\":\"integer\",\"description\":\"Default 60, max 600. The process tree is killed on timeout.\"}},\"required\":[\"command\"]}", 0, 0, "run_command", 0, 0);
}

#endif /* LLAMAC_TOOLS_FS_H */
