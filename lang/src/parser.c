/* parser.c — recursive descent parser.
 *
 *   stmt    := say | if | while | for | fun | return | break | continue
 *            | try | block | assign | expr
 *   assign  := target ('='|':='|'+='|'-='|'*='|'/='|'%=') (assign | expr)
 *   expr    := or
 *   or      := and (('or'|'||') and)*
 *   and     := cmp (('and'|'&&') cmp)*
 *   cmp     := add (('=='|'!='|'<'|'>'|'<='|'>=') add)*
 *   add     := mul (('+'|'-') mul)*
 *   mul     := unary (('*'|'/'|'%') unary)*
 *   unary   := ('-'|'+'|'!'|'not') unary | postfix
 *   postfix := primary ( '(' args ')' | '[' expr ']' | '.' ident )*
 *   primary := NUM | STR | BOOL | NIL | IDENT | list | map | fun | '(' expr ')'
 *
 * Soft mode: `then`/`do` are optional, `end`/`fin`/`done`/`}` all close a
 * block, commas are optional in `say`/lists/maps, and a missing `end`
 * simply ends the block at the end of the enclosing block.
 */
#include "parser.h"

#include "common.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* open block markers */
enum { B_TOP, B_SOFT, B_BRACE };

static int parse_errors = 0;
static int parse_incomplete = 0;

int parse_error_count(void) { return parse_errors; }
int parse_is_incomplete(void) { return parse_incomplete; }

typedef struct {
    Token *t;
    int n;
    int i;
    int *stack;
    int sp;
    int cap;
    int errors;
} Parser;

/* ------------------------------------------------------------------ */
/* token helpers                                                       */
/* ------------------------------------------------------------------ */

static Token *peek(Parser *p) { return &p->t[p->i]; }
static Token *peek_at(Parser *p, int k) {
    int j = p->i + k;
    if (j >= p->n) j = p->n - 1;
    return &p->t[j];
}
static TokKind kind(Parser *p) { return p->t[p->i].kind; }
static int at(Parser *p, TokKind k) { return p->t[p->i].kind == k; }
static Token *advance(Parser *p) { return &p->t[p->i < p->n - 1 ? p->i++ : p->i]; }
static int accept(Parser *p, TokKind k) {
    if (at(p, k)) { p->i++; return 1; }
    return 0;
}

/* Every recovery point of the parser goes through here. In soft mode the
 * program is going to run anyway, so calling it an error would be a lie: the
 * message says warning, like the lexer's own pardons. Strict mode really does
 * abort, and then it is an error. */
static void p_error(Parser *p, const char *fmt, ...) {
    va_list ap;
    int line = peek(p)->line;
    p->errors++;
    parse_errors++;
    if (bs_quiet) return;
    fflush(stdout);
    fputs(bs_soft ? "bobshit: warning: " : "bobshit: parse error: ", stderr);
    if (line > 0) fprintf(stderr, "line %d: ", line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    if (!bs_soft) {
        fputs("bobshit: strict mode, aborting\n", stderr);
        exit(1);
    }
}

static void skip_nl(Parser *p) {
    while (at(p, T_NL)) p->i++;
}

/* Only skip the newlines that the lexer marked as "the expression was not
 * finished" — this is what makes a trailing comma a trailing comma instead
 * of a greedy grab for the next line. */
static void skip_nl_cont(Parser *p) {
    while (at(p, T_NL) && peek(p)->cont) p->i++;
}

static int can_start_expr(Parser *p) {
    switch (kind(p)) {
        case T_NUM: case T_STR: case T_IDENT:
        case T_KW_TRUE: case T_KW_FALSE: case T_KW_NIL:
        case T_KW_NOT: case T_KW_FUN:
        case T_LBRACKET: case T_LBRACE: case T_LPAREN:
        case T_MINUS: case T_PLUS:
            return 1;
        default:
            return 0;
    }
}

/* Error recovery: after an unclosed bracket, skip forward to the matching
 * closer but give up quickly so a single typo cannot eat the whole file. */
static void skip_to_closer(Parser *p, TokKind closer) {
    int budget = 32;
    while (!at(p, T_EOF) && kind(p) != closer && budget-- > 0) {
        switch (kind(p)) {
            case T_KW_IF: case T_KW_WHILE: case T_KW_FOR: case T_KW_SAY:
            case T_KW_FUN: case T_KW_RETURN: case T_KW_BREAK:
            case T_KW_CONTINUE: case T_KW_TRY: case T_KW_END:
            case T_KW_ELSE: case T_KW_ELIF: case T_KW_CATCH:
            case T_NL: case T_ASSIGN: case T_DEFL:
                return; /* clearly a new statement, stop here */
            default:
                p->i++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* block stack                                                         */
/* ------------------------------------------------------------------ */

static void push_block(Parser *p, int m) {
    if (p->sp == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 32;
        p->stack = bs_realloc(p->stack, sizeof(int) * (size_t)p->cap);
    }
    p->stack[p->sp++] = m;
}

static void pop_block(Parser *p) {
    if (p->sp > 0) p->sp--;
}

/* ------------------------------------------------------------------ */
/* forward declarations                                                */
/* ------------------------------------------------------------------ */

static Node *parse_expr(Parser *p);
static Node *parse_stmt(Parser *p);
static Node *parse_block(Parser *p, int brace);
static Node *parse_fun(Parser *p, int line);

/* ------------------------------------------------------------------ */
/* primary                                                             */
/* ------------------------------------------------------------------ */

static Node *parse_list(Parser *p) {
    int line = peek(p)->line;
    Node *n = node_new(N_LIST, line);
    advance(p); /* [ */
    for (;;) {
        skip_nl(p);
        if (at(p, T_RBRACKET) || at(p, T_EOF)) break;
        Node *item = parse_expr(p);
        if (item) node_push(n, item);
        skip_nl(p);
        if (accept(p, T_COMMA)) {
            skip_nl_cont(p);
            continue;
        }
        if (at(p, T_RBRACKET)) break;
        if (!can_start_expr(p)) break;
        /* soft: the comma was forgotten, keep collecting items */
    }
    if (!accept(p, T_RBRACKET)) {
        p_error(p, "missing ']', list closed implicitly");
        skip_to_closer(p, T_RBRACKET);
        accept(p, T_RBRACKET);
    }
    return n;
}

/* map keys are literals: name, "string" or 42 */
static char *key_from_node(Node *e) {
    if (!e) return NULL;
    switch (e->kind) {
        case N_STR: return bs_strdup(e->str ? e->str : "");
        case N_IDENT: return bs_strdup(e->str ? e->str : "");
        case N_NUM: return bs_dtoa(e->num);
        case N_BOOL: return bs_strdup(e->boolean ? "true" : "false");
        case N_NIL: return bs_strdup("nil");
        default: return NULL;
    }
}

static Node *parse_map(Parser *p) {
    int line = peek(p)->line;
    Node *n = node_new(N_MAP, line);
    advance(p); /* { */
    for (;;) {
        skip_nl(p);
        if (at(p, T_RBRACE) || at(p, T_EOF)) break;
        Node *kexpr = parse_expr(p);
        char *key = key_from_node(kexpr);
        if (!key) {
            p_error(p, "map keys must be a name, a string or a number");
            skip_nl(p);
            if (!accept(p, T_COMMA)) break;
            continue;
        }
        if (!accept(p, T_COLON)) {
            p_error(p, "expected ':' after the map key '%s'", key);
        }
        Node *val = parse_expr(p);
        if (!val) val = node_new(N_NIL, line);
        Node *k = node_new(N_STR, kexpr ? kexpr->line : line);
        k->str = key;
        node_push(n, k);
        node_push(n, val);
        skip_nl(p);
        if (accept(p, T_COMMA)) {
            skip_nl_cont(p);
            continue;
        }
        if (at(p, T_RBRACE)) break;
        if (!can_start_expr(p)) break;
    }
    if (!accept(p, T_RBRACE)) {
        p_error(p, "missing '}', map closed implicitly");
        skip_to_closer(p, T_RBRACE);
        accept(p, T_RBRACE);
    }
    return n;
}

static Node *parse_primary(Parser *p) {
    Token *t = peek(p);
    int line = t->line;
    switch (t->kind) {
        case T_NUM: {
            Node *n = node_new(N_NUM, line);
            n->num = t->num;
            p->i++;
            return n;
        }
        case T_STR: {
            Node *n = node_new(N_STR, line);
            n->str = bs_strndup(t->text ? t->text : "", t->len);
            p->i++;
            return n;
        }
        case T_KW_TRUE: case T_KW_FALSE: {
            Node *n = node_new(N_BOOL, line);
            n->boolean = (t->kind == T_KW_TRUE);
            p->i++;
            return n;
        }
        case T_KW_NIL:
            p->i++;
            return node_new(N_NIL, line);
        case T_IDENT: {
            Node *n = node_new(N_IDENT, line);
            n->str = bs_strdup(t->text);
            p->i++;
            return n;
        }
        case T_LBRACKET: return parse_list(p);
        case T_LBRACE: return parse_map(p);
        case T_LPAREN: {
            p->i++;
            skip_nl(p);
            Node *e = parse_expr(p);
            skip_nl(p);
            if (!accept(p, T_RPAREN)) {
                p_error(p, "missing ')'");
                skip_to_closer(p, T_RPAREN);
                accept(p, T_RPAREN);
            }
            return e ? e : node_new(N_NIL, line);
        }
        case T_KW_FUN: return parse_fun(p, line);
        default:
            p_error(p, "unexpected '%s' in expression", tok_kind_name(t->kind));
            p->i++;
            return node_new(N_NIL, line);
    }
}

static Node *parse_postfix(Parser *p) {
    Node *n = parse_primary(p);
    for (;;) {
        if (at(p, T_LPAREN)) {
            int line = peek(p)->line;
            advance(p);
            Node *call = node_new(N_CALL, line);
            call->a = n;
            for (;;) {
                skip_nl(p);
                if (at(p, T_RPAREN) || at(p, T_EOF)) break;
                Node *arg = parse_expr(p);
                if (arg) node_push(call, arg);
                skip_nl(p);
                if (accept(p, T_COMMA)) continue;
                if (at(p, T_RPAREN)) break;
                if (!can_start_expr(p)) break;
            }
            if (!accept(p, T_RPAREN)) {
                p_error(p, "missing ')' after the arguments");
                skip_to_closer(p, T_RPAREN);
                accept(p, T_RPAREN);
            }
            n = call;
        } else if (at(p, T_LBRACKET)) {
            int line = peek(p)->line;
            advance(p);
            skip_nl(p);
            Node *idx = node_new(N_INDEX, line);
            idx->a = n;
            idx->b = parse_expr(p);
            skip_nl(p);
            if (!accept(p, T_RBRACKET)) {
                p_error(p, "missing ']' after the index");
                skip_to_closer(p, T_RBRACKET);
                accept(p, T_RBRACKET);
            }
            n = idx;
        } else if (at(p, T_DOT)) {
            advance(p);
            Node *dot = node_new(N_DOT, peek(p)->line);
            dot->a = n;
            if (at(p, T_IDENT)) {
                Token *nt = advance(p);
                dot->str = bs_strdup(nt->text);
            } else if (at(p, T_NUM)) {
                Token *nt = advance(p); /* a.0 is a["0"] */
                dot->str = bs_dtoa(nt->num);
            } else {
                p_error(p, "expected a field name after '.'");
                dot->str = bs_strdup("");
            }
            n = dot;
        } else {
            break;
        }
    }
    return n;
}

static Node *parse_unary(Parser *p) {
    /* an operand may live on the next line when the previous line ended on a
     * dangling operator: `x = 1 +\n    2` */
    skip_nl_cont(p);
    TokKind k = kind(p);
    if (k == T_MINUS || k == T_PLUS || k == T_KW_NOT) {
        int line = peek(p)->line;
        advance(p);
        Node *n = node_new(N_UN, line);
        n->op = (int)k;
        n->a = parse_unary(p);
        return n;
    }
    return parse_postfix(p);
}

typedef Node *(*SubFn)(Parser *);

static Node *parse_binary_level(Parser *p, const TokKind *ops, int nops, SubFn sub) {
    Node *left = sub(p);
    for (;;) {
        TokKind k = kind(p);
        int matched = 0;
        for (int i = 0; i < nops; i++)
            if (k == ops[i]) { matched = 1; break; }
        if (!matched) break;
        int line = peek(p)->line;
        advance(p);
        Node *right = sub(p);
        Node *n = node_new(N_BIN, line);
        n->op = (int)k;
        n->a = left;
        n->b = right;
        left = n;
    }
    return left;
}

static Node *parse_mul(Parser *p) {
    static const TokKind ops[] = {T_STAR, T_SLASH, T_PERCENT};
    return parse_binary_level(p, ops, 3, parse_unary);
}

static Node *parse_add(Parser *p) {
    static const TokKind ops[] = {T_PLUS, T_MINUS};
    return parse_binary_level(p, ops, 2, parse_mul);
}

static Node *parse_cmp(Parser *p) {
    static const TokKind ops[] = {T_EQ, T_NE, T_LT, T_GT, T_LE, T_GE};
    return parse_binary_level(p, ops, 6, parse_add);
}

static Node *parse_and(Parser *p) {
    static const TokKind ops[] = {T_KW_AND};
    return parse_binary_level(p, ops, 1, parse_cmp);
}

static Node *parse_or(Parser *p) {
    static const TokKind ops[] = {T_KW_OR};
    return parse_binary_level(p, ops, 1, parse_and);
}

static Node *parse_expr(Parser *p) { return parse_or(p); }

/* ------------------------------------------------------------------ */
/* blocks                                                              */
/* ------------------------------------------------------------------ */

static Node *parse_block(Parser *p, int brace) {
    int line = peek(p)->line;
    Node *blk = node_new(N_BLOCK, line);
    push_block(p, brace ? B_BRACE : B_SOFT);
    if (brace && !accept(p, T_LBRACE)) p_error(p, "expected '{'");

    for (;;) {
        skip_nl(p);
        TokKind k = kind(p);
        if (k == T_EOF) break;
        if (k == T_KW_END || k == T_RBRACE) { /* both closers are accepted */
            p->i++;
            pop_block(p);
            return blk;
        }
        if (k == T_KW_ELSE || k == T_KW_ELIF || k == T_KW_CATCH) {
            pop_block(p);
            return blk; /* the caller consumes the keyword */
        }
        Node *s = parse_stmt(p);
        if (s) node_push(blk, s);
        else skip_nl(p);
    }

    /* end of file: the block was never closed */
    pop_block(p);
    parse_incomplete = 1;
    p_error(p, "missing '%s' at end of file, block closed implicitly", brace ? "}" : "end");
    return blk;
}

/* ------------------------------------------------------------------ */
/* statements                                                          */
/* ------------------------------------------------------------------ */

static void parse_optional_then(Parser *p) {
    if (at(p, T_KW_THEN) || at(p, T_KW_DO)) p->i++;
}

static Node *parse_if(Parser *p) {
    int line = peek(p)->line;
    advance(p); /* if / si */
    Node *head = node_new(N_IF, line);
    Node *n = head;
    n->a = parse_expr(p);
    parse_optional_then(p);
    n->b = parse_block(p, at(p, T_LBRACE));
    for (;;) {
        skip_nl(p);
        if (at(p, T_KW_ELIF)) {
            advance(p);
        } else if (at(p, T_KW_ELSE) && peek_at(p, 1)->kind == T_KW_IF) {
            p->i += 2; /* `else if` is just an elif */
        } else {
            break;
        }
        int l2 = peek(p)->line;
        Node *sub = node_new(N_IF, l2);
        sub->a = parse_expr(p);
        parse_optional_then(p);
        sub->b = parse_block(p, at(p, T_LBRACE));
        n->c = sub; /* the elif chain hangs off `c` */
        n = sub;
    }
    skip_nl(p);
    if (at(p, T_KW_ELSE)) {
        advance(p);
        n->c = parse_block(p, at(p, T_LBRACE));
    }
    return head;
}

static Node *parse_while(Parser *p) {
    int line = peek(p)->line;
    advance(p);
    Node *n = node_new(N_WHILE, line);
    n->a = parse_expr(p);
    parse_optional_then(p);
    n->b = parse_block(p, at(p, T_LBRACE));
    return n;
}

static Node *parse_for(Parser *p) {
    int line = peek(p)->line;
    advance(p); /* for / pour */

    Node *vars = node_new(N_FOREACH, line);
    for (;;) {
        if (at(p, T_IDENT)) {
            Token *t = advance(p);
            node_push_str(vars, t->text);
        } else {
            p_error(p, "expected a loop variable after 'for'");
            break;
        }
        if (accept(p, T_COMMA)) continue;
        break;
    }

    if (!accept(p, T_KW_IN))
        p_error(p, "expected 'in' after the loop variable, found '%s'", tok_kind_name(kind(p)));

    Node *first = parse_expr(p);

    if (at(p, T_KW_TO)) {
        /* range loop: for i in <start> to <end> [step <step>] */
        int l2 = peek(p)->line;
        advance(p);
        Node *n = node_new(N_FOR, l2);
        for (int i = 0; i < vars->nparams; i++) node_push_str(n, vars->params[i]);
        n->a = first; /* start value */
        n->b = parse_expr(p);
        if (!n->b) n->b = node_new(N_NIL, l2);
        if (accept(p, T_KW_STEP)) {
            n->c = parse_expr(p);
            if (!n->c) n->c = node_new(N_NUM, l2);
        } else {
            n->c = node_new(N_NUM, l2);
            n->c->num = 1;
        }
        parse_optional_then(p);
        n->d = parse_block(p, at(p, T_LBRACE));
        ast_free(vars);
        return n;
    }

    Node *n = vars; /* N_FOREACH */
    n->b = first;
    parse_optional_then(p);
    n->c = parse_block(p, at(p, T_LBRACE));
    return n;
}

static Node *parse_fun(Parser *p, int line) {
    advance(p); /* fun / fn / def / fonction */
    Node *n = node_new(N_FUN, line);

    if (at(p, T_IDENT)) {
        Token *t = advance(p);
        n->str = bs_strdup(t->text);
    }

    if (accept(p, T_LPAREN)) {
        for (;;) {
            skip_nl(p);
            if (at(p, T_RPAREN) || at(p, T_EOF)) break;
            if (at(p, T_IDENT)) {
                Token *t = advance(p);
                node_push_str(n, t->text);
            } else {
                p_error(p, "expected a parameter name in the parameter list");
                while (!at(p, T_COMMA) && !at(p, T_RPAREN) && !at(p, T_EOF)) p->i++;
            }
            skip_nl(p);
            if (accept(p, T_COMMA)) continue;
            break;
        }
        if (!accept(p, T_RPAREN)) {
            p_error(p, "missing ')' after the parameter list");
            skip_to_closer(p, T_RPAREN);
            accept(p, T_RPAREN);
        }
    } else {
        while (at(p, T_IDENT)) { /* soft form: fun add a b */
            Token *t = advance(p);
            node_push_str(n, t->text);
        }
    }

    n->a = parse_block(p, at(p, T_LBRACE));
    return n;
}

static Node *parse_try(Parser *p) {
    int line = peek(p)->line;
    advance(p);
    Node *n = node_new(N_TRY, line);
    n->a = parse_block(p, at(p, T_LBRACE));
    skip_nl(p);
    if (at(p, T_KW_CATCH)) {
        advance(p);
        if (at(p, T_IDENT)) {
            Token *t = advance(p);
            n->str = bs_strdup(t->text);
        } else {
            n->str = bs_strdup("err");
        }
        n->d = parse_block(p, at(p, T_LBRACE));
    }
    return n;
}

static Node *parse_say(Parser *p) {
    int line = peek(p)->line;
    advance(p);
    Node *n = node_new(N_SAY, line);
    for (;;) {
        skip_nl(p);
        if (!can_start_expr(p)) break;
        Node *e = parse_expr(p);
        if (e) node_push(n, e);
        if (accept(p, T_COMMA)) {
            skip_nl_cont(p);
            if (!can_start_expr(p)) { n->op = 1; break; } /* trailing comma */
            continue;
        }
        if (can_start_expr(p)) continue; /* soft: commas are optional */
        break;
    }
    if (n->nitems == 0) p_error(p, "'say' without a value");
    return n;
}

static int is_assign_target(Node *n) {
    return n && (n->kind == N_IDENT || n->kind == N_INDEX || n->kind == N_DOT);
}

static int is_compound_assign(TokKind k) {
    return k == T_ADDEQ || k == T_SUBEQ || k == T_MULEQ || k == T_DIVEQ || k == T_MODEQ;
}

static Node *parse_stmt(Parser *p) {
    skip_nl(p);
    switch (kind(p)) {
        case T_EOF: return NULL;
        case T_KW_IF: return parse_if(p);
        case T_KW_WHILE: return parse_while(p);
        case T_KW_FOR: return parse_for(p);
        case T_KW_FUN: {
            int line = peek(p)->line;
            /* a named `fun` is a declaration, an anonymous one is a value */
            int named = peek_at(p, 1)->kind == T_IDENT;
            Node *f = parse_fun(p, line);
            if (named) return f;
            Node *n = node_new(N_EXPR, line);
            n->a = f;
            return n;
        }
        case T_KW_TRY: return parse_try(p);
        case T_KW_SAY: return parse_say(p);
        case T_KW_RETURN: {
            int line = peek(p)->line;
            advance(p);
            Node *n = node_new(N_RETURN, line);
            if (can_start_expr(p)) n->a = parse_expr(p);
            return n;
        }
        case T_KW_BREAK:
            advance(p);
            return node_new(N_BREAK, peek(p)->line);
        case T_KW_CONTINUE:
            advance(p);
            return node_new(N_CONTINUE, peek(p)->line);
        case T_LBRACE: return parse_block(p, 1);
        case T_KW_END: case T_RBRACE: case T_KW_ELSE:
        case T_KW_ELIF: case T_KW_CATCH: {
            p_error(p, "unexpected '%s' outside of a block, skipped", tok_kind_name(kind(p)));
            p->i++;
            return NULL;
        }
        default: break;
    }

    int line = peek(p)->line;
    Node *e = parse_expr(p);

    if (at(p, T_ASSIGN) || at(p, T_DEFL) || is_compound_assign(kind(p))) {
        TokKind op = kind(p);
        if (!is_assign_target(e)) {
            p_error(p, "cannot assign to this expression");
            return e;
        }
        advance(p);
        Node *n = node_new(N_ASSIGN, line);
        n->op = (int)op;
        n->a = e;
        if (at(p, T_ASSIGN) || at(p, T_DEFL)) n->b = parse_stmt(p); /* a = b = 1 */
        else n->b = parse_expr(p);
        if (!n->b) n->b = node_new(N_NIL, line);
        return n;
    }

    Node *n = node_new(N_EXPR, line);
    n->a = e;
    return n;
}

Node *parse_program(TokenStream *ts) {
    Parser p;
    memset(&p, 0, sizeof p);
    p.t = ts->items;
    p.n = ts->count;
    parse_errors = 0;
    parse_incomplete = 0;

    Node *prog = node_new(N_BLOCK, 1);
    push_block(&p, B_TOP);

    for (;;) {
        skip_nl(&p);
        if (at(&p, T_EOF)) break;
        int before = p.i;
        Node *s = parse_stmt(&p);
        if (s) node_push(prog, s);
        if (p.i == before) p.i++; /* never spin */
    }

    pop_block(&p);
    free(p.stack);
    return prog;
}
