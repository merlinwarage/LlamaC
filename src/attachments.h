/* attachments.h - Pending attachments and file loading */
#ifndef LLAMAC_ATTACHMENTS_H
#define LLAMAC_ATTACHMENTS_H

/* ---------- attachments ---------- */
static void pend_clear(void) {
    for (int i = 0; i < npend; i++) { free(pend[i].name); free(pend[i].data); free(pend[i].mime); }
    npend = 0;
}
static void pend_status(void) {
    SB s = {0}; sb_puts(&s, "Attached: ");
    for (int i = 0; i < npend; i++) { if (i) sb_puts(&s, ", "); sb_puts(&s, pend[i].name); }
    set_status(sbs(&s)); free(s.p);
}
static void pend_add(int kind, const char *name, const char *data, const char *mime) {
    if (npend >= 16) return;
    Att *a = &pend[npend++]; a->kind = kind; a->name = xstrdup(name); a->data = xstrdup(data); a->mime = xstrdup(mime);
    pend_status();
}
static void add_file(const wchar_t *path) {
    size_t len; unsigned char *d = read_file(path, &len);
    const wchar_t *nm = wcsrchr(path, L'\\'); nm = nm ? nm + 1 : path;
    const wchar_t *ext = wcsrchr(path, L'.'); char *name = u_from(nm);
    if (!d) { set_status("Cannot read file (or file larger than 100 MB)"); free(name); return; }
    const char *img = NULL, *aud = NULL;
    if (ext) {
        if (!_wcsicmp(ext, L".png")) img = "image/png"; else if (!_wcsicmp(ext, L".jpg") || !_wcsicmp(ext, L".jpeg")) img = "image/jpeg";
        else if (!_wcsicmp(ext, L".gif")) img = "image/gif"; else if (!_wcsicmp(ext, L".webp")) img = "image/webp";
        else if (!_wcsicmp(ext, L".mp3")) aud = "audio/mpeg"; else if (!_wcsicmp(ext, L".wav")) aud = "audio/wav";
        else if (!_wcsicmp(ext, L".pdf")) { set_status("PDF is not supported in this native build (convert to text or images)"); free(d); free(name); return; }
    }
    if (img || aud) {
        char *e = b64(d, len);
        if (img) { SB u = {0}; sb_printf(&u, "data:%s;base64,", img); sb_puts(&u, e); pend_add(ATT_IMAGE, name, sbs(&u), img); free(u.p); }
        else pend_add(ATT_AUDIO, name, e, aud);
        free(e);
    } else {
        size_t chk = len < 8192 ? len : 8192; int bin = 0;
        for (size_t i = 0; i < chk; i++) if (d[i] == 0) { bin = 1; break; }
        if (bin) set_status("Unsupported binary file"); else pend_add(ATT_TEXT, name, (char *)d, "text/plain");
    }
    free(d); free(name);
}

#endif /* LLAMAC_ATTACHMENTS_H */
