/* Glaze: one "C" locale. */
#ifndef GLAZE_VLC_LOCALE_H
#define GLAZE_VLC_LOCALE_H
#define LC_ALL 0
#define LC_CTYPE 1
#define LC_NUMERIC 2
#define LC_MESSAGES 3
static inline char *setlocale(int c, const char *l) { (void)c; (void)l; return (char *)"C"; }
#endif
