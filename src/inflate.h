/* inflate.h - DEFLATE decompressor (RFC 1951) and the zlib wrapper (RFC 1950), portable.
   Needed for ZIP entries (DOCX, XLSX) and PDF FlateDecode streams; Windows has no API for raw deflate.
   Canonical-Huffman decoding as in zlib's reference "puff". */
#ifndef LLAMAC_INFLATE_H
#define LLAMAC_INFLATE_H

typedef struct
{
    const unsigned char *src, *end;
    unsigned bitbuf;
    int bitcnt;
    int err; /* 1 = input ended or is invalid, 2 = output limit reached */
    SB *out;
    size_t max;
} InfS;
typedef struct
{
    short count[16];
    short symbol[320];
} InfH;

static int inf_bits(InfS *s, int n)
{
    unsigned v;
    while (s->bitcnt < n)
    {
        if (s->src >= s->end)
        {
            s->err = 1;
            return 0;
        }
        s->bitbuf |= (unsigned)(*s->src++) << s->bitcnt;
        s->bitcnt += 8;
    }
    v = s->bitbuf & ((1u << n) - 1);
    s->bitbuf >>= n;
    s->bitcnt -= n;
    return (int)v;
}
static void inf_build(InfH *h, const short *len, int n)
{
    short offs[16];
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++)
        h->count[len[i]]++;
    h->count[0] = 0;
    offs[1] = 0;
    for (int i = 1; i < 15; i++)
        offs[i + 1] = (short)(offs[i] + h->count[i]);
    for (int i = 0; i < n; i++)
        if (len[i])
            h->symbol[offs[len[i]]++] = (short)i;
}
static int inf_decode(InfS *s, const InfH *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++)
    {
        int count;
        code |= inf_bits(s, 1);
        if (s->err)
            return -1;
        count = h->count[len];
        if (code - count < first)
            return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    s->err = 1;
    return -1;
}
static void inf_put(InfS *s, unsigned char c)
{
    if (s->out->n >= s->max)
    {
        s->err = 2;
        return;
    }
    sb_add(s->out, (const char *)&c, 1);
}
static void inf_codes(InfS *s, const InfH *lc, const InfH *dc)
{
    static const short lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const short dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    for (;;)
    {
        int sym = inf_decode(s, lc), len, dist;
        if (s->err)
            return;
        if (sym < 256)
        {
            inf_put(s, (unsigned char)sym);
            if (s->err)
                return;
            continue;
        }
        if (sym == 256)
            return;
        sym -= 257;
        if (sym >= 29)
        {
            s->err = 1;
            return;
        }
        len = lbase[sym] + inf_bits(s, lext[sym]);
        sym = inf_decode(s, dc);
        if (s->err || sym < 0 || sym >= 30)
        {
            s->err = 1;
            return;
        }
        dist = dbase[sym] + inf_bits(s, dext[sym]);
        if (s->err || (size_t)dist > s->out->n)
        {
            s->err = 1;
            return;
        }
        while (len--)
        {
            inf_put(s, (unsigned char)s->out->p[s->out->n - (size_t)dist]);
            if (s->err)
                return;
        }
    }
}
static void inf_stored(InfS *s)
{
    unsigned len, nlen;
    s->bitbuf = 0;
    s->bitcnt = 0; /* to the next byte boundary */
    if (s->end - s->src < 4)
    {
        s->err = 1;
        return;
    }
    len = s->src[0] | (s->src[1] << 8);
    nlen = s->src[2] | (s->src[3] << 8);
    s->src += 4;
    if (len != (~nlen & 0xFFFF) || (size_t)(s->end - s->src) < len)
    {
        s->err = 1;
        return;
    }
    while (len--)
    {
        inf_put(s, *s->src++);
        if (s->err)
            return;
    }
}
static void inf_fixed(InfS *s)
{
    static InfH lc, dc;
    static int built;
    if (!built)
    {
        short l[320];
        int i = 0;
        for (; i < 144; i++)
            l[i] = 8;
        for (; i < 256; i++)
            l[i] = 9;
        for (; i < 280; i++)
            l[i] = 7;
        for (; i < 288; i++)
            l[i] = 8;
        inf_build(&lc, l, 288);
        for (i = 0; i < 30; i++)
            l[i] = 5;
        inf_build(&dc, l, 30);
        built = 1;
    }
    inf_codes(s, &lc, &dc);
}
static void inf_dynamic(InfS *s)
{
    static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    short lengths[320];
    InfH lc, dc;
    int nlen = inf_bits(s, 5) + 257, ndist = inf_bits(s, 5) + 1, ncode = inf_bits(s, 4) + 4, idx;
    if (s->err || nlen > 286 || ndist > 30)
    {
        s->err = 1;
        return;
    }
    for (idx = 0; idx < ncode; idx++)
        lengths[order[idx]] = (short)inf_bits(s, 3);
    for (; idx < 19; idx++)
        lengths[order[idx]] = 0;
    inf_build(&lc, lengths, 19);
    for (idx = 0; idx < nlen + ndist;)
    {
        int sym = inf_decode(s, &lc), len = 0, rep;
        if (s->err || sym < 0)
        {
            s->err = 1;
            return;
        }
        if (sym < 16)
        {
            lengths[idx++] = (short)sym;
            continue;
        }
        if (sym == 16)
        {
            if (!idx)
            {
                s->err = 1;
                return;
            }
            len = lengths[idx - 1];
            rep = 3 + inf_bits(s, 2);
        }
        else if (sym == 17)
            rep = 3 + inf_bits(s, 3);
        else
            rep = 11 + inf_bits(s, 7);
        if (idx + rep > nlen + ndist)
        {
            s->err = 1;
            return;
        }
        while (rep--)
            lengths[idx++] = (short)len;
    }
    inf_build(&lc, lengths, nlen);
    inf_build(&dc, lengths + nlen, ndist);
    inf_codes(s, &lc, &dc);
}
/* raw deflate data -> out (appended, at most max bytes). 0 = complete; otherwise out keeps what was decoded. */
static int inflate_raw(const unsigned char *src, size_t n, SB *out, size_t max)
{
    InfS s;
    int last;
    memset(&s, 0, sizeof s);
    s.src = src;
    s.end = src + n;
    s.out = out;
    s.max = out->n + max;
    do
    {
        int type;
        last = inf_bits(&s, 1);
        type = inf_bits(&s, 2);
        if (s.err)
            break;
        if (type == 0)
            inf_stored(&s);
        else if (type == 1)
            inf_fixed(&s);
        else if (type == 2)
            inf_dynamic(&s);
        else
            s.err = 1;
    } while (!last && !s.err);
    return s.err;
}
/* zlib stream (2-byte header, deflate data, adler32) as used by PDF FlateDecode; raw deflate is accepted too */
static int inflate_zlib(const unsigned char *src, size_t n, SB *out, size_t max)
{
    if (n >= 2 && (src[0] & 0x0F) == 8 && ((src[0] << 8) | src[1]) % 31 == 0)
        return inflate_raw(src + 2, n - 2, out, max);
    return inflate_raw(src, n, out, max);
}

#endif /* LLAMAC_INFLATE_H */
