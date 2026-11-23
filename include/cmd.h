/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Rafael Seguí Revert. See LICENSE. */
/**
 * BCL Builtin Commands
 */

#ifndef BCL_CMD_H
#define BCL_CMD_H

#include "bcl.h"

/* Command table */
#define BCL_CMD_BUCKETS 256

typedef struct bcl_cmd {
    char *name;           /* command name, uppercase */
    bcl_cmdfn_t fn;       /* function pointer */
    int argc;             /* expected arguments (-1 = special/structural) */
    void *owner;          /* owning module, NULL for builtin */
    struct bcl_cmd *next; /* next in hash bucket */
} bcl_cmd_t;

typedef struct {
    bcl_cmd_t *buckets[BCL_CMD_BUCKETS];
} bcl_cmdtable_t;

/* Command registration */
void bcl_cmd_register(bcl_interp_t *interp, const char *name, bcl_cmdfn_t fn, int argc);
void bcl_register_cmd(bcl_interp_t *interp, const char *name, bcl_cmdfn_t fn, int argc);
bcl_cmd_t *bcl_cmd_lookup(bcl_interp_t *interp, const char *name);

/* Loaded-module identity helpers for modules that need opaque ownership tags. */
void *bcl_module_current_token(void);
void *bcl_module_token_for_address(bcl_interp_t *interp, void *address);
const char *bcl_module_path_from_token(void *token);
int bcl_module_token_is_loaded(bcl_interp_t *interp, void *token);

/*
 * Per-interpreter BLL dependency graph. Dependencies are acquired by the
 * module currently running bcl_module_init and released automatically during
 * rollback or unload.
 */
enum {
    BCL_MODULE_DEP_OK = 0,
    BCL_MODULE_DEP_INVALID = -1,
    BCL_MODULE_DEP_NOT_LOADED = -2,
    BCL_MODULE_DEP_CYCLE = -3,
    BCL_MODULE_DEP_DUPLICATE = -4,
    BCL_MODULE_DEP_VERSION = -5,
    BCL_MODULE_DEP_NOMEM = -6
};

int bcl_module_dependency_acquire(bcl_interp_t *interp, void *provider_token,
                                  const char *minimum_version);
int bcl_module_dependency_release(bcl_interp_t *interp, void *provider_token);
int bcl_module_dependency_count(bcl_interp_t *interp, void *module_token);
int bcl_module_dependent_count(bcl_interp_t *interp, void *module_token);

typedef int (*bcl_module_guard_cb_t)(bcl_interp_t *interp,
                                     void *target_module_token,
                                     const char *target_module_path,
                                     void *userdata);
int bcl_module_unload_guard_register(bcl_interp_t *interp,
                                     bcl_module_guard_cb_t callback,
                                     void *userdata);
void bcl_module_unload_guard_unregister(bcl_interp_t *interp,
                                        bcl_module_guard_cb_t callback,
                                        void *userdata);

/* Optional module metadata symbol:
 *   const char *bcl_module_version(void);
 * LOAD reports its return value as version=<value>; missing/empty means unknown.
 */

/* Dispatch */
bcl_val_t bcl_dispatch(bcl_interp_t *interp, bcl_env_t *env, const char *name, int argc, bcl_val_t *argv);

/* Builtin command implementations */
bcl_val_t bcl_cmd_set(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_puts(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_putsn(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_gets(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_calc(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_incr(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_incrv(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_exists(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_arrexists(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_arrleng(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_arrkeys(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_arrget(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_arrset(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_arrdel(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_arrclear(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_strleng(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_strget(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_strrang(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_strapp(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_strins(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_strdel(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_strupp(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_strlow(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_strrev(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_streq(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_strfmt(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_strfind(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_lstleng(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_lstget(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_lstrang(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_lstfind(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_lstapp(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_lstins(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_lstdel(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_lstsort(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_lstfmt(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_exit(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_global(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_unset(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_load(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_unload(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_source(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);
bcl_val_t bcl_cmd_sourcedir(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);

/* Register all builtins */
void bcl_register_builtin_cmds(bcl_interp_t *interp);
void bcl_runtime_cleanup(bcl_interp_t *interp);

#endif /* BCL_CMD_H */
