/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Rafael Seguí Revert. See LICENSE. */
/**
 * BCL CALC - Numeric Expression Evaluator
 */

#ifndef BCL_CALC_H
#define BCL_CALC_H

#include "bcl.h"

/* Evaluate a CALC expression and return the formatted result. */
char *bcl_calc_eval(bcl_interp_t *interp, const char *expr, void *env);

/* Check if a string is numeric */
int bcl_calc_is_numeric(const char *s, double *out);

/* Convert result to string */
char *bcl_calc_result(bcl_interp_t *interp, double value);

#endif /* BCL_CALC_H */
