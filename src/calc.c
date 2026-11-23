/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Rafael Seguí Revert. See LICENSE. */
/**
 * BCL CALC - Numeric Expression Evaluator
 */

#include <ctype.h>
#include <complex.h>

/* CMPLX es de C11; algunas bibliotecas C (MinGW) no la traen. */
#ifndef CMPLX
#define CMPLX(x, y) __builtin_complex((double)(x), (double)(y))
#endif

#ifdef _WIN32
#include "win_complex.h"
#endif

/* Entero de CALC: siempre 64 bits (long es de 32 bits en Windows). */
typedef long long bcl_int;
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "calc.h"

/* Android only provides clog/cpow starting at API 26. */
#if defined(__ANDROID__) && __ANDROID_API__ < 26
static double complex calc_clog(double complex z) {
    double large = fmax(fabs(creal(z)), fabs(cimag(z)));
    double small = fmin(fabs(creal(z)), fabs(cimag(z)));
    /* Avoid overflowing |z| when log(|z|) is still representable. */
    double ratio = small / large;
    return CMPLX(log(large) + 0.5 * log1p(ratio * ratio), carg(z));
}

static double complex calc_cpow(double complex base, double complex exponent) {
    /* Zero base/exponent cases are handled by calc_checked_power. */
    return cexp(exponent * calc_clog(base));
}
#else
#define calc_clog clog
#define calc_cpow cpow
#endif

typedef struct {
    bcl_interp_t *interp;
    const char *src;
    size_t pos;
    size_t len;
} calc_ctx_t;

static double complex calc_expr(calc_ctx_t *ctx);
static double complex calc_eval_value(bcl_interp_t *interp, const char *expr);

static void calc_skip_ws(calc_ctx_t *ctx) {
    while (ctx->pos < ctx->len && isspace((unsigned char)ctx->src[ctx->pos])) {
        ctx->pos++;
    }
}

static int calc_peek(calc_ctx_t *ctx) {
    calc_skip_ws(ctx);
    return ctx->pos < ctx->len ? (unsigned char)ctx->src[ctx->pos] : 0;
}

static int calc_starts_with(calc_ctx_t *ctx, const char *text) {
    size_t len = strlen(text);
    calc_skip_ws(ctx);
    return strncmp(ctx->src + ctx->pos, text, len) == 0;
}

static int calc_match(calc_ctx_t *ctx, const char *text) {
    size_t len = strlen(text);
    calc_skip_ws(ctx);
    if (strncmp(ctx->src + ctx->pos, text, len) == 0) {
        ctx->pos += len;
        return 1;
    }
    return 0;
}

static __attribute__((noreturn)) void calc_error_code(calc_ctx_t *ctx, const char *code, const char *fmt, ...) {
    va_list ap;
    char message[256];

    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);

    bcl_raise_code(ctx->interp, "CALC", 'F', code, "%s", message);
}

static int calc_is_zero(double x) {
    return x == 0.0;
}

static int calc_is_one(double x) {
    return x == 1.0;
}

static double complex calc_normalize(double complex z) {
    double re = creal(z);
    double im = cimag(z);

    if (calc_is_zero(re)) {
        re = 0.0;
    }
    if (calc_is_zero(im)) {
        im = 0.0;
    }

    return CMPLX(re, im);
}

static int calc_is_real(double complex z) {
    return calc_is_zero(cimag(calc_normalize(z)));
}

static double calc_require_real(calc_ctx_t *ctx, double complex z, const char *what) {
    z = calc_normalize(z);
    if (!calc_is_real(z)) {
        calc_error_code(ctx, "DOMAIN", "%s requires real arguments", what);
    }
    return creal(z);
}

static void calc_require_int_like(calc_ctx_t *ctx, double complex z, const char *what, bcl_int *out) {
    double value = trunc(calc_require_real(ctx, z, what));
    /* The positive bound is exclusive: LLONG_MAX rounds up in double. */
    if (!isfinite(value) || value < (double)LLONG_MIN || value >= -(double)LLONG_MIN) {
        calc_error_code(ctx, "RANGE", "%s is outside the integer range", what);
    }
    *out = (bcl_int)value;
}

static double calc_require_order_real(calc_ctx_t *ctx, double complex z) {
    z = calc_normalize(z);
    if (!calc_is_real(z)) {
        calc_error_code(ctx, "DOMAIN", "Ordering comparisons require real arguments");
    }
    return creal(z);
}

static double complex calc_check_finite(calc_ctx_t *ctx, double complex value) {
    if (!isfinite(creal(value)) || !isfinite(cimag(value))) {
        calc_error_code(ctx, "RANGE", "Non-finite numeric result");
    }
    return value;
}

static double complex calc_checked_power(calc_ctx_t *ctx, double complex base,
                                         double complex exponent) {
    if (creal(exponent) == 0.0 && cimag(exponent) == 0.0) return 1.0;
    if (creal(base) == 0.0 && cimag(base) == 0.0) {
        if (cimag(exponent) == 0.0 && creal(exponent) > 0.0) return 0.0;
        calc_error_code(ctx, "DOMAIN", "Zero base requires a positive real exponent");
    }
    return calc_check_finite(ctx, calc_cpow(base, exponent));
}

static char *calc_format_real(bcl_interp_t *interp, double value) {
    char *buf = bcl_xmalloc(interp, 64);

    if (calc_is_zero(value)) {
        value = 0.0;
    }

    snprintf(buf, 64, "%g", value);
    return buf;
}

static char *calc_format_complex(calc_ctx_t *ctx, double complex value) {
    char *re_text = NULL;
    char *im_text = NULL;
    char *result = NULL;
    double re;
    double im;

    value = calc_normalize(value);
    re = creal(value);
    im = cimag(value);

    if (calc_is_zero(im)) {
        return calc_format_real(ctx->interp, re);
    }

    if (calc_is_zero(re)) {
        if (calc_is_one(im)) {
            return bcl_xstrdup(ctx->interp, "i");
        }
        if (calc_is_one(-im)) {
            return bcl_xstrdup(ctx->interp, "-i");
        }

        im_text = calc_format_real(ctx->interp, im);
        result = bcl_xmalloc(ctx->interp, strlen(im_text) + 2);
        snprintf(result, strlen(im_text) + 2, "%si", im_text);
        free(im_text);
        return result;
    }

    re_text = calc_format_real(ctx->interp, re);
    if (calc_is_one(im)) {
        result = bcl_xmalloc(ctx->interp, strlen(re_text) + 3);
        snprintf(result, strlen(re_text) + 3, "%s+i", re_text);
    } else if (calc_is_one(-im)) {
        result = bcl_xmalloc(ctx->interp, strlen(re_text) + 3);
        snprintf(result, strlen(re_text) + 3, "%s-i", re_text);
    } else {
        im_text = calc_format_real(ctx->interp, fabs(im));
        result = bcl_xmalloc(ctx->interp, strlen(re_text) + strlen(im_text) + 3);
        snprintf(result, strlen(re_text) + strlen(im_text) + 3, "%s%c%si", re_text, im < 0.0 ? '-' : '+', im_text);
    }

    free(re_text);
    free(im_text);
    return result;
}

static double calc_parse_real_literal(calc_ctx_t *ctx) {
    const char *start;
    char *end = NULL;

    calc_skip_ws(ctx);
    start = ctx->src + ctx->pos;

    if (*start == '\0') {
        calc_error_code(ctx, "SYNTAX", "Expected number");
    }

    if (start[0] == '0' && (start[1] == 'b' || start[1] == 'B')) {
        bcl_int value = 0;
        size_t pos = ctx->pos + 2;

        if (pos >= ctx->len || (ctx->src[pos] != '0' && ctx->src[pos] != '1')) {
            calc_error_code(ctx, "BADVALUE", "Invalid binary literal");
        }

        while (pos < ctx->len && (ctx->src[pos] == '0' || ctx->src[pos] == '1')) {
            int bit = ctx->src[pos] - '0';
            if (value > (LLONG_MAX - bit) / 2) {
                calc_error_code(ctx, "RANGE", "Binary literal exceeds the integer range");
            }
            value = value * 2 + bit;
            pos++;
        }

        ctx->pos = pos;
        return (double)value;
    }

    if (start[0] == '0' && (start[1] == 'x' || start[1] == 'X')) {
        errno = 0;
        bcl_int value = strtoll(start, &end, 16);
        if (errno == ERANGE) calc_error_code(ctx, "RANGE", "Hexadecimal literal exceeds the integer range");
        if (end == start) {
            calc_error_code(ctx, "BADVALUE", "Invalid hexadecimal literal");
        }
        ctx->pos = (size_t)(end - ctx->src);
        return (double)value;
    }

    if (start[0] == '0' && isdigit((unsigned char)start[1])) {
        const char *p = start + 1;
        int octal = 1;

        while (isdigit((unsigned char)*p)) {
            if (*p >= '8') {
                octal = 0;
            }
            p++;
        }

        if (*p != '.' && *p != 'e' && *p != 'E' && octal) {
            errno = 0;
            bcl_int value = strtoll(start, &end, 8);
            if (errno == ERANGE) calc_error_code(ctx, "RANGE", "Octal literal exceeds the integer range");
            ctx->pos = (size_t)(end - ctx->src);
            return (double)value;
        }
    }

    {
        double value = strtod(start, &end);
        if (end == start) {
            calc_error_code(ctx, "BADVALUE", "Invalid numeric literal");
        }
        if (!isfinite(value)) calc_error_code(ctx, "RANGE", "Numeric literal is not finite");
        ctx->pos = (size_t)(end - ctx->src);
        return value;
    }
}

typedef enum {
    CALC_FN_ABS,
    CALC_FN_ACOS,
    CALC_FN_ARG,
    CALC_FN_ASIN,
    CALC_FN_ATAN,
    CALC_FN_ATAN2,
    CALC_FN_BOOL,
    CALC_FN_CEIL,
    CALC_FN_COMPLEX,
    CALC_FN_CONJ,
    CALC_FN_COS,
    CALC_FN_COSH,
    CALC_FN_DOUBLE,
    CALC_FN_ENTIER,
    CALC_FN_EXP,
    CALC_FN_FMOD,
    CALC_FN_FLOOR,
    CALC_FN_HYPOT,
    CALC_FN_IMAG,
    CALC_FN_INT,
    CALC_FN_ISQRT,
    CALC_FN_LOG,
    CALC_FN_LOG10,
    CALC_FN_MAX,
    CALC_FN_MIN,
    CALC_FN_NORM,
    CALC_FN_POLAR,
    CALC_FN_POW,
    CALC_FN_RAND,
    CALC_FN_REAL,
    CALC_FN_ROUND,
    CALC_FN_SIN,
    CALC_FN_SINH,
    CALC_FN_SQRT,
    CALC_FN_SRAND,
    CALC_FN_TAN,
    CALC_FN_TANH,
    CALC_FN_WIDE
} calc_fn_id_t;

typedef struct {
    const char *name;
    int min_args;
    int max_args;
    calc_fn_id_t id;
} calc_fn_entry_t;

static int calc_fn_entry_cmp(const void *key, const void *entry) {
    const char *name = (const char *)key;
    const calc_fn_entry_t *fn = (const calc_fn_entry_t *)entry;

    return strcmp(name, fn->name);
}

static const calc_fn_entry_t *calc_find_function(const char *name) {
    static const calc_fn_entry_t functions[] = {
        {"abs", 1, 1, CALC_FN_ABS},
        {"acos", 1, 1, CALC_FN_ACOS},
        {"arg", 1, 1, CALC_FN_ARG},
        {"asin", 1, 1, CALC_FN_ASIN},
        {"atan", 1, 1, CALC_FN_ATAN},
        {"atan2", 2, 2, CALC_FN_ATAN2},
        {"bool", 1, 1, CALC_FN_BOOL},
        {"ceil", 1, 1, CALC_FN_CEIL},
        {"complex", 2, 2, CALC_FN_COMPLEX},
        {"conj", 1, 1, CALC_FN_CONJ},
        {"cos", 1, 1, CALC_FN_COS},
        {"cosh", 1, 1, CALC_FN_COSH},
        {"double", 1, 1, CALC_FN_DOUBLE},
        {"entier", 1, 1, CALC_FN_ENTIER},
        {"exp", 1, 1, CALC_FN_EXP},
        {"floor", 1, 1, CALC_FN_FLOOR},
        {"fmod", 2, 2, CALC_FN_FMOD},
        {"hypot", 2, 2, CALC_FN_HYPOT},
        {"imag", 1, 1, CALC_FN_IMAG},
        {"int", 1, 1, CALC_FN_INT},
        {"isqrt", 1, 1, CALC_FN_ISQRT},
        {"log", 1, 1, CALC_FN_LOG},
        {"log10", 1, 1, CALC_FN_LOG10},
        {"max", 2, 2, CALC_FN_MAX},
        {"min", 2, 2, CALC_FN_MIN},
        {"norm", 1, 1, CALC_FN_NORM},
        {"polar", 2, 2, CALC_FN_POLAR},
        {"pow", 2, 2, CALC_FN_POW},
        {"rand", 0, 2, CALC_FN_RAND},
        {"real", 1, 1, CALC_FN_REAL},
        {"round", 1, 1, CALC_FN_ROUND},
        {"sin", 1, 1, CALC_FN_SIN},
        {"sinh", 1, 1, CALC_FN_SINH},
        {"sqrt", 1, 1, CALC_FN_SQRT},
        {"srand", 1, 1, CALC_FN_SRAND},
        {"tan", 1, 1, CALC_FN_TAN},
        {"tanh", 1, 1, CALC_FN_TANH},
        {"wide", 1, 1, CALC_FN_WIDE}
    };

    return bsearch(name, functions, sizeof(functions) / sizeof(functions[0]),
                   sizeof(functions[0]), calc_fn_entry_cmp);
}

static double complex calc_call_function_impl(calc_ctx_t *ctx, const char *name, double complex *args, int nargs) {
    const calc_fn_entry_t *fn = calc_find_function(name);
    int i;

    if (!fn || nargs < fn->min_args || nargs > fn->max_args) {
        calc_error_code(ctx, "UNKNOWN", "Unknown function '%s'", name);
    }

    for (i = 0; i < nargs; i++) {
        args[i] = calc_normalize(calc_check_finite(ctx, args[i]));
    }

    switch (fn->id) {
        case CALC_FN_ABS:
            return cabs(args[0]) + 0.0 * I;
        case CALC_FN_ACOS:
            return cacos(args[0]);
        case CALC_FN_ARG:
            return carg(args[0]) + 0.0 * I;
        case CALC_FN_ASIN:
            return casin(args[0]);
        case CALC_FN_ATAN:
            if (creal(args[0]) == 0.0 && fabs(cimag(args[0])) == 1.0) {
                calc_error_code(ctx, "DOMAIN", "atan has a pole at +i and -i");
            }
            return catan(args[0]);
        case CALC_FN_ATAN2: {
            double y = calc_require_real(ctx, args[0], "atan2");
            double x = calc_require_real(ctx, args[1], "atan2");
            return atan2(y, x) + 0.0 * I;
        }
        case CALC_FN_BOOL: {
            double value = calc_require_real(ctx, args[0], "bool");
            return (value != 0.0 ? 1.0 : 0.0) + 0.0 * I;
        }
        case CALC_FN_CEIL: {
            double value = calc_require_real(ctx, args[0], "ceil");
            return ceil(value) + 0.0 * I;
        }
        case CALC_FN_COMPLEX: {
            double re = calc_require_real(ctx, args[0], "complex");
            double im = calc_require_real(ctx, args[1], "complex");
            return CMPLX(re, im);
        }
        case CALC_FN_CONJ:
            return conj(args[0]);
        case CALC_FN_COS:
            return ccos(args[0]);
        case CALC_FN_COSH:
            return ccosh(args[0]);
        case CALC_FN_DOUBLE: {
            double value = calc_require_real(ctx, args[0], "double");
            return value + 0.0 * I;
        }
        case CALC_FN_ENTIER: {
            double value = calc_require_real(ctx, args[0], "entier");
            return floor(value) + 0.0 * I;
        }
        case CALC_FN_EXP:
            return cexp(args[0]);
        case CALC_FN_FMOD: {
            double left = calc_require_real(ctx, args[0], "fmod");
            double right = calc_require_real(ctx, args[1], "fmod");
            if (right == 0.0) calc_error_code(ctx, "DOMAIN", "fmod divisor must be non-zero");
            return fmod(left, right) + 0.0 * I;
        }
        case CALC_FN_FLOOR: {
            double value = calc_require_real(ctx, args[0], "floor");
            return floor(value) + 0.0 * I;
        }
        case CALC_FN_HYPOT: {
            double left = calc_require_real(ctx, args[0], "hypot");
            double right = calc_require_real(ctx, args[1], "hypot");
            return hypot(left, right) + 0.0 * I;
        }
        case CALC_FN_IMAG:
            return cimag(args[0]) + 0.0 * I;
        case CALC_FN_INT: {
            bcl_int value;
            calc_require_int_like(ctx, args[0], "int", &value);
            return (double)value;
        }
        case CALC_FN_ISQRT: {
            double value = calc_require_real(ctx, args[0], "isqrt");
            if (value < 0.0) {
                calc_error_code(ctx, "DOMAIN", "isqrt() requires a non-negative real argument");
            }
            return floor(sqrt(value)) + 0.0 * I;
        }
        case CALC_FN_LOG:
            if (creal(args[0]) == 0.0 && cimag(args[0]) == 0.0) {
                calc_error_code(ctx, "UNDEF", "log(0) is undefined");
            }
            return calc_clog(args[0]);
        case CALC_FN_LOG10:
            if (creal(args[0]) == 0.0 && cimag(args[0]) == 0.0) {
                calc_error_code(ctx, "UNDEF", "log10(0) is undefined");
            }
            return calc_clog(args[0]) / log(10.0);
        case CALC_FN_MAX: {
            double left = calc_require_real(ctx, args[0], "max");
            double right = calc_require_real(ctx, args[1], "max");
            return (left > right ? left : right) + 0.0 * I;
        }
        case CALC_FN_MIN: {
            double left = calc_require_real(ctx, args[0], "min");
            double right = calc_require_real(ctx, args[1], "min");
            return (left < right ? left : right) + 0.0 * I;
        }
        case CALC_FN_NORM: {
            double mag = cabs(args[0]);
            return (mag * mag) + 0.0 * I;
        }
        case CALC_FN_POLAR: {
            double r = calc_require_real(ctx, args[0], "polar");
            double theta = calc_require_real(ctx, args[1], "polar");
            return r * (cos(theta) + I * sin(theta));
        }
        case CALC_FN_POW:
            return calc_checked_power(ctx, args[0], args[1]);
        case CALC_FN_RAND:
            if (nargs == 0) {
                return (double)(rand() % 32768) + 0.0 * I;
            }
            if (nargs == 1) {
                double hi = calc_require_real(ctx, args[0], "rand");
                double unit = (double)rand() / (double)RAND_MAX;
                return (unit * hi) + 0.0 * I;
            }
            {
                double lo = calc_require_real(ctx, args[0], "rand");
                double hi = calc_require_real(ctx, args[1], "rand");
                double unit = (double)rand() / (double)RAND_MAX;
                if (hi < lo) {
                    double tmp = lo;
                    lo = hi;
                    hi = tmp;
                }
                return (lo + unit * (hi - lo)) + 0.0 * I;
            }
        case CALC_FN_REAL:
            return creal(args[0]) + 0.0 * I;
        case CALC_FN_ROUND: {
            double value = calc_require_real(ctx, args[0], "round");
            return round(value) + 0.0 * I;
        }
        case CALC_FN_SIN:
            return csin(args[0]);
        case CALC_FN_SINH:
            return csinh(args[0]);
        case CALC_FN_SQRT:
            return csqrt(args[0]);
        case CALC_FN_SRAND: {
            double seed = calc_require_real(ctx, args[0], "srand");
            double integer_seed = trunc(seed);
            if (!isfinite(seed) || integer_seed < 0.0 || integer_seed > (double)UINT_MAX) {
                calc_error_code(ctx, "RANGE", "srand seed is outside the unsigned integer range");
            }
            srand((unsigned int)integer_seed);
            return seed + 0.0 * I;
        }
        case CALC_FN_TAN:
            return ctan(args[0]);
        case CALC_FN_TANH:
            return ctanh(args[0]);
        case CALC_FN_WIDE: {
            double value = calc_require_real(ctx, args[0], "wide");
            value = trunc(value);
            if (!isfinite(value) || value < (double)LLONG_MIN || value >= -(double)LLONG_MIN) {
                calc_error_code(ctx, "RANGE", "wide is outside the integer range");
            }
            return (double)(long long)value;
        }
    }

    calc_error_code(ctx, "UNKNOWN", "Unknown function '%s'", name);
    return 0.0;
}

static double complex calc_call_function(calc_ctx_t *ctx, const char *name,
                                         double complex *args, int nargs) {
    return calc_check_finite(ctx, calc_call_function_impl(ctx, name, args, nargs));
}

static double complex calc_primary(calc_ctx_t *ctx) {
    int c = calc_peek(ctx);

    if (c == '(') {
        ctx->pos++;
        {
            double complex value = calc_expr(ctx);
            if (!calc_match(ctx, ")")) {
                calc_error_code(ctx, "SYNTAX", "Expected ')'");
            }
            return value;
        }
    }

    if (c == '"') {
        size_t start;
        char *inner;
        size_t out = 0;
        double complex value;

        ctx->pos++;
        start = ctx->pos;

        while (ctx->pos < ctx->len && ctx->src[ctx->pos] != '"') {
            if (ctx->src[ctx->pos] == '\\' && ctx->pos + 1 < ctx->len) {
                ctx->pos += 2;
            } else {
                ctx->pos++;
            }
        }

        if (ctx->pos >= ctx->len) {
            calc_error_code(ctx, "UNTERM", "Unterminated string literal");
        }

        inner = bcl_xmalloc(ctx->interp, (ctx->pos - start) + 1);
        ctx->pos = start;
        while (ctx->pos < ctx->len && ctx->src[ctx->pos] != '"') {
            if (ctx->src[ctx->pos] == '\\' && ctx->pos + 1 < ctx->len) {
                ctx->pos++;
            }
            inner[out++] = ctx->src[ctx->pos++];
        }
        inner[out] = '\0';

        if (!calc_match(ctx, "\"")) {
            free(inner);
            calc_error_code(ctx, "UNTERM", "Unterminated string literal");
        }

        value = calc_eval_value(ctx->interp, inner);
        free(inner);
        return value;
    }

    if (isdigit((unsigned char)c) || c == '.') {
        double value = calc_parse_real_literal(ctx);
        if (ctx->pos < ctx->len && ctx->src[ctx->pos] == 'i') {
            ctx->pos++;
            return 0.0 + value * I;
        }
        return value + 0.0 * I;
    }

    if (isalpha((unsigned char)c) || c == '_') {
        size_t start = ctx->pos;
        char *name;

        while (ctx->pos < ctx->len &&
               (isalnum((unsigned char)ctx->src[ctx->pos]) || ctx->src[ctx->pos] == '_')) {
            ctx->pos++;
        }

        name = bcl_xmemdup0(ctx->interp, ctx->src + start, ctx->pos - start);
        calc_skip_ws(ctx);

        if (ctx->pos < ctx->len && ctx->src[ctx->pos] == '(') {
            double complex args[16];
            int nargs = 0;

            ctx->pos++;
            calc_skip_ws(ctx);
            if (!calc_match(ctx, ")")) {
                args[nargs++] = calc_expr(ctx);
                while (calc_match(ctx, ",")) {
                    if (nargs >= 16) {
                        free(name);
                        calc_error_code(ctx, "BADARGS", "Too many function arguments");
                    }
                    args[nargs++] = calc_expr(ctx);
                }
                if (!calc_match(ctx, ")")) {
                    free(name);
                    calc_error_code(ctx, "SYNTAX", "Expected ')' after function arguments");
                }
            }

            {
                double complex value = calc_call_function(ctx, name, args, nargs);
                free(name);
                return value;
            }
        }

        if (strcmp(name, "i") == 0) {
            free(name);
            return 0.0 + 1.0 * I;
        }

        free(name);
        calc_error_code(ctx, "UNKNOWN", "Unknown symbol");
    }

    calc_error_code(ctx, "SYNTAX", "Unexpected token");
    return 0.0;
}

static double complex calc_unary(calc_ctx_t *ctx) {
    if (calc_match(ctx, "-")) return -calc_unary(ctx);
    if (calc_match(ctx, "+")) return calc_unary(ctx);
    if (calc_match(ctx, "!")) {
        double value = calc_require_real(ctx, calc_unary(ctx), "logical operator '!'");
        return (value == 0.0 ? 1.0 : 0.0) + 0.0 * I;
    }
    if (calc_match(ctx, "~")) {
        bcl_int value;
        calc_require_int_like(ctx, calc_unary(ctx), "bitwise operator '~'", &value);
        return (double)(~value) + 0.0 * I;
    }
    return calc_primary(ctx);
}

static double complex calc_power(calc_ctx_t *ctx) {
    double complex left = calc_unary(ctx);
    if (calc_match(ctx, "**")) {
        return calc_checked_power(ctx, left, calc_power(ctx));
    }
    return left;
}

static double complex calc_multiplicative(calc_ctx_t *ctx) {
    double complex left = calc_power(ctx);

    while (1) {
        if (calc_match(ctx, "*")) {
            left *= calc_power(ctx);
        } else if (calc_match(ctx, "/")) {
            double complex right = calc_power(ctx);
            if (creal(right) == 0.0 && cimag(right) == 0.0) {
                calc_error_code(ctx, "DOMAIN", "Division by zero");
            } else {
                left /= right;
            }
        } else if (calc_match(ctx, "%")) {
            bcl_int lval;
            bcl_int rval;
            calc_require_int_like(ctx, left, "modulo operator '%'", &lval);
            calc_require_int_like(ctx, calc_power(ctx), "modulo operator '%'", &rval);
            if (rval == 0) calc_error_code(ctx, "DOMAIN", "Modulo divisor must be non-zero");
            /* LLONG_MIN % -1 is undefined in C; the remainder is zero. */
            left = (lval == LLONG_MIN && rval == -1)
                ? 0.0 : (double)(lval % rval);
        } else {
            return left;
        }
        left = calc_check_finite(ctx, left);
    }
}

static double complex calc_additive(calc_ctx_t *ctx) {
    double complex left = calc_multiplicative(ctx);

    while (1) {
        if (calc_match(ctx, "+")) {
            left += calc_multiplicative(ctx);
        } else if (calc_match(ctx, "-")) {
            left -= calc_multiplicative(ctx);
        } else {
            return left;
        }
        left = calc_check_finite(ctx, left);
    }
}

static double complex calc_shift(calc_ctx_t *ctx) {
    double complex left = calc_additive(ctx);

    while (1) {
        if (calc_match(ctx, "<<")) {
            bcl_int lval;
            bcl_int rval;
            calc_require_int_like(ctx, left, "shift operator '<<'", &lval);
            calc_require_int_like(ctx, calc_additive(ctx), "shift operator '<<'", &rval);
            if (lval < 0) {
                calc_error_code(ctx, "BADVALUE", "Left shift operand must be non-negative");
            }
            if (rval < 0 || rval >= (bcl_int)(sizeof(bcl_int) * CHAR_BIT)) {
                calc_error_code(ctx, "BADVALUE", "Shift count out of range");
            }
            if (lval > (LLONG_MAX >> rval)) {
                calc_error_code(ctx, "RANGE", "Left shift result exceeds the integer range");
            }
            left = (double)(lval << rval);
        } else if (calc_match(ctx, ">>")) {
            bcl_int lval;
            bcl_int rval;
            calc_require_int_like(ctx, left, "shift operator '>>'", &lval);
            calc_require_int_like(ctx, calc_additive(ctx), "shift operator '>>'", &rval);
            if (rval < 0 || rval >= (bcl_int)(sizeof(bcl_int) * CHAR_BIT)) {
                calc_error_code(ctx, "BADVALUE", "Shift count out of range");
            }
            left = (double)(lval >> rval);
        } else {
            return left;
        }
    }
}

static double complex calc_relational(calc_ctx_t *ctx) {
    double complex left = calc_shift(ctx);

    while (1) {
        if (calc_match(ctx, "<=")) {
            double lhs = calc_require_order_real(ctx, left);
            double rhs = calc_require_order_real(ctx, calc_shift(ctx));
            left = lhs <= rhs ? 1.0 : 0.0;
        } else if (calc_match(ctx, ">=")) {
            double lhs = calc_require_order_real(ctx, left);
            double rhs = calc_require_order_real(ctx, calc_shift(ctx));
            left = lhs >= rhs ? 1.0 : 0.0;
        } else if (calc_match(ctx, "<")) {
            double lhs = calc_require_order_real(ctx, left);
            double rhs = calc_require_order_real(ctx, calc_shift(ctx));
            left = lhs < rhs ? 1.0 : 0.0;
        } else if (calc_match(ctx, ">")) {
            double lhs = calc_require_order_real(ctx, left);
            double rhs = calc_require_order_real(ctx, calc_shift(ctx));
            left = lhs > rhs ? 1.0 : 0.0;
        } else {
            return left;
        }
    }
}

static double complex calc_equality(calc_ctx_t *ctx) {
    double complex left = calc_relational(ctx);

    while (1) {
        if (calc_match(ctx, "==")) {
            double complex diff = calc_normalize(left - calc_relational(ctx));
            left = cabs(diff) < 1e-12 ? 1.0 : 0.0;
        } else if (calc_match(ctx, "!=")) {
            double complex diff = calc_normalize(left - calc_relational(ctx));
            left = cabs(diff) >= 1e-12 ? 1.0 : 0.0;
        } else {
            return left;
        }
    }
}

static double complex calc_bitand(calc_ctx_t *ctx) {
    double complex left = calc_equality(ctx);

    while (!calc_starts_with(ctx, "&&") && calc_match(ctx, "&")) {
        bcl_int lval;
        bcl_int rval;
        calc_require_int_like(ctx, left, "bitwise operator '&'", &lval);
        calc_require_int_like(ctx, calc_equality(ctx), "bitwise operator '&'", &rval);
        left = (double)(lval & rval);
    }

    return left;
}

static double complex calc_xor(calc_ctx_t *ctx) {
    double complex left = calc_bitand(ctx);

    while (calc_match(ctx, "^")) {
        bcl_int lval;
        bcl_int rval;
        calc_require_int_like(ctx, left, "bitwise operator '^'", &lval);
        calc_require_int_like(ctx, calc_bitand(ctx), "bitwise operator '^'", &rval);
        left = (double)(lval ^ rval);
    }

    return left;
}

static double complex calc_bitor(calc_ctx_t *ctx) {
    double complex left = calc_xor(ctx);

    while (!calc_starts_with(ctx, "||") && calc_match(ctx, "|")) {
        bcl_int lval;
        bcl_int rval;
        calc_require_int_like(ctx, left, "bitwise operator '|'", &lval);
        calc_require_int_like(ctx, calc_xor(ctx), "bitwise operator '|'", &rval);
        left = (double)(lval | rval);
    }

    return left;
}

static double complex calc_and(calc_ctx_t *ctx) {
    double complex left = calc_bitor(ctx);

    while (calc_match(ctx, "&&")) {
        double lhs = calc_require_real(ctx, left, "logical operator '&&'");
        double rhs = calc_require_real(ctx, calc_bitor(ctx), "logical operator '&&'");
        left = (lhs != 0.0 && rhs != 0.0) ? 1.0 : 0.0;
    }

    return left;
}

static double complex calc_or(calc_ctx_t *ctx) {
    double complex left = calc_and(ctx);

    while (calc_match(ctx, "||")) {
        double lhs = calc_require_real(ctx, left, "logical operator '||'");
        double rhs = calc_require_real(ctx, calc_and(ctx), "logical operator '||'");
        left = (lhs != 0.0 || rhs != 0.0) ? 1.0 : 0.0;
    }

    return left;
}

static double complex calc_expr(calc_ctx_t *ctx) {
    return calc_or(ctx);
}

static double complex calc_eval_value(bcl_interp_t *interp, const char *expr) {
    calc_ctx_t ctx;
    double complex value;

    ctx.interp = interp;
    ctx.src = expr;
    ctx.pos = 0;
    ctx.len = strlen(expr);

    value = calc_expr(&ctx);
    calc_skip_ws(&ctx);
    if (ctx.pos != ctx.len) {
        calc_error_code(&ctx, "SYNTAX", "Trailing input");
    }

    return calc_normalize(calc_check_finite(&ctx, value));
}

char *bcl_calc_eval(bcl_interp_t *interp, const char *expr, void *env) {
    double complex value;
    (void)env;

    value = calc_eval_value(interp, expr);
    {
        calc_ctx_t ctx;
        ctx.interp = interp;
        ctx.src = expr;
        ctx.pos = 0;
        ctx.len = strlen(expr);
        return calc_format_complex(&ctx, value);
    }
}

int bcl_calc_is_numeric(const char *s, double *out) {
    calc_ctx_t ctx;
    const char *start;
    char *end = NULL;
    double value;

    if (!s || !*s) {
        return 0;
    }

    ctx.interp = NULL;
    ctx.src = s;
    ctx.pos = 0;
    ctx.len = strlen(s);
    calc_skip_ws(&ctx);
    start = ctx.src + ctx.pos;

    if (start[0] == '0' && (start[1] == 'b' || start[1] == 'B')) {
        bcl_int acc = 0;
        size_t pos = ctx.pos + 2;

        if (pos >= ctx.len || (ctx.src[pos] != '0' && ctx.src[pos] != '1')) {
            return 0;
        }

        while (pos < ctx.len && (ctx.src[pos] == '0' || ctx.src[pos] == '1')) {
            acc = (acc << 1) | (ctx.src[pos] - '0');
            pos++;
        }

        ctx.pos = pos;
        value = (double)acc;
    } else if (start[0] == '0' && (start[1] == 'x' || start[1] == 'X')) {
        value = strtoll(start, &end, 16);
        if (end == start) {
            return 0;
        }
        ctx.pos = (size_t)(end - ctx.src);
    } else if (start[0] == '0' && isdigit((unsigned char)start[1]) &&
               !strchr(start, '.') && !strchr(start, 'e') && !strchr(start, 'E')) {
        value = strtoll(start, &end, 8);
        if (end == start) {
            return 0;
        }
        ctx.pos = (size_t)(end - ctx.src);
    } else {
        value = strtod(start, &end);
        if (end == start) {
            return 0;
        }
        ctx.pos = (size_t)(end - ctx.src);
    }

    calc_skip_ws(&ctx);
    if (ctx.pos != ctx.len) {
        return 0;
    }

    if (out) {
        *out = value;
    }
    return 1;
}

char *bcl_calc_result(bcl_interp_t *interp, double value) {
    return calc_format_real(interp, value);
}
