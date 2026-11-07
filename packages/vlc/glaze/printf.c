/*
 * A complete printf and scanf for VLC (compat.h renames the standard names
 * to these). Glaze's libc printf has no long long, no floating point and few
 * flags, and VLC formats 64-bit times (I64Fd), floats and padded fields in
 * its messages, configuration file and playlists.
 *
 * Floating point is soft float on the VR4131; it is only met in messages,
 * configuration values and the like, never per sample or per pixel.
 */
#include <ctype.h>
#include <stdbool.h>

/* ---- output -------------------------------------------------------------- */
struct out {
    char *buf;
    size_t n, len;
};

static void put(struct out *o, char c)
{
    if (o->len + 1 < o->n)
        o->buf[o->len] = c;
    o->len++;
}

static void put_n(struct out *o, char c, int n)
{
    while (n-- > 0)
        put(o, c);
}

struct spec {
    bool left, plus, space, alt, zero;
    int width, prec;            /* prec -1: not given */
};

/* Emits sign/prefix, then body, padded to the field width. Zero padding
 * goes between the prefix and the body. */
static void field(struct out *o, const struct spec *sp, const char *prefix, const char *body,
                  int body_len, int zeros)
{
    int plen = (int)strlen(prefix);
    int total = plen + zeros + body_len;
    int pad = sp->width > total ? sp->width - total : 0;

    if (!sp->left && !sp->zero)
        put_n(o, ' ', pad);
    for (int i = 0; i < plen; i++)
        put(o, prefix[i]);
    if (!sp->left && sp->zero)
        put_n(o, '0', pad);
    put_n(o, '0', zeros);
    for (int i = 0; i < body_len; i++)
        put(o, body[i]);
    if (sp->left)
        put_n(o, ' ', pad);
}

static int utoa(char *end, unsigned long long v, unsigned base, bool upper)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char *p = end;

    do {
        *--p = digits[v % base];
        v /= base;
    } while (v);
    return (int)(end - p);
}

static void integer(struct out *o, struct spec sp, unsigned long long v, bool neg, unsigned base,
                    bool upper)
{
    char tmp[24], *end = tmp + sizeof(tmp);
    int len = (v == 0 && sp.prec == 0) ? 0 : utoa(end, v, base, upper);
    const char *body = end - len;
    char prefix[3] = "";
    int zeros = 0;

    if (neg)
        strcpy(prefix, "-");
    else if (sp.plus && base == 10)
        strcpy(prefix, "+");
    else if (sp.space && base == 10)
        strcpy(prefix, " ");
    if (sp.alt && base == 16 && v)
        strcpy(prefix, upper ? "0X" : "0x");
    if (sp.alt && base == 8 && (len == 0 || body[0] != '0'))
        zeros = 1;
    if (sp.prec >= 0) {
        sp.zero = false;
        if (sp.prec > len + zeros)
            zeros = sp.prec - len;
    }
    field(o, &sp, prefix, body, len, zeros);
}

/* ---- floating point ------------------------------------------------------ */
static const unsigned long long pow10_u64[] = {
    1ull, 10ull, 100ull, 1000ull, 10000ull, 100000ull, 1000000ull, 10000000ull,
    100000000ull, 1000000000ull, 10000000000ull, 100000000000ull, 1000000000000ull,
    10000000000000ull, 100000000000000ull, 1000000000000000ull, 10000000000000000ull,
    100000000000000000ull,
};
#define MAX_PREC 17

/* Digits of |v| in fixed notation with prec decimals into buf (no sign);
 * returns the length, or -1 if v is too large for 64 bits. */
static int fixed_digits(char *buf, double v, int prec, bool alt)
{
    if (v >= 1.8e19)
        return -1;
    unsigned long long ip = (unsigned long long)v;
    double frac = v - (double)ip;
    unsigned long long scale = pow10_u64[prec];
    unsigned long long fp = (unsigned long long)(frac * (double)scale + 0.5);

    if (fp >= scale) {          /* rounding carried into the integer part */
        fp -= scale;
        ip++;
    }
    char tmp[24], *end = tmp + sizeof(tmp);
    int n = utoa(end, ip, 10, false);
    memcpy(buf, end - n, (size_t)n);
    if (prec > 0 || alt)
        buf[n++] = '.';
    for (int i = prec - 1; i >= 0; i--) {
        buf[n + i] = (char)('0' + fp % 10);
        fp /= 10;
    }
    return n + prec;
}

/* v = m * 10^e with 1 <= m < 10 (v > 0). */
static int decimal_exponent(double *v)
{
    int e = 0;

    while (*v >= 1e16) { *v /= 1e16; e += 16; }
    while (*v >= 10.0) { *v /= 10.0; e++; }
    while (*v < 1e-16) { *v *= 1e16; e -= 16; }
    while (*v < 1.0) { *v *= 10.0; e--; }
    return e;
}

static int exp_digits(char *buf, double v, int prec, bool alt, bool upper)
{
    int e = 0;

    if (v != 0.0) {
        e = decimal_exponent(&v);
        /* Rounding may give 10.000: renormalise. */
        double r = v + 0.5 / (double)pow10_u64[prec];
        if (r >= 10.0) {
            v /= 10.0;
            e++;
        }
    }
    int n = fixed_digits(buf, v, prec, alt);
    buf[n++] = upper ? 'E' : 'e';
    buf[n++] = e < 0 ? '-' : '+';
    if (e < 0)
        e = -e;
    if (e >= 100)
        buf[n++] = (char)('0' + e / 100);
    buf[n++] = (char)('0' + e / 10 % 10);
    buf[n++] = (char)('0' + e % 10);
    return n;
}

static int strip_zeros(char *buf, int n)
{
    int dot = -1, exp = n;

    for (int i = 0; i < n; i++) {
        if (buf[i] == '.')
            dot = i;
        if (buf[i] == 'e' || buf[i] == 'E') {
            exp = i;
            break;
        }
    }
    if (dot < 0)
        return n;
    int end = exp;
    while (end > dot + 1 && buf[end - 1] == '0')
        end--;
    if (end == dot + 1)
        end = dot;
    memmove(buf + end, buf + exp, (size_t)(n - exp));
    return end + (n - exp);
}

static void floating(struct out *o, struct spec sp, double v, char conv)
{
    char buf[64];
    char prefix[2] = "";
    bool upper = conv == 'F' || conv == 'E' || conv == 'G';
    int n;

    if (v < 0.0 || (v == 0.0 && 1.0 / v < 0.0)) {
        v = -v;
        prefix[0] = '-';
    } else if (sp.plus) {
        prefix[0] = '+';
    } else if (sp.space) {
        prefix[0] = ' ';
    }
    if (v != v || v > 1.7976931348623157e308) {
        sp.zero = false;
        field(o, &sp, prefix, v != v ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf"), 3, 0);
        return;
    }
    int prec = sp.prec < 0 ? 6 : sp.prec > MAX_PREC ? MAX_PREC : sp.prec;

    switch (conv) {
    case 'f':
    case 'F':
        n = fixed_digits(buf, v, prec, sp.alt);
        if (n < 0)
            n = exp_digits(buf, v, prec, sp.alt, upper);
        break;
    case 'e':
    case 'E':
        n = exp_digits(buf, v, prec, sp.alt, upper);
        break;
    default: {                  /* g, G */
        int p = prec == 0 ? 1 : prec;
        int x = 0;
        if (v != 0.0) {
            double m = v;
            x = decimal_exponent(&m);
            if (m + 0.5 / (double)pow10_u64[p - 1 > MAX_PREC ? MAX_PREC : p - 1] >= 10.0)
                x++;
        }
        if (p > x && x >= -4)
            n = fixed_digits(buf, v, p - 1 - x > MAX_PREC ? MAX_PREC : p - 1 - x, sp.alt);
        else
            n = exp_digits(buf, v, p - 1, sp.alt, upper);
        if (n < 0)
            n = exp_digits(buf, v, p - 1, sp.alt, upper);
        if (!sp.alt)
            n = strip_zeros(buf, n);
        break;
    }
    }
    field(o, &sp, prefix, buf, n, 0);
}

/* ---- vsnprintf ----------------------------------------------------------- */
enum { L_INT, L_CHAR, L_SHORT, L_LONG, L_LLONG, L_SIZE, L_DOUBLE };

int glaze_vsnprintf(char *buf, size_t n, const char *fmt, va_list ap)
{
    struct out o = { buf, n, 0 };

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            put(&o, *fmt);
            continue;
        }
        struct spec sp = { .prec = -1 };
        for (;; fmt++) {
            char c = fmt[1];
            if (c == '-') sp.left = true;
            else if (c == '+') sp.plus = true;
            else if (c == ' ') sp.space = true;
            else if (c == '#') sp.alt = true;
            else if (c == '0') sp.zero = true;
            else break;
        }
        fmt++;
        if (*fmt == '*') {
            sp.width = va_arg(ap, int);
            if (sp.width < 0) {
                sp.left = true;
                sp.width = -sp.width;
            }
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9')
                sp.width = sp.width * 10 + (*fmt++ - '0');
        }
        if (*fmt == '.') {
            fmt++;
            sp.prec = 0;
            if (*fmt == '*') {
                sp.prec = va_arg(ap, int);
                fmt++;
            } else {
                while (*fmt >= '0' && *fmt <= '9')
                    sp.prec = sp.prec * 10 + (*fmt++ - '0');
            }
        }
        if (sp.left)
            sp.zero = false;

        int len = L_INT;
        switch (*fmt) {
        case 'h':
            len = fmt[1] == 'h' ? (fmt++, L_CHAR) : L_SHORT;
            fmt++;
            break;
        case 'l':
            len = fmt[1] == 'l' ? (fmt++, L_LLONG) : L_LONG;
            fmt++;
            break;
        case 'q':
        case 'j':
            len = L_LLONG;
            fmt++;
            break;
        case 'z':
        case 't':
            len = L_SIZE;
            fmt++;
            break;
        case 'L':
            len = L_DOUBLE;
            fmt++;
            break;
        }

        char conv = *fmt;
        if (!conv)
            break;
        switch (conv) {
        case 'd':
        case 'i': {
            long long v;
            if (len == L_LLONG)
                v = va_arg(ap, long long);
            else if (len == L_LONG)
                v = va_arg(ap, long);
            else
                v = va_arg(ap, int);
            if (len == L_CHAR)
                v = (signed char)v;
            else if (len == L_SHORT)
                v = (short)v;
            integer(&o, sp, v < 0 ? 0ull - (unsigned long long)v : (unsigned long long)v, v < 0, 10,
                    false);
            break;
        }
        case 'u':
        case 'o':
        case 'x':
        case 'X': {
            unsigned long long v;
            if (len == L_LLONG)
                v = va_arg(ap, unsigned long long);
            else if (len == L_LONG)
                v = va_arg(ap, unsigned long);
            else
                v = va_arg(ap, unsigned int);
            if (len == L_CHAR)
                v = (unsigned char)v;
            else if (len == L_SHORT)
                v = (unsigned short)v;
            sp.plus = sp.space = false;
            integer(&o, sp, v, false, conv == 'u' ? 10 : conv == 'o' ? 8 : 16, conv == 'X');
            break;
        }
        case 'p':
            sp.alt = true;
            sp.plus = sp.space = false;
            integer(&o, sp, (uintptr_t)va_arg(ap, void *), false, 16, false);
            break;
        case 'c': {
            char c = (char)va_arg(ap, int);
            sp.zero = false;
            field(&o, &sp, "", &c, 1, 0);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            int sl = sp.prec >= 0 ? (int)strnlen(s, (size_t)sp.prec) : (int)strlen(s);
            sp.zero = false;
            field(&o, &sp, "", s, sl, 0);
            break;
        }
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
            floating(&o, sp, va_arg(ap, double), conv);
            break;
        case 'n':
            *va_arg(ap, int *) = (int)o.len;
            break;
        case '%':
            put(&o, '%');
            break;
        default:
            put(&o, '%');
            put(&o, conv);
            break;
        }
    }
    if (n)
        buf[o.len < n ? o.len : n - 1] = 0;
    return (int)o.len;
}

int glaze_snprintf(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = glaze_vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

int glaze_vsprintf(char *buf, const char *fmt, va_list ap)
{
    return glaze_vsnprintf(buf, (size_t)INT_MAX, fmt, ap);
}

int glaze_sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = glaze_vsnprintf(buf, (size_t)INT_MAX, fmt, ap);
    va_end(ap);
    return r;
}

int glaze_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return r;
}

/* ---- vsscanf ------------------------------------------------------------- */
static void store_int(void *p, int len, unsigned long long v)
{
    switch (len) {
    case L_CHAR: *(unsigned char *)p = (unsigned char)v; break;
    case L_SHORT: *(unsigned short *)p = (unsigned short)v; break;
    case L_LONG: *(unsigned long *)p = (unsigned long)v; break;
    case L_LLONG: *(unsigned long long *)p = v; break;
    case L_SIZE: *(size_t *)p = (size_t)v; break;
    default: *(unsigned int *)p = (unsigned int)v; break;
    }
}

int glaze_vsscanf(const char *s, const char *fmt, va_list ap)
{
    const char *start = s;
    int assigned = 0;
    bool any = false;           /* a conversion was attempted on input */

    for (; *fmt; fmt++) {
        if (isspace((unsigned char)*fmt)) {
            while (isspace((unsigned char)*s))
                s++;
            continue;
        }
        if (*fmt != '%' || fmt[1] == '%') {
            if (*fmt == '%')
                fmt++;
            if (*s != *fmt)
                break;
            s++;
            continue;
        }
        fmt++;
        bool skip = false;
        if (*fmt == '*') {
            skip = true;
            fmt++;
        }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        int len = L_INT;
        if (*fmt == 'h') {
            len = fmt[1] == 'h' ? (fmt++, L_CHAR) : L_SHORT;
            fmt++;
        } else if (*fmt == 'l') {
            len = fmt[1] == 'l' ? (fmt++, L_LLONG) : L_LONG;
            fmt++;
        } else if (*fmt == 'q' || *fmt == 'j') {
            len = L_LLONG;
            fmt++;
        } else if (*fmt == 'z' || *fmt == 't') {
            len = L_SIZE;
            fmt++;
        } else if (*fmt == 'L') {
            len = L_DOUBLE;
            fmt++;
        }
        char conv = *fmt;
        if (!conv)
            break;
        if (conv == 'n') {
            if (!skip)
                *va_arg(ap, int *) = (int)(s - start);
            continue;
        }
        if (conv != 'c' && conv != '[')
            while (isspace((unsigned char)*s))
                s++;
        if (!*s)
            return any || assigned ? assigned : EOF;
        any = true;

        /* Numbers are parsed from a copy limited to the field width. */
        char tmp[64];
        size_t lim = width > 0 && width < (int)sizeof(tmp) ? (size_t)width : sizeof(tmp) - 1;
        if (strchr("diuoxXpfeEgGaA", conv)) {
            size_t k = strnlen(s, lim);
            memcpy(tmp, s, k);
            tmp[k] = 0;
        }

        switch (conv) {
        case 'd':
        case 'i':
        case 'u':
        case 'o':
        case 'x':
        case 'X':
        case 'p': {
            int base = conv == 'd' || conv == 'u' ? 10 : conv == 'o' ? 8 : conv == 'i' ? 0 : 16;
            char *end;
            unsigned long long v;
            if (conv == 'd' || conv == 'i')
                v = (unsigned long long)strtoll(tmp, &end, base);
            else
                v = strtoull(tmp, &end, base);
            if (end == tmp)
                return assigned;
            s += end - tmp;
            if (!skip) {
                if (conv == 'p')
                    *va_arg(ap, void **) = (void *)(uintptr_t)v;
                else
                    store_int(va_arg(ap, void *), len, v);
                assigned++;
            }
            break;
        }
        case 'f':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'a':
        case 'A': {
            char *end;
            double v = strtod(tmp, &end);
            if (end == tmp)
                return assigned;
            s += end - tmp;
            if (!skip) {
                if (len == L_LONG || len == L_DOUBLE)
                    *va_arg(ap, double *) = v;
                else
                    *va_arg(ap, float *) = (float)v;
                assigned++;
            }
            break;
        }
        case 's': {
            char *d = skip ? NULL : va_arg(ap, char *);
            int k = 0;
            while (*s && !isspace((unsigned char)*s) && (width == 0 || k < width)) {
                if (d)
                    d[k] = *s;
                k++;
                s++;
            }
            if (d) {
                d[k] = 0;
                assigned++;
            }
            break;
        }
        case 'c': {
            char *d = skip ? NULL : va_arg(ap, char *);
            int k = width ? width : 1;
            for (int i = 0; i < k; i++) {
                if (!*s)
                    return assigned;
                if (d)
                    d[i] = *s;
                s++;
            }
            if (d)
                assigned++;
            break;
        }
        case '[': {
            bool set[256] = { false }, neg = false;
            fmt++;
            if (*fmt == '^') {
                neg = true;
                fmt++;
            }
            if (*fmt == ']') {
                set[']'] = true;
                fmt++;
            }
            for (; *fmt && *fmt != ']'; fmt++) {
                if (fmt[1] == '-' && fmt[2] && fmt[2] != ']') {
                    for (int c = (unsigned char)fmt[0]; c <= (unsigned char)fmt[2]; c++)
                        set[c] = true;
                    fmt += 2;
                } else {
                    set[(unsigned char)*fmt] = true;
                }
            }
            if (!*fmt)
                return assigned;
            char *d = skip ? NULL : va_arg(ap, char *);
            int k = 0;
            while (*s && set[(unsigned char)*s] != neg && (width == 0 || k < width)) {
                if (d)
                    d[k] = *s;
                k++;
                s++;
            }
            if (k == 0)
                return assigned;
            if (d) {
                d[k] = 0;
                assigned++;
            }
            break;
        }
        default:
            return assigned;
        }
    }
    return assigned;
}

int glaze_sscanf(const char *s, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = glaze_vsscanf(s, fmt, ap);
    va_end(ap);
    return r;
}
