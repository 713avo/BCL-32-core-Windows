/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Rafael Seguí Revert. See LICENSE. */
/**
 * BCL Interpreter - Main header
 */

#ifndef BCL_H
#define BCL_H

#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>

#define BCL_VERSION "1.2"
#define BCL_MAX_PARSE_DEPTH 128
#define BCL_MAX_COND_DEPTH 128
#define BCL_MAX_PROC_DEPTH 128
#define BCL_MAX_SUBCMD_DEPTH 128

/* Forward declarations */
typedef struct bcl_interp bcl_interp_t;
typedef struct bcl_val bcl_val_t;
typedef struct bcl_env bcl_env_t;
typedef struct bcl_node bcl_node_t;
typedef struct bcl_try_frame bcl_try_frame_t;
typedef struct bcl_handler bcl_handler_t;
typedef struct bcl_exec_frame bcl_exec_frame_t;

/* Value type - all values are strings */
struct bcl_val {
    char *data;    /* string content, always null-terminated */
    size_t len;    /* length in bytes without null terminator */
};

struct bcl_try_frame {
    jmp_buf env;
    struct bcl_try_frame *prev;
};

typedef enum {
    BCL_FRAME_FILE,
    BCL_FRAME_SOURCE,
    BCL_FRAME_PROC,
    BCL_FRAME_SUBCMD,
    BCL_FRAME_CALLBACK
} bcl_framekind_t;

struct bcl_exec_frame {
    bcl_framekind_t kind;
    const char *name;
    const char *call_source;
    int call_line;
    int call_col;
    struct bcl_exec_frame *prev;
};

/* Evaluation result */
typedef enum {
    EVAL_OK,
    EVAL_RETURN,
    EVAL_BREAK,
    EVAL_NEXT,
    EVAL_ERROR,
    EVAL_EXIT
} bcl_evalstatus_t;

typedef struct {
    bcl_evalstatus_t status;
    bcl_val_t value;
    int exit_code;
} bcl_result_t;

/* Command function pointer */
typedef bcl_val_t (*bcl_cmdfn_t)(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv);

/* Interpreter state */
struct bcl_interp {
    bcl_env_t *global_env;
    void *commands;       /* bcl_cmdtable_t */
    void *procs;          /* bcl_proc_t list */
    void *proc_buckets;   /* bcl_proc_t* hash buckets */
    bcl_handler_t *handlers; /* registered event handlers */
    void *handler_buckets; /* bcl_handler_t* hash buckets */
    int handler_count;
    void *evtsys_data;    /* event-system module private data */
    int running;
    int exit_code;
    jmp_buf error_jmp;
    char *error_msg;
    char *error_display;
    char *error_summary;
    char *error_file;
    char *error_proc;
    char *error_trace;
    char *error_event_source;
    char *error_oscode;
    char *error_osmsg;
    char error_facility[16];
    char error_code[32];
    char error_severity;
    const char *current_source_name;
    const char *current_proc_name;
    bcl_node_t *current_node;
    int error_line;
    int error_col;
    int error_has_location;
    bcl_val_t current_evtdata;    /* current event data */
    bcl_val_t current_evtsource;  /* current event source */
    void *modules;        /* loaded modules */
    void *module_buckets; /* bcl_module_t* hash buckets */
    void *parser_cleanups; /* parser-owned bcl_node_t cleanup stack */
    bcl_try_frame_t *try_stack;
    bcl_exec_frame_t *frame_stack;
    bcl_evalstatus_t pending_status;
    int pending_exit_code;
    char *pending_oscode;
    char *pending_osmsg;
    int restricted_mode;
    char *source_root;
    void *module_guards;  /* bcl_module_guard_t* unload guards; append-only ABI */
};

/* Initialize interpreter */
void bcl_interp_init(bcl_interp_t *interp);
void bcl_interp_cleanup(bcl_interp_t *interp);
void bcl_interp_set_restricted(bcl_interp_t *interp, int restricted);
int bcl_interp_set_source_root(bcl_interp_t *interp, const char *path);
int bcl_source_allowed(bcl_interp_t *interp, const char *filename);

/* Run script or REPL */
int bcl_run_file(bcl_interp_t *interp, const char *filename);
/* Fichero de inicio (~/.bcl.rc): 0 = bien o inexistente, 1 = error (ya mostrado).
 * *exit_requested pasa a 1 si el fichero ejecuto EXIT. */
int bcl_run_startup_file(bcl_interp_t *interp, const char *path, int *exit_requested);
/* Numero de build del interprete (ver BUILD y VERSIONS.md). */
int bcl_build_number(void);
int bcl_source_file(bcl_interp_t *interp, bcl_env_t *env, const char *filename);
void bcl_repl(bcl_interp_t *interp);

/* Register built-in commands */
void bcl_register_builtins(bcl_interp_t *interp);

/* Utility functions */
bcl_val_t bcl_val_make(const char *data, size_t len);
bcl_val_t bcl_val_empty(void);
void bcl_val_free(bcl_val_t *val);
void *bcl_xmalloc(bcl_interp_t *interp, size_t size);
void *bcl_xcalloc(bcl_interp_t *interp, size_t count, size_t size);
void *bcl_xrealloc(bcl_interp_t *interp, void *ptr, size_t size);
char *bcl_xstrdup(bcl_interp_t *interp, const char *text);
char *bcl_xmemdup0(bcl_interp_t *interp, const char *data, size_t len);

/* Error handling */
__attribute__((noreturn)) void bcl_error(bcl_interp_t *interp, const char *fmt, ...);
__attribute__((noreturn)) void bcl_raise_code(bcl_interp_t *interp, const char *facility, char severity, const char *code, const char *fmt, ...);
void bcl_set_pending_os_error(bcl_interp_t *interp, const char *code, const char *msg);
void bcl_clear_pending_os_error(bcl_interp_t *interp);
void bcl_frame_push(bcl_interp_t *interp, bcl_exec_frame_t *frame, bcl_framekind_t kind, const char *name, const char *call_source, int call_line, int call_col);
void bcl_frame_pop(bcl_interp_t *interp, bcl_exec_frame_t *frame);

#endif /* BCL_H */
