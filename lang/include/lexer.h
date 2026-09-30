/* lexer.h — source text -> token stream. */
#ifndef BOBSHIT_LEXER_H
#define BOBSHIT_LEXER_H

#include <stddef.h>

typedef enum {
    T_EOF = 0,
    T_NL,   /* soft statement separator (blank lines collapsed) */
    T_NUM,
    T_STR,
    T_IDENT,

    /* keywords (EN + FR aliases are folded onto the same token) */
    T_KW_IF, T_KW_ELIF, T_KW_ELSE, T_KW_END, T_KW_WHILE, T_KW_FOR, T_KW_IN,
    T_KW_TO, T_KW_STEP, T_KW_FUN, T_KW_RETURN, T_KW_BREAK, T_KW_CONTINUE,
    T_KW_TRY, T_KW_CATCH, T_KW_SAY, T_KW_AND, T_KW_OR, T_KW_NOT, T_KW_TRUE,
    T_KW_FALSE, T_KW_NIL, T_KW_THEN, T_KW_DO,

    /* operators & punctuation */
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PERCENT,
    T_EQ, T_NE, T_LT, T_GT, T_LE, T_GE,
    T_ASSIGN, T_DEFL,
    T_ADDEQ, T_SUBEQ, T_MULEQ, T_DIVEQ, T_MODEQ,
    T_LPAREN, T_RPAREN, T_LBRACKET, T_RBRACKET, T_LBRACE, T_RBRACE,
    T_COMMA, T_DOT, T_COLON, T_SEMI
} TokKind;

typedef struct {
    TokKind kind;
    char *text;  /* T_STR / T_IDENT payload, malloc'd */
    size_t len;  /* T_STR length in bytes */
    double num;  /* T_NUM payload */
    int line;
    int cont;    /* T_NL only: the expression was incomplete, so this line
                  * continues the previous one (used by `say a,` ...) */
} Token;

typedef struct {
    Token *items;
    int count;
    int unclosed; /* number of brackets still open at the end of the input */
} TokenStream;

/* Tokenise `src`. Never fails: in soft mode a malformed tail is closed
 * and warned about, in strict mode a diagnostic is printed and the stream
 * is truncated at the offending byte. */
TokenStream *lex(const char *src);
void tokens_free(TokenStream *ts);
const char *tok_kind_name(TokKind k);

#endif /* BOBSHIT_LEXER_H */
