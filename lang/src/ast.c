/* ast.c — node constructors, dumper, destructor. */
#include "ast.h"

#include "common.h"
#include "lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Node *node_new(NodeKind kind, int line) {
    Node *n = bs_calloc(1, sizeof(Node));
    n->kind = kind;
    n->line = line;
    return n;
}

void node_push(Node *parent, Node *child) {
    if (!parent || !child) return;
    if (parent->nitems == parent->cap_items) {
        parent->cap_items = parent->cap_items ? parent->cap_items * 2 : 4;
        parent->items = bs_realloc(parent->items, sizeof(Node *) * (size_t)parent->cap_items);
    }
    parent->items[parent->nitems++] = child;
}

void node_push_str(Node *parent, const char *s) {
    if (!parent || !s) return;
    int cap = parent->cap_params ? parent->cap_params : 4;
    while (parent->nparams >= cap) cap *= 2;
    parent->params = bs_realloc(parent->params, sizeof(char *) * (size_t)cap);
    parent->cap_params = cap;
    parent->params[parent->nparams++] = bs_strdup(s);
}

static const char *kind_name(NodeKind k) {
    switch (k) {
        case N_NUM: return "num";
        case N_STR: return "str";
        case N_BOOL: return "bool";
        case N_NIL: return "nil";
        case N_IDENT: return "ident";
        case N_LIST: return "list";
        case N_MAP: return "map";
        case N_INDEX: return "index";
        case N_DOT: return "dot";
        case N_CALL: return "call";
        case N_BIN: return "binop";
        case N_UN: return "unop";
        case N_FUN: return "fun";
        case N_ASSIGN: return "assign";
        case N_BLOCK: return "block";
        case N_SAY: return "say";
        case N_EXPR: return "expr";
        case N_IF: return "if";
        case N_WHILE: return "while";
        case N_FOR: return "for";
        case N_FOREACH: return "foreach";
        case N_RETURN: return "return";
        case N_BREAK: return "break";
        case N_CONTINUE: return "continue";
        case N_TRY: return "try";
    }
    return "?";
}

static void dump_child(Node *n, const char *label, int indent);

static void dump_node(Node *n, int indent) {
    if (!n) return;
    char pad[64];
    int width = indent * 2;
    if (width > 60) width = 60;
    memset(pad, ' ', (size_t)width);
    pad[width] = '\0';

    switch (n->kind) {
        case N_NUM: {
            char *s = bs_dtoa(n->num);
            printf("%s%s(%s)\n", pad, kind_name(n->kind), s);
            free(s);
            break;
        }
        case N_STR:
            printf("%s%s(%s)\n", pad, kind_name(n->kind), n->str ? n->str : "");
            break;
        case N_BOOL:
            printf("%s%s(%s)\n", pad, kind_name(n->kind), n->boolean ? "true" : "false");
            break;
        case N_NIL:
            printf("%s%s\n", pad, kind_name(n->kind));
            break;
        case N_IDENT:
            printf("%s%s(%s)\n", pad, kind_name(n->kind), n->str ? n->str : "?");
            break;
        case N_LIST:
            printf("%s%s[%d]\n", pad, kind_name(n->kind), n->nitems);
            for (int i = 0; i < n->nitems; i++) dump_child(n->items[i], "item", indent + 1);
            break;
        case N_MAP:
            printf("%s%s[%d]\n", pad, kind_name(n->kind), n->nitems);
            for (int i = 0; i < n->nitems; i += 2) {
                dump_child(n->items[i], "key", indent + 1);
                dump_child(n->items[i + 1], "value", indent + 1);
            }
            break;
        case N_BIN:
            printf("%s%s(%s)\n", pad, kind_name(n->kind), tok_kind_name((TokKind)n->op));
            dump_child(n->a, "lhs", indent + 1);
            dump_child(n->b, "rhs", indent + 1);
            break;
        case N_UN:
            printf("%s%s(%s)\n", pad, kind_name(n->kind), tok_kind_name((TokKind)n->op));
            dump_child(n->a, "operand", indent + 1);
            break;
        case N_ASSIGN:
            printf("%s%s(%s)\n", pad, kind_name(n->kind), tok_kind_name((TokKind)n->op));
            dump_child(n->a, "target", indent + 1);
            dump_child(n->b, "value", indent + 1);
            break;
        case N_INDEX:
            printf("%s%s\n", pad, kind_name(n->kind));
            dump_child(n->a, "object", indent + 1);
            dump_child(n->b, "key", indent + 1);
            break;
        case N_DOT:
            printf("%s%s(.%s)\n", pad, kind_name(n->kind), n->str ? n->str : "?");
            dump_child(n->a, "object", indent + 1);
            break;
        case N_CALL:
            printf("%s%s[%d]\n", pad, kind_name(n->kind), n->nitems);
            dump_child(n->a, "callee", indent + 1);
            for (int i = 0; i < n->nitems; i++) dump_child(n->items[i], "arg", indent + 1);
            break;
        case N_FUN:
            printf("%s%s(%s)[%d params]\n", pad, kind_name(n->kind),
                   n->str ? n->str : "<anon>", n->nparams);
            for (int i = 0; i < n->nparams; i++)
                printf("%s  param %s\n", pad, n->params[i]);
            dump_child(n->a, "body", indent + 1);
            break;
        case N_BLOCK:
            printf("%s%s[%d]\n", pad, kind_name(n->kind), n->nitems);
            for (int i = 0; i < n->nitems; i++) dump_child(n->items[i], "stmt", indent + 1);
            break;
        case N_SAY:
            printf("%ssay[%d]\n", pad, n->nitems);
            for (int i = 0; i < n->nitems; i++) dump_child(n->items[i], "arg", indent + 1);
            break;
        case N_EXPR:
            printf("%sexpr\n", pad);
            dump_child(n->a, "value", indent + 1);
            break;
        case N_IF:
            printf("%s%s\n", pad, kind_name(n->kind));
            dump_child(n->a, "cond", indent + 1);
            dump_child(n->b, "then", indent + 1);
            dump_child(n->c, "else", indent + 1);
            break;
        case N_WHILE:
            printf("%swhile\n", pad);
            dump_child(n->a, "cond", indent + 1);
            dump_child(n->b, "body", indent + 1);
            break;
        case N_FOR:
            printf("%sfor\n", pad);
            dump_child(n->a, "init", indent + 1);
            dump_child(n->b, "cond", indent + 1);
            dump_child(n->c, "step", indent + 1);
            dump_child(n->d, "body", indent + 1);
            break;
        case N_FOREACH:
            printf("%sforeach(%s)\n", pad, n->str ? n->str : "?");
            dump_child(n->b, "iterable", indent + 1);
            dump_child(n->c, "body", indent + 1);
            break;
        case N_RETURN:
            printf("%sreturn\n", pad);
            dump_child(n->a, "value", indent + 1);
            break;
        case N_BREAK: printf("%sbreak\n", pad); break;
        case N_CONTINUE: printf("%scontinue\n", pad); break;
        case N_TRY:
            printf("%stry\n", pad);
            dump_child(n->a, "body", indent + 1);
            dump_child(n->d, "catch", indent + 1);
            break;
    }
}

static void dump_child(Node *n, const char *label, int indent) {
    if (!n) return;
    printf("%*s%s:\n", indent * 2, "", label);
    dump_node(n, indent + 1);
}

void ast_dump(Node *n, int indent) { dump_node(n, indent); }

void ast_free(Node *n) {
    if (!n) return;
    ast_free(n->a);
    ast_free(n->b);
    ast_free(n->c);
    ast_free(n->d);
    for (int i = 0; i < n->nitems; i++) ast_free(n->items[i]);
    for (int i = 0; i < n->nparams; i++) free(n->params[i]);
    free(n->items);
    free(n->params);
    free(n->str);
    free(n);
}
