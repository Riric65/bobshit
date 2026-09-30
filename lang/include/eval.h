/* eval.h — tree walking interpreter entry points. */
#ifndef BOBSHIT_EVAL_H
#define BOBSHIT_EVAL_H

#include "ast.h"
#include "value.h"

/* Call depth cap (see docs: guard anti stack overflow). */
#define BS_MAX_FRAMES 256
/* Loop iteration cap. */
#define BS_MAX_ITERATIONS 1000000L

/* Create the global environment (natives are pre-bound). */
Env *eval_global_env(void);

/* Evaluate a parsed program. */
Value *eval_run(Node *prog, Env *env);

/* Lex + parse + evaluate `src` in `env`. Returns the last value or nil. */
Value *eval_source(const char *src, Env *env);

/* Call any callable value (used by natives such as sort()). */
Value *eval_call(Value *callee, Value **args, int nargs, int line);

/* Read the whole of `path` into a NUL terminated buffer (or NULL). */
char *eval_read_file(const char *path);

#endif /* BOBSHIT_EVAL_H */
