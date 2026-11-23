/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Rafael Seguí Revert. See LICENSE. */
/**
 * BCL Lexer/Tokenizer implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include "lexer.h"

/* Escape table */
static const struct { char esc; char val; } escape_table[] = {
    {'a', 0x07}, {'b', 0x08}, {'f', 0x0C}, {'n', 0x0A},
    {'r', 0x0D}, {'t', 0x09}, {'v', 0x0B},
    {'\\', '\\'}, {'"', '"'}, {'$', '$'}, {'[', '['},
    {']', ']'}, {'#', '#'}, {0, 0}
};

/* Process escape sequences in string */
static size_t process_escape(const char *src, size_t pos, char *out, size_t *written) {
    char c = src[pos];
    *written = 1;
    if (c == '\\' && src[pos+1]) {
        /* Check simple escapes */
        for (int i = 0; escape_table[i].esc; i++) {
            if (src[pos+1] == escape_table[i].esc) {
                *out = escape_table[i].val;
                return pos + 2;
            }
        }
        /* Octal \0dd */
        if (src[pos+1] >= '0' && src[pos+1] <= '7') {
            char buf[4] = {0};
            int i = 0;
            while (i < 3 && src[pos+1+i] >= '0' && src[pos+1+i] <= '7') {
                buf[i] = src[pos+1+i];
                i++;
            }
            *out = (char)strtol(buf, NULL, 8);
            return pos + 1 + i;
        }
        /* Hex \xHH */
        if (src[pos+1] == 'x' && src[pos+2]) {
            char buf[4] = {0};
            int i = 0;
            while (i < 2 && isxdigit((unsigned char)src[pos+2+i])) {
                buf[i] = src[pos+2+i];
                i++;
            }
            if (i > 0) {
                *out = (char)strtol(buf, NULL, 16);
                return pos + 2 + i;
            }
        }
        /* Unicode \uHHHH */
        if (src[pos+1] == 'u' && isxdigit((unsigned char)src[pos+2])) {
            char buf[5] = {0};
            int i = 0;
            while (i < 4 && isxdigit((unsigned char)src[pos+2+i])) {
                buf[i] = src[pos+2+i];
                i++;
            }
            if (i > 0) {
                unsigned int cp = strtol(buf, NULL, 16);
                /* Simple UTF-8 encoding */
                if (cp < 0x80) {
                    *out = (char)cp;
                    return pos + 2 + i;
                } else if (cp < 0x800) {
                    out[0] = 0xC0 | (cp >> 6);
                    out[1] = 0x80 | (cp & 0x3F);
                    *written = 2;
                    return pos + 2 + i;
                } else {
                    out[0] = 0xE0 | (cp >> 12);
                    out[1] = 0x80 | ((cp >> 6) & 0x3F);
                    out[2] = 0x80 | (cp & 0x3F);
                    *written = 3;
                    return pos + 2 + i;
                }
            }
        }
    }
    *out = c;
    return pos + 1;
}

char *bcl_unescape(const char *src, size_t len, size_t *out_len) {
    char *out = bcl_xmalloc(NULL, len + 1);
    size_t out_pos = 0;

    for (size_t i = 0; i < len; i++) {
        if (src[i] == '\\' && i + 1 < len) {
            size_t written = 0;
            i = process_escape(src, i, &out[out_pos], &written) - 1;
            out_pos += written;
        } else {
            out[out_pos++] = src[i];
        }
    }

    out[out_pos] = '\0';
    if (out_len) {
        *out_len = out_pos;
    }
    return out;
}

static int lexer_col_from_offset(const bcl_lexer_t *lexer, size_t offset) {
    size_t col = 1;

    while (offset > 0 && lexer->src[offset - 1] != '\n') {
        offset--;
        col++;
    }

    return (int)col;
}

static __attribute__((noreturn)) void lexer_raise(bcl_lexer_t *lexer, int line, size_t offset, const char *code, const char *fmt, ...) {
    va_list args;
    char buf[512];

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (!lexer->interp) {
        fputs(buf, stderr);
        fputc('\n', stderr);
        abort();
    }

    lexer->interp->error_line = line;
    lexer->interp->error_col = lexer_col_from_offset(lexer, offset);
    lexer->interp->error_has_location = 1;
    bcl_raise_code(lexer->interp, "LEX", 'F', code, "%s", buf);
}

void bcl_lexer_init(bcl_lexer_t *lexer, bcl_interp_t *interp, const char *src) {
    lexer->interp = interp;
    lexer->src = src;
    lexer->pos = 0;
    lexer->len = strlen(src);
    lexer->line = 1;
    lexer->in_string = 0;
    lexer->in_subcmd = 0;
    lexer->subcmd_depth = 0;
    lexer->parse_depth = 0;
}

/* Caracteres validos dentro de un nombre de variable tras el '$'.
 *
 * Tiene que decir exactamente lo mismo que is_var_name_char() en eval.c: esta
 * se usa cuando la palabra empieza por '$' y el lexer emite un TOK_VARREF, y
 * aquella cuando el '$' aparece dentro de una palabra o de una cadena. Si las
 * dos divergen, la misma referencia se resuelve distinto segun donde este
 * escrita.
 *
 * Vease BCL_kernel_mod_02: '\', '.', '-' y '/' salieron de la lista porque
 * hacian que el nombre se comiera texto que no era el nombre, siempre en
 * silencio. */
static int is_word_char(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == ':';
}

static int lexer_skip_quoted(bcl_lexer_t *lexer) {
    int escape = 0;

    lexer->pos++;
    while (lexer->pos < lexer->len) {
        char ch = lexer->src[lexer->pos];

        if (!escape && ch == '"') {
            lexer->pos++;
            return 1;
        }
        if (ch == '\n') {
            lexer->line++;
        }
        escape = (!escape && ch == '\\');
        lexer->pos++;
    }
    return 0;
}

static int lexer_scan_subcmd(bcl_lexer_t *lexer, size_t *inner_start, size_t *inner_len) {
    int depth = 1;

    lexer->pos++;
    *inner_start = lexer->pos;

    while (lexer->pos < lexer->len && depth > 0) {
        char ch = lexer->src[lexer->pos];

        if (ch == '"') {
            if (!lexer_skip_quoted(lexer)) {
                break;
            }
            continue;
        }
        if (ch == '#') {
            while (lexer->pos < lexer->len && lexer->src[lexer->pos] != '\n') {
                lexer->pos++;
            }
            continue;
        }
        if (ch == '\n') {
            lexer->line++;
        } else if (ch == '[') {
            depth++;
        } else if (ch == ']') {
            depth--;
        }
        lexer->pos++;
    }

    if (depth > 0) {
        *inner_len = 0;
        return 0;
    }

    *inner_len = lexer->pos - *inner_start - 1;
    return 1;
}

bcl_token_t bcl_lexer_next(bcl_lexer_t *lexer) {
    bcl_token_t tok;
    tok.text = NULL;
    tok.type = TOK_ERROR;
    tok.line = lexer->line;
    tok.start = lexer->pos;
    tok.end = lexer->pos;

    if (lexer->pos >= lexer->len) {
        tok.type = TOK_EOF;
        tok.start = lexer->pos;
        tok.end = lexer->pos;
        return tok;
    }

    /* Skip whitespace */
    while (lexer->pos < lexer->len) {
        char c = lexer->src[lexer->pos];
        if (c == ' ' || c == '\t' || c == '\r') {
            lexer->pos++;
            continue;
        }
        if (c == '\n') {
            lexer->pos++;
            lexer->line++;
            if (!lexer->in_string && !lexer->in_subcmd) {
                tok.type = TOK_NEWLINE;
                tok.start = lexer->pos - 1;
                tok.end = lexer->pos;
                return tok;
            }
            continue;
        }
        break;
    }

    if (lexer->pos >= lexer->len) {
        tok.type = TOK_EOF;
        tok.start = lexer->pos;
        tok.end = lexer->pos;
        return tok;
    }

    char c = lexer->src[lexer->pos];
    tok.start = lexer->pos;

    /* Comment */
    if (c == '#' && !lexer->in_string) {
        while (lexer->pos < lexer->len && lexer->src[lexer->pos] != '\n') {
            lexer->pos++;
        }
        return bcl_lexer_next(lexer);
    }

    /* String */
    if (c == '"') {
        size_t start;
        size_t end;
        int escape = 0;

        lexer->pos++;
        start = lexer->pos;

        while (lexer->pos < lexer->len) {
            char ch = lexer->src[lexer->pos];
            if (!escape && ch == '"') {
                end = lexer->pos;
                lexer->pos++;
                tok.text = bcl_xmemdup0(lexer->interp, lexer->src + start, end - start);
                tok.type = TOK_STRING;
                tok.end = lexer->pos;
                return tok;
            }
            if (ch == '\n') {
                lexer->line++;
            }
            escape = (!escape && ch == '\\');
            lexer->pos++;
        }

        if (!lexer->interp) {
            tok.type = TOK_ERROR;
            tok.text = bcl_xstrdup(NULL, "unterminated string literal");
            tok.end = lexer->pos;
            return tok;
        }
        lexer_raise(lexer, tok.line, tok.start, "UNTERMSTR", "Unterminated string literal");
    }

    /* Command substitution [...] */
    if (c == '[' && !lexer->in_string) {
        size_t start;
        size_t inner_len;

        lexer->subcmd_depth++;
        if (!lexer_scan_subcmd(lexer, &start, &inner_len)) {
            if (!lexer->interp) {
                tok.type = TOK_ERROR;
                tok.text = bcl_xstrdup(NULL, "unterminated command substitution");
                tok.end = lexer->pos;
                return tok;
            }
            lexer_raise(lexer, tok.line, tok.start, "UNTERMSUB", "Unterminated command substitution");
        }
        tok.text = bcl_xmemdup0(lexer->interp, lexer->src + start, inner_len);
        tok.type = TOK_SUBCMD;
        tok.end = lexer->pos;
        return tok;
    }

    /* Variable reference $ */
    if (c == '$' && !lexer->in_string) {
        lexer->pos++;
        size_t start = lexer->pos;
        while (lexer->pos < lexer->len && is_word_char(lexer->src[lexer->pos])) {
            lexer->pos++;
        }
        if (lexer->pos < lexer->len && lexer->src[lexer->pos] == '(') {
            int depth = 1;
            lexer->pos++;
            while (lexer->pos < lexer->len && depth > 0) {
                char ch = lexer->src[lexer->pos];
                if (ch == '"') {
                    if (!lexer_skip_quoted(lexer)) {
                        break;
                    }
                    continue;
                }
                if (ch == '[') {
                    size_t sub_start;
                    size_t sub_len;

                    if (!lexer_scan_subcmd(lexer, &sub_start, &sub_len)) {
                        break;
                    }
                    continue;
                }
                if (ch == '(') {
                    depth++;
                } else if (ch == ')') {
                    depth--;
                }
                if (ch == '\n') {
                    lexer->line++;
                }
                lexer->pos++;
            }
            if (depth > 0) {
                if (!lexer->interp) {
                    tok.type = TOK_ERROR;
                    tok.text = bcl_xstrdup(NULL, "unterminated variable reference");
                    tok.end = lexer->pos;
                    return tok;
                }
                lexer_raise(lexer, tok.line, tok.start, "UNTERMVAR", "Unterminated variable reference");
            }
        }
        size_t len = lexer->pos - start;
        tok.text = bcl_xmemdup0(lexer->interp, lexer->src + start, len);
        tok.type = TOK_VARREF;
        tok.end = lexer->pos;
        return tok;
    }

    /* Word */
    size_t start = lexer->pos;
    while (lexer->pos < lexer->len) {
        char ch = lexer->src[lexer->pos];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' ||
            ch == '[' || ch == ']' || ch == '"' || ch == '#' || ch == '$') {
            break;
        }
        lexer->pos++;
    }

    size_t len = lexer->pos - start;
    if (len > 0) {
        tok.text = bcl_xmemdup0(lexer->interp, lexer->src + start, len);
        tok.type = TOK_WORD;
        tok.end = lexer->pos;
    } else {
        if (!lexer->interp) {
            lexer->pos++;
            tok.type = TOK_ERROR;
            tok.text = bcl_xstrdup(NULL, "unexpected character");
            tok.end = lexer->pos;
            return tok;
        }
        lexer_raise(lexer, tok.line, tok.start, "BADCHAR", "Unexpected character: %c", c);
    }

    return tok;
}

void bcl_lexer_free(bcl_lexer_t *lexer) {
    (void)lexer;  /* nothing to free */
}
