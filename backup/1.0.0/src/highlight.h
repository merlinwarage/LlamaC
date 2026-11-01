/* highlight.h - Syntax highlighter and language tables */
#ifndef LLAMAC_HIGHLIGHT_H
#define LLAMAC_HIGHLIGHT_H

/* ---------- syntax highlighting (basic tokenizer: comments, strings, numbers, keywords, calls) ---------- */
#define LF_SLASH 1
#define LF_BLOCK 2
#define LF_HASH 4
#define LF_DASH 8
#define LF_HTML 16
#define LF_ICASE 32
#define LF_TRIPLE 64
#define LF_PREPROC 128
#define LF_JSONKEY 256
typedef struct { const char *names, *kw; unsigned flags; } Lang;
#define KW_C "if else for while do switch case break continue return goto default struct union enum typedef class interface extends implements " \
    "public private protected static const final void int long short char float double unsigned signed bool boolean true false null nullptr NULL " \
    "new delete this super try catch finally throw throws import export from package using namespace template typename virtual override inline " \
    "extern volatile sizeof let var function async await yield of in is as fn mut pub impl trait use mod match loop where self Self def func type " \
    "chan go defer range select string byte constexpr auto fun val when object abstract sealed readonly int8 int16 int32 int64 uint8 uint16 uint32 " \
    "uint64 size_t u8 u16 u32 u64 i8 i16 i32 i64 f32 f64 usize isize str lambda with not and or"
#define KW_PY "def class return if elif else for while in not and or is import from as with try except finally raise pass break continue lambda " \
    "yield None True False global nonlocal async await del assert self cls print match case"
#define KW_SH "if then else elif fi for do done while until case esac function in echo export local return exit cd set unset source alias " \
    "true false select time read printf test sudo apt git docker make cmake pip npm Write-Host param"
#define KW_SQL "select from where insert into values update set delete create table drop alter add index primary key foreign references join left right " \
    "inner outer full on group by order having limit offset as and or not null is in like between distinct union all case when then else end " \
    "count sum avg min max asc desc default unique constraint view database"
static const Lang LANGS[] = {
    { " c h cpp c++ cc hpp cxx cs csharp java js javascript jsx ts typescript tsx go golang rust rs kotlin kt swift php dart scala objc ", KW_C, LF_SLASH | LF_BLOCK | LF_PREPROC },
    { " python py python3 ", KW_PY, LF_HASH | LF_TRIPLE },
    { " bash sh shell zsh console powershell ps1 pwsh ", KW_SH, LF_HASH },
    { " yaml yml toml ini ruby rb dockerfile makefile r conf ", KW_PY, LF_HASH },
    { " json jsonc json5 ", "true false null", LF_SLASH | LF_BLOCK | LF_JSONKEY },
    { " sql mysql postgresql sqlite ", KW_SQL, LF_DASH | LF_BLOCK | LF_ICASE },
    { " html xml svg xhtml vue ", "", LF_HTML },
    { " css scss less ", "", LF_BLOCK },
};
static const Lang *find_lang(const char *lang) {
    char key[40];
    if (!lang || !*lang) return NULL;
    snprintf(key, sizeof key, " %s ", lang);
    for (size_t i = 0; i < sizeof LANGS / sizeof LANGS[0]; i++) if (strstr(LANGS[i].names, key)) return &LANGS[i];
    return NULL;
}
static int in_list(const char *list, const char *w, size_t n, int icase) {
    const char *p = list;
    while (*p) {
        while (*p == ' ') p++;
        const char *e = p; while (*e && *e != ' ') e++;
        if ((size_t)(e - p) == n && (icase ? _strnicmp(p, w, n) : strncmp(p, w, n)) == 0) return 1;
        p = e;
    }
    return 0;
}
static int has_lower(const char *s, size_t k) { for (size_t i = 0; i < k; i++) if (islower((unsigned char)s[i])) return 1; return 0; }
static void highlight(const char *s, size_t n, const char *lang) {
    const Lang *L = find_lang(lang);
    size_t i = 0, ps = 0, k;
    if (!L) { re_code(s, n, cFg, 0); return; }
#define PLAIN() do { if (i > ps) re_code(s + ps, i - ps, cFg, 0); } while (0)
#define EMIT(len, col, eff) do { PLAIN(); re_code(s + i, (len), (col), (eff)); i += (len); ps = i; } while (0)
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        if ((L->flags & LF_SLASH) && c == '/' && i + 1 < n && s[i + 1] == '/') { k = 0; while (i + k < n && s[i + k] != '\n') k++; EMIT(k, cCom, CFE_ITALIC); continue; }
        if ((L->flags & LF_HASH) && c == '#') { k = 0; while (i + k < n && s[i + k] != '\n') k++; EMIT(k, cCom, CFE_ITALIC); continue; }
        if ((L->flags & LF_DASH) && c == '-' && i + 1 < n && s[i + 1] == '-') { k = 0; while (i + k < n && s[i + k] != '\n') k++; EMIT(k, cCom, CFE_ITALIC); continue; }
        if ((L->flags & LF_BLOCK) && c == '/' && i + 1 < n && s[i + 1] == '*') {
            k = 2; while (i + k + 1 < n && !(s[i + k] == '*' && s[i + k + 1] == '/')) k++;
            k = (i + k + 1 < n) ? k + 2 : n - i; EMIT(k, cCom, CFE_ITALIC); continue;
        }
        if ((L->flags & LF_HTML) && c == '<') {
            if (i + 3 < n && !strncmp(s + i, "<!--", 4)) { k = 4; while (i + k + 2 < n && strncmp(s + i + k, "-->", 3)) k++; k = (i + k + 2 < n) ? k + 3 : n - i; EMIT(k, cCom, CFE_ITALIC); }
            else { k = 1; while (i + k < n && s[i + k] != '>') k++; if (i + k < n) k++; EMIT(k, cKw, 0); }
            continue;
        }
        if (c == '\'' && i > 0 && isalnum((unsigned char)s[i - 1])) { i++; continue; }
        if (c == '"' || c == '\'' || c == '`') {
            if ((L->flags & LF_TRIPLE) && i + 2 < n && s[i + 1] == (char)c && s[i + 2] == (char)c) {
                size_t j = i + 3; while (j + 2 < n && !(s[j] == (char)c && s[j + 1] == (char)c && s[j + 2] == (char)c)) j++;
                k = (j + 2 < n) ? j + 3 - i : n - i; EMIT(k, cStr, 0); continue;
            }
            size_t j = i + 1;
            while (j < n && s[j] != (char)c && (s[j] != '\n' || c == '`')) { if (s[j] == '\\' && j + 1 < n) j++; j++; }
            if (j < n && s[j] == (char)c) {
                k = j + 1 - i; COLORREF sc = cStr;
                if (L->flags & LF_JSONKEY) { size_t q = j + 1; while (q < n && (s[q] == ' ' || s[q] == '\t')) q++; if (q < n && s[q] == ':') sc = cFn; }
                EMIT(k, sc, 0); continue;
            }
            if (c == '"' ) { k = j - i; EMIT(k, cStr, 0); continue; }
            i++; continue;
        }
        if (isdigit(c) && (i == 0 || (!isalnum((unsigned char)s[i - 1]) && s[i - 1] != '_'))) {
            k = 1; while (i + k < n && (isalnum((unsigned char)s[i + k]) || s[i + k] == '.' || s[i + k] == '_')) k++;
            EMIT(k, cNum, 0); continue;
        }
        if ((L->flags & LF_PREPROC) && c == '#') {
            size_t j = i; while (j > 0 && (s[j - 1] == ' ' || s[j - 1] == '\t')) j--;
            if (j == 0 || s[j - 1] == '\n') { k = 1; while (i + k < n && isalpha((unsigned char)s[i + k])) k++; if (k > 1) { EMIT(k, cKw, 0); continue; } }
        }
        if (isalpha(c) || c == '_' || c == '$') {
            k = 1; while (i + k < n && (isalnum((unsigned char)s[i + k]) || s[i + k] == '_')) k++;
            if (in_list(L->kw, s + i, k, (L->flags & LF_ICASE) != 0)) EMIT(k, cKw, CFE_BOLD);
            else if (i + k < n && s[i + k] == '(') EMIT(k, cFn, 0);
            else if (isupper(c) && (L->flags & (LF_SLASH | LF_TRIPLE)) && !(L->flags & LF_JSONKEY) && has_lower(s + i, k)) EMIT(k, cType, 0);
            else i += k;
            continue;
        }
        i++;
    }
    PLAIN();
#undef PLAIN
#undef EMIT
}

#endif /* LLAMAC_HIGHLIGHT_H */
