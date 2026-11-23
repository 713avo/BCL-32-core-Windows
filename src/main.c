/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Rafael Seguí Revert. See LICENSE. */
/**
 * BCL Interpreter - Main entry point
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bcl.h"
#include "platform.h"
#include "cmd.h"
#include "eval.h"

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [--restricted] [--no-rc] [--source-root DIR] [--] [script.bcl [args...]]\n", prog);
    fprintf(stderr, "       %s --version\n", prog);
}

static int arg_needs_braces(const char *text) {
    if (*text == '\0') {
        return 1;
    }

    while (*text) {
        if (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r' ||
            *text == '{' || *text == '}' || *text == '"' || *text == '\\') {
            return 1;
        }
        text++;
    }

    return 0;
}

static void append_arg_bytes(char **buf, size_t *len, size_t *cap, const char *data, size_t data_len) {
    if (data_len > (size_t)-1 - *len - 1) {
        fputs("BCL: out of memory\n", stderr);
        abort();
    }
    if (*len + data_len + 1 > *cap) {
        while (*len + data_len + 1 > *cap) {
            if (*cap > (size_t)-1 / 2) {
                fputs("BCL: out of memory\n", stderr);
                abort();
            }
            *cap *= 2;
        }
        *buf = bcl_xrealloc(NULL, *buf, *cap);
    }

    memcpy(*buf + *len, data, data_len);
    *len += data_len;
    (*buf)[*len] = '\0';
}

static void append_arg_char(char **buf, size_t *len, size_t *cap, char ch) {
    append_arg_bytes(buf, len, cap, &ch, 1);
}

static char *build_args_list(int argc, char **argv) {
    size_t cap = 64;
    size_t len = 0;
    char *buf = bcl_xmalloc(NULL, cap);

    buf[0] = '\0';

    for (int i = 0; i < argc; i++) {
        const char *arg = argv[i];

        if (i > 0) {
            append_arg_char(&buf, &len, &cap, ' ');
        }

        if (!arg_needs_braces(arg)) {
            append_arg_bytes(&buf, &len, &cap, arg, strlen(arg));
            continue;
        }

        append_arg_char(&buf, &len, &cap, '{');
        while (*arg) {
            if (*arg == '{' || *arg == '}' || *arg == '\\') {
                append_arg_char(&buf, &len, &cap, '\\');
            }
            append_arg_char(&buf, &len, &cap, *arg);
            arg++;
        }
        append_arg_char(&buf, &len, &cap, '}');
    }

    return buf;
}

/* El fichero de inicio se omite con --no-rc o con BCL_NO_RC distinta de vacio/0. */
static int startup_disabled(int no_rc_flag) {
    const char *env = getenv("BCL_NO_RC");

    return no_rc_flag || (env && *env && strcmp(env, "0") != 0);
}

/* Ejecuta $HOME/.bcl.rc, si existe, antes de cualquier otra instruccion.
 * Devuelve -1 para continuar; en otro caso, el codigo de salida del proceso. */
static int run_startup(bcl_interp_t *interp, int no_rc_flag) {
    const char *home = bcl_home_dir();
    char *path;
    int exit_requested = 0;
    int rc;

    if (startup_disabled(no_rc_flag) || interp->restricted_mode || !home || !*home) {
        return -1;
    }
    path = bcl_xmalloc(interp, strlen(home) + sizeof(BCL_PATH_SEP ".bcl.rc"));
    sprintf(path, "%s" BCL_PATH_SEP ".bcl.rc", home);

    rc = bcl_run_startup_file(interp, path, &exit_requested);
    if (rc != 0) {
        fprintf(stderr, "BCL: the startup file %s failed (use --no-rc to skip it)\n", path);
        free(path);
        return 1;
    }
    free(path);
    return exit_requested ? interp->exit_code : -1;
}

int main(int argc, char **argv) {
    bcl_interp_t interp;

    bcl_platform_init();
    int script_index = 1;
    int no_rc = 0;
    int rc;

    /* Initialize interpreter */
    bcl_interp_init(&interp);

    while (script_index < argc) {
        if (strcmp(argv[script_index], "--version") == 0) {
            printf("BCL-32 v%s build %d\n", BCL_VERSION, bcl_build_number());
            bcl_interp_cleanup(&interp);
            return 0;
        }
        if (strcmp(argv[script_index], "--no-rc") == 0) {
            no_rc = 1;
            script_index++;
            continue;
        }
        if (strcmp(argv[script_index], "--restricted") == 0) {
            bcl_interp_set_restricted(&interp, 1);
            script_index++;
            continue;
        }
        if (strcmp(argv[script_index], "--source-root") == 0) {
            if (script_index + 1 >= argc) {
                print_usage(argv[0]);
                bcl_interp_cleanup(&interp);
                return 2;
            }
            if (!bcl_interp_set_source_root(&interp, argv[script_index + 1])) {
                fprintf(stderr, "Invalid source root: %s\n", argv[script_index + 1]);
                bcl_interp_cleanup(&interp);
                return 2;
            }
            script_index += 2;
            continue;
        }
        if (strcmp(argv[script_index], "--") == 0) {
            script_index++;
        }
        break;
    }

    /* Register builtin commands */
    bcl_register_builtin_cmds(&interp);

    /* ARGS existe ya en el fichero de inicio (solo en modo script). */
    if (script_index < argc) {
        if (argc > script_index + 1) {
            char *args_buf = build_args_list(argc - script_index - 1, argv + script_index + 1);
            bcl_env_set(&interp, interp.global_env, "ARGS", args_buf);
            free(args_buf);
        } else {
            bcl_env_set(&interp, interp.global_env, "ARGS", "");
        }
    }

    /* Fichero de inicio: antes que cualquier script, SOURCE o sesion interactiva. */
    rc = run_startup(&interp, no_rc);
    if (rc >= 0) {
        bcl_interp_cleanup(&interp);
        return rc;
    }

    if (script_index >= argc) {
        /* REPL mode */
        printf("BCL-32 v%s build %d - Basic Command Language 32\n", BCL_VERSION, bcl_build_number());
        printf("Type 'exit', 'quit' or 'EXIT <code>' to quit\n\n");
        bcl_repl(&interp);
    } else {
        /* Run script file */
        rc = bcl_run_file(&interp, argv[script_index]);

        /* Cleanup */
        bcl_interp_cleanup(&interp);
        return rc;
    }

    bcl_interp_cleanup(&interp);

    return interp.exit_code;
}
