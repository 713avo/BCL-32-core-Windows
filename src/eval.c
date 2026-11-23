/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Rafael Seguí Revert. See LICENSE. */
/**
 * BCL Evaluator implementation
 */

#include <ctype.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "bcl.h"
#include "calc.h"
#include "cmd.h"
#include "eval.h"
#include "lexer.h"
#include "parser.h"

typedef struct {
    const char *src;
    size_t pos;
    int depth;
} cond_ctx_t;

static bcl_result_t eval_subcmd_text(bcl_interp_t *interp, bcl_env_t *env, const char *text);
static void export_catch_error(bcl_interp_t *interp, bcl_env_t *env, const char *base_name);
static const char *array_name_open_paren(const char *name);
static void env_array_index_insert(bcl_interp_t *interp, bcl_env_t *env, bcl_var_t *var);
static void env_array_index_unlink(bcl_env_t *env, bcl_var_t *var);

static bcl_val_t result_take_value(bcl_result_t *result) {
    bcl_val_t value = result->value;
    result->value = bcl_val_empty();
    return value;
}

static void result_discard_value(bcl_result_t *result) {
    bcl_val_free(&result->value);
}

static unsigned long env_var_hash(const char *name) {
    unsigned long hash = 5381;
    unsigned char ch;

    while ((ch = (unsigned char)*name++) != '\0') {
        hash = ((hash << 5) + hash) ^ ch;
    }

    return hash;
}

static unsigned long env_alias_hash(const char *name) {
    unsigned long hash = 5381;
    unsigned char ch;

    while ((ch = (unsigned char)*name++) != '\0') {
        hash = ((hash << 5) + hash) ^ ch;
    }

    return hash;
}

static bcl_var_t *find_var_in_scope(bcl_env_t *env, const char *name) {
    bcl_var_t *var;

    if (!env || !name) {
        return NULL;
    }

    var = env->var_buckets[env_var_hash(name) % BCL_ENV_VAR_BUCKETS];
    while (var) {
        if (strcmp(var->name, name) == 0) {
            return var;
        }
        var = var->hash_next;
    }
    return NULL;
}

static void env_var_insert(bcl_interp_t *interp, bcl_env_t *env, bcl_var_t *var) {
    unsigned long bucket;

    if (!env || !var || !var->name) {
        return;
    }

    bucket = env_var_hash(var->name) % BCL_ENV_VAR_BUCKETS;
    var->next = env->vars;
    env->vars = var;
    var->hash_next = env->var_buckets[bucket];
    env->var_buckets[bucket] = var;
    env_array_index_insert(interp, env, var);
}

static void env_var_unlink_hash(bcl_env_t *env, bcl_var_t *target) {
    unsigned long bucket;
    bcl_var_t **link;

    if (!env || !target || !target->name) {
        return;
    }

    bucket = env_var_hash(target->name) % BCL_ENV_VAR_BUCKETS;
    link = &env->var_buckets[bucket];
    while (*link) {
        if (*link == target) {
            *link = target->hash_next;
            target->hash_next = NULL;
            env_array_index_unlink(env, target);
            return;
        }
        link = &(*link)->hash_next;
    }
}

static void free_var_node(bcl_var_t *var) {
    if (!var) {
        return;
    }

    free(var->name);
    bcl_val_free(&var->value);
    free(var);
}

static void free_array_aliases(bcl_env_t *env) {
    bcl_array_alias_t *alias = env ? env->array_aliases : NULL;

    while (alias) {
        bcl_array_alias_t *next = alias->next;
        free(alias->name);
        free(alias);
        alias = next;
    }
}

static void free_array_buckets(bcl_env_t *env) {
    if (!env) {
        return;
    }

    for (int i = 0; i < BCL_ARRAY_BUCKETS; i++) {
        bcl_array_bucket_t *bucket = env->array_buckets[i];
        while (bucket) {
            bcl_array_bucket_t *next = bucket->next;
            free(bucket->base);
            free(bucket);
            bucket = next;
        }
        env->array_buckets[i] = NULL;
    }
}

/* Caracteres validos dentro de un nombre de variable tras el '$'.
 *
 * Hasta BCL_kernel_mod_02 la lista incluia tambien '\', '.', '-' y '/'. Esos
 * cuatro hacian que el analisis del nombre se comiera texto que no era el
 * nombre, y siempre en silencio, porque una variable inexistente vale la
 * cadena vacia:
 *
 *   "hola $n\nadios"   ->  el nombre era "n\nadios": ni salto de linea ni valor
 *   "$n.txt"           ->  el nombre era "n.txt"
 *   "$dir/fichero"     ->  el nombre era "dir/fichero"
 *   [CALC $a-$b]       ->  el nombre era "a-", y la resta devolvia $b
 *
 * El ultimo era el peor: aritmetica incorrecta sin ningun aviso. Vease
 * doc/audit/analisis_barra_invertida_varref.md y doc/mods/BCL_kernel_mod_02.txt.
 *
 * Se conservan '_' y ':': el primero es universal en identificadores y el
 * segundo esta reservado para espacios de nombres. */
static int is_var_name_char(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == ':';
}

static const char *array_name_open_paren(const char *name) {
    const char *open;

    if (!name || !*name) {
        return NULL;
    }

    open = strchr(name, '(');
    if (!open || open == name || name[strlen(name) - 1] != ')') {
        return NULL;
    }

    return open;
}

static unsigned long env_array_hash(const char *base) {
    unsigned long hash = 5381;
    unsigned char ch;

    while ((ch = (unsigned char)*base++) != '\0') {
        hash = ((hash << 5) + hash) ^ ch;
    }

    return hash;
}

static bcl_array_bucket_t *find_array_bucket_in_scope(bcl_env_t *env, const char *base) {
    bcl_array_bucket_t *bucket;

    if (!env || !base) {
        return NULL;
    }

    bucket = env->array_buckets[env_array_hash(base) % BCL_ARRAY_BUCKETS];
    while (bucket) {
        if (strcmp(bucket->base, base) == 0) {
            return bucket;
        }
        bucket = bucket->next;
    }

    return NULL;
}

static void env_array_index_insert(bcl_interp_t *interp, bcl_env_t *env, bcl_var_t *var) {
    const char *open;
    char *base;
    unsigned long slot;
    bcl_array_bucket_t *bucket;

    if (!env || !var || !var->name) {
        return;
    }

    open = array_name_open_paren(var->name);
    if (!open) {
        return;
    }

    base = bcl_xmemdup0(interp, var->name, (size_t)(open - var->name));
    slot = env_array_hash(base) % BCL_ARRAY_BUCKETS;
    bucket = env->array_buckets[slot];
    while (bucket) {
        if (strcmp(bucket->base, base) == 0) {
            free(base);
            var->array_bucket = bucket;
            var->array_next = bucket->vars;
            bucket->vars = var;
            bucket->count++;
            return;
        }
        bucket = bucket->next;
    }

    bucket = bcl_xcalloc(interp, 1, sizeof(*bucket));
    bucket->base = base;
    bucket->next = env->array_buckets[slot];
    env->array_buckets[slot] = bucket;
    var->array_bucket = bucket;
    var->array_next = bucket->vars;
    bucket->vars = var;
    bucket->count++;
}

static void env_array_index_unlink(bcl_env_t *env, bcl_var_t *var) {
    bcl_array_bucket_t *bucket;
    bcl_var_t **link;

    (void)env;

    if (!var || !var->array_bucket) {
        return;
    }

    bucket = var->array_bucket;
    link = &bucket->vars;
    while (*link) {
        if (*link == var) {
            *link = var->array_next;
            var->array_next = NULL;
            var->array_bucket = NULL;
            if (bucket->count > 0) {
                bucket->count--;
            }
            return;
        }
        link = &(*link)->array_next;
    }
}

static int env_has_array_like_in_scope(bcl_env_t *env, const char *base) {
    bcl_array_bucket_t *bucket = find_array_bucket_in_scope(env, base);
    return bucket && bucket->count > 0;
}

static bcl_array_alias_t *find_array_alias_in_scope(bcl_env_t *env, const char *name) {
    bcl_array_alias_t *alias;

    if (!env || !name) {
        return NULL;
    }

    alias = env->array_alias_buckets[env_alias_hash(name) % BCL_ARRAY_ALIAS_BUCKETS];
    while (alias) {
        if (strcmp(alias->name, name) == 0) {
            return alias;
        }
        alias = alias->hash_next;
    }

    return NULL;
}

static bcl_array_alias_t *find_array_alias(bcl_env_t *env, const char *name) {
    while (env) {
        bcl_array_alias_t *alias = find_array_alias_in_scope(env, name);
        if (alias) {
            return alias;
        }
        env = env->parent;
    }

    return NULL;
}

static void ensure_array_alias(bcl_interp_t *interp, bcl_env_t *env, const char *name, bcl_env_t *target_env) {
    bcl_array_alias_t *alias = find_array_alias_in_scope(env, name);

    if (alias) {
        alias->target_env = target_env;
        return;
    }

    alias = bcl_xcalloc(interp, 1, sizeof(bcl_array_alias_t));
    alias->name = bcl_xstrdup(interp, name);
    alias->target_env = target_env;
    alias->next = env->array_aliases;
    env->array_aliases = alias;
    {
        unsigned long bucket = env_alias_hash(name) % BCL_ARRAY_ALIAS_BUCKETS;
        alias->hash_next = env->array_alias_buckets[bucket];
        env->array_alias_buckets[bucket] = alias;
    }
}

static bcl_env_t *array_target_env_for_name(bcl_env_t *env, const char *name) {
    bcl_array_alias_t *alias = find_array_alias(env, name);
    return alias ? alias->target_env : env;
}

static bcl_var_t **find_var_link_in_scope(bcl_env_t *env, bcl_var_t *target) {
    bcl_var_t **link = env ? (bcl_var_t **)&env->vars : NULL;

    while (link && *link) {
        if (*link == target) {
            return link;
        }
        link = &(*link)->next;
    }

    return NULL;
}

static bcl_var_t **find_var_link_anywhere(bcl_interp_t *interp, bcl_env_t *env, bcl_var_t *target, bcl_env_t **owner_out) {
    bcl_env_t *scan = env;

    while (scan) {
        bcl_var_t **link = find_var_link_in_scope(scan, target);
        if (link) {
            if (owner_out) {
                *owner_out = scan;
            }
            return link;
        }
        scan = scan->parent;
    }

    if (interp->global_env && interp->global_env != env) {
        bcl_var_t **link = find_var_link_in_scope(interp->global_env, target);
        if (link && owner_out) {
            *owner_out = interp->global_env;
        }
        return link;
    }

    return NULL;
}

static void unset_alias_target(bcl_interp_t *interp, bcl_env_t *env, bcl_var_t *var) {
    bcl_var_t **link;
    bcl_env_t *owner = NULL;

    if (!var || !var->alias_target) {
        return;
    }

    link = find_var_link_anywhere(interp, env, var->alias_target, &owner);
    if (link) {
        bcl_var_t *target = *link;
        *link = target->next;
        env_var_unlink_hash(owner, target);
        free_var_node(target);
    }
}

static int env_unset_exact(bcl_interp_t *interp, bcl_env_t *env, const char *name) {
    bcl_var_t **link = env ? (bcl_var_t **)&env->vars : NULL;

    while (link && *link) {
        bcl_var_t *var = *link;

        if (strcmp(var->name, name) != 0) {
            link = &var->next;
            continue;
        }

        if (var->alias_target) {
            unset_alias_target(interp, env, var);
        } else if (var->protected) {
            bcl_raise_code(interp, "UNSET", 'F', "BADVALUE", "Cannot remove active parameter '%s'", name);
        }

        *link = var->next;
        env_var_unlink_hash(env, var);
        free_var_node(var);
        return 1;
    }

    return 0;
}

static int env_unset_array_like(bcl_interp_t *interp, bcl_env_t *env, const char *name) {
    bcl_array_bucket_t *bucket;
    int removed = 0;

    if (!env) {
        return 0;
    }

    bucket = find_array_bucket_in_scope(env, name);
    while (bucket && bucket->vars) {
        bcl_var_t *var = bucket->vars;
        bcl_var_t **link;

        link = find_var_link_in_scope(env, var);
        if (!link) {
            env_array_index_unlink(env, var);
            continue;
        }

        if (var->alias_target) {
            unset_alias_target(interp, env, var);
        } else if (var->protected) {
            bcl_raise_code(interp, "UNSET", 'F', "BADVALUE", "Cannot remove active parameter '%s'", var->name);
        }

        *link = var->next;
        env_var_unlink_hash(env, var);
        free_var_node(var);
        removed++;
    }

    return removed;
}

static void bcl_env_set_internal(bcl_interp_t *interp, bcl_env_t *env, const char *name, const char *value, int protect) {
    const char *open = array_name_open_paren(name);
    bcl_env_t *target_env = env;
    bcl_var_t *var;

    if (open) {
        char *base = bcl_xmemdup0(interp, name, (size_t)(open - name));
        target_env = array_target_env_for_name(env, base);
        if (find_var_in_scope(target_env, base)) {
            bcl_raise_code(interp, "SET", 'F', "DUPLICATE", "Variable '%s' already exists as scalar", base);
        }
        free(base);
    } else {
        if (env_has_array_like_in_scope(env, name) || find_array_alias(env, name)) {
            bcl_raise_code(interp, "SET", 'F', "DUPLICATE", "Variable '%s' already exists as array", name);
        }
    }

    var = find_var_in_scope(target_env, name);

    if (var) {
        bcl_var_t *target = var->alias_target ? var->alias_target : var;
        bcl_val_free(&target->value);
        target->value = bcl_val_make(value, strlen(value));
        if (protect && !var->alias_target) {
            var->protected = 1;
        }
        return;
    }

    var = bcl_xcalloc(interp, 1, sizeof(bcl_var_t));
    var->name = bcl_xstrdup(interp, name);
    var->value = bcl_val_make(value, strlen(value));
    var->protected = protect;
    env_var_insert(interp, target_env, var);
}

static int is_truthy_str(const char *value) {
    return value && value[0] != '\0' && strcmp(value, "0") != 0;
}

static void append_bytes(bcl_interp_t *interp, char **buf, size_t *len, size_t *cap, const char *data, size_t data_len) {
    if (data_len > (size_t)-1 - *len - 1) {
        bcl_raise_code(interp, "BCL", 'F', "NOMEM", "Buffer exceeds maximum size");
    }
    if (*len + data_len + 1 > *cap) {
        while (*len + data_len + 1 > *cap) {
            if (*cap > (size_t)-1 / 2) {
                bcl_raise_code(interp, "BCL", 'F', "NOMEM", "Buffer exceeds maximum size");
            }
            *cap *= 2;
        }
        *buf = bcl_xrealloc(interp, *buf, *cap);
    }

    memcpy(*buf + *len, data, data_len);
    *len += data_len;
    (*buf)[*len] = '\0';
}

static void append_cstr(bcl_interp_t *interp, char **buf, size_t *len, size_t *cap, const char *text) {
    append_bytes(interp, buf, len, cap, text, strlen(text));
}

static void append_char(bcl_interp_t *interp, char **buf, size_t *len, size_t *cap, char ch) {
    append_bytes(interp, buf, len, cap, &ch, 1);
}

static int parse_hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static void append_utf8(bcl_interp_t *interp, char **buf, size_t *len, size_t *cap, unsigned int cp) {
    char tmp[4];
    size_t tmp_len = 0;

    if (cp < 0x80) {
        tmp[tmp_len++] = (char)cp;
    } else if (cp < 0x800) {
        tmp[tmp_len++] = (char)(0xC0 | (cp >> 6));
        tmp[tmp_len++] = (char)(0x80 | (cp & 0x3F));
    } else {
        tmp[tmp_len++] = (char)(0xE0 | (cp >> 12));
        tmp[tmp_len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[tmp_len++] = (char)(0x80 | (cp & 0x3F));
    }

    append_bytes(interp, buf, len, cap, tmp, tmp_len);
}

static char *eval_word_text(bcl_interp_t *interp, bcl_env_t *env, const char *raw, int process_escapes);

static int scan_quoted_span(const char *src, size_t *pos) {
    int escape = 0;

    (*pos)++;
    while (src[*pos]) {
        char ch = src[*pos];

        if (!escape && ch == '"') {
            (*pos)++;
            return 1;
        }
        escape = (!escape && ch == '\\');
        (*pos)++;
    }
    return 0;
}

static int scan_bracketed_subcmd_span(const char *src, size_t *pos) {
    int depth = 1;

    (*pos)++;
    while (src[*pos] && depth > 0) {
        char ch = src[*pos];

        if (ch == '"') {
            if (!scan_quoted_span(src, pos)) {
                break;
            }
            continue;
        }
        if (ch == '#') {
            while (src[*pos] && src[*pos] != '\n') {
                (*pos)++;
            }
            continue;
        }
        if (ch == '[') {
            depth++;
        } else if (ch == ']') {
            depth--;
        }
        (*pos)++;
    }

    return depth == 0;
}

static int scan_varref_end(const char *raw, size_t start, size_t *end_out) {
    size_t pos = start;

    while (raw[pos] && is_var_name_char(raw[pos])) {
        pos++;
    }

    if (pos == start) {
        *end_out = start;
        return 1;
    }

    if (raw[pos] == '(') {
        int depth = 1;
        pos++;
        while (raw[pos] && depth > 0) {
            char ch = raw[pos];
            if (ch == '"') {
                if (!scan_quoted_span(raw, &pos)) {
                    break;
                }
                continue;
            }
            if (ch == '[') {
                if (!scan_bracketed_subcmd_span(raw, &pos)) {
                    break;
                }
                continue;
            }
            if (ch == '(') {
                depth++;
            } else if (ch == ')') {
                depth--;
            }
            pos++;
        }
        if (depth > 0) {
            *end_out = pos;
            return 0;
        }
    }

    *end_out = pos;
    return 1;
}

static char *resolve_varref_text(bcl_interp_t *interp, bcl_env_t *env, const char *expr) {
    const char *open = array_name_open_paren(expr);

    if (!open) {
        bcl_val_t val = bcl_env_get(env, expr);
        char *out = bcl_xstrdup(interp, val.data);
        bcl_val_free(&val);
        return out;
    }

    {
        char *base = bcl_xmemdup0(interp, expr, (size_t)(open - expr));
        size_t key_len = strlen(open + 1);
        char *key_raw;
        char *key;
        char *full;
        bcl_val_t val;
        char *out;

        if (key_len == 0) {
            free(base);
            return bcl_xstrdup(interp, "");
        }

        key_raw = bcl_xmemdup0(interp, open + 1, key_len - 1);
        key = eval_word_text(interp, env, key_raw, 0);
        full = bcl_xmalloc(interp, strlen(base) + strlen(key) + 3);
        snprintf(full, strlen(base) + strlen(key) + 3, "%s(%s)", base, key);

        val = bcl_env_get(env, full);
        out = bcl_xstrdup(interp, val.data);

        bcl_val_free(&val);
        free(full);
        free(key);
        free(key_raw);
        free(base);
        return out;
    }
}

static int scan_subcmd_text(const char *src, size_t open_pos, size_t *inner_start, size_t *inner_len,
                            size_t *close_pos) {
    size_t pos = open_pos + 1;
    int depth = 1;

    *inner_start = pos;
    while (src[pos] && depth > 0) {
        char ch = src[pos];

        if (ch == '"') {
            if (!scan_quoted_span(src, &pos)) {
                break;
            }
            continue;
        }
        if (ch == '#') {
            while (src[pos] && src[pos] != '\n') {
                pos++;
            }
            continue;
        }
        if (ch == '[') {
            depth++;
        } else if (ch == ']') {
            depth--;
        }
        pos++;
    }

    if (depth > 0) {
        return 0;
    }

    *close_pos = pos - 1;
    *inner_len = *close_pos - *inner_start;
    return 1;
}

static char *eval_word_text(bcl_interp_t *interp, bcl_env_t *env, const char *raw, int process_escapes) {
    size_t cap = strlen(raw) + 32;
    size_t len = 0;
    char *out = bcl_xmalloc(interp, cap);
    out[0] = '\0';

    for (size_t i = 0; raw[i]; i++) {
        if (process_escapes && raw[i] == '\\' && raw[i + 1]) {
            char next = raw[++i];

            switch (next) {
                case 'a': append_char(interp, &out, &len, &cap, '\a'); break;
                case 'b': append_char(interp, &out, &len, &cap, '\b'); break;
                case 'f': append_char(interp, &out, &len, &cap, '\f'); break;
                case 'n': append_char(interp, &out, &len, &cap, '\n'); break;
                case 'r': append_char(interp, &out, &len, &cap, '\r'); break;
                case 't': append_char(interp, &out, &len, &cap, '\t'); break;
                case 'v': append_char(interp, &out, &len, &cap, '\v'); break;
                case '\\':
                case '"':
                case '$':
                case '[':
                case ']':
                case '#':
                    append_char(interp, &out, &len, &cap, next);
                    break;
                case 'x': {
                    unsigned int value = 0;
                    int saw = 0;
                    while (raw[i + 1] && isxdigit((unsigned char)raw[i + 1])) {
                        int digit = parse_hex_digit(raw[++i]);
                        value = (value << 4) | (unsigned int)digit;
                        saw = 1;
                    }
                    append_char(interp, &out, &len, &cap, saw ? (char)(value & 0xFF) : 'x');
                    break;
                }
                case 'u': {
                    unsigned int value = 0;
                    int ok = 1;
                    for (int j = 0; j < 4; j++) {
                        int digit = parse_hex_digit(raw[i + 1 + j]);
                        if (digit < 0) {
                            ok = 0;
                            break;
                        }
                        value = (value << 4) | (unsigned int)digit;
                    }
                    if (ok) {
                        i += 4;
                        append_utf8(interp, &out, &len, &cap, value);
                    } else {
                        append_char(interp, &out, &len, &cap, 'u');
                    }
                    break;
                }
                default:
                    if (next >= '0' && next <= '7') {
                        unsigned int value = (unsigned int)(next - '0');
                        int consumed = 0;
                        while (consumed < 2 && raw[i + 1] >= '0' && raw[i + 1] <= '7') {
                            value = (value * 8) + (unsigned int)(raw[++i] - '0');
                            consumed++;
                        }
                        append_char(interp, &out, &len, &cap, (char)value);
                    } else {
                        append_char(interp, &out, &len, &cap, next);
                    }
                    break;
            }
            continue;
        }

        if (raw[i] == '$') {
            size_t start = i + 1;
            size_t end;

            if (!scan_varref_end(raw, start, &end)) {
                bcl_raise_code(interp, "EVAL", 'F', "UNTERMVAR", "Unterminated variable reference");
            }
            if (end == start) {
                append_char(interp, &out, &len, &cap, '$');
                continue;
            }

            char *name = bcl_xmemdup0(interp, raw + start, end - start);
            char *value = resolve_varref_text(interp, env, name);
            append_cstr(interp, &out, &len, &cap, value);
            free(value);
            free(name);
            i = end - 1;
            continue;
        }

        if (raw[i] == '[') {
            size_t start;
            size_t sub_len;
            size_t close_pos;

            if (scan_subcmd_text(raw, i, &start, &sub_len, &close_pos)) {
                char *sub = bcl_xmemdup0(interp, raw + start, sub_len);
                bcl_result_t sub_result = eval_subcmd_text(interp, env, sub);
                append_cstr(interp, &out, &len, &cap, sub_result.value.data);
                bcl_val_free(&sub_result.value);
                free(sub);
                i = close_pos;
                continue;
            }
        }

        append_char(interp, &out, &len, &cap, raw[i]);
    }

    return out;
}

static char *interpolate_string(bcl_interp_t *interp, bcl_env_t *env, const char *raw) {
    return eval_word_text(interp, env, raw, 1);
}

/* Environment management */
bcl_env_t *bcl_env_new(bcl_env_t *parent) {
    bcl_env_t *env = bcl_xcalloc(NULL, 1, sizeof(bcl_env_t));
    env->parent = parent;
    return env;
}

void bcl_env_free(bcl_env_t *env) {
    if (!env) {
        return;
    }

    bcl_var_t *var = env->vars;
    while (var) {
        bcl_var_t *next = var->next;
        free_var_node(var);
        var = next;
    }

    free_array_aliases(env);
    free_array_buckets(env);
    free(env);
}

bcl_val_t bcl_env_get(bcl_env_t *env, const char *name) {
    bcl_env_t *scan = env;

    while (scan) {
        bcl_var_t *var = find_var_in_scope(scan, name);
        if (var) {
            bcl_var_t *target = var->alias_target ? var->alias_target : var;
            return bcl_val_make(target->value.data, target->value.len);
        }
        scan = scan->parent;
    }

    {
        const char *open = array_name_open_paren(name);
        if (open) {
            char *base = bcl_xmemdup0(NULL, name, (size_t)(open - name));
            bcl_array_alias_t *alias = find_array_alias(env, base);
            free(base);

            if (alias) {
                bcl_var_t *var = find_var_in_scope(alias->target_env, name);
                if (var) {
                    bcl_var_t *target = var->alias_target ? var->alias_target : var;
                    return bcl_val_make(target->value.data, target->value.len);
                }
            }
        }
    }

    return bcl_val_empty();
}

int bcl_env_exists(bcl_env_t *env, const char *name) {
    bcl_env_t *scan = env;

    while (scan) {
        if (find_var_in_scope(scan, name)) {
            return 1;
        }
        scan = scan->parent;
    }

    {
        const char *open = array_name_open_paren(name);
        if (open) {
            char *base = bcl_xmemdup0(NULL, name, (size_t)(open - name));
            bcl_array_alias_t *alias = find_array_alias(env, base);
            free(base);

            if (alias && find_var_in_scope(alias->target_env, name)) {
                return 1;
            }
        }
    }

    return 0;
}

void bcl_env_set(bcl_interp_t *interp, bcl_env_t *env, const char *name, const char *value) {
    bcl_env_set_internal(interp, env, name, value, 0);
}

void bcl_env_set_protected(bcl_interp_t *interp, bcl_env_t *env, const char *name, const char *value) {
    bcl_env_set_internal(interp, env, name, value, 1);
}

int bcl_env_bind_global(bcl_interp_t *interp, bcl_env_t *env, const char *name) {
    bcl_env_t *global = interp->global_env;
    bcl_var_t *target = find_var_in_scope(global, name);
    bcl_var_t *local;

    if (!target) {
        if (find_var_in_scope(env, name) || env_has_array_like_in_scope(env, name)) {
            bcl_raise_code(interp, "GLOBAL", 'F', "CONFLICT", "Local variable '%s' conflicts with array binding", name);
        }
        ensure_array_alias(interp, env, name, global);
        return 1;
    }

    local = find_var_in_scope(env, name);
    if (!local) {
        local = bcl_xcalloc(interp, 1, sizeof(bcl_var_t));
        local->name = bcl_xstrdup(interp, name);
        local->value = bcl_val_empty();
        env_var_insert(interp, env, local);
    }

    bcl_val_free(&local->value);
    local->value = bcl_val_empty();
    local->alias_target = target;
    return 1;
}

int bcl_env_unset(bcl_interp_t *interp, bcl_env_t *env, const char *name) {
    int removed = 0;
    const char *open;

    if (!env || !name || name[0] == '\0') {
        return 0;
    }

    open = array_name_open_paren(name);
    if (open) {
        char *base = bcl_xmemdup0(interp, name, (size_t)(open - name));
        bcl_env_t *target_env = array_target_env_for_name(env, base);
        free(base);
        return env_unset_exact(interp, target_env, name);
    }

    env = array_target_env_for_name(env, name);
    removed += env_unset_exact(interp, env, name);
    removed += env_unset_array_like(interp, env, name);
    return removed;
}

static void export_catch_field(bcl_interp_t *interp, bcl_env_t *env, const char *base_name, const char *field, const char *value) {
    size_t name_len = strlen(base_name) + strlen(field) + 3;
    char *full = bcl_xmalloc(interp, name_len);

    snprintf(full, name_len, "%s(%s)", base_name, field);
    bcl_env_set(interp, env, full, value ? value : "");
    free(full);
}

static void export_catch_number(bcl_interp_t *interp, bcl_env_t *env, const char *base_name, const char *field, int value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    export_catch_field(interp, env, base_name, field, buf);
}

static void export_catch_error(bcl_interp_t *interp, bcl_env_t *env, const char *base_name) {
    size_t info_len = strlen(base_name) + 6;
    char *info_base = bcl_xmalloc(interp, info_len);

    snprintf(info_base, info_len, "%s_INFO", base_name);
    bcl_env_set(interp, env, base_name, interp->error_msg ? interp->error_msg : "");
    export_catch_field(interp, env, info_base, "TEXT", interp->error_msg ? interp->error_msg : "");
    export_catch_field(interp, env, info_base, "FACILITY", interp->error_facility);
    {
        char sev[2] = {interp->error_severity ? interp->error_severity : 'F', '\0'};
        export_catch_field(interp, env, info_base, "SEVERITY", sev);
    }
    export_catch_field(interp, env, info_base, "CODE", interp->error_code);
    export_catch_field(interp, env, info_base, "SUMMARY", interp->error_summary ? interp->error_summary : "");
    export_catch_field(interp, env, info_base, "FILE", interp->error_file ? interp->error_file : "");
    export_catch_number(interp, env, info_base, "LINE", interp->error_line);
    export_catch_number(interp, env, info_base, "COL", interp->error_col);
    export_catch_field(interp, env, info_base, "PROC", interp->error_proc ? interp->error_proc : "");
    export_catch_field(interp, env, info_base, "TRACE", interp->error_trace ? interp->error_trace : "");
    export_catch_field(interp, env, info_base, "EVENT", interp->error_event_source ? interp->error_event_source : "");
    export_catch_field(interp, env, info_base, "OSCODE", interp->error_oscode ? interp->error_oscode : "");
    export_catch_field(interp, env, info_base, "OSMSG", interp->error_osmsg ? interp->error_osmsg : "");
    export_catch_field(interp, env, info_base, "DISPLAY", interp->error_display ? interp->error_display : "");
    free(info_base);
}

static unsigned long proc_hash(const char *name) {
    unsigned long hash = 5381;
    unsigned char ch;

    while ((ch = (unsigned char)*name++) != '\0') {
        hash = ((hash << 5) + hash) ^ (unsigned char)tolower(ch);
    }

    return hash;
}

static bcl_proc_t **proc_table(bcl_interp_t *interp) {
    if (!interp->proc_buckets) {
        interp->proc_buckets = bcl_xcalloc(interp, BCL_PROC_BUCKETS, sizeof(bcl_proc_t *));
    }
    return (bcl_proc_t **)interp->proc_buckets;
}

static bcl_proc_t *proc_lookup(bcl_interp_t *interp, const char *name) {
    bcl_proc_t *proc;

    if (interp->proc_buckets) {
        bcl_proc_t **buckets = (bcl_proc_t **)interp->proc_buckets;
        proc = buckets[proc_hash(name) % BCL_PROC_BUCKETS];
        while (proc) {
            if (strcasecmp(proc->name, name) == 0) {
                return proc;
            }
            proc = proc->hash_next;
        }
        return NULL;
    }

    proc = (bcl_proc_t *)interp->procs;
    while (proc) {
        if (strcasecmp(proc->name, name) == 0) {
            return proc;
        }
        proc = proc->next;
    }
    return NULL;
}

static void proc_register(bcl_interp_t *interp, const char *name, bcl_procdef_t *def) {
    if (def->invalid_param) {
        bcl_raise_code(interp, "PAR", 'F', "PROCDEF", "PROC %s: invalid optional parameter order near '%s'", name, def->invalid_param);
    }

    bcl_proc_t *existing = proc_lookup(interp, name);

    if (existing) {
        for (int i = 0; i < existing->nparams; i++) {
            free(existing->params[i].name);
        }
        free(existing->params);
        free(existing->source_name);
        bcl_node_free(existing->body);
        free(existing->body_src);
        existing->params = def->params;
        existing->nparams = def->nparams;
        existing->nrequired = def->nrequired;
        existing->source_name = def->source_name;
        existing->body = def->body;
        existing->body_src = def->body_src;
        def->params = NULL;
        def->nparams = 0;
        def->nrequired = 0;
        def->source_name = NULL;
        def->body = NULL;
        def->body_src = NULL;
        return;
    }

    bcl_proc_t *proc = bcl_xcalloc(interp, 1, sizeof(bcl_proc_t));
    bcl_proc_t **buckets = proc_table(interp);
    unsigned long bucket = proc_hash(name) % BCL_PROC_BUCKETS;

    proc->name = bcl_xstrdup(interp, name);
    proc->params = def->params;
    proc->nparams = def->nparams;
    proc->nrequired = def->nrequired;
    proc->source_name = def->source_name;
    proc->body = def->body;
    proc->body_src = def->body_src;
    proc->next = (bcl_proc_t *)interp->procs;
    interp->procs = proc;
    proc->hash_next = buckets[bucket];
    buckets[bucket] = proc;

    def->params = NULL;
    def->nparams = 0;
    def->nrequired = 0;
    def->source_name = NULL;
    def->body = NULL;
    def->body_src = NULL;
}

static bcl_result_t result_ok_empty(void) {
    bcl_result_t result;
    result.status = EVAL_OK;
    result.value = bcl_val_empty();
    result.exit_code = 0;
    return result;
}

static bcl_result_t result_from_value(bcl_val_t value) {
    bcl_result_t result = result_ok_empty();
    bcl_val_free(&result.value);
    result.value = value;
    return result;
}

static bcl_val_t eval_literal(bcl_node_t *node) {
    return bcl_val_make(node->val.data, node->val.len);
}

static bcl_val_t eval_string(bcl_interp_t *interp, bcl_env_t *env, bcl_node_t *node) {
    char *value = interpolate_string(interp, env, node->val.data);
    bcl_val_t result = bcl_val_make(value, strlen(value));
    free(value);
    return result;
}

static bcl_val_t eval_word(bcl_interp_t *interp, bcl_env_t *env, bcl_node_t *node) {
    char *value = eval_word_text(interp, env, node->val.data, 0);
    bcl_val_t result = bcl_val_make(value, strlen(value));
    free(value);
    return result;
}

static bcl_result_t eval_subcmd(bcl_interp_t *interp, bcl_env_t *env, bcl_node_t *node) {
    return eval_subcmd_text(interp, env, node->name);
}

static bcl_result_t call_proc(bcl_interp_t *interp, bcl_proc_t *proc, int argc, bcl_val_t *argv) {
    int proc_depth = 0;
    for (bcl_exec_frame_t *frame = interp->frame_stack; frame; frame = frame->prev) {
        if (frame->kind == BCL_FRAME_PROC) {
            proc_depth++;
        }
    }
    if (proc_depth >= BCL_MAX_PROC_DEPTH) {
        bcl_raise_code(interp, "EVAL", 'F', "DEPTH", "Procedure call depth exceeds limit");
    }

    bcl_env_t *local = bcl_env_new(NULL);
    bcl_result_t result = result_ok_empty();
    const char *prev_proc = interp->current_proc_name;
    const char *prev_source = interp->current_source_name;
    bcl_try_frame_t frame;
    bcl_exec_frame_t exec_frame;
    int call_line = interp->current_node ? interp->current_node->line : 0;
    int call_col = interp->current_node ? interp->current_node->col : 0;

    if (argc < proc->nrequired || argc > proc->nparams) {
        bcl_env_free(local);
        bcl_raise_code(interp, "EVAL", 'F', "BADARGS", "%s: expected between %d and %d arguments, got %d",
                       proc->name, proc->nrequired, proc->nparams, argc);
    }

    for (int i = 0; i < argc; i++) {
        bcl_env_set_protected(interp, local, proc->params[i].name, argv[i].data);
    }

    frame.prev = interp->try_stack;
    interp->try_stack = &frame;
    interp->current_proc_name = proc->name;
    interp->current_source_name = proc->source_name ? proc->source_name : prev_source;
    bcl_frame_push(interp, &exec_frame, BCL_FRAME_PROC, proc->name, prev_source, call_line, call_col);

    if (setjmp(frame.env) != 0) {
        interp->try_stack = frame.prev;
        interp->current_proc_name = prev_proc;
        interp->current_source_name = prev_source;
        bcl_frame_pop(interp, &exec_frame);
        bcl_env_free(local);
        if (frame.prev) {
            longjmp(frame.prev->env, 1);
        }
        longjmp(interp->error_jmp, 1);
    }

    result = bcl_eval_block(interp, local, proc->body);
    interp->try_stack = frame.prev;
    interp->current_proc_name = prev_proc;
    interp->current_source_name = prev_source;
    bcl_frame_pop(interp, &exec_frame);
    bcl_env_free(local);

    if (result.status == EVAL_RETURN) {
        result.status = EVAL_OK;
    }

    return result;
}

static bcl_result_t invoke_named(bcl_interp_t *interp, bcl_env_t *env, const char *name, int argc, bcl_val_t *argv) {
    bcl_cmd_t *cmd = bcl_cmd_lookup(interp, name);

    if (cmd) {
        if (cmd->argc >= 0 && argc != cmd->argc) {
            bcl_raise_code(interp, "EVAL", 'F', "BADARGS", "%s: expected %d arguments, got %d", name, cmd->argc, argc);
        }

        bcl_val_t value = bcl_dispatch(interp, env, name, argc, argv);
        if (interp->pending_status != EVAL_OK) {
            bcl_result_t pending = result_ok_empty();
            pending.status = interp->pending_status;
            pending.exit_code = interp->pending_exit_code;
            interp->pending_status = EVAL_OK;
            interp->pending_exit_code = 0;
            bcl_val_free(&value);
            return pending;
        }
        return result_from_value(value);
    }

    bcl_proc_t *proc = proc_lookup(interp, name);
    if (!proc) {
        bcl_raise_code(interp, "EVAL", 'F', "UNKCMD", "Unknown command: %s", name);
    }

    return call_proc(interp, proc, argc, argv);
}

bcl_result_t bcl_eval_invoke(bcl_interp_t *interp, bcl_env_t *env, const char *name, int argc, bcl_val_t *argv) {
    return invoke_named(interp, env, name, argc, argv);
}

static bcl_result_t eval_cmd(bcl_interp_t *interp, bcl_env_t *env, bcl_node_t *node) {
    volatile int argc = 0;
    int i = 0;
    volatile int evaluated = 0;
    bcl_node_t *volatile arg = node->children;
    bcl_val_t *volatile argv = NULL;
    bcl_try_frame_t frame;

    while (arg) {
        argc++;
        arg = arg->next;
    }

    argv = argc ? bcl_xcalloc(interp, (size_t)argc, sizeof(bcl_val_t)) : NULL;
    arg = node->children;

    frame.prev = interp->try_stack;
    interp->try_stack = &frame;
    if (setjmp(frame.env) != 0) {
        interp->try_stack = frame.prev;
        for (i = 0; i < evaluated; i++) {
            bcl_val_free(&argv[i]);
        }
        free(argv);
        if (frame.prev) {
            longjmp(frame.prev->env, 1);
        }
        longjmp(interp->error_jmp, 1);
    }

    while (arg) {
        bcl_result_t arg_result = bcl_eval_node(interp, env, arg);
        if (arg_result.status != EVAL_OK) {
            interp->try_stack = frame.prev;
            for (int j = 0; j < evaluated; j++) {
                bcl_val_free(&argv[j]);
            }
            free(argv);
            return arg_result;
        }
        argv[evaluated] = result_take_value(&arg_result);
        evaluated++;
        arg = arg->next;
    }

    bcl_result_t result = invoke_named(interp, env, node->name, argc, argv);
    interp->try_stack = frame.prev;
    for (i = 0; i < argc; i++) {
        bcl_val_free(&argv[i]);
    }
    free(argv);
    return result;
}

static void cond_skip_ws(cond_ctx_t *ctx) {
    while (ctx->src[ctx->pos] && isspace((unsigned char)ctx->src[ctx->pos])) {
        ctx->pos++;
    }
}

static int cond_match(cond_ctx_t *ctx, const char *text) {
    size_t len = strlen(text);
    cond_skip_ws(ctx);
    if (strncmp(ctx->src + ctx->pos, text, len) == 0) {
        ctx->pos += len;
        return 1;
    }
    return 0;
}

static void cond_enter(bcl_interp_t *interp, cond_ctx_t *ctx) {
    if (ctx->depth >= BCL_MAX_COND_DEPTH) {
        bcl_raise_code(interp, "EVAL", 'F', "DEPTH", "Condition nesting exceeds limit");
    }
    ctx->depth++;
}

static void cond_leave(cond_ctx_t *ctx) {
    if (ctx->depth > 0) {
        ctx->depth--;
    }
}

static char *cond_parse_scalar(bcl_interp_t *interp, bcl_env_t *env, cond_ctx_t *ctx);

static int cond_parse_or(bcl_interp_t *interp, bcl_env_t *env, cond_ctx_t *ctx);

static int compare_values(const char *left, const char *op, const char *right) {
    double ln = 0.0;
    double rn = 0.0;
    int left_num = bcl_calc_is_numeric(left, &ln);
    int right_num = bcl_calc_is_numeric(right, &rn);

    if (strcmp(op, "==") == 0) {
        if (left_num && right_num) {
            return fabs(ln - rn) < 1e-12;
        }
        return strcmp(left, right) == 0;
    }
    if (strcmp(op, "!=") == 0) {
        if (left_num && right_num) {
            return fabs(ln - rn) >= 1e-12;
        }
        return strcmp(left, right) != 0;
    }
    if (strcmp(op, "<") == 0) {
        return left_num && right_num ? (ln < rn) : (strcmp(left, right) < 0);
    }
    if (strcmp(op, ">") == 0) {
        return left_num && right_num ? (ln > rn) : (strcmp(left, right) > 0);
    }
    if (strcmp(op, "<=") == 0) {
        return left_num && right_num ? (ln <= rn) : (strcmp(left, right) <= 0);
    }
    if (strcmp(op, ">=") == 0) {
        return left_num && right_num ? (ln >= rn) : (strcmp(left, right) >= 0);
    }

    return 0;
}

static int cond_parse_comparison(bcl_interp_t *interp, bcl_env_t *env, cond_ctx_t *ctx) {
    int result;

    cond_enter(interp, ctx);
    cond_skip_ws(ctx);

    if (ctx->src[ctx->pos] == '(') {
        ctx->pos++;
        int value = cond_parse_or(interp, env, ctx);
        cond_skip_ws(ctx);
        if (ctx->src[ctx->pos] != ')') {
            bcl_raise_code(interp, "EVAL", 'F', "UNTERMCOND", "Unterminated condition group");
        }
        ctx->pos++;
        cond_leave(ctx);
        return value;
    }

    char *left = cond_parse_scalar(interp, env, ctx);
    char op[3] = {0};

    cond_skip_ws(ctx);

    if (strncmp(ctx->src + ctx->pos, "==", 2) == 0 ||
        strncmp(ctx->src + ctx->pos, "!=", 2) == 0 ||
        strncmp(ctx->src + ctx->pos, "<=", 2) == 0 ||
        strncmp(ctx->src + ctx->pos, ">=", 2) == 0) {
        op[0] = ctx->src[ctx->pos++];
        op[1] = ctx->src[ctx->pos++];
    } else if (ctx->src[ctx->pos] == '<' || ctx->src[ctx->pos] == '>') {
        op[0] = ctx->src[ctx->pos++];
    }

    if (op[0]) {
        char *right = cond_parse_scalar(interp, env, ctx);
        result = compare_values(left, op, right);
        free(right);
    } else {
        result = is_truthy_str(left);
    }

    free(left);
    cond_leave(ctx);
    return result;
}

static int cond_parse_unary(bcl_interp_t *interp, bcl_env_t *env, cond_ctx_t *ctx) {
    int result;

    cond_enter(interp, ctx);
    cond_skip_ws(ctx);
    if (ctx->src[ctx->pos] == '!') {
        ctx->pos++;
        result = !cond_parse_unary(interp, env, ctx);
        cond_leave(ctx);
        return result;
    }
    result = cond_parse_comparison(interp, env, ctx);
    cond_leave(ctx);
    return result;
}

static int cond_parse_and(bcl_interp_t *interp, bcl_env_t *env, cond_ctx_t *ctx) {
    int value = cond_parse_unary(interp, env, ctx);
    while (cond_match(ctx, "&&")) {
        value = value && cond_parse_unary(interp, env, ctx);
    }
    return value;
}

static int cond_parse_or(bcl_interp_t *interp, bcl_env_t *env, cond_ctx_t *ctx) {
    int value = cond_parse_and(interp, env, ctx);
    while (cond_match(ctx, "||")) {
        value = value || cond_parse_and(interp, env, ctx);
    }
    return value;
}

static char *cond_parse_quoted(bcl_interp_t *interp, bcl_env_t *env, cond_ctx_t *ctx) {
    size_t start;
    int escape = 0;
    char *raw;

    ctx->pos++;
    start = ctx->pos;

    while (ctx->src[ctx->pos]) {
        char c = ctx->src[ctx->pos];
        if (!escape && c == '"') {
            break;
        }
        escape = (!escape && c == '\\');
        if (escape && c != '\\') {
            escape = 0;
        }
        ctx->pos++;
    }

    if (ctx->src[ctx->pos] != '"') {
        bcl_raise_code(interp, "EVAL", 'F', "UNTERMSTR", "Unterminated string literal");
    }
    raw = bcl_xmemdup0(interp, ctx->src + start, ctx->pos - start);
    ctx->pos++;

    char *value = interpolate_string(interp, env, raw);
    free(raw);
    return value;
}

static char *cond_parse_scalar(bcl_interp_t *interp, bcl_env_t *env, cond_ctx_t *ctx) {
    size_t start;

    cond_skip_ws(ctx);

    if (ctx->src[ctx->pos] == '"') {
        return cond_parse_quoted(interp, env, ctx);
    }

    if (ctx->src[ctx->pos] == '$') {
        size_t end;
        char *name;
        char *out;

        ctx->pos++;
        start = ctx->pos;
        if (!scan_varref_end(ctx->src, start, &end)) {
            bcl_raise_code(interp, "EVAL", 'F', "UNTERMVAR", "Unterminated variable reference");
        }
        name = bcl_xmemdup0(interp, ctx->src + start, end - start);
        out = resolve_varref_text(interp, env, name);
        free(name);
        ctx->pos = end;
        return out;
    }

    if (ctx->src[ctx->pos] == '[') {
        size_t script_len;
        size_t close_pos;

        if (!scan_subcmd_text(ctx->src, ctx->pos, &start, &script_len, &close_pos)) {
            bcl_raise_code(interp, "EVAL", 'F', "UNTERMSUB", "Unterminated command substitution");
        }
        ctx->pos = close_pos + 1;
        char *script = bcl_xmemdup0(interp, ctx->src + start, script_len);
        bcl_result_t value = eval_subcmd_text(interp, env, script);
        char *out = bcl_xstrdup(interp, value.value.data);
        bcl_val_free(&value.value);
        free(script);
        return out;
    }

    start = ctx->pos;
    while (ctx->src[ctx->pos] &&
           !isspace((unsigned char)ctx->src[ctx->pos]) &&
           ctx->src[ctx->pos] != ')' &&
           !(ctx->src[ctx->pos] == '&' && ctx->src[ctx->pos + 1] == '&') &&
           !(ctx->src[ctx->pos] == '|' && ctx->src[ctx->pos + 1] == '|') &&
           !(ctx->src[ctx->pos] == '=' && ctx->src[ctx->pos + 1] == '=') &&
           !(ctx->src[ctx->pos] == '!' && ctx->src[ctx->pos + 1] == '=') &&
           !(ctx->src[ctx->pos] == '<' && ctx->src[ctx->pos + 1] == '=') &&
           !(ctx->src[ctx->pos] == '>' && ctx->src[ctx->pos + 1] == '=') &&
           ctx->src[ctx->pos] != '<' &&
           ctx->src[ctx->pos] != '>') {
        ctx->pos++;
    }

    if (ctx->pos == start) {
        return bcl_xstrdup(interp, "");
    }

    return bcl_xmemdup0(interp, ctx->src + start, ctx->pos - start);
}

bcl_result_t bcl_eval_source_named(bcl_interp_t *interp, bcl_env_t *env, const char *src, const char *source_name) {
    bcl_lexer_t lexer;
    bcl_result_t last = result_ok_empty();
    const char *prev_source = interp->current_source_name;
    bcl_node_t *prev_node = interp->current_node;
    interp->current_source_name = source_name ? source_name : prev_source;

    bcl_lexer_init(&lexer, interp, src);

    while (1) {
        bcl_node_t *node = bcl_parse(interp, &lexer);
        if (!node) {
            break;
        }

        result_discard_value(&last);
        last = bcl_eval_node(interp, env, node);
        bcl_node_free(node);

        if (last.status != EVAL_OK) {
            break;
        }
    }

    bcl_lexer_free(&lexer);
    interp->current_source_name = prev_source;
    interp->current_node = prev_node;
    return last;
}

bcl_result_t bcl_eval_source(bcl_interp_t *interp, bcl_env_t *env, const char *src) {
    return bcl_eval_source_named(interp, env, src, interp->current_source_name);
}

static bcl_result_t eval_subcmd_text(bcl_interp_t *interp, bcl_env_t *env, const char *text) {
    bcl_try_frame_t frame;
    bcl_exec_frame_t exec_frame;
    bcl_result_t result;
    int call_line = interp->current_node ? interp->current_node->line : 0;
    int call_col = interp->current_node ? interp->current_node->col : 0;
    int subcmd_depth = 0;

    for (bcl_exec_frame_t *scan = interp->frame_stack; scan; scan = scan->prev) {
        if (scan->kind == BCL_FRAME_SUBCMD) {
            subcmd_depth++;
        }
    }
    if (subcmd_depth >= BCL_MAX_SUBCMD_DEPTH) {
        bcl_raise_code(interp, "EVAL", 'F', "DEPTH", "Subcommand nesting exceeds limit");
    }

    frame.prev = interp->try_stack;
    interp->try_stack = &frame;
    bcl_frame_push(interp, &exec_frame, BCL_FRAME_SUBCMD, "<subcmd>", interp->current_source_name, call_line, call_col);

    if (setjmp(frame.env) != 0) {
        interp->try_stack = frame.prev;
        bcl_frame_pop(interp, &exec_frame);
        if (frame.prev) {
            longjmp(frame.prev->env, 1);
        }
        longjmp(interp->error_jmp, 1);
    }

    result = bcl_eval_source_named(interp, env, text, "<subcmd>");
    interp->try_stack = frame.prev;
    bcl_frame_pop(interp, &exec_frame);
    return result;
}

int bcl_eval_condition(bcl_interp_t *interp, bcl_env_t *env, bcl_node_t *cond) {
    cond_ctx_t ctx;

    if (!cond || !cond->val.data) {
        return 0;
    }

    ctx.src = cond->val.data;
    ctx.pos = 0;
    ctx.depth = 0;
    return cond_parse_or(interp, env, &ctx);
}

bcl_result_t bcl_eval_block(bcl_interp_t *interp, bcl_env_t *env, bcl_node_t *block) {
    bcl_result_t result = result_ok_empty();

    while (block) {
        result_discard_value(&result);
        result = bcl_eval_node(interp, env, block);
        if (result.status != EVAL_OK) {
            return result;
        }
        block = block->next;
    }

    return result;
}

bcl_result_t bcl_eval_node(bcl_interp_t *interp, bcl_env_t *env, bcl_node_t *node) {
    bcl_result_t result = result_ok_empty();

    if (!node) {
        return result;
    }

    interp->current_node = node;

    switch (node->type) {
        case NODE_LITERAL:
            return result_from_value(eval_literal(node));

        case NODE_STRING:
            return result_from_value(eval_string(interp, env, node));

        case NODE_WORD:
            return result_from_value(eval_word(interp, env, node));

        case NODE_VARREF: {
            char *value = resolve_varref_text(interp, env, node->name);
            bcl_val_t result_value = bcl_val_make(value, strlen(value));
            free(value);
            return result_from_value(result_value);
        }

        case NODE_SUBCMD:
            return eval_subcmd(interp, env, node);

        case NODE_CMD:
            return eval_cmd(interp, env, node);

        case NODE_BLOCK:
            return bcl_eval_block(interp, env, node->children);

        case NODE_RETURN:
            if (node->children) {
                result = bcl_eval_node(interp, env, node->children);
            }
            result.status = EVAL_RETURN;
            return result;

        case NODE_BREAK:
            result.status = EVAL_BREAK;
            return result;

        case NODE_NEXT:
            result.status = EVAL_NEXT;
            return result;

        case NODE_ERROR:
            if (node->children) {
                bcl_result_t msg = bcl_eval_node(interp, env, node->children);
                char summary[1024];
                snprintf(summary, sizeof(summary), "%s", msg.value.data);
                bcl_val_free(&msg.value);
                bcl_raise_code(interp, "EVAL", 'F', "USERERR", "%s", summary);
            }
            bcl_raise_code(interp, "EVAL", 'F', "USERERR", "Error");
            break;

        case NODE_EXIT:
            if (node->children) {
                bcl_result_t code = bcl_eval_node(interp, env, node->children);
                result.exit_code = atoi(code.value.data);
                bcl_val_free(&code.value);
            }
            result.status = EVAL_EXIT;
            return result;

        case NODE_IF_THEN: {
            bcl_if_data_t *data = (bcl_if_data_t *)node->extra;
            bcl_if_clause_t *clause = data->clauses;

            while (clause) {
                if (bcl_eval_condition(interp, env, clause->cond)) {
                    return bcl_eval_block(interp, env, clause->body);
                }
                clause = clause->next;
            }

            if (data->else_body) {
                return bcl_eval_block(interp, env, data->else_body);
            }

            return result;
        }

        case NODE_IF_DO: {
            bcl_loop_data_t *data = (bcl_loop_data_t *)node->extra;
            int exhausted = 1;

            while (bcl_eval_condition(interp, env, data->cond)) {
                result_discard_value(&result);
                result = bcl_eval_block(interp, env, data->body);

                if (result.status == EVAL_NEXT) {
                    result.status = EVAL_OK;
                    continue;
                }
                if (result.status == EVAL_BREAK) {
                    result.status = EVAL_OK;
                    exhausted = 0;
                    break;
                }
                if (result.status != EVAL_OK) {
                    return result;
                }
            }

            if (exhausted && data->else_body) {
                return bcl_eval_block(interp, env, data->else_body);
            }

            return result;
        }

        case NODE_PROC:
            proc_register(interp, node->name, (bcl_procdef_t *)node->extra);
            return result;

        case NODE_TRY: {
            bcl_try_data_t *data = (bcl_try_data_t *)node->extra;
            bcl_try_frame_t frame;
            frame.prev = interp->try_stack;
            interp->try_stack = &frame;

            if (setjmp(frame.env) == 0) {
                result = bcl_eval_block(interp, env, data->try_body);
                interp->try_stack = frame.prev;
                return result;
            }

            interp->try_stack = frame.prev;
            if (data->catch_var) {
                export_catch_error(interp, env, data->catch_var);
            }
            return bcl_eval_block(interp, env, data->catch_body);
        }

        default:
            return result;
    }

    return result;
}
