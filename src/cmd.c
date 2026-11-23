/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Rafael Seguí Revert. See LICENSE. */
/**
 * BCL Builtin Commands implementation
 */

#define _GNU_SOURCE

#include "platform.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "bcl.h"
#include "calc.h"
#include "cmd.h"
#include "eval.h"
#include "evtsys.h"

typedef struct bcl_module_dependency {
    struct bcl_module *provider;
    struct bcl_module_dependency *next;
} bcl_module_dependency_t;

typedef struct bcl_module {
    char *path;
    char *version;
    void *handle;
    void *base_addr;
    void (*cleanup)(bcl_interp_t *interp);
    char **cmd_names;
    int ncmds;
    int cmd_cap;
    int active_calls;
    unsigned long load_seq;
    bcl_module_dependency_t *dependencies;
    struct bcl_module *next;
    struct bcl_module *hash_next;
} bcl_module_t;

typedef struct bcl_module_guard {
    bcl_module_t *owner;
    bcl_module_guard_cb_t callback;
    void *userdata;
    struct bcl_module_guard *next;
} bcl_module_guard_t;

#define BCL_MODULE_BUCKETS 128

static bcl_module_t *current_loading_module = NULL;
static unsigned long next_module_load_seq = 1;
static int runtime_cleanup_active = 0;

typedef int (*bcl_module_unload_guard_fn)(bcl_interp_t *interp, void *module_token, const char *module_path);
typedef void (*bcl_module_unload_notify_fn)(bcl_interp_t *interp, void *module_token, const char *module_path);
typedef void (*bcl_module_runtime_prepare_fn)(bcl_interp_t *interp, void *module_token, const char *module_path);
typedef const char *(*bcl_module_version_fn)(void);

static uint32_t cmd_hash(const char *name) {
    uint32_t h = 5381;
    while (*name) {
        char c = *name++;
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        h = h * 33u ^ (unsigned char)c;
    }
    return h % BCL_CMD_BUCKETS;
}

static bcl_val_t join_args(bcl_interp_t *interp, int argc, bcl_val_t *argv, const char *sep) {
    size_t sep_len = strlen(sep);
    size_t total = 1;
    char *buf;
    size_t len = 0;

    for (int i = 0; i < argc; i++) {
        if (argv[i].len > (size_t)-1 - total) {
            bcl_raise_code(interp, "BCL", 'F', "NOMEM", "Argument join exceeds maximum size");
        }
        total += argv[i].len;
        if (i > 0) {
            if (sep_len > (size_t)-1 - total) {
                bcl_raise_code(interp, "BCL", 'F', "NOMEM", "Argument join exceeds maximum size");
            }
            total += sep_len;
        }
    }

    buf = bcl_xmalloc(interp, total);
    buf[0] = '\0';

    for (int i = 0; i < argc; i++) {
        if (i > 0) {
            memcpy(buf + len, sep, sep_len);
            len += sep_len;
        }
        memcpy(buf + len, argv[i].data, argv[i].len);
        len += argv[i].len;
    }
    buf[len] = '\0';

    bcl_val_t value;
    value.data = buf;
    value.len = len;
    return value;
}

typedef struct {
    char **items;
    int count;
    int cap;
} bcl_list_t;

typedef struct {
    char *text;
    int is_numeric;
    double numeric;
} bcl_list_sort_entry_t;

static void append_buf_bytes(bcl_interp_t *interp, char **buf, size_t *len, size_t *cap, const char *data, size_t data_len) {
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

static void append_buf_char(bcl_interp_t *interp, char **buf, size_t *len, size_t *cap, char ch) {
    append_buf_bytes(interp, buf, len, cap, &ch, 1);
}

static size_t utf8_char_bytes(const char *s) {
    unsigned char c = (unsigned char)s[0];

    if ((c & 0x80) == 0) {
        return 1;
    }
    if ((c & 0xE0) == 0xC0 && s[1]) {
        return 2;
    }
    if ((c & 0xF0) == 0xE0 && s[1] && s[2]) {
        return 3;
    }
    if ((c & 0xF8) == 0xF0 && s[1] && s[2] && s[3]) {
        return 4;
    }

    return 1;
}

static int utf8_char_count(const char *s) {
    int count = 0;

    while (*s) {
        s += utf8_char_bytes(s);
        count++;
    }

    return count;
}

static int utf8_offset_for_pos(const char *s, int pos, size_t *offset_out, int allow_end) {
    int current = 1;
    size_t offset = 0;

    if (pos < 1) {
        return 0;
    }

    while (s[offset]) {
        if (current == pos) {
            *offset_out = offset;
            return 1;
        }
        offset += utf8_char_bytes(s + offset);
        current++;
    }

    if (allow_end && current == pos) {
        *offset_out = offset;
        return 1;
    }

    return 0;
}

static bcl_val_t utf8_make_range(const char *s, int start_pos, int end_pos) {
    size_t start_off;
    size_t end_off;
    int total_chars;

    if (end_pos < start_pos) {
        return bcl_val_empty();
    }

    total_chars = utf8_char_count(s);
    if (total_chars == 0) {
        return bcl_val_empty();
    }

    if (start_pos < 1) {
        start_pos = 1;
    }
    if (end_pos > total_chars) {
        end_pos = total_chars;
    }
    if (start_pos > total_chars) {
        return bcl_val_empty();
    }

    utf8_offset_for_pos(s, start_pos, &start_off, 0);
    utf8_offset_for_pos(s, end_pos + 1, &end_off, 1);
    return bcl_val_make(s + start_off, end_off - start_off);
}

static bcl_val_t utf8_make_char_at(const char *s, int pos) {
    size_t start_off;
    size_t len;

    if (!utf8_offset_for_pos(s, pos, &start_off, 0)) {
        return bcl_val_empty();
    }

    len = utf8_char_bytes(s + start_off);
    return bcl_val_make(s + start_off, len);
}

static bcl_val_t utf8_insert_text(bcl_interp_t *interp, const char *s, const char *insert, int pos) {
    size_t off;
    size_t s_len = strlen(s);
    size_t insert_len = strlen(insert);
    size_t cap;
    size_t len = 0;
    char *buf;
    int total_chars = utf8_char_count(s);

    if (insert_len > (size_t)-1 - s_len - 1) {
        bcl_raise_code(interp, "BCL", 'F', "NOMEM", "String exceeds maximum size");
    }
    cap = s_len + insert_len + 1;
    buf = bcl_xmalloc(interp, cap);
    buf[0] = '\0';

    if (pos < 1) {
        pos = 1;
    }
    if (pos > total_chars + 1) {
        pos = total_chars + 1;
    }

    utf8_offset_for_pos(s, pos, &off, 1);
    append_buf_bytes(interp, &buf, &len, &cap, s, off);
    append_buf_bytes(interp, &buf, &len, &cap, insert, insert_len);
    append_buf_bytes(interp, &buf, &len, &cap, s + off, strlen(s + off));
    return (bcl_val_t){buf, len};
}

static bcl_val_t utf8_delete_range(bcl_interp_t *interp, const char *s, int pos, int num) {
    size_t start_off;
    size_t end_off;
    size_t cap = strlen(s) + 1;
    size_t len = 0;
    char *buf = bcl_xmalloc(interp, cap);
    int total_chars = utf8_char_count(s);

    buf[0] = '\0';

    if (num <= 0 || total_chars == 0) {
        append_buf_bytes(interp, &buf, &len, &cap, s, strlen(s));
        return (bcl_val_t){buf, len};
    }
    if (pos < 1) {
        pos = 1;
    }
    if (pos > total_chars) {
        append_buf_bytes(interp, &buf, &len, &cap, s, strlen(s));
        return (bcl_val_t){buf, len};
    }

    utf8_offset_for_pos(s, pos, &start_off, 0);
    if (!utf8_offset_for_pos(s, pos + num, &end_off, 1)) {
        end_off = strlen(s);
    }

    append_buf_bytes(interp, &buf, &len, &cap, s, start_off);
    append_buf_bytes(interp, &buf, &len, &cap, s + end_off, strlen(s + end_off));
    return (bcl_val_t){buf, len};
}

static bcl_val_t utf8_reverse_text(bcl_interp_t *interp, const char *s) {
    size_t byte_len = strlen(s);
    size_t cap = byte_len + 1;
    size_t len = 0;
    size_t *offsets;
    int count = 0;
    char *buf = bcl_xmalloc(interp, cap);
    size_t offset = 0;

    buf[0] = '\0';

    while (s[offset]) {
        count++;
        offset += utf8_char_bytes(s + offset);
    }

    offsets = bcl_xmalloc(interp, sizeof(size_t) * (size_t)(count + 1));
    offset = 0;
    for (int i = 0; i < count; i++) {
        offsets[i] = offset;
        offset += utf8_char_bytes(s + offset);
    }
    offsets[count] = offset;

    for (int i = count - 1; i >= 0; i--) {
        append_buf_bytes(interp, &buf, &len, &cap, s + offsets[i], offsets[i + 1] - offsets[i]);
    }

    free(offsets);
    return (bcl_val_t){buf, len};
}

static void list_init(bcl_list_t *list) {
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

static void list_push_n(bcl_interp_t *interp, bcl_list_t *list, const char *text, size_t len) {
    if (list->count == list->cap) {
        int new_cap = list->cap ? list->cap * 2 : 4;
        if (new_cap <= list->cap) {
            bcl_raise_code(interp, "BCL", 'F', "NOMEM", "List exceeds maximum size");
        }
        list->items = bcl_xrealloc(interp, list->items, sizeof(char *) * (size_t)new_cap);
        list->cap = new_cap;
    }

    list->items[list->count] = bcl_xmemdup0(interp, text, len);
    list->count++;
}

static void list_push_copy(bcl_interp_t *interp, bcl_list_t *list, const char *text) {
    list_push_n(interp, list, text, strlen(text));
}

static void list_free(bcl_list_t *list) {
    for (int i = 0; i < list->count; i++) {
        free(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

static void list_parse(bcl_interp_t *interp, const char *text, bcl_list_t *list) {
    const char *p = text;

    list_init(list);

    while (*p) {
        size_t cap = 16;
        size_t len = 0;
        char *buf;

        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (!*p) {
            break;
        }

        buf = bcl_xmalloc(interp, cap);
        buf[0] = '\0';

        if (*p == '{') {
            int closed = 0;

            p++;
            while (*p) {
                if (*p == '\\' && p[1]) {
                    p++;
                    append_buf_char(interp, &buf, &len, &cap, *p++);
                    continue;
                }
                if (*p == '}') {
                    p++;
                    closed = 1;
                    break;
                }
                append_buf_char(interp, &buf, &len, &cap, *p++);
            }
            if (!closed || (*p && !isspace((unsigned char)*p))) {
                free(buf);
                list_free(list);
                bcl_raise_code(interp, "LIST", 'F', "SYNTAX",
                               !closed ? "Unterminated braced list item" :
                               "Unexpected characters after braced list item");
            }
        } else if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) {
                    p++;
                }
                append_buf_char(interp, &buf, &len, &cap, *p);
                p++;
            }
            if (*p == '"') {
                p++;
            }
        } else {
            while (*p && !isspace((unsigned char)*p)) {
                if (*p == '\\' && p[1]) {
                    p++;
                } else if (*p == '{' || *p == '}') {
                    free(buf);
                    list_free(list);
                    bcl_raise_code(interp, "LIST", 'F', "SYNTAX",
                                   "Literal braces in list items must be escaped");
                }
                append_buf_char(interp, &buf, &len, &cap, *p);
                p++;
            }
        }

        list_push_n(interp, list, buf, len);
        free(buf);
    }
}

static int list_item_needs_braces(const char *text) {
    if (*text == '\0') {
        return 1;
    }

    while (*text) {
        if (isspace((unsigned char)*text) || *text == '{' || *text == '}' ||
            *text == '"' || *text == '\\') {
            return 1;
        }
        text++;
    }

    return 0;
}

static bcl_val_t list_to_value(bcl_interp_t *interp, const bcl_list_t *list) {
    size_t cap = 32;
    size_t len = 0;
    char *buf = bcl_xmalloc(interp, cap);

    buf[0] = '\0';

    for (int i = 0; i < list->count; i++) {
        const char *item = list->items[i];

        if (i > 0) {
            append_buf_char(interp, &buf, &len, &cap, ' ');
        }

        if (!list_item_needs_braces(item)) {
            append_buf_bytes(interp, &buf, &len, &cap, item, strlen(item));
            continue;
        }

        append_buf_char(interp, &buf, &len, &cap, '{');
        while (*item) {
            if (*item == '{' || *item == '}' || *item == '\\') {
                append_buf_char(interp, &buf, &len, &cap, '\\');
            }
            append_buf_char(interp, &buf, &len, &cap, *item);
            item++;
        }
        append_buf_char(interp, &buf, &len, &cap, '}');
    }

    return (bcl_val_t){buf, len};
}

static bcl_array_alias_t *cmd_find_array_alias(bcl_env_t *env, const char *name) {
    unsigned long hash = 5381;
    const unsigned char *p = (const unsigned char *)name;

    while (*p) {
        hash = ((hash << 5) + hash) ^ *p++;
    }

    while (env) {
        bcl_array_alias_t *alias = env->array_alias_buckets[hash % BCL_ARRAY_ALIAS_BUCKETS];
        while (alias) {
            if (strcmp(alias->name, name) == 0) {
                return alias;
            }
            alias = alias->hash_next;
        }
        env = env->parent;
    }

    return NULL;
}

static bcl_env_t *cmd_array_target_env(bcl_env_t *env, const char *name) {
    bcl_array_alias_t *alias = cmd_find_array_alias(env, name);
    return alias ? alias->target_env : env;
}

static int cmd_array_name_matches_base(const char *name, const char *base) {
    size_t base_len = strlen(base);
    size_t name_len = strlen(name);

    return name_len > base_len + 1 &&
           strncmp(name, base, base_len) == 0 &&
           name[base_len] == '(' &&
           name[name_len - 1] == ')';
}

static unsigned long cmd_array_hash(const char *base) {
    unsigned long hash = 5381;
    unsigned char ch;

    while ((ch = (unsigned char)*base++) != '\0') {
        hash = ((hash << 5) + hash) ^ ch;
    }

    return hash;
}

static bcl_array_bucket_t *cmd_array_bucket(bcl_env_t *env, const char *base) {
    bcl_array_bucket_t *bucket;

    if (!env || !base) {
        return NULL;
    }

    bucket = env->array_buckets[cmd_array_hash(base) % BCL_ARRAY_BUCKETS];
    while (bucket) {
        if (strcmp(bucket->base, base) == 0) {
            return bucket;
        }
        bucket = bucket->next;
    }

    return NULL;
}

static char *cmd_array_entry_key(bcl_interp_t *interp, const char *name, const char *base) {
    size_t base_len = strlen(base);
    size_t name_len = strlen(name);

    if (!cmd_array_name_matches_base(name, base)) {
        return NULL;
    }

    return bcl_xmemdup0(interp, name + base_len + 1, name_len - base_len - 2);
}

static int cmd_array_count(bcl_env_t *env, const char *base) {
    bcl_array_bucket_t *bucket = cmd_array_bucket(env, base);
    return bucket ? bucket->count : 0;
}

static int cmp_list_sort_entry(const void *a, const void *b) {
    const bcl_list_sort_entry_t *left = (const bcl_list_sort_entry_t *)a;
    const bcl_list_sort_entry_t *right = (const bcl_list_sort_entry_t *)b;

    if (left->is_numeric != right->is_numeric) {
        return right->is_numeric - left->is_numeric;
    }
    if (left->is_numeric) {
        if (left->numeric < right->numeric) {
            return -1;
        }
        if (left->numeric > right->numeric) {
            return 1;
        }
    }

    {
        int cmp = strcasecmp(left->text, right->text);
        if (cmp != 0) {
            return cmp;
        }
    }
    return strcmp(left->text, right->text);
}

static bcl_module_t *module_find(bcl_interp_t *interp, const char *path, bcl_module_t **prev_out) {
    bcl_module_t *mod;

    if (prev_out) {
        *prev_out = NULL;
    }

    if (interp->module_buckets) {
        bcl_module_t **buckets = (bcl_module_t **)interp->module_buckets;
        uint32_t bucket = cmd_hash(path) % BCL_MODULE_BUCKETS;
        mod = buckets[bucket];
        while (mod) {
            if (strcmp(mod->path, path) == 0) {
                return mod;
            }
            mod = mod->hash_next;
        }
        return NULL;
    }

    mod = (bcl_module_t *)interp->modules;
    while (mod) {
        if (strcmp(mod->path, path) == 0) {
            return mod;
        }
        mod = mod->next;
    }
    return NULL;
}

static bcl_module_t **module_table(bcl_interp_t *interp) {
    if (!interp->module_buckets) {
        interp->module_buckets = bcl_xcalloc(interp, BCL_MODULE_BUCKETS, sizeof(bcl_module_t *));
    }
    return (bcl_module_t **)interp->module_buckets;
}

static void module_insert(bcl_interp_t *interp, bcl_module_t *module) {
    bcl_module_t **buckets = module_table(interp);
    uint32_t bucket = cmd_hash(module->path) % BCL_MODULE_BUCKETS;

    module->next = (bcl_module_t *)interp->modules;
    interp->modules = module;
    module->hash_next = buckets[bucket];
    buckets[bucket] = module;
}

static void module_unlink_hash(bcl_interp_t *interp, bcl_module_t *module) {
    bcl_module_t **buckets;
    bcl_module_t **link;

    if (!interp->module_buckets || !module) {
        return;
    }

    buckets = (bcl_module_t **)interp->module_buckets;
    link = &buckets[cmd_hash(module->path) % BCL_MODULE_BUCKETS];
    while (*link) {
        if (*link == module) {
            *link = module->hash_next;
            module->hash_next = NULL;
            return;
        }
        link = &(*link)->hash_next;
    }
}

static bcl_module_t **module_list_link(bcl_interp_t *interp, bcl_module_t *module) {
    bcl_module_t **link = (bcl_module_t **)&interp->modules;

    while (*link) {
        if (*link == module) {
            return link;
        }
        link = &(*link)->next;
    }

    return NULL;
}

static void module_track_command(bcl_interp_t *interp, bcl_module_t *module, const char *name) {
    if (!module) {
        return;
    }

    for (int i = 0; i < module->ncmds; i++) {
        if (strcasecmp(module->cmd_names[i], name) == 0) {
            return;
        }
    }

    if (module->ncmds == module->cmd_cap) {
        int new_cap = module->cmd_cap ? module->cmd_cap * 2 : 4;
        if (new_cap <= module->cmd_cap) {
            bcl_raise_code(interp, "BCL", 'F', "NOMEM", "Module command list exceeds maximum size");
        }
        module->cmd_names = bcl_xrealloc(interp, module->cmd_names, sizeof(char *) * (size_t)new_cap);
        module->cmd_cap = new_cap;
    }

    module->cmd_names[module->ncmds] = bcl_xstrdup(interp, name);
    module->ncmds++;
}

static void unregister_module_commands_for_name(bcl_interp_t *interp, const char *name, bcl_module_t *owner, unsigned long max_load_seq, int remove_older_overrides) {
    bcl_cmdtable_t *ct = (bcl_cmdtable_t *)interp->commands;
    bcl_cmd_t *prev = NULL;
    bcl_cmd_t *cmd = ct->buckets[cmd_hash(name)];

    while (cmd) {
        bcl_cmd_t *next = cmd->next;
        int remove = 0;

        if (strcasecmp(cmd->name, name) == 0 && cmd->owner) {
            bcl_module_t *cmd_owner = (bcl_module_t *)cmd->owner;
            if (remove_older_overrides) {
                remove = cmd_owner->load_seq <= max_load_seq;
            } else {
                remove = cmd_owner == owner;
            }
        }

        if (remove) {
            if (prev) {
                prev->next = next;
            } else {
                ct->buckets[cmd_hash(name)] = next;
            }
            free(cmd->name);
            free(cmd);
        } else {
            prev = cmd;
        }

        cmd = next;
    }
}

static void free_module(bcl_module_t *module) {
    bcl_module_dependency_t *dependency;
    if (!module) {
        return;
    }

    dependency = module->dependencies;
    while (dependency) {
        bcl_module_dependency_t *next = dependency->next;
        free(dependency);
        dependency = next;
    }
    for (int i = 0; i < module->ncmds; i++) {
        free(module->cmd_names[i]);
    }
    free(module->cmd_names);
    free(module->path);
    free(module->version);
    free(module);
}

static bcl_val_t module_load_result(bcl_interp_t *interp, const bcl_module_t *module) {
    const char *version = module->version ? module->version : "unknown";
    size_t needed = snprintf(NULL, 0, "LOAD OK module=%s version=%s", module->path, version);
    char *buf = bcl_xmalloc(interp, needed + 1);
    snprintf(buf, needed + 1, "LOAD OK module=%s version=%s", module->path, version);
    return (bcl_val_t){buf, needed};
}

void *bcl_module_current_token(void) {
    return current_loading_module;
}

void *bcl_module_token_for_address(bcl_interp_t *interp, void *address) {
    Dl_info info;
    bcl_module_t *module;

    if (!address || dladdr(address, &info) == 0 || !info.dli_fbase) {
        return NULL;
    }

    module = (bcl_module_t *)interp->modules;
    while (module) {
        if (module->base_addr == info.dli_fbase) {
            return module;
        }
        module = module->next;
    }

    if (current_loading_module && current_loading_module->base_addr == info.dli_fbase) {
        return current_loading_module;
    }

    return NULL;
}

const char *bcl_module_path_from_token(void *token) {
    bcl_module_t *module = (bcl_module_t *)token;

    return module ? module->path : NULL;
}

int bcl_module_token_is_loaded(bcl_interp_t *interp, void *token) {
    bcl_module_t *module = (bcl_module_t *)interp->modules;

    while (module) {
        if (module == (bcl_module_t *)token) {
            return 1;
        }
        module = module->next;
    }
    return current_loading_module == (bcl_module_t *)token;
}

static int module_version_at_least(const char *actual, const char *minimum) {
    const char *left = actual ? actual : "";
    const char *right = minimum ? minimum : "";
    if (!*right) return 1;
    while (*left || *right) {
        unsigned long left_value = 0;
        unsigned long right_value = 0;
        while (*left && !isdigit((unsigned char)*left)) left++;
        while (*right && !isdigit((unsigned char)*right)) right++;
        while (isdigit((unsigned char)*left)) {
            left_value = left_value * 10u + (unsigned long)(*left - '0');
            left++;
        }
        while (isdigit((unsigned char)*right)) {
            right_value = right_value * 10u + (unsigned long)(*right - '0');
            right++;
        }
        if (left_value != right_value) return left_value > right_value;
        if (!*left && !*right) return 1;
    }
    return 1;
}

static int module_depends_on(bcl_module_t *module, bcl_module_t *target,
                             int depth) {
    bcl_module_dependency_t *dependency;
    if (!module || depth > 1024) return 0;
    if (module == target) return 1;
    dependency = module->dependencies;
    while (dependency) {
        if (module_depends_on(dependency->provider, target, depth + 1))
            return 1;
        dependency = dependency->next;
    }
    return 0;
}

int bcl_module_dependency_acquire(bcl_interp_t *interp, void *provider_token,
                                  const char *minimum_version) {
    bcl_module_t *consumer = current_loading_module;
    bcl_module_t *provider = (bcl_module_t *)provider_token;
    bcl_module_dependency_t *dependency;
    if (!interp || !consumer || !provider || consumer == provider)
        return BCL_MODULE_DEP_INVALID;
    if (!bcl_module_token_is_loaded(interp, provider))
        return BCL_MODULE_DEP_NOT_LOADED;
    if (!module_version_at_least(provider->version, minimum_version))
        return BCL_MODULE_DEP_VERSION;
    dependency = consumer->dependencies;
    while (dependency) {
        if (dependency->provider == provider)
            return BCL_MODULE_DEP_DUPLICATE;
        dependency = dependency->next;
    }
    if (module_depends_on(provider, consumer, 0))
        return BCL_MODULE_DEP_CYCLE;
    dependency = (bcl_module_dependency_t *)calloc(1, sizeof(*dependency));
    if (!dependency) return BCL_MODULE_DEP_NOMEM;
    dependency->provider = provider;
    dependency->next = consumer->dependencies;
    consumer->dependencies = dependency;
    return BCL_MODULE_DEP_OK;
}

int bcl_module_dependency_release(bcl_interp_t *interp, void *provider_token) {
    bcl_module_t *consumer = current_loading_module;
    bcl_module_dependency_t **link;
    (void)interp;
    if (!consumer || !provider_token) return BCL_MODULE_DEP_INVALID;
    link = &consumer->dependencies;
    while (*link) {
        if ((*link)->provider == (bcl_module_t *)provider_token) {
            bcl_module_dependency_t *removed = *link;
            *link = removed->next;
            free(removed);
            return BCL_MODULE_DEP_OK;
        }
        link = &(*link)->next;
    }
    return BCL_MODULE_DEP_NOT_LOADED;
}

int bcl_module_dependency_count(bcl_interp_t *interp, void *module_token) {
    bcl_module_t *module = (bcl_module_t *)module_token;
    bcl_module_dependency_t *dependency;
    int count = 0;
    if (!interp || !bcl_module_token_is_loaded(interp, module)) return -1;
    dependency = module->dependencies;
    while (dependency) {
        count++;
        dependency = dependency->next;
    }
    return count;
}

int bcl_module_dependent_count(bcl_interp_t *interp, void *module_token) {
    bcl_module_t *module;
    int count = 0;
    if (!interp || !bcl_module_token_is_loaded(interp, module_token)) return -1;
    module = (bcl_module_t *)interp->modules;
    while (module) {
        bcl_module_dependency_t *dependency = module->dependencies;
        while (dependency) {
            if (dependency->provider == (bcl_module_t *)module_token) {
                count++;
                break;
            }
            dependency = dependency->next;
        }
        module = module->next;
    }
    if (current_loading_module &&
        current_loading_module != (bcl_module_t *)module_token) {
        bcl_module_dependency_t *dependency = current_loading_module->dependencies;
        while (dependency) {
            if (dependency->provider == (bcl_module_t *)module_token) {
                count++;
                break;
            }
            dependency = dependency->next;
        }
    }
    return count;
}

int bcl_module_unload_guard_register(bcl_interp_t *interp,
                                     bcl_module_guard_cb_t callback,
                                     void *userdata) {
    bcl_module_guard_t *guard;
    bcl_module_guard_t *item;
    if (!interp || !current_loading_module || !callback) return -1;
    item = (bcl_module_guard_t *)interp->module_guards;
    while (item) {
        if (item->owner == current_loading_module &&
            item->callback == callback && item->userdata == userdata)
            return 0;
        item = item->next;
    }
    guard = (bcl_module_guard_t *)calloc(1, sizeof(*guard));
    if (!guard) return -1;
    guard->owner = current_loading_module;
    guard->callback = callback;
    guard->userdata = userdata;
    guard->next = (bcl_module_guard_t *)interp->module_guards;
    interp->module_guards = guard;
    return 0;
}

void bcl_module_unload_guard_unregister(bcl_interp_t *interp,
                                        bcl_module_guard_cb_t callback,
                                        void *userdata) {
    bcl_module_guard_t **link;
    if (!interp || !callback) return;
    link = (bcl_module_guard_t **)&interp->module_guards;
    while (*link) {
        if ((*link)->callback == callback && (*link)->userdata == userdata) {
            bcl_module_guard_t *removed = *link;
            *link = removed->next;
            free(removed);
            continue;
        }
        link = &(*link)->next;
    }
}

static void module_remove_owned_guards(bcl_interp_t *interp,
                                       bcl_module_t *owner) {
    bcl_module_guard_t **link = (bcl_module_guard_t **)&interp->module_guards;
    while (*link) {
        if ((*link)->owner == owner) {
            bcl_module_guard_t *removed = *link;
            *link = removed->next;
            free(removed);
        } else {
            link = &(*link)->next;
        }
    }
}

static void unload_module_internal(bcl_interp_t *interp, bcl_module_t *module, bcl_module_t *prev) {
    bcl_module_unload_guard_fn guard =
        (bcl_module_unload_guard_fn)dlsym(RTLD_DEFAULT, "linux_evtmod_before_module_unload");
    bcl_module_unload_notify_fn after =
        (bcl_module_unload_notify_fn)dlsym(RTLD_DEFAULT, "linux_evtmod_after_module_unload");
    bcl_module_runtime_prepare_fn runtime_prepare =
        (bcl_module_runtime_prepare_fn)dlsym(RTLD_DEFAULT, "linux_evtmod_prepare_runtime_module_unload");

    if (!runtime_cleanup_active && module->active_calls > 0) {
        bcl_raise_code(interp, "LOAD", 'F', "BUSY", "Module is executing: %s", module->path);
    }

    if (!runtime_cleanup_active && bcl_module_dependent_count(interp, module) > 0) {
        bcl_raise_code(interp, "LOAD", 'F', "DEPENDENCY",
                       "Module has loaded consumers: %s", module->path);
    }

    if (!runtime_cleanup_active) {
        bcl_module_guard_t *item = (bcl_module_guard_t *)interp->module_guards;
        while (item) {
            if (!item->callback(interp, module, module->path, item->userdata)) {
                bcl_raise_code(interp, "LOAD", 'F', "BUSY",
                               "Module unload rejected by lifecycle guard: %s",
                               module->path);
            }
            item = item->next;
        }
    }

    if (!runtime_cleanup_active && guard && !guard(interp, module, module->path)) {
        return;
    }

    if (runtime_cleanup_active && runtime_prepare) {
        runtime_prepare(interp, module, module->path);
    }

    if (module->cleanup) {
        bcl_module_t *previous_loading = current_loading_module;
        current_loading_module = module;
        module->cleanup(interp);
        current_loading_module = previous_loading;
    }

    if (!runtime_cleanup_active && after) {
        after(interp, module, module->path);
    }

    for (int i = 0; i < module->ncmds; i++) {
        bcl_cmd_t *active = bcl_cmd_lookup(interp, module->cmd_names[i]);
        if (active && active->owner == module) {
            unregister_module_commands_for_name(interp, module->cmd_names[i], NULL, module->load_seq, 1);
        } else {
            unregister_module_commands_for_name(interp, module->cmd_names[i], module, 0, 0);
        }
    }

    if (prev) {
        prev->next = module->next;
    } else {
        bcl_module_t **link = module_list_link(interp, module);
        if (link) {
            *link = module->next;
        }
    }
    module_unlink_hash(interp, module);
    module_remove_owned_guards(interp, module);

    dlclose(module->handle);
    free_module(module);
}

static void rollback_module_init(bcl_interp_t *interp, bcl_module_t *module) {
    int i;
    if (!module) return;
    if (module->cleanup) module->cleanup(interp);
    for (i = 0; i < module->ncmds; i++)
        unregister_module_commands_for_name(interp, module->cmd_names[i],
                                            module, 0, 0);
    module_remove_owned_guards(interp, module);
}

typedef struct {
    int conversions;
    char spec;
    size_t conv_start;
    size_t conv_end;
} bcl_format_info_t;

#define BCL_STRFMT_MAX_WIDTH 4096
#define BCL_STRFMT_MAX_PRECISION 4096

static int is_format_flag(char ch) {
    return ch == '-' || ch == '+' || ch == ' ' || ch == '#' || ch == '0';
}

static int is_supported_format_spec(char ch) {
    return strchr("sdiuoxXfeEgGc", ch) != NULL;
}

static long parse_format_bound(bcl_interp_t *interp, const char *fmt, size_t *pos, long max, const char *field) {
    long value = 0;
    int saw_digit = 0;

    while (fmt[*pos] && isdigit((unsigned char)fmt[*pos])) {
        saw_digit = 1;
        value = value * 10 + (fmt[*pos] - '0');
        if (value > max) {
            bcl_raise_code(interp, "STRFMT", 'F', "BADVALUE", "%s exceeds limit", field);
        }
        (*pos)++;
    }

    return saw_digit ? value : -1;
}

static bcl_format_info_t parse_format_info(bcl_interp_t *interp, const char *fmt) {
    bcl_format_info_t info = {0, '\0', 0, 0};

    for (size_t i = 0; fmt[i]; i++) {
        if (fmt[i] != '%') {
            continue;
        }
        if (fmt[i + 1] == '%') {
            i++;
            continue;
        }

        size_t j = i + 1;
        while (fmt[j] && is_format_flag(fmt[j])) {
            j++;
        }
        parse_format_bound(interp, fmt, &j, BCL_STRFMT_MAX_WIDTH, "field width");
        if (fmt[j] == '.') {
            j++;
            parse_format_bound(interp, fmt, &j, BCL_STRFMT_MAX_PRECISION, "precision");
        }
        if (!fmt[j]) {
            bcl_raise_code(interp, "STRFMT", 'F', "BADARGS", "Incomplete format specifier");
        }
        if (fmt[j] == '*' || strchr("hlLzjt", fmt[j]) != NULL) {
            bcl_raise_code(interp, "STRFMT", 'F', "UNSUPPORTED", "Unsupported format modifier");
        }
        if (!is_supported_format_spec(fmt[j])) {
            bcl_raise_code(interp, "STRFMT", 'F', "UNSUPPORTED", "Unsupported conversion specifier '%c'", fmt[j]);
        }
        info.conversions++;
        if (info.conversions > 1) {
            bcl_raise_code(interp, "STRFMT", 'F', "BADARGS", "Format must contain exactly one conversion specifier");
        }
        info.spec = fmt[j];
        info.conv_start = i;
        info.conv_end = j + 1;
        i = j;
    }

    return info;
}

static void strfmt_append(bcl_interp_t *interp, char **buf, size_t *len, size_t *cap, const char *data, size_t data_len) {
    char *tmp;

    if (data_len == 0) {
        return;
    }

    if (data_len > (size_t)-1 - *len - 1) {
        bcl_raise_code(interp, "STRFMT", 'F', "BADVALUE", "Output too large");
    }

    while (*len + data_len + 1 > *cap) {
        if (*cap > ((size_t)-1) / 2) {
            bcl_raise_code(interp, "STRFMT", 'F', "BADVALUE", "Output too large");
        }
        *cap *= 2;
    }

    tmp = bcl_xrealloc(interp, *buf, *cap);
    *buf = tmp;

    memcpy(*buf + *len, data, data_len);
    *len += data_len;
    (*buf)[*len] = '\0';
}

static void strfmt_append_literal(bcl_interp_t *interp, char **buf, size_t *len, size_t *cap, const char *fmt, size_t start, size_t end) {
    size_t segment_start = start;

    for (size_t i = start; i < end; i++) {
        if (fmt[i] != '%') {
            continue;
        }
        if (i + 1 < end && fmt[i + 1] == '%') {
            strfmt_append(interp, buf, len, cap, fmt + segment_start, i - segment_start);
            strfmt_append(interp, buf, len, cap, "%", 1);
            i++;
            segment_start = i + 1;
            continue;
        }
        bcl_raise_code(interp, "STRFMT", 'F', "BADARGS", "Invalid literal percent");
    }

    strfmt_append(interp, buf, len, cap, fmt + segment_start, end - segment_start);
}

static char *strfmt_render_value(bcl_interp_t *interp, const char *conv_fmt, char spec, const char *value, size_t *out_len) {
    int needed;
    char *buf;

    switch (spec) {
        case 'd':
        case 'i':
            needed = snprintf(NULL, 0, conv_fmt, (int)strtol(value, NULL, 0));
            break;
        case 'u':
        case 'o':
        case 'x':
        case 'X':
            needed = snprintf(NULL, 0, conv_fmt, (unsigned int)strtoul(value, NULL, 0));
            break;
        case 'f':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
            needed = snprintf(NULL, 0, conv_fmt, strtod(value, NULL));
            break;
        case 'c':
            needed = snprintf(NULL, 0, conv_fmt, (int)strtol(value, NULL, 0));
            break;
        case 's':
            needed = snprintf(NULL, 0, conv_fmt, value);
            break;
        default:
            bcl_raise_code(interp, "STRFMT", 'F', "UNSUPPORTED", "Unsupported conversion specifier '%c'", spec);
    }

    if (needed < 0) {
        bcl_raise_code(interp, "STRFMT", 'F', "ERROR", "Formatting failed");
    }

    buf = bcl_xmalloc(interp, (size_t)needed + 1);

    switch (spec) {
        case 'd':
        case 'i':
            snprintf(buf, (size_t)needed + 1, conv_fmt, (int)strtol(value, NULL, 0));
            break;
        case 'u':
        case 'o':
        case 'x':
        case 'X':
            snprintf(buf, (size_t)needed + 1, conv_fmt, (unsigned int)strtoul(value, NULL, 0));
            break;
        case 'f':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
            snprintf(buf, (size_t)needed + 1, conv_fmt, strtod(value, NULL));
            break;
        case 'c':
            snprintf(buf, (size_t)needed + 1, conv_fmt, (int)strtol(value, NULL, 0));
            break;
        case 's':
            snprintf(buf, (size_t)needed + 1, conv_fmt, value);
            break;
    }

    *out_len = (size_t)needed;
    return buf;
}

void bcl_cmd_register(bcl_interp_t *interp, const char *name, bcl_cmdfn_t fn, int argc) {
    bcl_cmdtable_t *ct = (bcl_cmdtable_t *)interp->commands;
    bcl_cmd_t *cmd = bcl_xcalloc(interp, 1, sizeof(bcl_cmd_t));

    cmd->name = bcl_xstrdup(interp, name);
    cmd->fn = fn;
    cmd->argc = argc;
    cmd->owner = current_loading_module;

    uint32_t h = cmd_hash(name);
    cmd->next = ct->buckets[h];
    ct->buckets[h] = cmd;
    module_track_command(interp, current_loading_module, name);
}

void bcl_register_cmd(bcl_interp_t *interp, const char *name, bcl_cmdfn_t fn, int argc) {
    bcl_cmd_register(interp, name, fn, argc);
}

bcl_cmd_t *bcl_cmd_lookup(bcl_interp_t *interp, const char *name) {
    bcl_cmdtable_t *ct = (bcl_cmdtable_t *)interp->commands;
    bcl_cmd_t *cmd = ct->buckets[cmd_hash(name)];

    while (cmd) {
        if (strcasecmp(cmd->name, name) == 0) {
            return cmd;
        }
        cmd = cmd->next;
    }

    return NULL;
}

bcl_val_t bcl_dispatch(bcl_interp_t *interp, bcl_env_t *env, const char *name, int argc, bcl_val_t *argv) {
    bcl_cmd_t *cmd = bcl_cmd_lookup(interp, name);
    bcl_module_t *owner;
    bcl_try_frame_t frame;
    bcl_val_t value;

    if (!cmd) {
        bcl_raise_code(interp, "EVAL", 'F', "UNKCMD", "Unknown command: %s", name);
    }

    if (cmd->argc >= 0 && argc != cmd->argc) {
        bcl_raise_code(interp, "EVAL", 'F', "BADARGS", "%s: expected %d arguments, got %d", name, cmd->argc, argc);
    }

    owner = (bcl_module_t *)cmd->owner;
    if (!owner) {
        return cmd->fn(interp, env, argc, argv);
    }

    owner->active_calls++;
    frame.prev = interp->try_stack;
    interp->try_stack = &frame;
    if (setjmp(frame.env) != 0) {
        interp->try_stack = frame.prev;
        owner->active_calls--;
        if (frame.prev) {
            longjmp(frame.prev->env, 1);
        }
        longjmp(interp->error_jmp, 1);
    }

    value = cmd->fn(interp, env, argc, argv);
    interp->try_stack = frame.prev;
    owner->active_calls--;
    return value;
}

bcl_val_t bcl_cmd_set(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    if (argc < 2) {
        bcl_raise_code(interp, "SET", 'F', "BADARGS", "Expected at least 2 arguments");
    }

    bcl_val_t joined = argc == 2 ? bcl_val_make(argv[1].data, argv[1].len)
                                 : join_args(interp, argc - 1, argv + 1, " ");
    bcl_env_set(interp, env, argv[0].data, joined.data);
    bcl_val_free(&joined);
    return bcl_val_empty();
}

bcl_val_t bcl_cmd_puts(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    (void)interp;
    (void)env;

    bcl_val_t value = join_args(interp, argc, argv, " ");
    printf("%s\n", value.data);
    bcl_val_free(&value);
    return bcl_val_empty();
}

bcl_val_t bcl_cmd_putsn(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    (void)interp;
    (void)env;

    bcl_val_t value = join_args(interp, argc, argv, " ");
    printf("%s", value.data);
    bcl_val_free(&value);
    return bcl_val_empty();
}

bcl_val_t bcl_cmd_gets(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    size_t cap = 256;
    size_t len = 0;
    char *buf = bcl_xmalloc(interp, cap);
    int ch;
    bcl_val_t value;
    (void)env;
    (void)argc;
    (void)argv;

    while ((ch = fgetc(stdin)) != EOF) {
        if (ch == '\n') {
            break;
        }
        if (len + 1 >= cap) {
            if (cap > (size_t)-1 / 2) {
                free(buf);
                bcl_raise_code(interp, "BCL", 'F', "NOMEM", "GETS input line is too large");
            }
            cap *= 2;
            buf = bcl_xrealloc(interp, buf, cap);
        }
        buf[len++] = (char)ch;
    }

    if (ch == EOF && len == 0) {
        free(buf);
        return bcl_val_empty();
    }

    buf[len] = '\0';
    value = bcl_val_make(buf, len);
    free(buf);
    return value;
}

bcl_val_t bcl_cmd_calc(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    bcl_val_t expr;
    char *text;

    expr = argc == 1 ? bcl_val_make(argv[0].data, argv[0].len) : join_args(interp, argc, argv, " ");
    text = bcl_calc_eval(interp, expr.data, env);

    bcl_val_t result = bcl_val_make(text, strlen(text));
    free(text);
    bcl_val_free(&expr);
    return result;
}

/* Delta numerico estricto para INCR: acepta enteros, decimales y signo. */
static double incr_delta(bcl_interp_t *interp, const char *cmd, const char *text) {
    char *end;
    double delta;

    errno = 0;
    delta = strtod(text, &end);
    if (end == text || *end != '\0' || errno == ERANGE || delta != delta) {
        bcl_raise_code(interp, cmd, 'F', "BADVALUE", "Increment must be a number: %s", text);
    }
    return delta;
}

static bcl_val_t incr_apply(bcl_interp_t *interp, bcl_env_t *env, const char *name, double delta) {
    bcl_val_t value = bcl_env_get(env, name);
    double number = value.len ? strtod(value.data, NULL) : 0.0;
    char *text;

    bcl_val_free(&value);
    text = bcl_calc_result(interp, number + delta);
    bcl_env_set(interp, env, name, text);
    free(text);
    return bcl_val_empty();
}

/* INCR var [delta]: por omision suma 1; el delta puede ser negativo o decimal. */
bcl_val_t bcl_cmd_incr(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    double delta = 1.0;

    if (argc < 1 || argc > 2) {
        bcl_raise_code(interp, "INCR", 'F', "BADARGS", "Expected 1 or 2 arguments");
    }
    if (argc == 2) {
        delta = incr_delta(interp, "INCR", argv[1].data);
    }
    return incr_apply(interp, env, argv[0].data, delta);
}

bcl_val_t bcl_cmd_incrv(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    if (argc != 2) {
        bcl_raise_code(interp, "INCRV", 'F', "BADARGS", "Expected 2 arguments");
    }
    /* INCRV conserva su comportamiento historico: un delta no numerico suma 0. */
    return incr_apply(interp, env, argv[0].data, strtod(argv[1].data, NULL));
}

bcl_val_t bcl_cmd_exists(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    (void)interp;

    if (argc != 1) {
        return bcl_val_empty();
    }

    return bcl_val_make(bcl_env_exists(env, argv[0].data) ? "1" : "0", 1);
}

bcl_val_t bcl_cmd_arrexists(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    bcl_env_t *target_env;
    (void)interp;

    if (argc != 1) {
        return bcl_val_make("0", 1);
    }

    target_env = cmd_array_target_env(env, argv[0].data);
    return bcl_val_make(cmd_array_count(target_env, argv[0].data) ? "1" : "0", 1);
}

bcl_val_t bcl_cmd_arrleng(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    char buf[32];
    bcl_env_t *target_env;
    (void)interp;

    if (argc != 1) {
        return bcl_val_make("0", 1);
    }

    target_env = cmd_array_target_env(env, argv[0].data);
    snprintf(buf, sizeof(buf), "%d", cmd_array_count(target_env, argv[0].data));
    return bcl_val_make(buf, strlen(buf));
}

bcl_val_t bcl_cmd_arrkeys(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    bcl_list_t out;
    bcl_env_t *target_env;
    bcl_array_bucket_t *bucket;
    bcl_var_t *var;
    bcl_val_t value;
    (void)interp;

    if (argc != 1) {
        return bcl_val_empty();
    }

    target_env = cmd_array_target_env(env, argv[0].data);
    bucket = cmd_array_bucket(target_env, argv[0].data);
    list_init(&out);
    for (var = bucket ? bucket->vars : NULL; var; var = var->array_next) {
        char *key = cmd_array_entry_key(interp, var->name, argv[0].data);
        if (!key) {
            continue;
        }
        list_push_copy(interp, &out, key);
        free(key);
    }

    value = list_to_value(interp, &out);
    list_free(&out);
    return value;
}

bcl_val_t bcl_cmd_arrget(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    bcl_list_t out;
    bcl_env_t *target_env;
    bcl_array_bucket_t *bucket;
    bcl_var_t *var;
    bcl_val_t value;

    if (argc != 1) {
        return bcl_val_empty();
    }

    target_env = cmd_array_target_env(env, argv[0].data);
    bucket = cmd_array_bucket(target_env, argv[0].data);
    list_init(&out);
    for (var = bucket ? bucket->vars : NULL; var; var = var->array_next) {
        char *key = cmd_array_entry_key(interp, var->name, argv[0].data);
        if (!key) {
            continue;
        }
        list_push_copy(interp, &out, key);
        list_push_copy(interp, &out, var->value.data);
        free(key);
    }

    value = list_to_value(interp, &out);
    list_free(&out);
    return value;
}

bcl_val_t bcl_cmd_arrset(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    bcl_list_t pairs;
    bcl_env_t *target_env;

    if (argc != 2) {
        return bcl_val_empty();
    }

    list_parse(interp, argv[1].data, &pairs);
    if ((pairs.count % 2) != 0) {
        list_free(&pairs);
        bcl_raise_code(interp, "ARRSET", 'F', "BADARGS", "List must contain key/value pairs");
    }

    target_env = cmd_array_target_env(env, argv[0].data);
    for (int i = 0; i < pairs.count; i += 2) {
        size_t full_len = strlen(argv[0].data) + strlen(pairs.items[i]) + 3;
        char *full = bcl_xmalloc(interp, full_len);
        snprintf(full, full_len, "%s(%s)", argv[0].data, pairs.items[i]);
        bcl_env_set(interp, target_env, full, pairs.items[i + 1]);
        free(full);
    }

    list_free(&pairs);
    return bcl_val_empty();
}

bcl_val_t bcl_cmd_arrdel(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    size_t full_len;
    char *full;

    if (argc != 2) {
        return bcl_val_empty();
    }

    full_len = strlen(argv[0].data) + strlen(argv[1].data) + 3;
    full = bcl_xmalloc(interp, full_len);
    snprintf(full, full_len, "%s(%s)", argv[0].data, argv[1].data);
    bcl_env_unset(interp, env, full);
    free(full);
    return bcl_val_empty();
}

bcl_val_t bcl_cmd_arrclear(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    if (argc != 1) {
        return bcl_val_empty();
    }

    bcl_env_unset(interp, env, argv[0].data);
    return bcl_val_empty();
}

bcl_val_t bcl_cmd_strleng(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    char buf[32];
    (void)interp;
    (void)env;

    snprintf(buf, sizeof(buf), "%d", argc > 0 ? utf8_char_count(argv[0].data) : 0);
    return bcl_val_make(buf, strlen(buf));
}

bcl_val_t bcl_cmd_strget(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    (void)interp;
    (void)env;

    if (argc != 2) {
        return bcl_val_empty();
    }

    return utf8_make_char_at(argv[0].data, atoi(argv[1].data));
}

bcl_val_t bcl_cmd_strrang(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    (void)interp;
    (void)env;

    if (argc != 3) {
        return bcl_val_empty();
    }

    return utf8_make_range(argv[0].data, atoi(argv[1].data), atoi(argv[2].data));
}

bcl_val_t bcl_cmd_streq(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    int equal;
    (void)interp;
    (void)env;

    equal = argc >= 2 && strcmp(argv[0].data, argv[1].data) == 0;
    return bcl_val_make(equal ? "1" : "0", 1);
}

bcl_val_t bcl_cmd_strapp(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    (void)interp;
    (void)env;

    if (argc != 2) {
        return bcl_val_empty();
    }

    return join_args(interp, argc, argv, "");
}

bcl_val_t bcl_cmd_strins(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    (void)interp;
    (void)env;

    if (argc != 3) {
        return bcl_val_empty();
    }

    return utf8_insert_text(interp, argv[0].data, argv[1].data, atoi(argv[2].data));
}

bcl_val_t bcl_cmd_strdel(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    (void)interp;
    (void)env;

    if (argc != 3) {
        return bcl_val_empty();
    }

    return utf8_delete_range(interp, argv[0].data, atoi(argv[1].data), atoi(argv[2].data));
}

bcl_val_t bcl_cmd_strupp(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    char *buf;
    size_t len;
    (void)interp;
    (void)env;

    if (argc != 1) {
        return bcl_val_empty();
    }

    len = strlen(argv[0].data);
    buf = bcl_xmalloc(interp, len + 1);
    for (size_t i = 0; i < len; i++) {
        buf[i] = (char)toupper((unsigned char)argv[0].data[i]);
    }
    buf[len] = '\0';
    return (bcl_val_t){buf, len};
}

bcl_val_t bcl_cmd_strlow(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    char *buf;
    size_t len;
    (void)env;

    if (argc != 1) {
        return bcl_val_empty();
    }

    len = strlen(argv[0].data);
    buf = bcl_xmalloc(interp, len + 1);
    for (size_t i = 0; i < len; i++) {
        buf[i] = (char)tolower((unsigned char)argv[0].data[i]);
    }
    buf[len] = '\0';
    return (bcl_val_t){buf, len};
}

bcl_val_t bcl_cmd_strrev(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    (void)interp;
    (void)env;

    if (argc != 1) {
        return bcl_val_empty();
    }

    return utf8_reverse_text(interp, argv[0].data);
}

bcl_val_t bcl_cmd_strfmt(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    bcl_format_info_t fmt;
    char *out;
    size_t out_len = 0;
    size_t out_cap = 64;
    (void)env;

    if (argc < 1 || argc > 2) {
        bcl_raise_code(interp, "STRFMT", 'F', "BADARGS", "Expected 1 or 2 arguments, got %d", argc);
    }

    fmt = parse_format_info(interp, argv[0].data);

    if (fmt.conversions == 0) {
        if (argc != 1) {
            bcl_raise_code(interp, "STRFMT", 'F', "BADARGS", "Literal format does not take a value");
        }
        out = bcl_xcalloc(interp, 1, out_cap);
        strfmt_append_literal(interp, &out, &out_len, &out_cap, argv[0].data, 0, argv[0].len);
        return (bcl_val_t){out, out_len};
    }

    if (argc != 2) {
        bcl_raise_code(interp, "STRFMT", 'F', "BADARGS", "Format with conversion requires exactly 1 value");
    }

    out = bcl_xcalloc(interp, 1, out_cap);

    strfmt_append_literal(interp, &out, &out_len, &out_cap, argv[0].data, 0, fmt.conv_start);
    {
        char conv_fmt[64];
        char *rendered;
        size_t rendered_len;
        size_t conv_len = fmt.conv_end - fmt.conv_start;

        if (conv_len >= sizeof(conv_fmt)) {
            free(out);
            bcl_raise_code(interp, "STRFMT", 'F', "BADVALUE", "Conversion too long");
        }
        memcpy(conv_fmt, argv[0].data + fmt.conv_start, conv_len);
        conv_fmt[conv_len] = '\0';
        rendered = strfmt_render_value(interp, conv_fmt, fmt.spec, argv[1].data, &rendered_len);
        strfmt_append(interp, &out, &out_len, &out_cap, rendered, rendered_len);
        free(rendered);
    }
    strfmt_append_literal(interp, &out, &out_len, &out_cap, argv[0].data, fmt.conv_end, argv[0].len);

    return (bcl_val_t){out, out_len};
}

bcl_val_t bcl_cmd_strfind(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    bcl_list_t positions;
    char pos_buf[32];
    const char *haystack;
    const char *needle;
    size_t needle_len;
    int pos = 1;
    (void)interp;
    (void)env;

    if (argc != 2 || argv[1].data[0] == '\0') {
        return bcl_val_make("0", 1);
    }

    list_init(&positions);
    haystack = argv[0].data;
    needle = argv[1].data;
    needle_len = strlen(needle);

    while (*haystack) {
        size_t char_len = utf8_char_bytes(haystack);

        if (strncmp(haystack, needle, needle_len) == 0) {
            snprintf(pos_buf, sizeof(pos_buf), "%d", pos);
            list_push_copy(interp, &positions, pos_buf);
        }

        haystack += char_len;
        pos++;
    }

    if (positions.count == 0) {
        return bcl_val_make("0", 1);
    }

    bcl_val_t value = list_to_value(interp, &positions);
    list_free(&positions);
    return value;
}

bcl_val_t bcl_cmd_lstleng(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    bcl_list_t list;
    char buf[32];
    (void)interp;
    (void)env;

    list_parse(interp, argc > 0 ? argv[0].data : "", &list);
    snprintf(buf, sizeof(buf), "%d", list.count);
    list_free(&list);
    return bcl_val_make(buf, strlen(buf));
}

bcl_val_t bcl_cmd_lstget(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    bcl_list_t list;
    int pos;
    bcl_val_t value;
    (void)interp;
    (void)env;

    if (argc != 2) {
        return bcl_val_empty();
    }

    list_parse(interp, argv[0].data, &list);
    pos = atoi(argv[1].data);
    if (pos < 1 || pos > list.count) {
        list_free(&list);
        return bcl_val_empty();
    }

    value = bcl_val_make(list.items[pos - 1], strlen(list.items[pos - 1]));
    list_free(&list);
    return value;
}

bcl_val_t bcl_cmd_lstrang(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    bcl_list_t list;
    bcl_list_t out;
    int start;
    int end;
    bcl_val_t value;
    (void)interp;
    (void)env;

    if (argc != 3) {
        return bcl_val_empty();
    }

    list_parse(interp, argv[0].data, &list);
    list_init(&out);

    start = atoi(argv[1].data);
    end = atoi(argv[2].data);
    if (start < 1) {
        start = 1;
    }
    if (end > list.count) {
        end = list.count;
    }

    for (int i = start; i <= end; i++) {
        list_push_copy(interp, &out, list.items[i - 1]);
    }

    value = list_to_value(interp, &out);
    list_free(&out);
    list_free(&list);
    return value;
}

bcl_val_t bcl_cmd_lstfind(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    bcl_list_t list;
    bcl_list_t out;
    char pos_buf[32];
    bcl_val_t value;
    (void)interp;
    (void)env;

    if (argc != 2) {
        return bcl_val_make("0", 1);
    }

    list_parse(interp, argv[0].data, &list);
    list_init(&out);

    for (int i = 0; i < list.count; i++) {
        if (strcmp(list.items[i], argv[1].data) == 0) {
            snprintf(pos_buf, sizeof(pos_buf), "%d", i + 1);
            list_push_copy(interp, &out, pos_buf);
        }
    }

    if (out.count == 0) {
        list_free(&out);
        list_free(&list);
        return bcl_val_make("0", 1);
    }

    value = list_to_value(interp, &out);
    list_free(&out);
    list_free(&list);
    return value;
}

bcl_val_t bcl_cmd_lstapp(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    bcl_list_t left;
    bcl_list_t right;
    bcl_list_t out;
    bcl_val_t value;
    (void)interp;
    (void)env;

    if (argc != 2) {
        return bcl_val_empty();
    }

    list_parse(interp, argv[0].data, &left);
    list_parse(interp, argv[1].data, &right);
    list_init(&out);

    for (int i = 0; i < left.count; i++) {
        list_push_copy(interp, &out, left.items[i]);
    }
    for (int i = 0; i < right.count; i++) {
        list_push_copy(interp, &out, right.items[i]);
    }

    value = list_to_value(interp, &out);
    list_free(&out);
    list_free(&left);
    list_free(&right);
    return value;
}

bcl_val_t bcl_cmd_lstins(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    bcl_list_t left;
    bcl_list_t right;
    bcl_list_t out;
    int pos;
    bcl_val_t value;
    (void)interp;
    (void)env;

    if (argc != 3) {
        return bcl_val_empty();
    }

    list_parse(interp, argv[0].data, &left);
    list_parse(interp, argv[1].data, &right);
    list_init(&out);

    pos = atoi(argv[2].data);
    if (pos < 1) {
        pos = 1;
    }
    if (pos > left.count + 1) {
        pos = left.count + 1;
    }

    for (int i = 0; i < pos - 1; i++) {
        list_push_copy(interp, &out, left.items[i]);
    }
    for (int i = 0; i < right.count; i++) {
        list_push_copy(interp, &out, right.items[i]);
    }
    for (int i = pos - 1; i < left.count; i++) {
        list_push_copy(interp, &out, left.items[i]);
    }

    value = list_to_value(interp, &out);
    list_free(&out);
    list_free(&left);
    list_free(&right);
    return value;
}

bcl_val_t bcl_cmd_lstdel(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    bcl_list_t list;
    bcl_list_t out;
    int pos;
    int num;
    bcl_val_t value;
    (void)interp;
    (void)env;

    if (argc != 3) {
        return bcl_val_empty();
    }

    list_parse(interp, argv[0].data, &list);
    list_init(&out);

    pos = atoi(argv[1].data);
    num = atoi(argv[2].data);
    if (pos < 1) {
        pos = 1;
    }
    if (num < 0) {
        num = 0;
    }

    for (int i = 0; i < list.count; i++) {
        if (i < pos - 1 || i >= pos - 1 + num) {
            list_push_copy(interp, &out, list.items[i]);
        }
    }

    value = list_to_value(interp, &out);
    list_free(&out);
    list_free(&list);
    return value;
}

bcl_val_t bcl_cmd_lstsort(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    bcl_list_t list;
    bcl_list_sort_entry_t *items;
    bcl_list_t out;
    bcl_val_t value;
    (void)interp;
    (void)env;

    if (argc != 1) {
        return bcl_val_empty();
    }

    list_parse(interp, argv[0].data, &list);
    items = list.count ? bcl_xcalloc(interp, (size_t)list.count, sizeof(bcl_list_sort_entry_t)) : NULL;
    for (int i = 0; i < list.count; i++) {
        items[i].text = list.items[i];
        items[i].is_numeric = bcl_calc_is_numeric(list.items[i], &items[i].numeric);
    }
    qsort(items, (size_t)list.count, sizeof(bcl_list_sort_entry_t), cmp_list_sort_entry);

    list_init(&out);
    for (int i = 0; i < list.count; i++) {
        list_push_copy(interp, &out, items[i].text);
    }

    value = list_to_value(interp, &out);
    free(items);
    list_free(&out);
    list_free(&list);
    return value;
}

bcl_val_t bcl_cmd_lstfmt(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    bcl_list_t out;
    bcl_val_t value;
    const char *text;
    const char *sep;
    size_t sep_len;
    (void)interp;
    (void)env;

    if (argc != 2) {
        return bcl_val_empty();
    }

    list_init(&out);
    text = argv[0].data;
    sep = argv[1].data;
    sep_len = strlen(sep);

    if (sep_len == 0) {
        list_push_copy(interp, &out, text);
        value = list_to_value(interp, &out);
        list_free(&out);
        return value;
    }

    while (1) {
        const char *next = strstr(text, sep);
        if (!next) {
            list_push_n(interp, &out, text, strlen(text));
            break;
        }
        list_push_n(interp, &out, text, (size_t)(next - text));
        text = next + sep_len;
    }

    value = list_to_value(interp, &out);
    list_free(&out);
    return value;
}

bcl_val_t bcl_cmd_exit(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    (void)env;

    interp->exit_code = argc > 0 ? atoi(argv[0].data) : 0;
    interp->running = 0;
    return bcl_val_empty();
}

bcl_val_t bcl_cmd_unset(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    if (argc != 1) {
        bcl_raise_code(interp, "UNSET", 'F', "BADARGS", "Expected 1 argument");
    }

    bcl_env_unset(interp, env, argv[0].data);
    return bcl_val_empty();
}

bcl_val_t bcl_cmd_global(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    if (argc != 1) {
        bcl_raise_code(interp, "GLOBAL", 'F', "BADARGS", "Expected 1 argument");
    }

    if (!bcl_env_bind_global(interp, env, argv[0].data)) {
        bcl_raise_code(interp, "GLOBAL", 'F', "ERROR", "Variable '%s' is not defined globally", argv[0].data);
    }

    return bcl_val_empty();
}

/* Resolucion del nombre de un modulo (BCL_LIBS).
 *
 *  - Un nombre con '/' (ruta absoluta o relativa) se usa tal cual: no se busca.
 *  - Un nombre sin '/' se busca en los directorios de la variable global
 *    BCL_LIBS, separados por comas y por orden de prioridad (el primero gana).
 *    Cada entrada admite espacios alrededor y '~/' al principio.
 *  - Sin BCL_LIBS el nombre se pasa a dlopen como siempre.
 *
 * Devuelve una cadena nueva con la ruta a cargar. *status: 0 = sin busqueda,
 * 1 = encontrado en BCL_LIBS, 2 = BCL_LIBS definida y el fichero no esta en
 * ninguno de sus directorios (en ese caso *libs recibe una copia de BCL_LIBS). */
static char *resolve_module_path(bcl_interp_t *interp, const char *name, int *status, char **libs) {
    bcl_val_t value;
    const char *p;
    const char *home = bcl_home_dir();

    *status = 0;
    if (libs) {
        *libs = NULL;
    }
    if (strchr(name, '/') != NULL || *name == '\0') {
        return bcl_xstrdup(interp, name);
    }

    value = bcl_env_get(interp->global_env, "BCL_LIBS");
    if (value.len == 0) {
        bcl_val_free(&value);
        return bcl_xstrdup(interp, name);
    }

    p = value.data;
    while (*p) {
        const char *start = p;
        const char *end;
        size_t len;

        while (*p && *p != ',') {
            p++;
        }
        end = p;
        if (*p == ',') {
            p++;
        }
        while (start < end && (*start == ' ' || *start == '\t')) {
            start++;
        }
        while (end > start && (end[-1] == ' ' || end[-1] == '\t')) {
            end--;
        }
        len = (size_t)(end - start);
        if (len == 0) {
            continue;
        }

        {
            const char *prefix = "";
            size_t prefix_len = 0;
            char *candidate;
            struct stat st;

            if (len >= 2 && start[0] == '~' && start[1] == '/' && home && *home) {
                prefix = home;
                prefix_len = strlen(home);
                start++;
                len--;
            }
            candidate = bcl_xmalloc(interp, prefix_len + len + strlen(name) + 2);
            memcpy(candidate, prefix, prefix_len);
            memcpy(candidate + prefix_len, start, len);
            len += prefix_len;
            if (len == 0 || candidate[len - 1] != '/') {
                candidate[len++] = '/';
            }
            strcpy(candidate + len, name);

            if (stat(candidate, &st) == 0 && S_ISREG(st.st_mode) && access(candidate, R_OK) == 0) {
                bcl_val_free(&value);
                *status = 1;
                return candidate;
            }
            free(candidate);
        }
    }

    *status = 2;
    if (libs) {
        *libs = bcl_xstrdup(interp, value.data);
    }
    bcl_val_free(&value);
    return bcl_xstrdup(interp, name);
}

bcl_val_t bcl_cmd_load(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    typedef int (*init_fn)(bcl_interp_t *);
    void *handle;
    init_fn init;
    bcl_module_version_fn version_fn;
    bcl_module_t *module;
    char *path;
    char *libs = NULL;
    int resolved = 0;
    (void)env;

    if (argc != 1) {
        bcl_raise_code(interp, "LOAD", 'F', "BADARGS", "Expected module name");
    }

    if (interp->restricted_mode) {
        bcl_raise_code(interp, "LOAD", 'F', "RESTRICTED", "LOAD is disabled in restricted mode");
    }

    path = resolve_module_path(interp, argv[0].data, &resolved, &libs);
    if (resolved == 2) {
        char *list = libs;
        char msg[1024];

        snprintf(msg, sizeof(msg), "Module not found in BCL_LIBS: %s (searched: %s)", argv[0].data, list);
        free(path);
        free(list);
        bcl_raise_code(interp, "LOAD", 'F', "NOTFOUND", "%s", msg);
    }

    if (module_find(interp, path, NULL)) {
        char dup[1024];

        snprintf(dup, sizeof(dup), "Module already loaded: %s", path);
        free(path);
        bcl_raise_code(interp, "LOAD", 'F', "DUPMOD", "%s", dup);
    }

    bcl_clear_pending_os_error(interp);
    handle = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        char oscode[32];
        const char *detail = dlerror();
        int saved_errno = errno;

        if (saved_errno > 0) {
            snprintf(oscode, sizeof(oscode), "%d", saved_errno);
            bcl_set_pending_os_error(interp, oscode, detail ? detail : strerror(saved_errno));
        } else {
            bcl_set_pending_os_error(interp, "DLOPEN", detail ? detail : "dlopen failed");
        }
        {
            char msg[1024];

            snprintf(msg, sizeof(msg), "Cannot open %s: %s", path, detail ? detail : "dlopen failed");
            free(path);
            bcl_raise_code(interp, "LOAD", 'F', "OPENFAIL", "%s", msg);
        }
    }

    init = (init_fn)dlsym(handle, "bcl_module_init");
    if (!init) {
        dlclose(handle);
        free(path);
        bcl_raise_code(interp, "LOAD", 'F', "NOSYMBOL", "Symbol 'bcl_module_init' not found");
    }

    module = bcl_xcalloc(interp, 1, sizeof(bcl_module_t));
    module->path = path;
    module->handle = handle;
    version_fn = (bcl_module_version_fn)dlsym(handle, "bcl_module_version");
    {
        const char *version = version_fn ? version_fn() : NULL;
        module->version = bcl_xstrdup(interp, (version && *version) ? version : "unknown");
    }
    module->cleanup = (void (*)(bcl_interp_t *))dlsym(handle, "bcl_module_cleanup");
    module->load_seq = next_module_load_seq++;
    {
        Dl_info info;
        if (dladdr((void *)init, &info) != 0) {
            module->base_addr = info.dli_fbase;
        }
    }

    current_loading_module = module;
    if (init(interp) != 0) {
        rollback_module_init(interp, module);
        current_loading_module = NULL;
        dlclose(handle);
        free_module(module); /* libera tambien module->path */
        bcl_raise_code(interp, "LOAD", 'F', "MODINIT", "Module initialization failed");
    }
    current_loading_module = NULL;

    module_insert(interp, module);
    return module_load_result(interp, module);
}

bcl_val_t bcl_cmd_unload(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    bcl_module_t *prev = NULL;
    bcl_module_t *module;
    (void)env;

    if (argc != 1) {
        bcl_raise_code(interp, "LOAD", 'F', "BADARGS", "Expected module name for unload");
    }

    if (interp->restricted_mode) {
        bcl_raise_code(interp, "LOAD", 'F', "RESTRICTED", "UNLOAD is disabled in restricted mode");
    }

    {
        int resolved = 0;
        char *path = resolve_module_path(interp, argv[0].data, &resolved, NULL);

        module = module_find(interp, path, &prev);
        free(path);
        if (!module) {
            /* por si BCL_LIBS cambio desde el LOAD: tambien se admite la cadena tal cual */
            module = module_find(interp, argv[0].data, &prev);
        }
    }
    if (!module) {
        bcl_raise_code(interp, "LOAD", 'F', "NOTLOADED", "Module not loaded: %s", argv[0].data);
    }

    unload_module_internal(interp, module, prev);
    return bcl_val_empty();
}

bcl_val_t bcl_cmd_source(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    const char *osmsg;

    if (argc != 1) {
        bcl_raise_code(interp, "SRC", 'F', "BADARGS", "Expected filename");
    }

    if (!bcl_source_allowed(interp, argv[0].data)) {
        bcl_raise_code(interp, "SRC", 'F', "RESTRICTED", "SOURCE path is not allowed in restricted mode: %s", argv[0].data);
    }

    bcl_clear_pending_os_error(interp);
    if (bcl_source_file(interp, env, argv[0].data) != 0) {
        osmsg = interp->pending_osmsg ? interp->pending_osmsg : "unable to open file";
        bcl_raise_code(interp, "SRC", 'F', "NOFILE", "Cannot open sourced file %s: %s", argv[0].data, osmsg);
    }

    return bcl_val_empty();
}

bcl_val_t bcl_cmd_sourcedir(bcl_interp_t *interp, bcl_env_t *env, int argc, bcl_val_t *argv) {
    const char *source;
    const char *slash;
    (void)env;
    (void)argv;

    if (argc != 0) {
        bcl_raise_code(interp, "SRC", 'F', "BADARGS", "SOURCEDIR expects no arguments");
    }

    source = interp->current_source_name;
    if (!source || source[0] == '<') {
        for (bcl_exec_frame_t *frame = interp->frame_stack; frame; frame = frame->prev) {
            if (frame->call_source && frame->call_source[0] != '<') {
                source = frame->call_source;
                break;
            }
        }
    }
    if (!source || !*source) {
        return bcl_val_make(".", 1);
    }
    slash = strrchr(source, '/');
    if (!slash) {
        return bcl_val_make(".", 1);
    }
    if (slash == source) {
        return bcl_val_make("/", 1);
    }
    return bcl_val_make(source, (size_t)(slash - source));
}

void bcl_register_builtin_cmds(bcl_interp_t *interp) {
    bcl_cmd_register(interp, "PUTS", bcl_cmd_puts, -1);
    bcl_cmd_register(interp, "PUTSN", bcl_cmd_putsn, -1);
    bcl_cmd_register(interp, "GETS", bcl_cmd_gets, 0);
    bcl_cmd_register(interp, "CALC", bcl_cmd_calc, -1);
    bcl_cmd_register(interp, "SET", bcl_cmd_set, -1);
    bcl_cmd_register(interp, "INCR", bcl_cmd_incr, -1);
    bcl_cmd_register(interp, "INCRV", bcl_cmd_incrv, 2);
    bcl_cmd_register(interp, "EXISTS", bcl_cmd_exists, 1);
    bcl_cmd_register(interp, "ARREXISTS", bcl_cmd_arrexists, 1);
    bcl_cmd_register(interp, "ARRLENG", bcl_cmd_arrleng, 1);
    bcl_cmd_register(interp, "ARRKEYS", bcl_cmd_arrkeys, 1);
    bcl_cmd_register(interp, "ARRGET", bcl_cmd_arrget, 1);
    bcl_cmd_register(interp, "ARRSET", bcl_cmd_arrset, 2);
    bcl_cmd_register(interp, "ARRDEL", bcl_cmd_arrdel, 2);
    bcl_cmd_register(interp, "ARRCLEAR", bcl_cmd_arrclear, 1);
    bcl_cmd_register(interp, "UNSET", bcl_cmd_unset, 1);
    bcl_cmd_register(interp, "GLOBAL", bcl_cmd_global, 1);
    bcl_cmd_register(interp, "STRLENG", bcl_cmd_strleng, 1);
    bcl_cmd_register(interp, "STRGET", bcl_cmd_strget, 2);
    bcl_cmd_register(interp, "STRRANG", bcl_cmd_strrang, 3);
    bcl_cmd_register(interp, "STRAPP", bcl_cmd_strapp, 2);
    bcl_cmd_register(interp, "STRINS", bcl_cmd_strins, 3);
    bcl_cmd_register(interp, "STRDEL", bcl_cmd_strdel, 3);
    bcl_cmd_register(interp, "STRUPP", bcl_cmd_strupp, 1);
    bcl_cmd_register(interp, "STRLOW", bcl_cmd_strlow, 1);
    bcl_cmd_register(interp, "STRREV", bcl_cmd_strrev, 1);
    bcl_cmd_register(interp, "STREQ", bcl_cmd_streq, 2);
    bcl_cmd_register(interp, "STRFMT", bcl_cmd_strfmt, -1);
    bcl_cmd_register(interp, "STRFIND", bcl_cmd_strfind, 2);
    bcl_cmd_register(interp, "LSTLENG", bcl_cmd_lstleng, 1);
    bcl_cmd_register(interp, "LSTGET", bcl_cmd_lstget, 2);
    bcl_cmd_register(interp, "LSTRANG", bcl_cmd_lstrang, 3);
    bcl_cmd_register(interp, "LSTFIND", bcl_cmd_lstfind, 2);
    bcl_cmd_register(interp, "LSTAPP", bcl_cmd_lstapp, 2);
    bcl_cmd_register(interp, "LSTINS", bcl_cmd_lstins, 3);
    bcl_cmd_register(interp, "LSTDEL", bcl_cmd_lstdel, 3);
    bcl_cmd_register(interp, "LSTSORT", bcl_cmd_lstsort, 1);
    bcl_cmd_register(interp, "LSTFMT", bcl_cmd_lstfmt, 2);
    bcl_cmd_register(interp, "EXIT", bcl_cmd_exit, -1);
    bcl_cmd_register(interp, "LOAD", bcl_cmd_load, 1);
    bcl_cmd_register(interp, "UNLOAD", bcl_cmd_unload, 1);
    bcl_cmd_register(interp, "SOURCE", bcl_cmd_source, 1);
    bcl_cmd_register(interp, "SOURCEDIR", bcl_cmd_sourcedir, 0);
}

void bcl_runtime_cleanup(bcl_interp_t *interp) {
    runtime_cleanup_active = 1;
    while (interp->modules) {
        bcl_module_t *candidate = (bcl_module_t *)interp->modules;
        bcl_module_t *item = candidate;
        while (item) {
            if (bcl_module_dependent_count(interp, item) == 0) {
                candidate = item;
                break;
            }
            item = item->next;
        }
        unload_module_internal(interp, candidate, NULL);
    }
    free(interp->module_buckets);
    interp->module_buckets = NULL;
    while (interp->module_guards) {
        bcl_module_guard_t *guard = (bcl_module_guard_t *)interp->module_guards;
        interp->module_guards = guard->next;
        free(guard);
    }
    runtime_cleanup_active = 0;

    bcl_evtsys_clear(interp);
}
