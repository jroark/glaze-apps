/* Glaze: the few libm functions VLC's kept code calls (glaze/compat.c). */
#ifndef GLAZE_VLC_MATH_H
#define GLAZE_VLC_MATH_H
double floor(double);
double ceil(double);
double fabs(double);
double sqrt(double);
double pow(double, double);
double log(double);
double log10(double);
double exp(double);
float floorf(float);
float sqrtf(float);
float powf(float, float);
float log10f(float);
#define HUGE_VAL (__builtin_huge_val())
#endif
