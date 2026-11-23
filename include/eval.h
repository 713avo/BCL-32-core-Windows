/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Rafael Seguí Revert. See LICENSE. */
/**
 * BCL Evaluator
 */

#ifndef BCL_EVAL_H
#define BCL_EVAL_H

#include "bcl.h"
#include "parser.h"

/* Environment/Variable management */
#define BCL_ENV_VAR_BUCKETS 256
#define BCL_ARRAY_ALIAS_BUCKETS 64
#define BCL_ARRAY_BUCKETS 128
#define BCL_PROC_BUCKETS 256

typedef struct bcl_array_alias {
    char *name;
    struct bcl_env *target_env;
    struct bcl_array_alias *next;
    struct bcl_array_alias *hash_next;
} bcl_array_alias_t;

typedef struct bcl_var {
    char *name;
    bcl_val_t value;
    int protected;
    struct bcl_var *alias_target;
    struct bcl_var *next;
    struct bcl_var *hash_next;
    struct bcl_var *array_next;
    struct bcl_array_bucket *array_bucket;
} bcl_var_t;

typedef struct bcl_array_bucket {
    char *base;
    bcl_var_t *vars;
    int count;
    struct bcl_array_bucket *next;
} bcl_array_bucket_t;

struct bcl_env {
    bcl_var_t *vars;
    bcl_var_t *var_buckets[BCL_ENV_VAR_BUCKETS];
    bcl_array_bucket_t *array_buckets[BCL_ARRAY_BUCKETS];
    bcl_array_alias_t *array_aliases;
    bcl_array_alias_t *array_alias_buckets[BCL_ARRAY_ALIAS_BUCKETS];
    struct bcl_env *parent;  /* parent scope (NULL = global) */
};

typedef struct bcl_proc {
    char *name;
    bcl_param_t *params;
    int nparams;
    int nrequired;
    char *source_name;
    bcl_node_t *body;
    char *body_src;
    struct bcl_proc *next;
    struct bcl_proc *hash_next;
} bcl_proc_t;

/* Evaluator functions */
bcl_result_t bcl_eval_node(bcl_interp_t *interp, bcl_env_t *env, bcl_node_t *node);
bcl_result_t bcl_eval_block(bcl_interp_t *interp, bcl_env_t *env, bcl_node_t *block);
bcl_result_t bcl_eval_source(bcl_interp_t *interp, bcl_env_t *env, const char *src);
bcl_result_t bcl_eval_source_named(bcl_interp_t *interp, bcl_env_t *env, const char *src, const char *source_name);
bcl_result_t bcl_eval_invoke(bcl_interp_t *interp, bcl_env_t *env, const char *name, int argc, bcl_val_t *argv);

/* Condition evaluation */
int bcl_eval_condition(bcl_interp_t *interp, bcl_env_t *env, bcl_node_t *cond);

/* Environment functions */
bcl_env_t *bcl_env_new(bcl_env_t *parent);
void bcl_env_free(bcl_env_t *env);
bcl_val_t bcl_env_get(bcl_env_t *env, const char *name);
int bcl_env_exists(bcl_env_t *env, const char *name);
void bcl_env_set(bcl_interp_t *interp, bcl_env_t *env, const char *name, const char *value);
void bcl_env_set_protected(bcl_interp_t *interp, bcl_env_t *env, const char *name, const char *value);
int bcl_env_bind_global(bcl_interp_t *interp, bcl_env_t *env, const char *name);
int bcl_env_unset(bcl_interp_t *interp, bcl_env_t *env, const char *name);

#endif /* BCL_EVAL_H */
