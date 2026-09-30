/* parser.h */
#ifndef BOBSHIT_PARSER_H
#define BOBSHIT_PARSER_H

#include "ast.h"
#include "lexer.h"

/* Parse a token stream into a program node (kind N_BLOCK).
 * Never fails: in soft mode errors are reported on stderr and the parser
 * resynchronises on the next statement boundary. */
Node *parse_program(TokenStream *ts);

/* Number of parse errors reported during the last parse. */
int parse_error_count(void);

/* Non-zero when the last parse stopped inside an unclosed block. The REPL
 * uses it to decide that it needs more input before evaluating. */
int parse_is_incomplete(void);

#endif /* BOBSHIT_PARSER_H */
