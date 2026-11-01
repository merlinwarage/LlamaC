/* tools.h - Built-in file access tools for MCP
   
   This module provides file access capabilities that can be called by the LLM via MCP.
   Includes file reading/writing, directory operations, and file system queries.
*/

#ifndef LLAMAC_TOOLS_H
#define LLAMAC_TOOLS_H

/* File access tools */

/* Tool to read a file content */
static void tool_read_file(const char *path, char **content, size_t *size) {
    wchar_t *wpath = w_from(path);
    *content = NULL; *size = 0;
    
    HANDLE f = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart > 100LL * 1024 * 1024) {
        CloseHandle(f);
        return;
    }
    
    *content = (char *)malloc((size_t)sz.QuadPart + 1);
    if (!*content) {
        CloseHandle(f);
        return;
    }
    
    DWORD got = 0;
    ReadFile(f, *content, (DWORD)sz.QuadPart, &got, NULL);
    (*content)[got] = 0;
    *size = got;
    CloseHandle(f);
}

/* Tool to write content to a file */
static int tool_write_file(const char *path, const char *content, size_t size) {
    wchar_t *wpath = w_from(path);
    HANDLE f = CreateFileW(wpath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    
    DWORD written = 0;
    BOOL ok = WriteFile(f, content, (DWORD)size, &written, NULL);
    CloseHandle(f);
    
    return ok && written == size;
}

/* Tool to check if file exists */
static int tool_file_exists(const char *path) {
    wchar_t *wpath = w_from(path);
    HANDLE f = CreateFileW(wpath, 0, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    CloseHandle(f);
    return 1;
}

/* Tool to get file size in bytes */
static long long tool_get_file_size(const char *path) {
    wchar_t *wpath = w_from(path);
    HANDLE f = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return -1;
    
    LARGE_INTEGER sz;
    GetFileSizeEx(f, &sz);
    CloseHandle(f);
    return sz.QuadPart;
}

/* Tool to list directory contents */
static char *tool_list_dir(const char *path) {
    wchar_t *wpath = w_from(path);
    SB s = {0};
    
    // Build search pattern
    wchar_t search_pattern[MAX_PATH];
    _snwprintf(search_pattern, MAX_PATH, L"%ls\\*", wpath);
    search_pattern[MAX_PATH - 1] = 0;
    
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(search_pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        sb_puts(&s, "[]");
        free(wpath);
        return s.p;
    }
    
    sb_putc(&s, '[');
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        
        if (!sb.n) sb_putc(&s, '}');
        char *name = u_from(fd.cFileName);
        sb_puts(&s, "{\"name\":\""); sb_json(&s, name); sb_puts(&s, "\",\"is_dir\":"); sb_puts(&s, fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? "true" : "false");
        free(name);
        sb_putc(&s, '}');
        
        if (!FindNextFileW(hFind, &fd)) break;
    } while (1);
    
    FindClose(hFind);
    sb_putc(&s, ']');
    free(wpath);
    return s.p;
}

/* Tool to create directory */
static int tool_make_dir(const char *path) {
    wchar_t *wpath = w_from(path);
    return CreateDirectoryW(wpath, NULL) != 0;
}

/* Tool to delete file */
static int tool_delete_file(const char *path) {
    wchar_t *wpath = w_from(path);
    return DeleteFileW(wpath) != 0;
}

/* Tool to copy file */
static int tool_copy_file(const char *src, const char *dst) {
    wchar_t *wsrc = w_from(src);
    wchar_t *wdst = w_from(dst);
    int ok = CopyFileW(wsrc, wdst, FALSE) != 0;
    free(wsrc); free(wdst);
    return ok;
}

/* Tool to check if path is directory */
static int tool_is_dir(const char *path) {
    wchar_t *wpath = w_from(path);
    DWORD attr = GetFileAttributesW(wpath);
    free(wpath);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

#endif /* LLAMAC_TOOLS_H */