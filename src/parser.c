/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Rafael Seguí Revert. See LICENSE. */
/**
 * BCL Parser implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include "parser.h"
#include "lexer.h"

typedef struct {
    const char **words;
    int count;
} stopwords_t;

typedef struct {
    size_t pos;
    int line;
    int in_string;
    int in_subcmd;
    int subcmd_depth;
    int parse_depth;
} lexer_checkpoint_t;

typedef struct parser_cleanup {
    bcl_node_t *node;
    struct parser_cleanup *next;
} parser_cleanup_t;

static const char *stop_end[] = {"END"};
static const char *stop_else_loop[] = {"ELSE", "LOOP"};
static const char *stop_loop[] = {"LOOP"};
static const char *stop_catch[] = {"CATCH"};

static bcl_node_t *parse_statement(bcl_interp_t *interp, bcl_lexer_t *lexer);

static void token_free(bcl_token_t *tok) {
    if (tok->text) {
        free(tok->text);
        tok->text = NULL;
    }
}

static void parser_track_node(bcl_interp_t *interp, bcl_node_t *node) {
    parser_cleanup_t *entry;

    if (!interp || !node) {
        return;
    }

    entry = malloc(sizeof(*entry));
    if (!entry) {
        bcl_node_free(node);
        bcl_raise_code(interp, "BCL", 'F', "NOMEM", "Out of memory");
    }

    entry->node = node;
    entry->next = (parser_cleanup_t *)interp->parser_cleanups;
    interp->parser_cleanups = entry;
}

static void parser_untrack_node(bcl_interp_t *interp, bcl_node_t *node) {
    parser_cleanup_t **link;

    if (!interp || !node) {
        return;
    }

    link = (parser_cleanup_t **)&interp->parser_cleanups;
    while (*link) {
        parser_cleanup_t *entry = *link;
        if (entry->node == node) {
            *link = entry->next;
            free(entry);
            return;
        }
        link = &entry->next;
    }
}

void bcl_parser_cleanup_all(bcl_interp_t *interp) {
    parser_cleanup_t *entry;

    if (!interp) {
        return;
    }

    entry = (parser_cleanup_t *)interp->parser_cleanups;
    interp->parser_cleanups = NULL;
    while (entry) {
        parser_cleanup_t *next = entry->next;
        bcl_node_free(entry->node);
        free(entry);
        entry = next;
    }
}

static lexer_checkpoint_t lexer_checkpoint(const bcl_lexer_t *lexer) {
    lexer_checkpoint_t checkpoint;

    checkpoint.pos = lexer->pos;
    checkpoint.line = lexer->line;
    checkpoint.in_string = lexer->in_string;
    checkpoint.in_subcmd = lexer->in_subcmd;
    checkpoint.subcmd_depth = lexer->subcmd_depth;
    checkpoint.parse_depth = lexer->parse_depth;
    return checkpoint;
}

static void lexer_restore(bcl_lexer_t *lexer, lexer_checkpoint_t checkpoint) {
    lexer->pos = checkpoint.pos;
    lexer->line = checkpoint.line;
    lexer->in_string = checkpoint.in_string;
    lexer->in_subcmd = checkpoint.in_subcmd;
    lexer->subcmd_depth = checkpoint.subcmd_depth;
    lexer->parse_depth = checkpoint.parse_depth;
}

static bcl_token_t lexer_peek(bcl_lexer_t *lexer) {
    lexer_checkpoint_t checkpoint = lexer_checkpoint(lexer);
    bcl_token_t tok = bcl_lexer_next(lexer);
    lexer_restore(lexer, checkpoint);
    return tok;
}

static bcl_token_t lexer_peek_non_newline(bcl_lexer_t *lexer) {
    lexer_checkpoint_t checkpoint = lexer_checkpoint(lexer);
    bcl_token_t tok;

    do {
        tok = bcl_lexer_next(lexer);
        if (tok.type != TOK_NEWLINE) {
            lexer_restore(lexer, checkpoint);
            return tok;
        }
        token_free(&tok);
    } while (1);
}

static void skip_newlines(bcl_lexer_t *lexer) {
    while (1) {
        bcl_token_t tok = lexer_peek(lexer);
        if (tok.type != TOK_NEWLINE) {
            token_free(&tok);
            return;
        }
        token_free(&tok);
        tok = bcl_lexer_next(lexer);
        token_free(&tok);
    }
}

static int is_stopword(const char *word, const stopwords_t *stops) {
    if (!stops) {
        return 0;
    }

    for (int i = 0; i < stops->count; i++) {
        if (strcasecmp(word, stops->words[i]) == 0) {
            return 1;
        }
    }

    return 0;
}

static char *append_text(bcl_interp_t *interp, char *dst, size_t *len, size_t *cap, const char *text) {
    size_t extra = strlen(text);
    if (extra > (size_t)-1 - *len - 1) {
        bcl_raise_code(interp, "BCL", 'F', "NOMEM", "Parser buffer exceeds maximum size");
    }
    if (*len + extra + 1 > *cap) {
        while (*len + extra + 1 > *cap) {
            if (*cap > (size_t)-1 / 2) {
                bcl_raise_code(interp, "BCL", 'F', "NOMEM", "Parser buffer exceeds maximum size");
            }
            *cap *= 2;
        }
        dst = bcl_xrealloc(interp, dst, *cap);
    }

    memcpy(dst + *len, text, extra);
    *len += extra;
    dst[*len] = '\0';
    return dst;
}

static char *append_char(bcl_interp_t *interp, char *dst, size_t *len, size_t *cap, char ch) {
    char tmp[2] = {ch, '\0'};
    return append_text(interp, dst, len, cap, tmp);
}

static char *escape_quoted(bcl_interp_t *interp, const char *text) {
    size_t cap = strlen(text) * 2 + 3;
    size_t len = 0;
    char *out = bcl_xmalloc(interp, cap);
    out[0] = '\0';

    out = append_char(interp, out, &len, &cap, '"');
    for (const char *p = text; *p; p++) {
        if (*p == '\\' || *p == '"') {
            out = append_char(interp, out, &len, &cap, '\\');
        }
        out = append_char(interp, out, &len, &cap, *p);
    }
    out = append_char(interp, out, &len, &cap, '"');
    return out;
}

static char *token_to_source(bcl_interp_t *interp, const bcl_token_t *tok) {
    char *out;
    size_t len;

    switch (tok->type) {
        case TOK_WORD:
            return bcl_xstrdup(interp, tok->text);
        case TOK_STRING:
            return escape_quoted(interp, tok->text);
        case TOK_VARREF:
            len = strlen(tok->text) + 2;
            out = bcl_xmalloc(interp, len);
            snprintf(out, len, "$%s", tok->text);
            return out;
        case TOK_SUBCMD:
            len = strlen(tok->text) + 3;
            out = bcl_xmalloc(interp, len);
            snprintf(out, len, "[%s]", tok->text);
            return out;
        default:
            return bcl_xstrdup(interp, "");
    }
}

static char *dup_trimmed_range(bcl_interp_t *interp, const char *src, size_t start, size_t end) {
    while (start < end &&
           (src[start] == ' ' || src[start] == '\t' || src[start] == '\r' || src[start] == '\n')) {
        start++;
    }

    while (end > start &&
           (src[end - 1] == ' ' || src[end - 1] == '\t' || src[end - 1] == '\r' || src[end - 1] == '\n')) {
        end--;
    }

    char *out = bcl_xmalloc(interp, end - start + 1);
    memcpy(out, src + start, end - start);
    out[end - start] = '\0';
    return out;
}

static int column_from_offset(const bcl_lexer_t *lexer, size_t offset) {
    size_t col = 1;

    while (offset > 0 && lexer->src[offset - 1] != '\n') {
        offset--;
        col++;
    }

    return (int)col;
}

static void set_error_location(bcl_interp_t *interp, const bcl_lexer_t *lexer, int line, size_t offset) {
    if (!interp) {
        return;
    }

    interp->error_line = line;
    interp->error_col = column_from_offset(lexer, offset);
    interp->error_has_location = 1;
}

static __attribute__((noreturn)) void parse_errorf(bcl_interp_t *interp, bcl_lexer_t *lexer, int line, size_t offset, const char *code, const char *fmt, ...) {
    va_list args;
    char buf[512];

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    set_error_location(interp, lexer, line, offset);
    bcl_raise_code(interp, "PAR", 'F', code, "%s", buf);
}

static void node_set_span_from_token(bcl_node_t *node, const bcl_token_t *tok, const bcl_lexer_t *lexer) {
    if (!node || !tok) {
        return;
    }

    node->line = tok->line;
    node->col = column_from_offset(lexer, tok->start);
    node->start = tok->start;
    node->end = tok->end;
}

static bcl_node_t *bcl_node_create(bcl_interp_t *interp, bcl_nodetype_t type) {
    bcl_node_t *node = bcl_xcalloc(interp, 1, sizeof(bcl_node_t));
    node->type = type;
    return node;
}

static bcl_node_t *node_from_token(const bcl_token_t *tok, const bcl_lexer_t *lexer) {
    bcl_node_t *node = NULL;

    switch (tok->type) {
        case TOK_WORD:
            node = bcl_node_create(lexer->interp, NODE_LITERAL);
            node->val = bcl_val_make(tok->text, strlen(tok->text));
            break;
        case TOK_STRING:
            node = bcl_node_create(lexer->interp, NODE_STRING);
            node->val = bcl_val_make(tok->text, strlen(tok->text));
            break;
        case TOK_VARREF:
            node = bcl_node_create(lexer->interp, NODE_VARREF);
            node->name = bcl_xstrdup(lexer->interp, tok->text);
            break;
        case TOK_SUBCMD:
            node = bcl_node_create(lexer->interp, NODE_SUBCMD);
            node->name = bcl_xstrdup(lexer->interp, tok->text);
            break;
        default:
            break;
    }

    node_set_span_from_token(node, tok, lexer);
    return node;
}

static int tokens_are_adjacent(const bcl_token_t *left, const bcl_token_t *right) {
    return left->end == right->start;
}

static bcl_node_t *compound_node_from_tokens(const bcl_token_t *first, bcl_lexer_t *lexer) {
    size_t cap = strlen(first->text) + 8;
    size_t len = 0;
    char *raw = bcl_xmalloc(lexer->interp, cap);
    bcl_token_t current = *first;
    int owns_current = 0;
    int count = 0;

    raw[0] = '\0';

    while (1) {
        char *piece = token_to_source(lexer->interp, &current);
        raw = append_text(lexer->interp, raw, &len, &cap, piece);
        free(piece);
        count++;

        bcl_token_t next = lexer_peek(lexer);
        if (next.type == TOK_NEWLINE || next.type == TOK_EOF || !tokens_are_adjacent(&current, &next)) {
            token_free(&next);
            break;
        }

        token_free(&next);
        if (owns_current) {
            token_free(&current);
        }
        current = bcl_lexer_next(lexer);
        owns_current = 1;
    }

    if (count == 1) {
        if (owns_current) {
            token_free(&current);
        }
        free(raw);
        return node_from_token(first, lexer);
    }

    if (owns_current) {
        token_free(&current);
    }
    bcl_node_t *node = bcl_node_create(lexer->interp, NODE_WORD);
    node->val = bcl_val_make(raw, strlen(raw));
    node_set_span_from_token(node, first, lexer);
    node->end = current.end;
    free(raw);
    return node;
}

static void append_child_node(bcl_node_t *parent, bcl_node_t **tail, bcl_node_t *child) {
    if (!child) {
        return;
    }

    child->next = NULL;
    if (!parent->children) {
        parent->children = child;
        *tail = child;
        return;
    }

    if (!*tail) {
        *tail = parent->children;
        while ((*tail)->next) {
            *tail = (*tail)->next;
        }
    }

    (*tail)->next = child;
    *tail = child;
}

static bcl_node_t *parse_line_value(bcl_lexer_t *lexer) {
    bcl_token_t tok = bcl_lexer_next(lexer);
    bcl_node_t *node = NULL;

    if (tok.type != TOK_NEWLINE && tok.type != TOK_EOF) {
        node = compound_node_from_tokens(&tok, lexer);

        while (1) {
            bcl_token_t next = lexer_peek(lexer);
            if (next.type == TOK_NEWLINE || next.type == TOK_EOF) {
                token_free(&next);
                break;
            }
            token_free(&next);
            next = bcl_lexer_next(lexer);
            token_free(&next);
        }
    }

    token_free(&tok);
    return node;
}

static bcl_node_t *parse_condition_until(bcl_interp_t *interp, bcl_lexer_t *lexer, const char *terminator, int require_terminator) {
    size_t cap = 64;
    size_t len = 0;
    char *raw = bcl_xmalloc(interp, cap);
    int first = 1;
    size_t prev_end = 0;
    int first_line = 0;
    int first_col = 0;
    size_t first_start = 0;
    int has_first_tok = 0;

    raw[0] = '\0';

    while (1) {
        bcl_token_t tok = bcl_lexer_next(lexer);
        if (tok.type == TOK_EOF || tok.type == TOK_NEWLINE) {
            if (require_terminator) {
                parse_errorf(interp, lexer, tok.line, tok.start, "MISSTERM", "Expected '%s' before end of line", terminator);
            }
            token_free(&tok);
            break;
        }

        if (tok.type == TOK_WORD && strcasecmp(tok.text, terminator) == 0) {
            token_free(&tok);
            break;
        }

        if (!has_first_tok) {
            first_line = tok.line;
            first_col = column_from_offset(lexer, tok.start);
            first_start = tok.start;
            has_first_tok = 1;
        }

        char *piece = token_to_source(interp, &tok);
        if (!first && tok.start > prev_end) {
            raw = append_char(interp, raw, &len, &cap, ' ');
        }
        raw = append_text(interp, raw, &len, &cap, piece);
        first = 0;
        prev_end = tok.end;

        free(piece);
        token_free(&tok);
    }

    bcl_node_t *node = bcl_node_create(interp, NODE_CONDITION);
    node->val = bcl_val_make(raw, strlen(raw));
    if (has_first_tok) {
        node->line = first_line;
        node->col = first_col;
        node->start = first_start;
        node->end = prev_end;
    }
    free(raw);
    return node;
}

static bcl_node_t *parse_command_tail(bcl_lexer_t *lexer, const bcl_token_t *name_tok) {
    bcl_node_t *node = bcl_node_create(lexer->interp, NODE_CMD);
    bcl_node_t *tail = NULL;

    parser_track_node(lexer->interp, node);
    node->name = bcl_xstrdup(lexer->interp, name_tok->text);
    node_set_span_from_token(node, name_tok, lexer);

    while (1) {
        bcl_token_t tok = bcl_lexer_next(lexer);
        bcl_node_t *arg = NULL;

        if (tok.type == TOK_NEWLINE || tok.type == TOK_EOF) {
            token_free(&tok);
            break;
        }

        arg = compound_node_from_tokens(&tok, lexer);
        append_child_node(node, &tail, arg);
        if (arg) {
            node->end = arg->end;
        }

        token_free(&tok);
    }

    parser_untrack_node(lexer->interp, node);
    return node;
}

static bcl_node_t *parse_calc_command(bcl_interp_t *interp, bcl_lexer_t *lexer, const bcl_token_t *tok) {
    bcl_node_t *node = bcl_node_create(interp, NODE_CMD);
    bcl_node_t *arg;

    parser_track_node(interp, node);
    arg = parse_condition_until(interp, lexer, "__UNUSED__", 0);
    node->name = bcl_xstrdup(interp, "CALC");
    node_set_span_from_token(node, tok, lexer);
    arg->type = NODE_STRING;
    node->children = arg;
    node->end = arg->end;
    parser_untrack_node(interp, node);
    return node;
}

static bcl_node_t *parse_block(bcl_interp_t *interp, bcl_lexer_t *lexer, const stopwords_t *stops, char **matched_word, const bcl_token_t *owner_tok) {
    bcl_node_t *head = NULL;
    bcl_node_t *tail = NULL;

    if (lexer->parse_depth >= BCL_MAX_PARSE_DEPTH) {
        int err_line = owner_tok ? owner_tok->line : lexer->line;
        size_t err_start = owner_tok ? owner_tok->start : lexer->pos;
        parse_errorf(interp, lexer, err_line, err_start, "DEPTH", "Parser block nesting exceeds limit");
    }
    lexer->parse_depth++;

    while (1) {
        skip_newlines(lexer);

        bcl_token_t peek = lexer_peek_non_newline(lexer);
        if (peek.type == TOK_EOF) {
            int err_line = owner_tok ? owner_tok->line : peek.line;
            size_t err_start = owner_tok ? owner_tok->start : peek.start;

            if (stops && stops->count > 0) {
                if (stops->count == 1) {
                    parse_errorf(interp, lexer, err_line, err_start, "MISSEND", "Expected '%s' before end of file", stops->words[0]);
                }
                parse_errorf(interp, lexer, err_line, err_start, "MISSTERM", "Expected block terminator before end of file");
            }
            token_free(&peek);
            break;
        }

        if (peek.type == TOK_WORD && is_stopword(peek.text, stops)) {
            if (matched_word) {
                *matched_word = bcl_xstrdup(interp, peek.text);
            }
            token_free(&peek);
            break;
        }
        token_free(&peek);

        bcl_node_t *stmt = parse_statement(interp, lexer);
        if (!stmt) {
            continue;
        }

        if (!head) {
            head = stmt;
            parser_track_node(interp, head);
        } else {
            tail->next = stmt;
        }

        tail = stmt;
        while (tail->next) {
            tail = tail->next;
        }
    }

    lexer->parse_depth--;
    parser_untrack_node(interp, head);
    return head;
}

static bcl_node_t *parse_if_then(bcl_interp_t *interp, bcl_lexer_t *lexer, const bcl_token_t *if_tok) {
    static const char *body_stops[] = {"ELSEIF", "ELSE", "END"};
    bcl_node_t *node = bcl_node_create(interp, NODE_IF_THEN);
    bcl_if_data_t *data = bcl_xcalloc(interp, 1, sizeof(bcl_if_data_t));
    bcl_if_clause_t *tail = NULL;

    parser_track_node(interp, node);
    node_set_span_from_token(node, if_tok, lexer);
    node->extra = data;

    while (1) {
        bcl_if_clause_t *clause = bcl_xcalloc(interp, 1, sizeof(bcl_if_clause_t));
        char *stop = NULL;

        if (!data->clauses) {
            data->clauses = clause;
        } else {
            tail->next = clause;
        }
        tail = clause;

        clause->cond = parse_condition_until(interp, lexer, "THEN", 1);
        clause->body = parse_block(interp, lexer, &(stopwords_t){body_stops, 3}, &stop, if_tok);

        if (!stop || strcasecmp(stop, "END") == 0) {
            free(stop);
            break;
        }

        if (strcasecmp(stop, "ELSE") == 0) {
            bcl_token_t consume = bcl_lexer_next(lexer);
            token_free(&consume);
            data->else_body = parse_block(interp, lexer, &(stopwords_t){stop_end, 1}, NULL, if_tok);
            free(stop);
            break;
        }

        if (strcasecmp(stop, "ELSEIF") == 0) {
            bcl_token_t consume = bcl_lexer_next(lexer);
            token_free(&consume);
            free(stop);
            continue;
        }

        free(stop);
        break;
    }

    bcl_token_t endtok = lexer_peek_non_newline(lexer);
    if (endtok.type == TOK_WORD && strcasecmp(endtok.text, "END") == 0) {
        token_free(&endtok);
        endtok = bcl_lexer_next(lexer);
    }
    token_free(&endtok);

    parser_untrack_node(interp, node);
    return node;
}

static bcl_node_t *parse_if_do(bcl_interp_t *interp, bcl_lexer_t *lexer, const bcl_token_t *if_tok) {
    bcl_node_t *node = bcl_node_create(interp, NODE_IF_DO);
    bcl_loop_data_t *data = bcl_xcalloc(interp, 1, sizeof(bcl_loop_data_t));
    char *stop = NULL;

    parser_track_node(interp, node);
    node_set_span_from_token(node, if_tok, lexer);
    node->extra = data;

    data->cond = parse_condition_until(interp, lexer, "DO", 1);
    data->body = parse_block(interp, lexer, &(stopwords_t){stop_else_loop, 2}, &stop, if_tok);

    if (stop && strcasecmp(stop, "ELSE") == 0) {
        bcl_token_t consume = bcl_lexer_next(lexer);
        token_free(&consume);
        data->else_body = parse_block(interp, lexer, &(stopwords_t){stop_loop, 1}, NULL, if_tok);
    }
    free(stop);

    bcl_token_t looptok = lexer_peek_non_newline(lexer);
    if (looptok.type == TOK_WORD && strcasecmp(looptok.text, "LOOP") == 0) {
        token_free(&looptok);
        looptok = bcl_lexer_next(lexer);
    }
    token_free(&looptok);

    parser_untrack_node(interp, node);
    return node;
}

static bcl_node_t *parse_if(bcl_interp_t *interp, bcl_lexer_t *lexer, const bcl_token_t *if_tok) {
    bcl_lexer_t copy = *lexer;
    bcl_token_t tok;

    while (1) {
        tok = bcl_lexer_next(&copy);
        if (tok.type == TOK_EOF || tok.type == TOK_NEWLINE) {
            token_free(&tok);
            break;
        }
        if (tok.type == TOK_WORD &&
            (strcasecmp(tok.text, "THEN") == 0 || strcasecmp(tok.text, "DO") == 0)) {
            int is_then = strcasecmp(tok.text, "THEN") == 0;
            token_free(&tok);
            return is_then ? parse_if_then(interp, lexer, if_tok) : parse_if_do(interp, lexer, if_tok);
        }
        token_free(&tok);
    }

    parse_errorf(interp, lexer, if_tok->line, if_tok->start, "IFTERM", "IF requires THEN or DO on the same statement");
}

static bcl_node_t *parse_proc(bcl_interp_t *interp, bcl_lexer_t *lexer, const bcl_token_t *proc_tok) {
    bcl_node_t *node = bcl_node_create(interp, NODE_PROC);
    bcl_token_t name = bcl_lexer_next(lexer);
    bcl_token_t tok = lexer_peek(lexer);
    bcl_procdef_t *data = bcl_xcalloc(interp, 1, sizeof(bcl_procdef_t));
    size_t body_start;
    size_t body_end;
    int saw_optional = 0;

    parser_track_node(interp, node);
    node_set_span_from_token(node, proc_tok, lexer);
    node->extra = data;

    if (name.type != TOK_WORD) {
        token_free(&name);
        token_free(&tok);
        parse_errorf(interp, lexer, proc_tok->line, proc_tok->start, "PROCDEF", "PROC requires a procedure name");
    }

    if (tok.type == TOK_WORD && strcasecmp(tok.text, "WITH") == 0) {
        token_free(&tok);
        tok = bcl_lexer_next(lexer);
        token_free(&tok);

        while (1) {
            tok = bcl_lexer_next(lexer);
            if (tok.type == TOK_WORD && strcasecmp(tok.text, "DO") == 0) {
                token_free(&tok);
                break;
            }
            if (tok.type == TOK_EOF || tok.type == TOK_NEWLINE) {
                token_free(&tok);
                parse_errorf(interp, lexer, name.line, name.start, "PROCDEF", "PROC %s requires DO before end of line", name.text);
            }
            if (tok.type == TOK_WORD) {
                const char *param_name = tok.text;
                int optional = 0;

                if (tok.text[0] == '@') {
                    optional = 1;
                    param_name = tok.text + 1;
                    saw_optional = 1;
                } else if (saw_optional && !data->invalid_param) {
                    data->invalid_param = bcl_xstrdup(interp, tok.text);
                }

                if (*param_name != '\0') {
                    data->params = bcl_xrealloc(interp, data->params, sizeof(bcl_param_t) * (size_t)(data->nparams + 1));
                    data->params[data->nparams].name = bcl_xstrdup(interp, param_name);
                    data->params[data->nparams].optional = optional;
                    data->nparams++;
                    if (!optional) {
                        data->nrequired++;
                    }
                } else if (!data->invalid_param) {
                    data->invalid_param = bcl_xstrdup(interp, tok.text);
                }
            }
            token_free(&tok);
        }
    } else {
        token_free(&tok);
        tok = bcl_lexer_next(lexer);
    if (tok.type != TOK_WORD || strcasecmp(tok.text, "DO") != 0) {
            int line = tok.type == TOK_WORD ? tok.line : name.line;
            size_t start = tok.type == TOK_WORD ? tok.start : name.start;
            char *proc_name = bcl_xstrdup(interp, name.text);
            token_free(&tok);
            token_free(&name);
            parse_errorf(interp, lexer, line, start, "PROCDEF", "PROC %s requires DO", proc_name);
        }
        token_free(&tok);
    }

    body_start = lexer->pos;
    data->body = parse_block(interp, lexer, &(stopwords_t){stop_end, 1}, NULL, proc_tok);
    body_end = lexer->pos;
    data->body_src = dup_trimmed_range(interp, lexer->src, body_start, body_end);
    data->source_name = bcl_xstrdup(interp, interp && interp->current_source_name ? interp->current_source_name : "<unknown>");

    tok = lexer_peek_non_newline(lexer);
    if (tok.type == TOK_WORD && strcasecmp(tok.text, "END") == 0) {
        token_free(&tok);
        tok = bcl_lexer_next(lexer);
    }
    token_free(&tok);

    node->name = bcl_xstrdup(interp, name.text);

    token_free(&name);
    parser_untrack_node(interp, node);
    return node;
}

static bcl_node_t *parse_try(bcl_interp_t *interp, bcl_lexer_t *lexer, const bcl_token_t *try_tok) {
    bcl_node_t *node = bcl_node_create(interp, NODE_TRY);
    bcl_try_data_t *data = bcl_xcalloc(interp, 1, sizeof(bcl_try_data_t));
    bcl_token_t tok;

    parser_track_node(interp, node);
    node_set_span_from_token(node, try_tok, lexer);
    node->extra = data;

    data->try_body = parse_block(interp, lexer, &(stopwords_t){stop_catch, 1}, NULL, try_tok);

    tok = bcl_lexer_next(lexer);
    if (tok.type == TOK_WORD && strcasecmp(tok.text, "CATCH") == 0) {
        bcl_token_t var = bcl_lexer_next(lexer);
        if (var.type == TOK_WORD) {
            data->catch_var = bcl_xstrdup(interp, var.text);
        } else {
            token_free(&var);
            token_free(&tok);
            parse_errorf(interp, lexer, try_tok->line, try_tok->start, "MISSCATCH", "TRY requires CATCH <var>");
        }
        token_free(&var);
    } else {
        token_free(&tok);
        parse_errorf(interp, lexer, try_tok->line, try_tok->start, "MISSCATCH", "TRY requires CATCH <var>");
    }
    token_free(&tok);

    data->catch_body = parse_block(interp, lexer, &(stopwords_t){stop_end, 1}, NULL, try_tok);

    tok = lexer_peek_non_newline(lexer);
    if (tok.type == TOK_WORD && strcasecmp(tok.text, "END") == 0) {
        token_free(&tok);
        tok = bcl_lexer_next(lexer);
    }
    token_free(&tok);

    parser_untrack_node(interp, node);
    return node;
}

static bcl_node_t *parse_statement(bcl_interp_t *interp, bcl_lexer_t *lexer) {
    bcl_token_t tok;

    skip_newlines(lexer);

    tok = bcl_lexer_next(lexer);
    if (tok.type == TOK_EOF) {
        token_free(&tok);
        return NULL;
    }

    if (tok.type != TOK_WORD) {
        parse_errorf(interp, lexer, tok.line, tok.start, "SYNTAX", "Expected command or keyword");
        token_free(&tok);
    }

    if (strcasecmp(tok.text, "IF") == 0) {
        bcl_node_t *node = parse_if(interp, lexer, &tok);
        token_free(&tok);
        return node;
    }

    if (strcasecmp(tok.text, "PROC") == 0) {
        bcl_node_t *node = parse_proc(interp, lexer, &tok);
        token_free(&tok);
        return node;
    }

    if (strcasecmp(tok.text, "TRY") == 0) {
        bcl_node_t *node = parse_try(interp, lexer, &tok);
        token_free(&tok);
        return node;
    }

    if (strcasecmp(tok.text, "RETURN") == 0) {
        bcl_node_t *node = bcl_node_create(interp, NODE_RETURN);
        parser_track_node(interp, node);
        node_set_span_from_token(node, &tok, lexer);
        node->children = parse_line_value(lexer);
        token_free(&tok);
        parser_untrack_node(interp, node);
        return node;
    }

    if (strcasecmp(tok.text, "ERROR") == 0) {
        bcl_node_t *node = bcl_node_create(interp, NODE_ERROR);
        parser_track_node(interp, node);
        node_set_span_from_token(node, &tok, lexer);
        node->children = parse_line_value(lexer);
        token_free(&tok);
        parser_untrack_node(interp, node);
        return node;
    }

    if (strcasecmp(tok.text, "EXIT") == 0) {
        bcl_node_t *node = bcl_node_create(interp, NODE_EXIT);
        parser_track_node(interp, node);
        node_set_span_from_token(node, &tok, lexer);
        node->children = parse_line_value(lexer);
        token_free(&tok);
        parser_untrack_node(interp, node);
        return node;
    }

    if (strcasecmp(tok.text, "BREAK") == 0) {
        bcl_node_t *node = bcl_node_create(interp, NODE_BREAK);
        parser_track_node(interp, node);
        node_set_span_from_token(node, &tok, lexer);
        token_free(&tok);
        parser_untrack_node(interp, node);
        return node;
    }

    if (strcasecmp(tok.text, "NEXT") == 0) {
        bcl_node_t *node = bcl_node_create(interp, NODE_NEXT);
        parser_track_node(interp, node);
        node_set_span_from_token(node, &tok, lexer);
        token_free(&tok);
        parser_untrack_node(interp, node);
        return node;
    }

    if (strcasecmp(tok.text, "CALC") == 0) {
        bcl_node_t *node = parse_calc_command(interp, lexer, &tok);
        token_free(&tok);
        return node;
    }

    bcl_node_t *node = parse_command_tail(lexer, &tok);
    token_free(&tok);
    return node;
}

bcl_node_t *bcl_parse(bcl_interp_t *interp, bcl_lexer_t *lexer) {
    return parse_statement(interp, lexer);
}

bcl_node_t *bcl_parse_line(bcl_interp_t *interp, bcl_lexer_t *lexer) {
    return parse_statement(interp, lexer);
}

void bcl_node_free(bcl_node_t *node) {
    if (!node) {
        return;
    }

    if (node->children) {
        bcl_node_free(node->children);
    }

    if (node->next) {
        bcl_node_free(node->next);
    }

    if (node->name) {
        free(node->name);
    }

    if (node->val.data) {
        bcl_val_free(&node->val);
    }

    if (node->extra) {
        switch (node->type) {
            case NODE_IF_THEN: {
                bcl_if_data_t *data = node->extra;
                bcl_if_clause_t *clause = data->clauses;
                while (clause) {
                    bcl_if_clause_t *next = clause->next;
                    bcl_node_free(clause->cond);
                    bcl_node_free(clause->body);
                    free(clause);
                    clause = next;
                }
                bcl_node_free(data->else_body);
                free(data);
                break;
            }
            case NODE_IF_DO: {
                bcl_loop_data_t *data = node->extra;
                bcl_node_free(data->cond);
                bcl_node_free(data->body);
                bcl_node_free(data->else_body);
                free(data);
                break;
            }
            case NODE_PROC: {
                bcl_procdef_t *data = node->extra;
                for (int i = 0; i < data->nparams; i++) {
                    free(data->params[i].name);
                }
                free(data->params);
                free(data->invalid_param);
                free(data->source_name);
                bcl_node_free(data->body);
                free(data->body_src);
                free(data);
                break;
            }
            case NODE_TRY: {
                bcl_try_data_t *data = node->extra;
                free(data->catch_var);
                bcl_node_free(data->try_body);
                bcl_node_free(data->catch_body);
                free(data);
                break;
            }
            default:
                free(node->extra);
                break;
        }
    }

    free(node);
}
