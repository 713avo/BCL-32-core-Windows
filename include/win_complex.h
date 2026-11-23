/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Rafael Seguí Revert. See LICENSE. */
/*
 * Funciones complejas de C99 para Windows.
 *
 * mingw-w64 enlaza csqrt, cabs... con ucrtbase.dll, cuyas versiones usan el
 * tipo _Dcomplex de Microsoft (otra convencion de paso de argumentos) y que ni
 * siquiera existen en todas las implementaciones (Wine no las tiene). Para no
 * depender de ellas se definen aqui con funciones reales de libm. Solo se
 * incluye en _WIN32, despues de <complex.h>.
 */
#ifndef BCL_WIN_COMPLEX_H
#define BCL_WIN_COMPLEX_H

#include <complex.h>
#include <math.h>

static inline double bcl_cabs(double complex z) { return hypot(creal(z), cimag(z)); }
static inline double bcl_carg(double complex z) { return atan2(cimag(z), creal(z)); }

static inline double complex bcl_cexp(double complex z) {
    double e = exp(creal(z));
    return CMPLX(e * cos(cimag(z)), e * sin(cimag(z)));
}

static inline double complex bcl_clog(double complex z) {
    return CMPLX(log(bcl_cabs(z)), bcl_carg(z));
}

static inline double complex bcl_csqrt(double complex z) {
    double x = creal(z), y = cimag(z), m, t;

    if (x == 0.0 && y == 0.0) return CMPLX(0.0, y);
    m = hypot(x, y);
    t = sqrt((fabs(x) + m) / 2.0);
    if (x >= 0.0) return CMPLX(t, y / (2.0 * t));
    return CMPLX(fabs(y) / (2.0 * t), copysign(t, y));
}

static inline double complex bcl_cpow(double complex a, double complex b) {
    if (creal(a) == 0.0 && cimag(a) == 0.0) {
        return (creal(b) == 0.0 && cimag(b) == 0.0) ? CMPLX(1.0, 0.0) : CMPLX(0.0, 0.0);
    }
    return bcl_cexp(b * bcl_clog(a));
}

static inline double complex bcl_csin(double complex z) {
    double x = creal(z), y = cimag(z);
    return CMPLX(sin(x) * cosh(y), cos(x) * sinh(y));
}

static inline double complex bcl_ccos(double complex z) {
    double x = creal(z), y = cimag(z);
    return CMPLX(cos(x) * cosh(y), -sin(x) * sinh(y));
}

static inline double complex bcl_csinh(double complex z) {
    double x = creal(z), y = cimag(z);
    return CMPLX(sinh(x) * cos(y), cosh(x) * sin(y));
}

static inline double complex bcl_ccosh(double complex z) {
    double x = creal(z), y = cimag(z);
    return CMPLX(cosh(x) * cos(y), sinh(x) * sin(y));
}

/* tanh(x+iy) por la formula de Kahan: no desborda para |x| grande. */
static inline double complex bcl_ctanh(double complex z) {
    double x = creal(z), y = cimag(z), t, beta, sh, rho, den;

    if (fabs(x) > 20.0) {
        return CMPLX(copysign(1.0, x), copysign(0.0, sin(2.0 * y)));
    }
    t = tan(y);
    beta = 1.0 + t * t;
    sh = sinh(x);
    rho = sqrt(1.0 + sh * sh);
    den = 1.0 + beta * sh * sh;
    return CMPLX(beta * rho * sh / den, t / den);
}

/* tan(z) = -i tanh(iz) */
static inline double complex bcl_ctan(double complex z) {
    double complex w = bcl_ctanh(CMPLX(-cimag(z), creal(z)));
    return CMPLX(cimag(w), -creal(w));
}

/* acosh(A) para A >= 1 sin desbordar A*A. */
static inline double bcl_acosh_big(double a) {
    return a > 1e8 ? log(a) + 0.69314718055994530942 : acosh(a);
}

/* casin / cacos con las formulas reales de Abramowitz-Stegun: respetan el signo
 * del cero en los cortes de rama, como exige C99. */
static inline void bcl_asin_parts(double x, double y, double *re_b, double *im_a) {
    double r = hypot(x + 1.0, y), s = hypot(x - 1.0, y);
    double a = (r + s) / 2.0, b;

    if (a < 1.0) a = 1.0;
    b = x / a;
    if (b > 1.0) b = 1.0;
    if (b < -1.0) b = -1.0;
    *re_b = b;
    *im_a = a;
}

static inline double complex bcl_casin(double complex z) {
    double b, a, y = cimag(z);
    bcl_asin_parts(creal(z), y, &b, &a);
    return CMPLX(asin(b), copysign(bcl_acosh_big(a), y));
}

static inline double complex bcl_cacos(double complex z) {
    double b, a, y = cimag(z);
    bcl_asin_parts(creal(z), y, &b, &a);
    return CMPLX(acos(b), -copysign(bcl_acosh_big(a), y));
}

/* catan(z) = (1/2) atan2(2x, 1-x^2-y^2) + (i/4) log((x^2+(y+1)^2)/(x^2+(y-1)^2)) */
static inline double complex bcl_catan(double complex z) {
    double x = creal(z), y = cimag(z), den, num;

    if (fabs(x) > 1e150 || fabs(y) > 1e150) {
        double m = fmax(fabs(x), fabs(y)), u = x / m, v = y / m, q = u * u + v * v;
        return CMPLX(copysign(1.5707963267948966192, x), (v / q) / m);
    }
    den = x * x + (y - 1.0) * (y - 1.0);
    num = x * x + (y + 1.0) * (y + 1.0);
    return CMPLX(0.5 * atan2(2.0 * x, 1.0 - x * x - y * y), 0.25 * log(num / den));
}

#define cabs bcl_cabs
#define carg bcl_carg
#define cexp bcl_cexp
#define clog bcl_clog
#define csqrt bcl_csqrt
#define cpow bcl_cpow
#define csin bcl_csin
#define ccos bcl_ccos
#define ctan bcl_ctan
#define csinh bcl_csinh
#define ccosh bcl_ccosh
#define ctanh bcl_ctanh
#define casin bcl_casin
#define cacos bcl_cacos
#define catan bcl_catan

#endif
