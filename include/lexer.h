/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Rafael Seguí Revert. See LICENSE. */
/**
 * BCL Lexer/Tokenizer
 */

#ifndef BCL_LEXER_H
#define BCL_LEXER_H

#include "bcl.h"

/* Token types */
typedef enum {
    TOK_WORD,       /* word or command */
    TOK_STRING,     /* quoted string */
    TOK_SUBCMD,     /* [...] command substitution */
    TOK_VARREF,     /* $variable */
    TOK_NEWLINE,    /* end of statement */
    TOK_EOF,
    TOK_ERROR
} bcl_toktype_t;

/* Token */
typedef struct {
    bcl_toktype_t type;
    char *text;     /* token content without delimiters */
    int line;       /* line number for error messages */
    size_t start;   /* byte offset of token start in source */
    size_t end;     /* byte offset just after token end */
} bcl_token_t;

/* Lexer state */
typedef struct {
    bcl_interp_t *interp;
    const char *src;
    size_t pos;
    size_t len;
    int line;
    int in_string;      /* processing inside quotes */
    int in_subcmd;      /* processing inside [...] */
    int subcmd_depth;   /* bracket nesting level */
    int parse_depth;    /* recursive block parser depth */
} bcl_lexer_t;

/* Lexer functions */
void bcl_lexer_init(bcl_lexer_t *lexer, bcl_interp_t *interp, const char *src);
bcl_token_t bcl_lexer_next(bcl_lexer_t *lexer);
void bcl_lexer_free(bcl_lexer_t *lexer);

/* Escape sequences */
char *bcl_unescape(const char *src, size_t len, size_t *out_len);

#endif /* BCL_LEXER_H */
