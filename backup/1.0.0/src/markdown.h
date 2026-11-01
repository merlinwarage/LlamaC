/* markdown.h - Markdown-ish inline/table/block renderer */
#ifndef LLAMAC_MARKDOWN_H
#define LLAMAC_MARKDOWN_H

/* ---------- markdown-ish text formatting ---------- */
static void md_inline(const char *s, size_t n, COLORREF col, DWORD base);
static void md_span(const char *s, size_t n, COLORREF col, DWORD eff) { md_inline(s, n, col, eff); }
static void md_inline(const char *s, size_t n, COLORREF col, DWORD base) {
    size_t i = 0, st = 0;
#define FLUSH() re_add(s + st, i - st, col, base, FACE)
    while (i < n) {
        char c = s[i];
        if (c == '`') {
            size_t j = i + 1; while (j < n && s[j] != '`') j++;
            if (j < n && j > i + 1) { FLUSH(); re_code(s + i + 1, j - i - 1, cCode, 0); i = j + 1; st = i; continue; }
        } else if ((c == '*' || c == '_') && i + 1 < n && s[i + 1] == c) {
            size_t j = i + 2; while (j + 1 < n && !(s[j] == c && s[j + 1] == c)) j++;
            if (j + 1 < n && j > i + 2) { FLUSH(); md_span(s + i + 2, j - i - 2, col, base | CFE_BOLD); i = j + 2; st = i; continue; }
        } else if (c == '~' && i + 1 < n && s[i + 1] == '~') {
            size_t j = i + 2; while (j + 1 < n && !(s[j] == '~' && s[j + 1] == '~')) j++;
            if (j + 1 < n && j > i + 2) { FLUSH(); md_span(s + i + 2, j - i - 2, cDim, base | CFE_STRIKEOUT); i = j + 2; st = i; continue; }
        } else if ((c == '*' || c == '_') && i + 1 < n && s[i + 1] != ' ' && (c == '*' || i == 0 || !StrChrA("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", s[i - 1]))) {
            size_t j = i + 1; while (j < n && s[j] != c) j++;
            if (j < n && j > i + 1 && s[j - 1] != ' ') { FLUSH(); md_span(s + i + 1, j - i - 1, col, base | CFE_ITALIC); i = j + 1; st = i; continue; }
        } else if (c == '[') {
            size_t j = i + 1; while (j < n && s[j] != ']') j++;
            if (j + 1 < n && s[j + 1] == '(') {
                size_t k = j + 2; while (k < n && s[k] != ')') k++;
                if (k < n) {
                    FLUSH(); md_span(s + i + 1, j - i - 1, cAccent, base | CFE_UNDERLINE);
                    re_add(" (", 2, cDim, 0, FACE); re_add(s + j + 2, k - j - 2, cDim, 0, FACE); re_add(")", 1, cDim, 0, FACE);
                    i = k + 1; st = i; continue;
                }
            }
        }
        i++;
    }
    FLUSH();
#undef FLUSH
}
static size_t u8len(const char *s, size_t n) { size_t c = 0; for (size_t i = 0; i < n; i++) if (((unsigned char)s[i] & 0xC0) != 0x80) c++; return c; }
typedef struct { const char *s; size_t l; } Cell;
#define MAXC 12
static void md_table(const char **ln, size_t *ll, int n) {
    Cell (*cells)[MAXC] = (Cell (*)[MAXC])calloc((size_t)n, sizeof *cells); int *nc = (int *)calloc((size_t)n, sizeof(int));
    char *isep = (char *)calloc((size_t)n, 1); size_t wd[MAXC] = {0}; int ncols = 0, hdr_sep = -1;
    for (int r = 0; r < n; r++) {
        const char *p = ln[r], *e = ln[r] + ll[r]; int allsep = 1, anydash = 0;
        while (p < e && (*p == ' ' || *p == '|')) { if (*p == '|') { p++; break; } p++; }
        while (p < e && nc[r] < MAXC) {
            const char *q = p; while (q < e && *q != '|') q++;
            const char *a = p, *b = q; while (a < b && (*a == ' ' || *a == '\t')) a++; while (b > a && (b[-1] == ' ' || b[-1] == '\t' || b[-1] == '\r')) b--;
            if (q == e && a == b) break;
            cells[r][nc[r]].s = a; cells[r][nc[r]].l = (size_t)(b - a); nc[r]++;
            for (const char *x = a; x < b; x++) { if (*x == '-') anydash = 1; else if (*x != ':') allsep = 0; }
            p = q < e ? q + 1 : e;
        }
        isep[r] = (char)(allsep && anydash && nc[r] > 0);
        if (isep[r] && hdr_sep < 0) hdr_sep = r;
        if (nc[r] > ncols) ncols = nc[r];
        if (!isep[r]) for (int c = 0; c < nc[r]; c++) { size_t L = u8len(cells[r][c].s, cells[r][c].l); if (L > wd[c]) wd[c] = L; }
    }
    for (int r = 0; r < n; r++) {
        SB o = {0};
        if (isep[r]) {
            sb_putc(&o, ' ');
            for (int c = 0; c < ncols; c++) { for (size_t k = 0; k < wd[c] + 2; k++) sb_puts(&o, HBAR); if (c + 1 < ncols) sb_puts(&o, CROSS); }
            sb_putc(&o, ' ');
            re_code(sbs(&o), o.n, cDim, 0);
        } else {
            sb_putc(&o, ' ');
            for (int c = 0; c < ncols; c++) {
                sb_putc(&o, ' ');
                if (c < nc[r]) sb_add(&o, cells[r][c].s, cells[r][c].l);
                size_t L = c < nc[r] ? u8len(cells[r][c].s, cells[r][c].l) : 0;
                for (size_t k = L; k < wd[c]; k++) sb_putc(&o, ' ');
                sb_putc(&o, ' '); if (c + 1 < ncols) sb_puts(&o, VBAR);
            }
            sb_putc(&o, ' ');
            re_code(sbs(&o), o.n, (hdr_sep > 0 && r < hdr_sep) ? cAccent : cFg, (hdr_sep > 0 && r < hdr_sep) ? CFE_BOLD : 0);
        }
        free(o.p); re_nl();
    }
    free(cells); free(nc); free(isep);
}
static void md_render(const char *s, COLORREF col) {
    const char *p = s;
    while (*p) {
        const char *e = strchr(p, '\n'); if (!e) e = p + strlen(p);
        size_t len = (size_t)(e - p), l = len; const char *q = p;
        while (l && (*q == ' ' || *q == '\t')) { q++; l--; }
        size_t indent = len - l; const char *next = *e ? e + 1 : e;
        if (l >= 3 && !strncmp(q, "```", 3)) {
            char lang[24]; size_t m = 0; const char *a = q + 3; size_t kq = l - 3;
            while (kq && *a == ' ') { a++; kq--; }
            while (m < kq && m < 23 && !isspace((unsigned char)a[m])) { lang[m] = (char)tolower((unsigned char)a[m]); m++; }
            lang[m] = 0;
            const char *body = *e ? e + 1 : e, *x = body, *bend = NULL;
            while (*x) {
                const char *xe = strchr(x, '\n'); if (!xe) xe = x + strlen(x);
                const char *y = x; while (y < xe && (*y == ' ' || *y == '\t')) y++;
                if (xe - y >= 3 && !strncmp(y, "```", 3)) { bend = x; next = *xe ? xe + 1 : xe; break; }
                x = *xe ? xe + 1 : xe;
            }
            if (!bend) { bend = x; next = x; }
            size_t bl = (size_t)(bend - body);
            if (bl && body[bl - 1] == '\n') bl--;
            if (bl && body[bl - 1] == '\r') bl--;
            code_block(lang, body, bl);
        }
        else if (l == 0) re_nl();
        else if (*q == '|') {
            const char *ls[200]; size_t lls[200]; int n = 0; const char *x = p;
            while (*x && n < 200) {
                const char *xe = strchr(x, '\n'); if (!xe) xe = x + strlen(x);
                const char *y = x; while (y < xe && (*y == ' ' || *y == '\t')) y++;
                if (y >= xe || *y != '|') break;
                ls[n] = y; lls[n] = (size_t)(xe - y); n++; x = *xe ? xe + 1 : xe;
            }
            md_table(ls, lls, n); next = x;
        }
        else if (*q == '#') {
            size_t h = 0; while (h < l && q[h] == '#') h++;
            if (h < l && q[h] == ' ' && h <= 6) {
                int sv = re_pt; re_pt = h == 1 ? 17 : h == 2 ? 14 : h == 3 ? 12 : 11;
                md_inline(q + h + 1, l - h - 1, cFg, CFE_BOLD); re_nl(); re_pt = sv;
            } else { md_inline(q, l, col, 0); re_nl(); }
        }
        else if (l >= 3 && (!strncmp(q, "---", 3) || !strncmp(q, "***", 3) || !strncmp(q, "___", 3)) && strspn(q, "-*_ \r") >= l) { re_rule(cBorder); }
        else if (*q == '>') {
            const char *r = q + 1; size_t rl = l - 1; if (rl && *r == ' ') { r++; rl--; }
            re_add("\xE2\x96\x8E ", 4, cAccent, 0, FACE); md_inline(r, rl, cDim, CFE_ITALIC); re_nl();
        }
        else if (l >= 2 && (q[0] == '-' || q[0] == '*' || q[0] == '+') && q[1] == ' ') {
            for (size_t k = 0; k < indent; k++) re_add(" ", 1, col, 0, FACE);
            re_add(indent >= 2 ? "\xE2\x97\xA6 " : BULLET " ", 4, cAccent, 0, FACE);
            md_inline(q + 2, l - 2, col, 0); re_nl();
        }
        else if (isdigit((unsigned char)*q)) {
            size_t d = 0; while (d < l && isdigit((unsigned char)q[d])) d++;
            if (d < l - 1 && (q[d] == '.' || q[d] == ')') && q[d + 1] == ' ') {
                for (size_t k = 0; k < indent; k++) re_add(" ", 1, col, 0, FACE);
                re_add(q, d + 1, cAccent, CFE_BOLD, FACE); re_add(" ", 1, col, 0, FACE); md_inline(q + d + 2, l - d - 2, col, 0); re_nl();
            } else { md_inline(p, len, col, 0); re_nl(); }
        }
        else { md_inline(p, len, col, 0); re_nl(); }
        if (!*e && next == e) break;
        p = next;
    }
}

#endif /* LLAMAC_MARKDOWN_H */
