/* skills.h - Agent Skills: folders with a SKILL.md (YAML front matter "name" and "description", then instructions)
   and optional companion files. Found in <file-tools root>\.agents\skills\<name>\ (project) and
   %USERPROFILE%\.agents\skills\<name>\ (user); a project skill hides a user skill of the same name.
   Skills are opt-in: the ones ticked in the Skills dialog (G.skillon, "|name|...|") are listed, name and description
   only, in the use_skill tool; the model loads one when a task matches. "/name" at the start of a message attaches
   that skill's SKILL.md to the message (any skill, ticked or not) and lets use_skill reach its files for the session. */
#ifndef LLAMAC_SKILLS_H
#define LLAMAC_SKILLS_H

#define SK_MAX 64
#define SK_DESC_SENT 400 /* most bytes of one description in the tool definition */
typedef struct
{
    char name[64];
    char desc[1024];
    wchar_t dir[MAX_PATH];
    int proj;   /* 1 = from the project's .agents\skills */
    int hidden; /* a user skill hidden by a project skill of the same name */
} Skill;
static Skill *g_sk; /* guarded by tl_enter: use_skill runs on tool threads */
static int g_nsk;
static char g_skused[1024]; /* skills invoked with /name in this session: use_skill reaches them too */

static const char SK_INTRO[] = "Load one of the user's skills: instructions (and files) for a kind of task. When the user's request matches a skill below, call this before starting and follow what it says. Available:";
static const char SK_SCHEMA_A[] = "{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\",\"enum\":[";
static const char SK_SCHEMA_B[] = "]},\"file\":{\"type\":\"string\",\"description\":\"Optional: a file in the skill's folder that the skill refers to, e.g. references/api.md. Omit to load the skill itself.\"}},\"required\":[\"name\"]}";

static int skill_name_ok(const char *n)
{
    size_t L = n ? strlen(n) : 0;
    if (!L || L > 63 || *n == '.' || strstr(n, ".."))
        return 0;
    for (const char *p = n; *p; p++)
        if (!(isalnum((unsigned char)*p) || *p == '-' || *p == '_' || *p == '.'))
            return 0;
    return 1;
}
/* the folder that holds skill folders, "" when there is none */
static void sk_base(int proj, wchar_t *out)
{
    out[0] = 0;
    if (proj)
    {
        wchar_t rfull[MAX_PATH], rfinal[MAX_PATH];
        if (G.fsroot[0] && fs_root(rfull, rfinal))
            _snwprintf(out, MAX_PATH, L"%ls\\.agents\\skills", rfull);
    }
    else
    {
        wchar_t up[MAX_PATH];
        DWORD n = GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH);
        if (n && n < MAX_PATH)
            _snwprintf(out, MAX_PATH, L"%ls\\.agents\\skills", up);
    }
    out[MAX_PATH - 1] = 0;
}
static void sk_scan_dir(const wchar_t *base, int proj, Skill *out, int *n)
{
    wchar_t pat[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    if (!base[0])
        return;
    _snwprintf(pat, MAX_PATH, L"%ls\\*", base);
    pat[MAX_PATH - 1] = 0;
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do
    {
        wchar_t dir[MAX_PATH], md[MAX_PATH];
        char *b, nm[64], ds[1024], *folder;
        size_t len, body;
        int dup = 0;
        Skill *s;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.')
            continue;
        _snwprintf(dir, MAX_PATH, L"%ls\\%ls", base, fd.cFileName);
        dir[MAX_PATH - 1] = 0;
        _snwprintf(md, MAX_PATH, L"%ls\\SKILL.md", dir);
        md[MAX_PATH - 1] = 0;
        b = (char *)read_file(md, &len);
        if (!b)
            continue;
        body = skill_frontmatter(b, nm, sizeof nm, ds, sizeof ds);
        folder = u_from(fd.cFileName);
        if (!skill_name_ok(nm))
            snprintf(nm, sizeof nm, "%s", skill_name_ok(folder) ? folder : "");
        free(folder);
        if (!nm[0])
        {
            free(b);
            continue;
        }
        if (!ds[0])
        { /* no description: the first line of text, without heading marks */
            const char *p = b + body, *e;
            while (*p == '\n' || *p == '\r' || *p == ' ' || *p == '#')
                p++;
            e = p;
            while (*e && *e != '\n' && *e != '\r')
                e++;
            snprintf(ds, sizeof ds, "%.*s", (int)(e - p < 1000 ? e - p : 1000), p);
        }
        free(b);
        for (int i = 0; i < *n; i++)
            if (!_stricmp(out[i].name, nm))
            {
                if (out[i].proj == proj)
                    dup = 1; /* two folders of one place with the same name: the first counts */
                else if (out[i].proj)
                    break; /* hidden below */
            }
        if (dup || *n >= SK_MAX)
            continue;
        s = &out[(*n)++];
        memset(s, 0, sizeof *s);
        snprintf(s->name, sizeof s->name, "%s", nm);
        snprintf(s->desc, sizeof s->desc, "%s", ds);
        wcscpy(s->dir, dir);
        s->proj = proj;
        for (int i = 0; i < *n - 1; i++)
            if (out[i].proj && !proj && !_stricmp(out[i].name, nm))
                s->hidden = 1;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
/* reads both skill places again (the folders may change at any time) */
static void skills_scan(void)
{
    Skill *nw = (Skill *)calloc(SK_MAX, sizeof(Skill)), *old;
    wchar_t pb[MAX_PATH], ub[MAX_PATH];
    int n = 0;
    sk_base(1, pb);
    sk_base(0, ub);
    sk_scan_dir(pb, 1, nw, &n);
    if (_wcsicmp(pb, ub)) /* the root folder may be the user's home folder: the same skills, once */
        sk_scan_dir(ub, 0, nw, &n);
    tl_enter();
    old = g_sk;
    g_sk = nw;
    g_nsk = n;
    tl_leave();
    free(old);
}
static int sk_offered(const Skill *s) { return !s->hidden && (name_in_list(G.skillon, s->name) || name_in_list(g_skused, s->name)); }
/* (re)registers use_skill with the ticked skills; not at all when none is ticked */
static void skills_register(void)
{
    SB d = {0}, sc = {0};
    int n = 0, same = 0;
    skills_scan();
    if (G.tools_on)
    {
        sb_puts(&d, SK_INTRO);
        sb_puts(&sc, SK_SCHEMA_A);
        tl_enter();
        for (int i = 0; i < g_nsk; i++)
            if (sk_offered(&g_sk[i]))
            {
                size_t L = strlen(g_sk[i].desc);
                sb_puts(&d, "\n- ");
                sb_puts(&d, g_sk[i].name);
                sb_puts(&d, ": ");
                sb_add(&d, g_sk[i].desc, tx_cut(g_sk[i].desc, L, SK_DESC_SENT));
                if (L > SK_DESC_SENT)
                    sb_puts(&d, "...");
                sb_puts(&sc, n++ ? "," : "");
                sb_json(&sc, g_sk[i].name);
            }
        sb_puts(&sc, SK_SCHEMA_B);
        /* unchanged: keep the tool where it is, so the request (and the server's KV cache) stays the same */
        for (int i = 0; i < g_ntools; i++)
            if (!strcmp(g_tools[i].name, "use_skill"))
                same = n && !strcmp(g_tools[i].desc, sbs(&d)) && !strcmp(g_tools[i].schema, sbs(&sc));
        tl_leave();
    }
    if (!same)
    {
        tool_del("use_skill");
        if (n)
            tool_add("use_skill", sbs(&d), sbs(&sc), 0, 0, "use_skill", 1, 0);
    }
    free(d.p);
    free(sc.p);
}

/* file names in a skill folder, relative with '/', up to 3 levels and 80 names */
static void sk_files(SB *o, const wchar_t *dir, const wchar_t *wrel, int depth, int *n)
{
    wchar_t pat[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    _snwprintf(pat, MAX_PATH, wrel[0] ? L"%ls\\%ls\\*" : L"%ls%ls\\*", dir, wrel);
    pat[MAX_PATH - 1] = 0;
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do
    {
        wchar_t sub[MAX_PATH];
        char *u;
        if (fd.cFileName[0] == L'.' || (!depth && !_wcsicmp(fd.cFileName, L"SKILL.md")))
            continue;
        if (*n >= 80)
        {
            if (*n == 80)
                sb_puts(o, ", ...");
            (*n)++;
            break;
        }
        _snwprintf(sub, MAX_PATH, wrel[0] ? L"%ls\\%ls" : L"%ls%ls", wrel, fd.cFileName);
        sub[MAX_PATH - 1] = 0;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        { /* linked folders may lead out of the skill, where use_skill cannot read: not listed */
            if (depth < 2 && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                sk_files(o, dir, sub, depth + 1, n);
            continue;
        }
        u = u_from(sub);
        for (char *c = u; *c; c++)
            if (*c == '\\')
                *c = '/';
        sb_puts(o, (*n)++ ? ", " : "");
        sb_puts(o, u);
        free(u);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
/* the text the model gets for a skill: where it is, its other files, the instructions */
static char *sk_render(const char *name, const wchar_t *dir, const char *intro)
{
    wchar_t md[MAX_PATH];
    char *b, nm[64], ds[1024], *wd;
    size_t len, body;
    SB o = {0}, f = {0};
    int nf = 0;
    _snwprintf(md, MAX_PATH, L"%ls\\SKILL.md", dir);
    md[MAX_PATH - 1] = 0;
    b = (char *)read_file(md, &len);
    if (!b)
        return xstrdup("Error: the skill's SKILL.md cannot be read any more");
    body = skill_frontmatter(b, nm, sizeof nm, ds, sizeof ds);
    sk_files(&f, dir, L"", 0, &nf);
    wd = u_from(dir);
    if (intro)
    {
        sb_puts(&o, intro);
        sb_putc(&o, '\n');
    }
    sb_printf(&o, "Skill: %s\nFolder: ", name);
    sb_puts(&o, wd);
    sb_puts(&o, "\nPaths in the skill are relative to this folder.\n");
    if (nf)
    {
        sb_printf(&o, "Other files of the skill (read one with use_skill {\"name\":\"%s\",\"file\":\"<path>\"} when the instructions call for it): ", name);
        sb_puts(&o, sbs(&f));
        sb_putc(&o, '\n');
    }
    sb_puts(&o, "\n");
    sb_puts(&o, b + body);
    free(wd);
    free(f.p);
    free(b);
    return o.p;
}
/* a file inside one skill folder; NULL = ok, else the reason */
static const char *sk_resolve(const wchar_t *dir, const char *rel, wchar_t *out)
{
    wchar_t *w = w_from(rel), joined[MAX_PATH * 2], full[MAX_PATH], dfull[MAX_PATH], dfin[MAX_PATH], fin[MAX_PATH];
    DWORD n;
    for (wchar_t *c = w; *c; c++)
        if (*c == L'/')
            *c = L'\\';
    if (!PathIsRelativeW(w) || w[0] == L'\\')
    {
        free(w);
        return "give a path relative to the skill's folder";
    }
    _snwprintf(joined, MAX_PATH * 2, L"%ls\\%ls", dir, w);
    joined[MAX_PATH * 2 - 1] = 0;
    free(w);
    n = GetFullPathNameW(joined, MAX_PATH, full, NULL);
    if (!n || n >= MAX_PATH)
        return "invalid or too long path";
    n = GetFullPathNameW(dir, MAX_PATH, dfull, NULL);
    if (!n || n >= MAX_PATH)
        return "the skill's folder is not accessible";
    fs_trim(dfull);
    if (!fs_inside(dfull, full))
        return "the path is outside the skill's folder";
    if (!fs_final(dfull, dfin, MAX_PATH))
        return "the skill's folder is not accessible";
    if (!fs_final(full, fin, MAX_PATH))
        return "no such file in the skill's folder";
    fs_trim(dfin);
    fs_trim(fin);
    if (!fs_inside(dfin, fin)) /* the skill folder itself may be a link; a link inside it may not lead out */
        return "the path resolves outside the skill's folder (link)";
    wcscpy(out, full);
    return NULL;
}
/* use_skill {"name", "file"?} */
static char *skill_call(J *a)
{
    const char *nm = j_str(j_get(a, "name")), *file = j_str(j_get(a, "file"));
    wchar_t dir[MAX_PATH] = L"", p[MAX_PATH];
    char name[64] = "";
    const char *err;
    unsigned char *b;
    size_t len;
    tl_enter();
    for (int i = 0; nm && i < g_nsk; i++)
        if (!_stricmp(g_sk[i].name, nm) && sk_offered(&g_sk[i]))
        {
            wcscpy(dir, g_sk[i].dir);
            snprintf(name, sizeof name, "%s", g_sk[i].name);
            break;
        }
    tl_leave();
    if (!dir[0])
        return xstrdup("Error: no skill with that name is enabled. Use one of the names listed in the tool description.");
    if (!file || !*file || !_stricmp(file, "SKILL.md"))
        return sk_render(name, dir, NULL);
    if ((err = sk_resolve(dir, file, p)) != NULL)
        return fs_err(err);
    if (GetFileAttributesW(p) & FILE_ATTRIBUTE_DIRECTORY)
        return fs_err("that is a folder; the skill's files are listed when it is loaded without \"file\"");
    b = read_file(p, &len);
    if (!b)
        return fs_err("the file cannot be read");
    if (memchr(b, 0, len))
    {
        free(b);
        return fs_err("not a text file");
    }
    return (char *)b;
}

/* "/name ..." at the start of a user message: attaches that skill's SKILL.md to node u. 1 = attached. */
static int skill_attach(Node *u, const char *text)
{
    char nm[64], name[64] = "", intro[200];
    wchar_t dir[MAX_PATH] = L"";
    size_t k = 0;
    char *body;
    if (!text || text[0] != '/')
        return 0;
    for (const char *p = text + 1; *p && !isspace((unsigned char)*p); p++)
        if (k < sizeof nm - 1)
            nm[k++] = *p;
        else
            return 0;
    nm[k] = 0;
    if (!skill_name_ok(nm))
        return 0;
    skills_scan();
    tl_enter();
    for (int i = 0; i < g_nsk; i++)
        if (!g_sk[i].hidden && !_stricmp(g_sk[i].name, nm))
        {
            wcscpy(dir, g_sk[i].dir);
            snprintf(name, sizeof name, "%s", g_sk[i].name);
            break;
        }
    tl_leave();
    if (!dir[0])
        return 0;
    snprintf(intro, sizeof intro, "The user invoked the skill \"%s\" with /%s: follow it for this request.", name, nm);
    body = sk_render(name, dir, intro);
    {
        char an[80];
        snprintf(an, sizeof an, "%s/SKILL.md", name);
        att_add(u, ATT_TEXT, an, body, "text/markdown");
    }
    free(body);
    if (!name_in_list(G.skillon, name) && !name_in_list(g_skused, name))
    { /* let use_skill read its companion files */
        name_list_set(g_skused, sizeof g_skused, name, 1);
        skills_register();
    }
    return 1;
}

/* "S 3/13": skills offered to the model / skills found */
static void skills_btn_update(void)
{
    int on = 0, n = 0;
    wchar_t t[32];
    char tip[300];
    HWND b = B(IDC_SKILLSBTN);
    if (!b)
        return;
    tl_enter();
    for (int i = 0; i < g_nsk; i++)
        if (!g_sk[i].hidden)
        {
            n++;
            on += name_in_list(G.skillon, g_sk[i].name) != 0;
        }
    tl_leave();
    _snwprintf(t, 32, L"S\t%d/%d", on, n);
    t[31] = 0;
    SetWindowTextW(b, t);
    snprintf(tip, sizeof tip, "Skills: %d of %d offered to the model%s. Click to choose. Type /name at the start of a message to use any skill once.", on, n,
             G.toolset == 2 ? " (tool set None: none is sent)" : "");
    main_tip(b, tip);
    ShowWindow(b, G.tools_on ? SW_SHOW : SW_HIDE);
    InvalidateRect(b, NULL, FALSE);
}
/* the Skills button in the input box */
static void act_skills(void)
{
    ChkSpec sp;
    double cpt = ms_cpt();
    char empty[400];
    skills_scan();
    td_n = 0;
    tl_enter();
    for (int i = 0; i < g_nsk && td_n < TD_MAX; i++)
    {
        TdRow *r = &td_rows[td_n++];
        const Skill *s = &g_sk[i];
        char *wd = u_from(s->dir);
        size_t L = strlen(s->desc);
        SB tp = {0};
        snprintf(r->name, sizeof r->name, "%s", s->name);
        snprintf(r->desc, sizeof r->desc, "%s%s", s->hidden ? "(hidden by the project's skill) " : s->proj ? "(project) " : "", s->desc);
        r->tok = (int)((strlen(s->name) + (L > SK_DESC_SENT ? SK_DESC_SENT : L) + 6) / cpt);
        r->inset = !s->hidden;
        sb_puts(&tp, s->desc);
        sb_puts(&tp, "\n\n");
        sb_puts(&tp, wd);
        r->tip = tp.p;
        free(wd);
    }
    tl_leave();
    memset(&sp, 0, sizeof sp);
    sp.title = L"Skills";
    sp.info = !G.tools_on      ? "Tools are off in Settings: no skill is offered to the model. /name at the start of a message still attaches a skill."
              : G.toolset == 2 ? "Tool set is None: no skill is offered to the model. /name at the start of a message still attaches a skill."
                               : "Ticked skills are offered to the model, which loads one when a task matches. Type /name at the start of a message to use any skill once.";
    snprintf(empty, sizeof empty, "No skills found. Each skill is a folder with a SKILL.md in %s.agents\\skills (project) or %%USERPROFILE%%\\.agents\\skills.",
             G.fsroot[0] ? "<root folder>\\" : "the file tools root folder's ");
    sp.empty = empty;
    sp.what = "skills";
    sp.liston = 1;
    sp.base = (int)((strlen(SK_INTRO) + strlen(SK_SCHEMA_A) + strlen(SK_SCHEMA_B) + 40) / cpt);
    if (chk_dialog(owner_wnd(), &sp, G.skillon, sizeof G.skillon))
    {
        cfg_save();
        skills_register();
        set_status("Skills updated: the next request uses the new selection.");
    }
    skills_btn_update(); /* the counts may have changed with the scan */
}

/* ---------- "/name" autocomplete in the input box ----------
   While the message starts with "/" and the caret is in that first word, a popup above the input lists the skills
   whose name starts with (then contains) what was typed; ticked skills first, marked with a dot. Up/Down choose,
   Tab or Enter insert "/name ", Esc closes; a click inserts too. The popup never takes the focus. */
#define SKAC_ROWS 8
#define SKAC_ROWH 28
#define SKAC_FOOT 22
typedef struct
{
    char name[64];
    char desc[240];
    int on;
} SkacRow;
static HWND g_skac;
static SkacRow g_skac_row[SKAC_ROWS];
static int g_skac_n, g_skac_sel, g_skac_tok, g_skac_eatesc; /* rows, selected row, wchars of "/prefix", swallow Esc char */
static int g_skac_more;                                     /* matches that did not fit */

static int skac_visible(void) { return g_skac && IsWindowVisible(g_skac); }
static void skac_hide(void)
{
    if (skac_visible())
        ShowWindow(g_skac, SW_HIDE);
}
/* the input box moved (layout, window move, the copilot panel growing while it streams): place the popup again once
   the layout is done - through the input's EN_CHANGE handler, which only resizes the box when its height changed */
static void skac_follow(void)
{
    if (skac_visible())
        PostMessageW(hMain, WM_COMMAND, MAKEWPARAM(IDC_INPUT, EN_CHANGE), (LPARAM)hInput);
}
static int skac_namech(wchar_t c) { return (c < 128 && isalnum(c)) || c == L'-' || c == L'_' || c == L'.'; }
/* inserts "/name " in place of the typed "/prefix" */
static void skac_accept(int i)
{
    wchar_t *rep;
    char ins[80];
    int len = GetWindowTextLengthW(hInput), sp = 0;
    if (i < 0 || i >= g_skac_n)
        return;
    if (len > g_skac_tok)
    { /* already followed by a space: do not add another */
        wchar_t *all = (wchar_t *)malloc(((size_t)len + 1) * sizeof(wchar_t));
        GetWindowTextW(hInput, all, len + 1);
        sp = all[g_skac_tok] == L' ';
        free(all);
    }
    snprintf(ins, sizeof ins, "/%s%s", g_skac_row[i].name, sp ? "" : " ");
    rep = w_from(ins);
    skac_hide();
    SendMessageW(hInput, EM_SETSEL, 0, g_skac_tok);
    SendMessageW(hInput, EM_REPLACESEL, TRUE, (LPARAM)rep);
    if (sp)
    { /* caret after the existing space */
        DWORD e = (DWORD)wcslen(rep) + 1;
        SendMessageW(hInput, EM_SETSEL, e, e);
    }
    free(rep);
}
static void skac_paint(HWND h)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps), m;
    RECT rc, r;
    HBITMAP bm;
    HGDIOBJ ob, of;
    HBRUSH bg;
    GetClientRect(h, &rc);
    m = CreateCompatibleDC(dc); /* double-buffered: no flicker while the selection moves */
    bm = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    ob = SelectObject(m, bm);
    bg = CreateSolidBrush(cSurf);
    FillRect(m, &rc, bg);
    DeleteObject(bg);
    rrect(m, &rc, 1, cSurf, cBorder);
    SetBkMode(m, TRANSPARENT);
    of = SelectObject(m, hFont);
    for (int i = 0; i < g_skac_n; i++)
    {
        wchar_t *wn = w_from(g_skac_row[i].name), *wd = w_from(g_skac_row[i].desc);
        SIZE sz;
        SetRect(&r, 4, 4 + i * SKAC_ROWH, rc.right - 4, 4 + (i + 1) * SKAC_ROWH);
        if (i == g_skac_sel)
            rrect(m, &r, 8, adj(cSurf, G.dark ? 18 : -12), adj(cSurf, G.dark ? 18 : -12));
        if (g_skac_row[i].on)
        { /* ticked in the Skills dialog: offered to the model anyway */
            RECT d;
            int cy = (r.top + r.bottom) / 2;
            SetRect(&d, r.left + 8, cy - 3, r.left + 14, cy + 3);
            rrect(m, &d, 6, cAccent, cAccent);
        }
        SelectObject(m, hFontB);
        SetTextColor(m, cFg);
        r.left += 22;
        GetTextExtentPoint32W(m, wn, (int)wcslen(wn), &sz);
        DrawTextW(m, wn, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        r.left += sz.cx + 12;
        SelectObject(m, hFont);
        SetTextColor(m, cDim);
        r.right -= 8;
        if (r.left < r.right)
            DrawTextW(m, wd, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        free(wn);
        free(wd);
    }
    SetRect(&r, 26, rc.bottom - SKAC_FOOT - 2, rc.right - 10, rc.bottom - 2);
    SelectObject(m, hFontXS);
    SetTextColor(m, cDim);
    {
        wchar_t foot[160], more[40] = L"";
        if (g_skac_more)
            _snwprintf(more, 40, L"+%d more, keep typing   ", g_skac_more);
        more[39] = 0;
        _snwprintf(foot, 160, L"%ls\x2191\x2193 choose   Tab/Enter insert   Esc close   \x25CF ticked in Skills", more);
        foot[159] = 0;
        DrawTextW(m, foot, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    BitBlt(dc, 0, 0, rc.right, rc.bottom, m, 0, 0, SRCCOPY);
    SelectObject(m, of);
    SelectObject(m, ob);
    DeleteObject(bm);
    DeleteDC(m);
    EndPaint(h, &ps);
}
static LRESULT CALLBACK SkacProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE; /* the input keeps the focus */
    case WM_MOUSEMOVE:
    {
        int i = ((short)HIWORD(l) - 4) / SKAC_ROWH;
        if (i >= 0 && i < g_skac_n && i != g_skac_sel)
        {
            g_skac_sel = i;
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        int i = ((short)HIWORD(l) - 4) / SKAC_ROWH;
        if (i >= 0 && i < g_skac_n)
            skac_accept(i);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        skac_paint(h);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
/* 2 = name starts with pre, 1 = contains it (not at the start), 0 = no match; ignores case */
static int skac_match(const char *name, const char *pre, int pl)
{
    char a[64], b[64];
    if (!_strnicmp(name, pre, (size_t)pl))
        return 2;
    if (!pl)
        return 0;
    snprintf(a, sizeof a, "%s", name);
    snprintf(b, sizeof b, "%s", pre);
    _strlwr(a);
    _strlwr(b);
    return strstr(a, b) != NULL;
}
/* re-reads the input after a change or a caret move: shows, updates or hides the popup */
static void skac_update(void)
{
    wchar_t t[80];
    char pre[64];
    DWORD s = 0, e = 0;
    int len, tok = 1, pl, n = 0, keep = -1;
    char keepn[64] = "";
    HWND owner;
    if (!hInput)
        return;
    len = GetWindowTextW(hInput, t, 80);
    SendMessageW(hInput, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    if (len < 1 || t[0] != L'/' || s != e)
    {
        skac_hide();
        return;
    }
    while (tok < len && skac_namech(t[tok]))
        tok++;
    pl = tok - 1;
    if ((tok < len && !iswspace(t[tok])) || (tok == len && len == 79) || e < 1 || (int)e > tok || pl > 63)
    { /* "/path/x", caret after the first word, or a word too long for a skill name */
        skac_hide();
        return;
    }
    for (int i = 0; i < pl; i++)
        pre[i] = (char)t[i + 1];
    pre[pl] = 0;
    if (!skac_visible())
        skills_scan(); /* the folders may have changed since the last look */
    if (skac_visible() && g_skac_sel < g_skac_n)
        snprintf(keepn, sizeof keepn, "%s", g_skac_row[g_skac_sel].name);
    tl_enter();
    for (int pass = 0; pass < 4 && n < SKAC_ROWS; pass++)
        for (int i = 0; i < g_nsk && n < SKAC_ROWS; i++)
        { /* 0: starts with, ticked  1: starts with  2: contains, ticked  3: contains */
            const Skill *k = &g_sk[i];
            int on = name_in_list(G.skillon, k->name) != 0, mt;
            if (k->hidden || on != !(pass & 1))
                continue;
            mt = skac_match(k->name, pre, pl);
            if (mt != (pass < 2 ? 2 : 1))
                continue;
            snprintf(g_skac_row[n].name, sizeof g_skac_row[n].name, "%s", k->name);
            snprintf(g_skac_row[n].desc, sizeof g_skac_row[n].desc, "%s", k->desc);
            g_skac_row[n].on = on;
            if (!strcmp(k->name, keepn))
                keep = n;
            n++;
        }
    g_skac_more = 0;
    for (int i = 0; i < g_nsk; i++)
        if (!g_sk[i].hidden && skac_match(g_sk[i].name, pre, pl))
            g_skac_more++;
    g_skac_more = g_skac_more > n ? g_skac_more - n : 0;
    tl_leave();
    if (!n || (n == 1 && !_stricmp(g_skac_row[0].name, pre)))
    { /* nothing matches, or the name is already complete */
        g_skac_n = 0;
        skac_hide();
        return;
    }
    g_skac_n = n;
    g_skac_sel = keep >= 0 ? keep : 0;
    g_skac_tok = tok;
    owner = GetAncestor(hInput, GA_ROOT); /* the main window or the copilot panel */
    if (g_skac && GetWindow(g_skac, GW_OWNER) != owner)
    {
        DestroyWindow(g_skac);
        g_skac = NULL;
    }
    if (!g_skac)
    {
        static int reg = 0;
        if (!reg)
        {
            WNDCLASSEXW wc;
            memset(&wc, 0, sizeof wc);
            wc.cbSize = sizeof wc;
            wc.style = CS_DROPSHADOW;
            wc.lpfnWndProc = SkacProc;
            wc.hInstance = GetModuleHandleW(NULL);
            wc.hCursor = LoadCursorW(NULL, IDC_HAND);
            wc.lpszClassName = L"LCSkillAC";
            RegisterClassExW(&wc);
            reg = 1;
        }
        g_skac = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"LCSkillAC", NULL, WS_POPUP, 0, 0, 10, 10, owner, NULL, GetModuleHandleW(NULL), NULL);
        if (!g_skac)
            return;
    }
    {
        RECT ir;
        MONITORINFO mi;
        int W, H = n * SKAC_ROWH + 8 + SKAC_FOOT, x, y;
        GetWindowRect(hInput, &ir);
        x = ir.left - 14;
        W = ir.right - ir.left + 28;
        y = ir.top - 13 - 6 - H; /* above the input box: the composer is at the bottom */
        mi.cbSize = sizeof mi;
        GetMonitorInfoW(MonitorFromWindow(hInput, MONITOR_DEFAULTTONEAREST), &mi);
        if (y < mi.rcWork.top)
            y = ir.bottom + 11 + 6; /* no room above (copilot panel at the top of the screen): below */
        SetWindowPos(g_skac, HWND_TOP, x, y, W, H, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(g_skac, NULL, FALSE);
    }
}
/* input box keys and focus while the popup is open; 1 = handled */
static int skac_key(UINT m, WPARAM w)
{
    if (m == WM_CHAR && w == 27 && g_skac_eatesc)
    { /* the character of the Esc that closed the popup: no beep, and the copilot panel stays */
        g_skac_eatesc = 0;
        return 1;
    }
    if (m == WM_KILLFOCUS)
    {
        skac_hide();
        return 0;
    }
    if ((m == WM_KEYUP && (w == VK_LEFT || w == VK_RIGHT || w == VK_HOME || w == VK_END)) || m == WM_LBUTTONUP)
    { /* the caret moved without a text change */
        if (skac_visible())
            skac_update();
        return 0;
    }
    if (!skac_visible())
        return 0;
    if (m == WM_KEYDOWN && (w == VK_UP || w == VK_DOWN))
    {
        g_skac_sel = (g_skac_sel + (w == VK_DOWN ? 1 : g_skac_n - 1)) % g_skac_n;
        InvalidateRect(g_skac, NULL, FALSE);
        return 1;
    }
    if (m == WM_KEYDOWN && w == VK_ESCAPE)
    {
        skac_hide();
        g_skac_eatesc = 1;
        return 1;
    }
    if (m == WM_CHAR && (w == L'\t' || (w == L'\r' && !(GetKeyState(VK_SHIFT) & 0x8000))))
    {
        skac_accept(g_skac_sel);
        return 1;
    }
    return 0;
}

/* ---------- AGENTS.md: standing instructions for coding agents (agents.md convention) ----------
   %USERPROFILE%\.agents\AGENTS.md (the user's, for every project) and <root folder>\AGENTS.md (the project's) are
   added to the system message when they exist. Read before every request: cheap, and an edit applies at once; the
   text only changes when a file does, so the server's KV prefix stays valid. */
#define AGM_MAX 20000 /* most bytes taken from one file */
static void agm_add(SB *o, const wchar_t *path, const char *head)
{
    size_t len, L;
    char *b = (char *)read_file(path, &len), *t;
    if (!b)
        return;
    t = b;
    if (!strncmp(t, "\xEF\xBB\xBF", 3))
        t += 3;
    while (*t == '\n' || *t == '\r' || *t == ' ' || *t == '\t')
        t++;
    L = strlen(t);
    while (L && (t[L - 1] == '\n' || t[L - 1] == '\r' || t[L - 1] == ' ' || t[L - 1] == '\t'))
        L--;
    if (L && !memchr(t, 0, L) && len == strlen(b)) /* text only */
    {
        size_t k = tx_cut(t, L, AGM_MAX);
        if (o->n)
            sb_puts(o, "\n\n");
        sb_puts(o, head);
        sb_add(o, t, k);
        if (k < L)
            sb_printf(o, "\n[... AGENTS.md cut at %d of %llu characters ...]", AGM_MAX, (unsigned long long)L);
    }
    free(b);
}
/* the texts added after the system prompt: where the file tools work (tools_fs.h) and AGENTS.md */
static void sys_extra_refresh(void)
{
    SB o = {0};
    wchar_t p[MAX_PATH], up[MAX_PATH], rfull[MAX_PATH], rfinal[MAX_PATH];
    int same = 0;
    char *env = fs_env_text();
    if (!(env && g_env_txt && !strcmp(env, g_env_txt)))
    { /* replaced only when it changed (a request built meanwhile never sees a freed pointer: same thread) */
        free(g_env_txt);
        g_env_txt = env;
    }
    else
        free(env);
    if (G.agentsmd)
    {
        DWORD n = GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH);
        if (n && n < MAX_PATH)
        {
            _snwprintf(p, MAX_PATH, L"%ls\\.agents\\AGENTS.md", up);
            p[MAX_PATH - 1] = 0;
            agm_add(&o, p, "# Your standing instructions (AGENTS.md of the user, for every project)\n");
        }
        if (G.fsroot[0] && fs_root(rfull, rfinal))
        {
            _snwprintf(p, MAX_PATH, L"%ls\\AGENTS.md", rfull);
            p[MAX_PATH - 1] = 0;
            agm_add(&o, p, "# Project instructions (AGENTS.md in the root folder of the file tools; they win over the user's general ones)\n");
        }
    }
    same = (!o.n && !g_agents_txt) || (o.n && g_agents_txt && !strcmp(g_agents_txt, o.p));
    if (same)
    {
        free(o.p);
        return;
    }
    free(g_agents_txt);
    g_agents_txt = o.n ? o.p : NULL;
    if (!o.n)
        free(o.p);
}

#endif /* LLAMAC_SKILLS_H */
