/* win_util.h - Win32 helpers: UTF-8/UTF-16, file IO, base64 */
#ifndef LLAMAC_WIN_UTIL_H
#define LLAMAC_WIN_UTIL_H

/* ---------- helpers ---------- */
static wchar_t *w_from(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = (wchar_t *)malloc((size_t)n * 2); MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n); return w;
}
static char *u_from(const wchar_t *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *s = (char *)malloc((size_t)n); WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL); return s;
}
static char *get_text(HWND h) {
    int n = GetWindowTextLengthW(h);
    wchar_t *w = (wchar_t *)malloc((size_t)(n + 1) * 2); GetWindowTextW(h, w, n + 1);
    char *u = u_from(w); free(w); return u;
}
static HWND B(int id) { return GetDlgItem(hMain, id); }
static void set_status(const char *s) { wchar_t *w = w_from(s); SetWindowTextW(hStatus, w); free(w); }
static long long now_ms(void) { return (long long)time(NULL) * 1000; }
static unsigned char *read_file(const wchar_t *path, size_t *len) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    LARGE_INTEGER sz; GetFileSizeEx(f, &sz);
    if (sz.QuadPart > 100LL * 1024 * 1024) { CloseHandle(f); return NULL; }
    unsigned char *b = (unsigned char *)malloc((size_t)sz.QuadPart + 1); DWORD got = 0;
    ReadFile(f, b, (DWORD)sz.QuadPart, &got, NULL); CloseHandle(f);
    b[got] = 0; *len = got; return b;
}
static int write_file(const wchar_t *path, const char *d, size_t len) {
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD w = 0; BOOL ok = WriteFile(f, d, (DWORD)len, &w, NULL); CloseHandle(f); return ok && w == len;
}
static char *b64(const unsigned char *d, size_t len) {
    DWORD n = 0;
    CryptBinaryToStringA(d, (DWORD)len, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &n);
    char *s = (char *)malloc(n + 1);
    CryptBinaryToStringA(d, (DWORD)len, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, s, &n); s[n] = 0; return s;
}

#endif /* LLAMAC_WIN_UTIL_H */
