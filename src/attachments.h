/* attachments.h - Pending attachments and file loading */
#ifndef LLAMAC_ATTACHMENTS_H
#define LLAMAC_ATTACHMENTS_H

/* ---------- attachments ---------- */
/* the attach button shows how many attachments are waiting */
static void attach_badge_refresh(void)
{
    HWND b = B(IDC_ATTACH);
    if (b)
        InvalidateRect(b, NULL, FALSE);
}
static void pend_clear(void)
{
    for (int i = 0; i < npend; i++)
    {
        free(pend[i].name);
        free(pend[i].data);
        free(pend[i].mime);
    }
    npend = 0;
    attach_badge_refresh();
}
static void pend_status(void)
{
    SB s = {0};
    sb_puts(&s, "Attached: ");
    for (int i = 0; i < npend; i++)
    {
        if (i)
            sb_puts(&s, ", ");
        sb_puts(&s, pend[i].name);
    }
    set_status(sbs(&s));
    free(s.p);
}
static void pend_remove(int i)
{
    if (i < 0 || i >= npend)
        return;
    free(pend[i].name);
    free(pend[i].data);
    free(pend[i].mime);
    memmove(&pend[i], &pend[i + 1], (size_t)(npend - i - 1) * sizeof(Att));
    npend--;
    if (npend)
        pend_status();
    else
        set_status("Attachment removed");
    attach_badge_refresh();
}
/* the + button while attachments are waiting: add more, or remove one / all. Returns 1 = open the file picker. */
static int attach_menu(void)
{
    static const char *const KIND[] = {"image", "text", "audio"};
    HWND b = B(IDC_ATTACH);
    HMENU m = CreatePopupMenu();
    RECT br;
    int cmd;
    AppendMenuW(m, MF_STRING, 1, L"Add files...");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    for (int i = 0; i < npend; i++)
    {
        const char *d = pend[i].data ? pend[i].data : "", *comma = strncmp(d, "data:", 5) ? NULL : strchr(d, ',');
        size_t bytes = comma ? strlen(comma + 1) / 4 * 3 : strlen(d); /* base64 data URL or plain text */
        SB lab = {0};
        wchar_t *w;
        sb_puts(&lab, "Remove  ");
        for (const char *q = pend[i].name ? pend[i].name : "?"; *q; q++)
            if (*q == '&')
                sb_puts(&lab, "&&"); /* & would underline the next letter */
            else if (*q != '\t')
                sb_putc(&lab, *q); /* a tab would split the menu item */
        sb_printf(&lab, "\t%s, %s%u KB", pend[i].kind >= 0 && pend[i].kind <= 2 ? KIND[pend[i].kind] : "file",
                  bytes < 1024 ? "<" : "", (unsigned)(bytes < 1024 ? 1 : (bytes + 512) / 1024));
        w = w_from(sbs(&lab));
        AppendMenuW(m, MF_STRING, (UINT_PTR)(100 + i), w);
        free(w);
        free(lab.p);
    }
    if (npend > 1)
    {
        AppendMenuW(m, MF_SEPARATOR, 0, NULL);
        AppendMenuW(m, MF_STRING, 2, L"Remove all");
    }
    GetWindowRect(b, &br);
    cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, br.left, br.top - 2, 0, owner_wnd(), NULL);
    DestroyMenu(m);
    if (cmd == 2)
    {
        pend_clear();
        set_status("Attachments removed");
    }
    else if (cmd >= 100 && cmd < 100 + npend)
        pend_remove(cmd - 100);
    return cmd == 1;
}
static void pend_add(int kind, const char *name, const char *data, const char *mime)
{
    if (npend >= 16)
        return;
    Att *a = &pend[npend++];
    a->kind = kind;
    a->name = xstrdup(name);
    a->data = xstrdup(data);
    a->mime = xstrdup(mime);
    pend_status();
    attach_badge_refresh();
}
/* clipboard bitmap -> PNG (WIC) -> data URL; NULL on failure. The PNG is much smaller than the raw bitmap. */
/* HBITMAP -> PNG (WIC) -> "data:image/png;base64,..."; NULL on failure. Used by paste and the screenshot tool. */
static char *hbm_png(HBITMAP hb)
{
    IWICImagingFactory *fac = NULL;
    IWICBitmap *bmp = NULL;
    IWICBitmapEncoder *enc = NULL;
    IWICBitmapFrameEncode *frm = NULL;
    IPropertyBag2 *props = NULL;
    IStream *st = NULL;
    char *out = NULL;
    HRESULT hc = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (hb && SUCCEEDED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&fac)) &&
        SUCCEEDED(IWICImagingFactory_CreateBitmapFromHBITMAP(fac, hb, NULL, WICBitmapIgnoreAlpha, &bmp)) &&
        SUCCEEDED(CreateStreamOnHGlobal(NULL, TRUE, &st)) &&
        SUCCEEDED(IWICImagingFactory_CreateEncoder(fac, &GUID_ContainerFormatPng, NULL, &enc)) &&
        SUCCEEDED(IWICBitmapEncoder_Initialize(enc, st, WICBitmapEncoderNoCache)) &&
        SUCCEEDED(IWICBitmapEncoder_CreateNewFrame(enc, &frm, &props)) &&
        SUCCEEDED(IWICBitmapFrameEncode_Initialize(frm, props)) &&
        SUCCEEDED(IWICBitmapFrameEncode_WriteSource(frm, (IWICBitmapSource *)bmp, NULL)) &&
        SUCCEEDED(IWICBitmapFrameEncode_Commit(frm)) && SUCCEEDED(IWICBitmapEncoder_Commit(enc)))
    {
        HGLOBAL g;
        STATSTG ss;
        if (SUCCEEDED(IStream_Stat(st, &ss, STATFLAG_NONAME)) && SUCCEEDED(GetHGlobalFromStream(st, &g)))
        {
            const unsigned char *d = (const unsigned char *)GlobalLock(g);
            if (d)
            {
                char *e = b64(d, (size_t)ss.cbSize.QuadPart);
                SB u = {0};
                sb_puts(&u, "data:image/png;base64,");
                sb_puts(&u, e);
                free(e);
                out = u.p;
                GlobalUnlock(g);
            }
        }
    }
    if (props)
        IPropertyBag2_Release(props);
    if (frm)
        IWICBitmapFrameEncode_Release(frm);
    if (enc)
        IWICBitmapEncoder_Release(enc);
    if (st)
        IStream_Release(st);
    if (bmp)
        IWICBitmap_Release(bmp);
    if (fac)
        IWICImagingFactory_Release(fac);
    if (SUCCEEDED(hc))
        CoUninitialize();
    return out;
}
/* clipboard bitmap as a PNG data URL (Windows converts CF_DIB / CF_DIBV5 to CF_BITMAP) */
static char *clip_image_png(void)
{
    char *out = NULL;
    if (OpenClipboard(hMain))
    {
        out = hbm_png((HBITMAP)GetClipboardData(CF_BITMAP));
        CloseClipboard();
    }
    return out;
}
static void add_file(const wchar_t *path);
/* Ctrl+V in the input box: files copied in Explorer and screenshots become attachments. 1 = handled. */
static int paste_special(void)
{
    if (IsClipboardFormatAvailable(CF_HDROP) && OpenClipboard(hMain))
    {
        HDROP d = (HDROP)GetClipboardData(CF_HDROP);
        UINT n = d ? DragQueryFileW(d, 0xFFFFFFFF, NULL, 0) : 0;
        wchar_t (*files)[MAX_PATH] = n ? (wchar_t (*)[MAX_PATH])calloc(n, sizeof *files) : NULL;
        for (UINT i = 0; i < n; i++)
            DragQueryFileW(d, i, files[i], MAX_PATH);
        CloseClipboard(); /* add_file may show a status; do not hold the clipboard */
        for (UINT i = 0; i < n; i++)
            add_file(files[i]);
        free(files);
        return n > 0;
    }
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) && (IsClipboardFormatAvailable(CF_BITMAP) || IsClipboardFormatAvailable(CF_DIB)))
    {
        char *png = clip_image_png();
        if (png)
        {
            char name[48];
            static int cnt;
            snprintf(name, sizeof name, "pasted-image-%d.png", ++cnt);
            pend_add(ATT_IMAGE, name, png, "image/png");
            free(png);
        }
        else
            set_status("Could not convert the clipboard image");
        return 1;
    }
    return 0;
}
static void add_file(const wchar_t *path)
{
    size_t len;
    unsigned char *d = read_file(path, &len);
    const wchar_t *nm = wcsrchr(path, L'\\');
    nm = nm ? nm + 1 : path;
    const wchar_t *ext = wcsrchr(path, L'.');
    char *name = u_from(nm);
    if (!d)
    {
        set_status("Cannot read file (or file larger than 100 MB)");
        free(name);
        return;
    }
    const char *img = NULL, *aud = NULL;
    if (ext)
    {
        if (!_wcsicmp(ext, L".png"))
            img = "image/png";
        else if (!_wcsicmp(ext, L".jpg") || !_wcsicmp(ext, L".jpeg"))
            img = "image/jpeg";
        else if (!_wcsicmp(ext, L".gif"))
            img = "image/gif";
        else if (!_wcsicmp(ext, L".webp"))
            img = "image/webp";
        else if (!_wcsicmp(ext, L".mp3"))
            aud = "audio/mpeg";
        else if (!_wcsicmp(ext, L".wav"))
            aud = "audio/wav";
        else if (doc_kind(name))
        { /* PDF / DOCX / XLSX: attached as their text */
            char err[300], *t = doc_text(name, d, len, err, sizeof err);
            if (t)
            {
                char an[300];
                snprintf(an, sizeof an, "%s (text)", name);
                pend_add(ATT_TEXT, an, t, "text/plain");
                free(t);
            }
            else
            {
                char st[400];
                snprintf(st, sizeof st, "Cannot attach %s: %s", name, err);
                set_status(st);
            }
            free(d);
            free(name);
            return;
        }
    }
    if (img || aud)
    {
        char *e = b64(d, len);
        if (img)
        {
            SB u = {0};
            sb_printf(&u, "data:%s;base64,", img);
            sb_puts(&u, e);
            pend_add(ATT_IMAGE, name, sbs(&u), img);
            free(u.p);
        }
        else
            pend_add(ATT_AUDIO, name, e, aud);
        free(e);
    }
    else
    {
        size_t chk = len < 8192 ? len : 8192;
        int bin = 0;
        for (size_t i = 0; i < chk; i++)
            if (d[i] == 0)
            {
                bin = 1;
                break;
            }
        if (bin)
            set_status("Unsupported binary file");
        else
            pend_add(ATT_TEXT, name, (char *)d, "text/plain");
    }
    free(d);
    free(name);
}

#endif /* LLAMAC_ATTACHMENTS_H */
