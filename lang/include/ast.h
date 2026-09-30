/* ast.h — AST node definitions produced by the parser. */
#ifndef BOBSHIT_AST_H
#define BOBSHIT_AST_H

#include <stddef.h>

typedef enum {
    /* --- expressions --- */
    N_NUM,
    N_STR,
    N_BOOL,
    N_NIL,
    N_IDENT,
    N_LIST,
    N_MAP,
    N_INDEX,   /* a[b]        -> a, b            */
    N_DOT,     /* a.b         -> a, key(str)     */
    N_CALL,    /* a(b, c)     -> a, b, c (items) */
    N_BIN,     /* a + b       -> a, b            */
    N_UN,      /* -a, not a   -> a                */
    N_FUN,     /* fun (decl or literal): str=name, params, a=body */
    N_ASSIGN,  /* a = b       -> a(target), b    */

    /* --- statements --- */
    N_BLOCK,
    N_SAY,     /* items */
    N_EXPR,    /* a */
    N_IF,      /* a=cond b=then c=else|elif-chain */
    N_WHILE,   /* a=cond b=body */
    N_FOR,     /* range loop: params=var, a=start, b=end, c=step, d=body */
    N_FOREACH, /* params=loop vars, b=iterable, c=body */
    N_RETURN,  /* a (may be NULL) */
    N_BREAK,
    N_CONTINUE,
    N_TRY      /* a=body b=catch_var(str) c=catch_body(d may be NULL) */
} NodeKind;

typedef struct Node Node;

struct Node {
    NodeKind kind;
    int line;

    /* literals / names */
    double num;
    char *str; /* N_STR payload, N_IDENT name, N_FUN name, N_TRY catch var */
    int boolean;

    /* operators */
    int op; /* TokKind of the operator, see lexer.h */

    /* children */
    Node *a, *b, *c, *d;

    /* list of children (list items, call args, say args, statements,
     * foreach loop variable names) */
    Node **items;
    int nitems;
    int cap_items;

    /* function parameters */
    char **params;
    int nparams;
    int cap_params;
};

/* constructors (all heap allocated, owned by the program tree) */
Node *node_new(NodeKind kind, int line);
void node_push(Node *parent, Node *child);
void node_push_str(Node *parent, const char *s);
void ast_free(Node *n);

/* pretty printer used by `bobshit --ast` */
void ast_dump(Node *n, int indent);

#endif /* BOBSHIT_AST_H */
