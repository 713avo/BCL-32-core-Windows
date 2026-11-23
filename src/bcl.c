/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Rafael Seguí Revert. See LICENSE. */
/**
 * BCL Interpreter - Core implementation
 */

#define _GNU_SOURCE

#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "bcl.h"
#include "platform.h"
#include "cmd.h"
#include "eval.h"
#include "lexer.h"
#include "parser.h"

#ifdef HAVE_READLINE
#include <readline/history.h>
#include <readline/readline.h>
#endif

typedef struct bcl_module {
    char *path;
    void *handle;
    struct bcl_module *next;
} bcl_module_t;

#define BCL_REPL_HISTORY_LIMIT 10
#define BCL_GETS_BUFSIZ 4096
static char bcl_empty_value_data[1] = "";

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static __attribute__((noreturn)) void bcl_oom(bcl_interp_t *interp) {
    if (interp) {
        bcl_raise_code(interp, "BCL", 'F', "NOMEM", "Out of memory");
    }
    fputs("BCL: out of memory\n", stderr);
    abort();
}

void *bcl_xmalloc(bcl_interp_t *interp, size_t size) {
    void *ptr;

    if (size == 0) {
        size = 1;
    }
    ptr = malloc(size);
    if (!ptr) {
        bcl_oom(interp);
    }
    return ptr;
}

void *bcl_xcalloc(bcl_interp_t *interp, size_t count, size_t size) {
    void *ptr;

    if (count == 0 || size == 0) {
        count = 1;
        size = 1;
    }
    if (size > (size_t)-1 / count) {
        bcl_oom(interp);
    }
    ptr = calloc(count, size);
    if (!ptr) {
        bcl_oom(interp);
    }
    return ptr;
}

void *bcl_xrealloc(bcl_interp_t *interp, void *ptr, size_t size) {
    void *new_ptr;

    if (size == 0) {
        size = 1;
    }
    new_ptr = realloc(ptr, size);
    if (!new_ptr) {
        bcl_oom(interp);
    }
    return new_ptr;
}

char *bcl_xstrdup(bcl_interp_t *interp, const char *text) {
    return bcl_xmemdup0(interp, text ? text : "", strlen(text ? text : ""));
}

char *bcl_xmemdup0(bcl_interp_t *interp, const char *data, size_t len) {
    char *copy;

    if (len > (size_t)-2) {
        bcl_oom(interp);
    }
    copy = bcl_xmalloc(interp, len + 1);
    if (len > 0) {
        memcpy(copy, data, len);
    }
    copy[len] = '\0';
    return copy;
}

static const char *skip_ws_str(const char *s) {
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    return s;
}

static void trim_in_place(char *s) {
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
}

static int starts_with_word_ci(const char *s, const char *word) {
    size_t len = strlen(word);

    if (strncasecmp(s, word, len) != 0) {
        return 0;
    }

    return s[len] == '\0' || isspace((unsigned char)s[len]);
}

static int parse_repl_exit_command(const char *line, int *exit_code) {
    const char *s = skip_ws_str(line);
    char *end = NULL;
    long code;

    if (!starts_with_word_ci(s, "exit") && !starts_with_word_ci(s, "quit")) {
        return 0;
    }

    s += 4;
    s = skip_ws_str(s);
    if (*s == '\0') {
        *exit_code = 0;
        return 1;
    }

    code = strtol(s, &end, 10);
    end = (char *)skip_ws_str(end);
    if (*end != '\0') {
        return 0;
    }

    *exit_code = (int)code;
    return 1;
}

static int env_truthy(const char *value) {
    if (!value || !*value) {
        return 0;
    }
    return strcasecmp(value, "0") != 0 &&
           strcasecmp(value, "false") != 0 &&
           strcasecmp(value, "no") != 0 &&
           strcasecmp(value, "off") != 0;
}

void bcl_interp_set_restricted(bcl_interp_t *interp, int restricted) {
    interp->restricted_mode = restricted ? 1 : 0;
}

int bcl_interp_set_source_root(bcl_interp_t *interp, const char *path) {
    char resolved[PATH_MAX];

    free(interp->source_root);
    interp->source_root = NULL;

    if (!path || !*path) {
        return 1;
    }

    if (!realpath(path, resolved)) {
        return 0;
    }

    interp->source_root = bcl_xstrdup(interp, resolved);
    return 1;
}

int bcl_source_allowed(bcl_interp_t *interp, const char *filename) {
    char resolved[PATH_MAX];
    size_t root_len;

    if (!interp || !interp->restricted_mode) {
        return 1;
    }
    if (!interp->source_root || !*interp->source_root) {
        return 0;
    }
    if (!filename || !realpath(filename, resolved)) {
        return 0;
    }

    root_len = strlen(interp->source_root);
    if (root_len == 1 && interp->source_root[0] == '/') {
        return resolved[0] == '/';
    }
    return strncmp(resolved, interp->source_root, root_len) == 0 &&
           (resolved[root_len] == '\0' || resolved[root_len] == '/');
}

static char *bcl_repl_readline(bcl_interp_t *interp, const char *prompt) {
#ifdef HAVE_READLINE
    if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
        return readline(prompt);
    }
#endif

    char buf[BCL_GETS_BUFSIZ];
    size_t len;

    printf("%s", prompt);
    fflush(stdout);

    if (!fgets(buf, sizeof(buf), stdin)) {
        return NULL;
    }

    len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
    }
    return bcl_xstrdup(interp, buf);
}

static void set_pending_errno_error(bcl_interp_t *interp, int errnum, const char *fallback) {
    char code[32];

    if (errnum <= 0) {
        errnum = EIO;
    }
    snprintf(code, sizeof(code), "%d", errnum);
    bcl_set_pending_os_error(interp, code, fallback ? fallback : strerror(errnum));
}

static int bcl_repl_has_line_editing(void) {
#ifdef HAVE_READLINE
    return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
#else
    return 0;
#endif
}

static void append_text(bcl_interp_t *interp, char **buf, size_t *len, size_t *cap, const char *text) {
    size_t text_len = strlen(text);

    if (text_len > (size_t)-1 - *len - 1) {
        bcl_raise_code(interp, "BCL", 'F', "NOMEM", "Out of memory");
    }
    if (*len + text_len + 1 > *cap) {
        while (*len + text_len + 1 > *cap) {
            if (*cap > (size_t)-1 / 2) {
                bcl_raise_code(interp, "BCL", 'F', "NOMEM", "Out of memory");
            }
            *cap *= 2;
        }
        *buf = bcl_xrealloc(interp, *buf, *cap);
    }

    memcpy(*buf + *len, text, text_len);
    *len += text_len;
    (*buf)[*len] = '\0';
}

static void repl_free_token(bcl_token_t *tok) {
    if (tok->text) {
        free(tok->text);
        tok->text = NULL;
    }
}

static int repl_needs_more_input(const char *src) {
    bcl_lexer_t lexer;
    bcl_token_t tok;
    int depth = 0;
    int line_start = 1;
    int line_if = 0;
    int if_opened = 0;

    bcl_lexer_init(&lexer, NULL, src);

    while ((tok = bcl_lexer_next(&lexer)).type != TOK_EOF) {
        if (tok.type == TOK_NEWLINE) {
            line_start = 1;
            line_if = 0;
            if_opened = 0;
            repl_free_token(&tok);
            continue;
        }

        if (line_start && tok.type == TOK_WORD) {
            if (strcasecmp(tok.text, "IF") == 0) {
                line_if = 1;
                if_opened = 0;
            } else if (strcasecmp(tok.text, "PROC") == 0 || strcasecmp(tok.text, "TRY") == 0) {
                depth++;
            } else if (strcasecmp(tok.text, "END") == 0 || strcasecmp(tok.text, "LOOP") == 0) {
                if (depth > 0) {
                    depth--;
                }
            }
            line_start = 0;
        } else if (line_if && tok.type == TOK_WORD && !if_opened) {
            if (strcasecmp(tok.text, "THEN") == 0 || strcasecmp(tok.text, "DO") == 0) {
                depth++;
                if_opened = 1;
            }
        }

        repl_free_token(&tok);
    }

    repl_free_token(&tok);
    bcl_lexer_free(&lexer);
    return depth > 0;
}

bcl_val_t bcl_val_make(const char *data, size_t len) {
    bcl_val_t val;

    len = data ? len : 0;
    val.data = bcl_xmemdup0(NULL, data ? data : "", len);
    val.len = len;
    return val;
}

bcl_val_t bcl_val_empty(void) {
    bcl_val_t val;
    val.data = bcl_empty_value_data;
    val.len = 0;
    return val;
}

void bcl_val_free(bcl_val_t *val) {
    if (val && val->data) {
        if (val->data != bcl_empty_value_data) {
            free(val->data);
        }
        val->data = NULL;
    }
    if (val) {
        val->len = 0;
    }
}

void bcl_interp_init(bcl_interp_t *interp) {
    const char *restricted_env;
    const char *source_root_env;
    const char *libs_env;

    memset(interp, 0, sizeof(*interp));
    interp->global_env = bcl_env_new(NULL);
    interp->commands = bcl_xcalloc(NULL, 1, sizeof(bcl_cmdtable_t));
    interp->running = 0;
    interp->exit_code = 0;
    interp->current_evtdata = bcl_val_empty();
    interp->current_evtsource = bcl_val_empty();

    restricted_env = getenv("BCL_RESTRICTED");
    if (env_truthy(restricted_env)) {
        bcl_interp_set_restricted(interp, 1);
    }
    source_root_env = getenv("BCL_SOURCE_ROOT");
    if (source_root_env && *source_root_env) {
        bcl_interp_set_source_root(interp, source_root_env);
    }
    /* BCL_LIBS: valor inicial desde el entorno; el programa puede cambiarlo con SET. */
    libs_env = getenv("BCL_LIBS");
    if (libs_env && *libs_env) {
        bcl_env_set(interp, interp->global_env, "BCL_LIBS", libs_env);
    }
}

static void free_error_details(bcl_interp_t *interp) {
    free(interp->error_display);
    free(interp->error_summary);
    free(interp->error_file);
    free(interp->error_proc);
    free(interp->error_trace);
    free(interp->error_event_source);
    free(interp->error_oscode);
    free(interp->error_osmsg);
    interp->error_display = NULL;
    interp->error_summary = NULL;
    interp->error_file = NULL;
    interp->error_proc = NULL;
    interp->error_trace = NULL;
    interp->error_event_source = NULL;
    interp->error_oscode = NULL;
    interp->error_osmsg = NULL;
    interp->error_facility[0] = '\0';
    interp->error_code[0] = '\0';
    interp->error_severity = '\0';
}

/* Numero de build: lo define bin/build_number.c (generado por make a partir de
 * BUILD). Esta version debil permite compilar el interprete a mano, sin make
 * (por ejemplo en las pruebas con sanitizers); entonces el build es 0. */
__attribute__((weak)) int bcl_build_number(void) {
    return 0;
}

void bcl_set_pending_os_error(bcl_interp_t *interp, const char *code, const char *msg) {
    if (!interp) {
        return;
    }

    free(interp->pending_oscode);
    free(interp->pending_osmsg);
    interp->pending_oscode = code ? bcl_xstrdup(interp, code) : NULL;
    interp->pending_osmsg = msg ? bcl_xstrdup(interp, msg) : NULL;
}

void bcl_clear_pending_os_error(bcl_interp_t *interp) {
    if (!interp) {
        return;
    }

    free(interp->pending_oscode);
    free(interp->pending_osmsg);
    interp->pending_oscode = NULL;
    interp->pending_osmsg = NULL;
}

void bcl_interp_cleanup(bcl_interp_t *interp) {
    bcl_runtime_cleanup(interp);
    bcl_parser_cleanup_all(interp);

    if (interp->global_env) {
        bcl_env_free(interp->global_env);
    }

    if (interp->commands) {
        bcl_cmdtable_t *ct = (bcl_cmdtable_t *)interp->commands;
        for (int i = 0; i < BCL_CMD_BUCKETS; i++) {
            bcl_cmd_t *cmd = ct->buckets[i];
            while (cmd) {
                bcl_cmd_t *next = cmd->next;
                free(cmd->name);
                free(cmd);
                cmd = next;
            }
        }
        free(ct);
    }

    while (interp->procs) {
        bcl_proc_t *proc = (bcl_proc_t *)interp->procs;
        interp->procs = proc->next;
        free(proc->name);
        for (int i = 0; i < proc->nparams; i++) {
            free(proc->params[i].name);
        }
        free(proc->params);
        free(proc->source_name);
        bcl_node_free(proc->body);
        free(proc->body_src);
        free(proc);
    }
    free(interp->proc_buckets);
    interp->proc_buckets = NULL;

    bcl_val_free(&interp->current_evtdata);
    bcl_val_free(&interp->current_evtsource);
    free(interp->source_root);
    interp->source_root = NULL;
    free(interp->error_msg);
    interp->error_msg = NULL;
    free_error_details(interp);
    bcl_clear_pending_os_error(interp);
}

void bcl_frame_push(bcl_interp_t *interp, bcl_exec_frame_t *frame, bcl_framekind_t kind, const char *name, const char *call_source, int call_line, int call_col) {
    frame->kind = kind;
    frame->name = name;
    frame->call_source = call_source;
    frame->call_line = call_line;
    frame->call_col = call_col;
    frame->prev = interp->frame_stack;
    interp->frame_stack = frame;
}

void bcl_frame_pop(bcl_interp_t *interp, bcl_exec_frame_t *frame) {
    if (interp->frame_stack == frame) {
        interp->frame_stack = frame->prev;
    }
}

static const char *frame_kind_name(bcl_framekind_t kind) {
    switch (kind) {
        case BCL_FRAME_FILE: return "FILE";
        case BCL_FRAME_SOURCE: return "SOURCE";
        case BCL_FRAME_PROC: return "PROC";
        case BCL_FRAME_SUBCMD: return "SUBCMD";
        case BCL_FRAME_CALLBACK: return "CALLBACK";
        default: return "FRAME";
    }
}

static void append_text_dyn(bcl_interp_t *interp, char **buf, size_t *len, size_t *cap, const char *text) {
    size_t extra = strlen(text);
    if (extra > (size_t)-1 - *len - 1) {
        bcl_raise_code(interp, "BCL", 'F', "NOMEM", "Out of memory");
    }
    if (*len + extra + 1 > *cap) {
        while (*len + extra + 1 > *cap) {
            if (*cap > (size_t)-1 / 2) {
                bcl_raise_code(interp, "BCL", 'F', "NOMEM", "Out of memory");
            }
            *cap *= 2;
        }
        *buf = bcl_xrealloc(interp, *buf, *cap);
    }
    memcpy(*buf + *len, text, extra);
    *len += extra;
    (*buf)[*len] = '\0';
}

static void append_format_dyn(bcl_interp_t *interp, char **buf, size_t *len, size_t *cap, const char *fmt, ...) {
    va_list args;
    char tmp[512];

    va_start(args, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    append_text_dyn(interp, buf, len, cap, tmp);
}

static char *build_trace_string(bcl_interp_t *interp) {
    bcl_exec_frame_t *frames[64];
    int count = 0;
    size_t cap = 256;
    size_t len = 0;
    char *buf = bcl_xmalloc(interp, cap);

    buf[0] = '\0';
    for (bcl_exec_frame_t *frame = interp->frame_stack; frame && count < 64; frame = frame->prev) {
        frames[count++] = frame;
    }

    for (int i = count - 1; i >= 0; i--) {
        bcl_exec_frame_t *frame = frames[i];
        if (len > 0) {
            append_text_dyn(interp, &buf, &len, &cap, " -> ");
        }
        append_format_dyn(interp, &buf, &len, &cap, "%s %s", frame_kind_name(frame->kind), frame->name ? frame->name : "<anon>");
        if (frame->call_source && frame->call_line > 0 && frame->call_col > 0) {
            append_format_dyn(interp, &buf, &len, &cap, " @ %s:%d:%d", frame->call_source, frame->call_line, frame->call_col);
        }
    }

    return buf;
}

static void infer_error_identity(const char *message, char *facility, size_t facility_size, char *severity, char *code, size_t code_size) {
    const char *summary = message;
    const char *colon;

    snprintf(facility, facility_size, "%s", "BCL");
    snprintf(code, code_size, "%s", "GENERIC");
    *severity = 'F';

    if (strncmp(summary, "LEX: ", 5) == 0) {
        snprintf(facility, facility_size, "%s", "LEX");
        if (strstr(summary, "unterminated string literal")) snprintf(code, code_size, "%s", "UNTERMSTR");
        else if (strstr(summary, "unterminated command substitution")) snprintf(code, code_size, "%s", "UNTERMSUB");
        else if (strstr(summary, "unterminated variable reference")) snprintf(code, code_size, "%s", "UNTERMVAR");
        return;
    }
    if (strncmp(summary, "PAR: ", 5) == 0) {
        snprintf(facility, facility_size, "%s", "PAR");
        if (strstr(summary, "block terminator")) snprintf(code, code_size, "%s", "MISSTERM");
        else if (strstr(summary, "expected 'END'")) snprintf(code, code_size, "%s", "MISSEND");
        else if (strstr(summary, "expected command or keyword")) snprintf(code, code_size, "%s", "SYNTAX");
        else if (strstr(summary, "TRY requires CATCH")) snprintf(code, code_size, "%s", "MISSCATCH");
        else if (strstr(summary, "PROC")) snprintf(code, code_size, "%s", "PROCDEF");
        else if (strstr(summary, "IF requires")) snprintf(code, code_size, "%s", "IFTERM");
        return;
    }
    if (strncmp(summary, "SOURCE:", 7) == 0) {
        snprintf(facility, facility_size, "%s", "SRC");
        if (strstr(summary, "expected filename")) snprintf(code, code_size, "%s", "BADARGS");
        else snprintf(code, code_size, "%s", "NOFILE");
        return;
    }
    if (strncmp(summary, "LOAD:", 5) == 0 || strncmp(summary, "UNLOAD:", 7) == 0) {
        snprintf(facility, facility_size, "%s", "LOAD");
        if (strstr(summary, "already loaded")) snprintf(code, code_size, "%s", "DUPMOD");
        else if (strstr(summary, "not loaded")) snprintf(code, code_size, "%s", "NOTLOADED");
        else if (strstr(summary, "cannot open")) snprintf(code, code_size, "%s", "OPENFAIL");
        else if (strstr(summary, "initialization failed")) snprintf(code, code_size, "%s", "MODINIT");
        else if (strstr(summary, "symbol")) snprintf(code, code_size, "%s", "NOSYMBOL");
        else snprintf(code, code_size, "%s", "GENERIC");
        return;
    }
    if (strncmp(summary, "CALC:", 5) == 0) {
        snprintf(facility, facility_size, "%s", "CALC");
        if (strstr(summary, "expected")) snprintf(code, code_size, "%s", "SYNTAX");
        else if (strstr(summary, "unknown")) snprintf(code, code_size, "%s", "UNKNOWN");
        else if (strstr(summary, "unterminated")) snprintf(code, code_size, "%s", "UNTERM");
        else if (strstr(summary, "invalid")) snprintf(code, code_size, "%s", "BADVALUE");
        else if (strstr(summary, "undefined")) snprintf(code, code_size, "%s", "UNDEF");
        else if (strstr(summary, "requires")) snprintf(code, code_size, "%s", "DOMAIN");
        else snprintf(code, code_size, "%s", "ERROR");
        return;
    }
    colon = strchr(summary, ':');
    if (colon && colon > summary && (size_t)(colon - summary) < facility_size) {
        int command_prefix = 1;

        for (const char *p = summary; p < colon; p++) {
            if (!(isupper((unsigned char)*p) || isdigit((unsigned char)*p) || *p == '_')) {
                command_prefix = 0;
                break;
            }
        }
        if (command_prefix) {
            size_t prefix_len = (size_t)(colon - summary);

            memcpy(facility, summary, prefix_len);
            facility[prefix_len] = '\0';
            if (strstr(summary, "expected") || strstr(summary, "requires") ||
                strstr(summary, "does not take") || strstr(summary, "must contain")) {
                snprintf(code, code_size, "%s", "BADARGS");
            }
            else if (strstr(summary, "invalid")) snprintf(code, code_size, "%s", "BADVALUE");
            else if (strstr(summary, "unsupported")) snprintf(code, code_size, "%s", "UNSUPPORTED");
            else if (strstr(summary, "unknown")) snprintf(code, code_size, "%s", "UNKNOWN");
            else if (strstr(summary, "already")) snprintf(code, code_size, "%s", "DUPLICATE");
            else if (strstr(summary, "conflict")) snprintf(code, code_size, "%s", "CONFLICT");
            else snprintf(code, code_size, "%s", "ERROR");
            return;
        }
    }
    if (strncmp(summary, "Unknown command:", 16) == 0) {
        snprintf(facility, facility_size, "%s", "EVAL");
        snprintf(code, code_size, "%s", "UNKCMD");
        return;
    }
}

static void store_error_details(bcl_interp_t *interp, const char *facility, char severity, const char *code, const char *summary) {
    const char *source = interp->current_source_name;
    const char *proc = interp->current_proc_name;
    bcl_exec_frame_t *top_frame = interp->frame_stack;
    char header[1200];
    char display[2400];
    int line = 0;
    int col = 0;
    char *trace;

    if (interp->error_has_location) {
        line = interp->error_line;
        col = interp->error_col;
    } else if (interp->current_node) {
        line = interp->current_node->line;
        col = interp->current_node->col;
    }

    if (top_frame && top_frame->kind == BCL_FRAME_SUBCMD &&
        source && strcmp(source, "<subcmd>") == 0 && top_frame->call_source) {
        source = top_frame->call_source;
        if (top_frame->call_line > 0 && top_frame->call_col > 0) {
            line = top_frame->call_line;
            col = top_frame->call_col;
        }
    }

    interp->error_line = line;
    interp->error_col = col;

    free_error_details(interp);

    snprintf(header, sizeof(header), "?%s-%c-%s, %s", facility, severity, code, summary);
    trace = build_trace_string(interp);

    if (source && line > 0 && col > 0) {
        snprintf(display, sizeof(display), "%s:%d:%d: %s", source, line, col, header);
    } else {
        snprintf(display, sizeof(display), "%s", header);
    }

    interp->error_summary = bcl_xstrdup(interp, summary);
    interp->error_file = source ? bcl_xstrdup(interp, source) : NULL;
    interp->error_proc = proc ? bcl_xstrdup(interp, proc) : NULL;
    interp->error_trace = trace;
    interp->error_event_source = interp->current_evtsource.len > 0 ? bcl_xstrdup(interp, interp->current_evtsource.data) : NULL;
    interp->error_oscode = interp->pending_oscode;
    interp->error_osmsg = interp->pending_osmsg;
    interp->pending_oscode = NULL;
    interp->pending_osmsg = NULL;
    snprintf(interp->error_facility, sizeof(interp->error_facility), "%s", facility);
    snprintf(interp->error_code, sizeof(interp->error_code), "%s", code);
    interp->error_severity = severity;

    if (interp->error_proc) {
        strncat(display, "\n  In: PROC ", sizeof(display) - strlen(display) - 1);
        strncat(display, interp->error_proc, sizeof(display) - strlen(display) - 1);
    }
    if (interp->error_event_source) {
        strncat(display, "\n  Event: source=", sizeof(display) - strlen(display) - 1);
        strncat(display, interp->error_event_source, sizeof(display) - strlen(display) - 1);
    }
    if (interp->error_osmsg && *interp->error_osmsg) {
        strncat(display, "\n  System: ", sizeof(display) - strlen(display) - 1);
        if (interp->error_oscode && *interp->error_oscode) {
            strncat(display, "[", sizeof(display) - strlen(display) - 1);
            strncat(display, interp->error_oscode, sizeof(display) - strlen(display) - 1);
            strncat(display, "] ", sizeof(display) - strlen(display) - 1);
        }
        strncat(display, interp->error_osmsg, sizeof(display) - strlen(display) - 1);
    }
    if (interp->error_trace && *interp->error_trace) {
        strncat(display, "\n  Trace: ", sizeof(display) - strlen(display) - 1);
        strncat(display, interp->error_trace, sizeof(display) - strlen(display) - 1);
    }

    free(interp->error_msg);
    interp->error_msg = bcl_xstrdup(interp, header);
    interp->error_display = bcl_xstrdup(interp, display);

    interp->error_has_location = 0;
}

static __attribute__((noreturn)) void bcl_error_vraise(bcl_interp_t *interp, const char *facility, char severity, const char *code, const char *fmt, va_list args) {
    char summary[1024];

    vsnprintf(summary, sizeof(summary), fmt, args);
    store_error_details(interp, facility, severity, code, summary);
    bcl_parser_cleanup_all(interp);

    if (interp->try_stack) {
        longjmp(interp->try_stack->env, 1);
    }

    longjmp(interp->error_jmp, 1);
}

__attribute__((noreturn)) void bcl_raise_code(bcl_interp_t *interp, const char *facility, char severity, const char *code, const char *fmt, ...) {
    va_list args;

    va_start(args, fmt);
    bcl_error_vraise(interp, facility, severity, code, fmt, args);
    va_end(args);
}

__attribute__((noreturn)) void bcl_error(bcl_interp_t *interp, const char *fmt, ...) {
    va_list args;
    char summary[1024];
    char facility[16];
    char code[32];
    char severity;

    va_start(args, fmt);
    vsnprintf(summary, sizeof(summary), fmt, args);
    va_end(args);

    infer_error_identity(summary, facility, sizeof(facility), &severity, code, sizeof(code));
    store_error_details(interp, facility, severity, code, summary);
    bcl_parser_cleanup_all(interp);
    if (interp->try_stack) {
        longjmp(interp->try_stack->env, 1);
    }
    longjmp(interp->error_jmp, 1);
}

/* Se activa cuando algun flujo termina con EXIT (lo consulta el fichero de inicio). */
static int exit_seen;

static int run_stream(bcl_interp_t *interp, bcl_env_t *env, const char *src, const char *source_name) {
    bcl_result_t result = bcl_eval_source_named(interp, env, src, source_name);

    if (result.status == EVAL_EXIT) {
        interp->exit_code = result.exit_code;
        exit_seen = 1;
    }

    bcl_val_free(&result.value);
    return result.status == EVAL_EXIT ? interp->exit_code : 0;
}

int bcl_source_file(bcl_interp_t *interp, bcl_env_t *env, const char *filename) {
    FILE *f = fopen(filename, "rb");
    char *src;
    long size;
    size_t read;
    int rc;
    const char *prev_source = interp->current_source_name;
    bcl_try_frame_t frame;
    bcl_exec_frame_t exec_frame;
    int call_line = interp->current_node ? interp->current_node->line : 0;
    int call_col = interp->current_node ? interp->current_node->col : 0;
    struct stat st;

    bcl_clear_pending_os_error(interp);
    if (!f) {
        set_pending_errno_error(interp, errno, NULL);
        return 1;
    }

    if (fstat(fileno(f), &st) != 0) {
        int saved_errno = errno;
        fclose(f);
        set_pending_errno_error(interp, saved_errno, NULL);
        return 1;
    }
    if (!S_ISREG(st.st_mode)) {
        fclose(f);
        set_pending_errno_error(interp, EINVAL, "not a regular file");
        return 1;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        int saved_errno = errno;
        fclose(f);
        set_pending_errno_error(interp, saved_errno, NULL);
        return 1;
    }
    size = ftell(f);
    if (size < 0) {
        int saved_errno = errno;
        fclose(f);
        set_pending_errno_error(interp, saved_errno, NULL);
        return 1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        int saved_errno = errno;
        fclose(f);
        set_pending_errno_error(interp, saved_errno, NULL);
        return 1;
    }
    if ((size_t)size > (size_t)-2) {
        fclose(f);
        set_pending_errno_error(interp, EFBIG, NULL);
        return 1;
    }

    src = bcl_xmalloc(interp, (size_t)size + 1);
    read = fread(src, 1, (size_t)size, f);
    if (read != (size_t)size) {
        int saved_errno = ferror(f) ? errno : EIO;
        free(src);
        fclose(f);
        set_pending_errno_error(interp, saved_errno, ferror(f) ? NULL : "short read");
        return 1;
    }
    src[read] = '\0';
    fclose(f);

    frame.prev = interp->try_stack;
    interp->try_stack = &frame;
    interp->current_source_name = filename;
    bcl_frame_push(interp, &exec_frame, prev_source ? BCL_FRAME_SOURCE : BCL_FRAME_FILE,
                   filename, prev_source, call_line, call_col);

    if (setjmp(frame.env) != 0) {
        interp->try_stack = frame.prev;
        interp->current_source_name = prev_source;
        bcl_frame_pop(interp, &exec_frame);
        free(src);
        if (frame.prev) {
            longjmp(frame.prev->env, 1);
        }
        longjmp(interp->error_jmp, 1);
    }

    rc = run_stream(interp, env, src, filename);
    interp->try_stack = frame.prev;
    interp->current_source_name = prev_source;
    bcl_frame_pop(interp, &exec_frame);
    free(src);
    return rc;
}

int bcl_run_file(bcl_interp_t *interp, const char *filename) {
    int rc;

    if (setjmp(interp->error_jmp) != 0) {
        fprintf(stderr, "Error: %s\n", interp->error_display ? interp->error_display : (interp->error_msg ? interp->error_msg : "unknown error"));
        return 1;
    }

    rc = bcl_source_file(interp, interp->global_env, filename);
    if (rc != 0) {
        if (interp->error_msg) {
            fprintf(stderr, "Error: %s\n", interp->error_display ? interp->error_display : interp->error_msg);
            return 1;
        }
        if (interp->exit_code != 0) {
            return interp->exit_code;
        }
        if (interp->pending_osmsg) {
            fprintf(stderr, "Cannot open file: %s: %s\n", filename, interp->pending_osmsg);
            bcl_clear_pending_os_error(interp);
            return 1;
        }
        fprintf(stderr, "Cannot open file: %s\n", filename);
        return 1;
    }

    return interp->exit_code;
}

int bcl_run_startup_file(bcl_interp_t *interp, const char *path, int *exit_requested) {
    struct stat st;
    int rc;

    *exit_requested = 0;
    if (!path || stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return 0; /* sin fichero de inicio no hay nada que hacer */
    }

    exit_seen = 0;
    if (setjmp(interp->error_jmp) != 0) {
        fprintf(stderr, "Error: %s\n", interp->error_display ? interp->error_display : (interp->error_msg ? interp->error_msg : "unknown error"));
        return 1;
    }

    rc = bcl_source_file(interp, interp->global_env, path);
    if (exit_seen) {
        *exit_requested = 1;
        return 0;
    }
    if (rc != 0) {
        if (interp->error_msg) {
            fprintf(stderr, "Error: %s\n", interp->error_display ? interp->error_display : interp->error_msg);
        } else if (interp->pending_osmsg) {
            fprintf(stderr, "Cannot read startup file: %s: %s\n", path, interp->pending_osmsg);
            bcl_clear_pending_os_error(interp);
        } else {
            fprintf(stderr, "Cannot read startup file: %s\n", path);
        }
        return 1;
    }
    return 0;
}

void bcl_repl(bcl_interp_t *interp) {
    char *buffer = bcl_xmalloc(interp, 256);
    size_t buffer_len = 0;
    size_t buffer_cap = 256;

    buffer[0] = '\0';

    if (bcl_repl_has_line_editing()) {
#ifdef HAVE_READLINE
        using_history();
        stifle_history(BCL_REPL_HISTORY_LIMIT);
        rl_readline_name = "bcl";
#endif
    }

    while (1) {
        int repl_exit_code = 0;
        int continued = repl_needs_more_input(buffer);
        char prompt[32];
        snprintf(prompt, sizeof(prompt), "BCL-32 %s> ", BCL_VERSION);
        char *linebuf = bcl_repl_readline(interp, continued ? "~ " : prompt);
        char *trimmed;

        if (!linebuf) {
            if (buffer_len > 0) {
                fprintf(stderr, "Error: incomplete block in REPL input\n");
            }
            printf("\n");
            break;
        }

        trimmed = linebuf;
        trim_in_place(trimmed);
        trimmed = (char *)skip_ws_str(trimmed);

        if (*trimmed == '\0') {
            free(linebuf);
            continue;
        }

        if (buffer_len == 0 && parse_repl_exit_command(trimmed, &repl_exit_code)) {
            interp->exit_code = repl_exit_code;
            free(linebuf);
            break;
        }

        append_text(interp, &buffer, &buffer_len, &buffer_cap, trimmed);
        append_text(interp, &buffer, &buffer_len, &buffer_cap, "\n");
        free(linebuf);

        if (repl_needs_more_input(buffer)) {
            continue;
        }

#ifdef HAVE_READLINE
        if (bcl_repl_has_line_editing() && *skip_ws_str(buffer) != '\0') {
            char *history_entry = bcl_xstrdup(interp, buffer);
            trim_in_place(history_entry);
            add_history(history_entry);
            free(history_entry);
        }
#endif

        if (setjmp(interp->error_jmp) != 0) {
            fprintf(stderr, "Error: %s\n", interp->error_display ? interp->error_display : (interp->error_msg ? interp->error_msg : "unknown error"));
            buffer_len = 0;
            buffer[0] = '\0';
            continue;
        }

        bcl_result_t result = bcl_eval_source_named(interp, interp->global_env, buffer, "<repl>");
        if (result.status == EVAL_EXIT) {
            interp->exit_code = result.exit_code;
            bcl_val_free(&result.value);
            break;
        }
        if (result.value.len > 0) {
            printf("%s\n", result.value.data);
        }
        bcl_val_free(&result.value);
        buffer_len = 0;
        buffer[0] = '\0';
    }

    free(buffer);
}
