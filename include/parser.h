/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Rafael Seguí Revert. See LICENSE. */
/**
 * BCL Parser - AST nodes
 */

#ifndef BCL_PARSER_H
#define BCL_PARSER_H

#include "bcl.h"
#include "lexer.h"

/* AST Node types */
typedef enum {
    NODE_CMD,       /* command invocation */
    NODE_IF_THEN,   /* conditional */
    NODE_IF_DO,     /* loop */
    NODE_PROC,      /* procedure definition */
    NODE_TRY,       /* try/catch block */
    NODE_RETURN,
    NODE_BREAK,
    NODE_NEXT,
    NODE_ERROR,
    NODE_EXIT,
    NODE_LITERAL,   /* string literal */
    NODE_STRING,    /* quoted string with interpolation */
    NODE_WORD,      /* bare word with interpolation */
    NODE_VARREF,    /* variable reference $name */
    NODE_SUBCMD,    /* command substitution [...] */
    NODE_BLOCK,     /* sequence of nodes */
    NODE_CONDITION  /* condition expression */
} bcl_nodetype_t;

typedef struct bcl_if_clause {
    bcl_node_t *cond;
    bcl_node_t *body;
    struct bcl_if_clause *next;
} bcl_if_clause_t;

typedef struct {
    bcl_if_clause_t *clauses;
    bcl_node_t *else_body;
} bcl_if_data_t;

typedef struct {
    bcl_node_t *cond;
    bcl_node_t *body;
    bcl_node_t *else_body;
} bcl_loop_data_t;

typedef struct {
    char *name;
    int optional;
} bcl_param_t;

typedef struct {
    bcl_param_t *params;
    int nparams;
    int nrequired;
    char *invalid_param;
    char *source_name;
    bcl_node_t *body;
    char *body_src;
} bcl_procdef_t;

typedef struct {
    char *catch_var;
    bcl_node_t *try_body;
    bcl_node_t *catch_body;
} bcl_try_data_t;

/* AST Node */
struct bcl_node {
    bcl_nodetype_t type;
    bcl_val_t val;       /* for NODE_LITERAL */
    char *name;          /* for NODE_CMD, NODE_VARREF */
    int line;
    int col;
    size_t start;
    size_t end;
    struct bcl_node *children;  /* first child */
    struct bcl_node *next;      /* next node at same level */
    void *extra;         /* type-specific data */
};

/* Parser functions */
bcl_node_t *bcl_parse(bcl_interp_t *interp, bcl_lexer_t *lexer);
bcl_node_t *bcl_parse_line(bcl_interp_t *interp, bcl_lexer_t *lexer);
void bcl_node_free(bcl_node_t *node);
void bcl_parser_cleanup_all(bcl_interp_t *interp);

#endif /* BCL_PARSER_H */
