/* main.c — command line interface.
 *
 *   bobshit file.shit        run a script
 *   bobshit run file.shit    same thing, spelled explicitly
 *   bobshit -e 'say "hi"'    run a one liner
 *   bobshit -i               interactive REPL
 *   bobshit -s file.shit     strict mode (no forgiveness)
 *   bobshit --ast file.shit  dump the AST
 *   bobshit --tokens file.shit  dump the token stream
 *   bobshit --hold file.shit run, then wait for Enter: this is what the
 *                            Windows file association uses, so that double
 *                            clicking a .shit does not close instantly
 */
#include "ast.h"
#include "common.h"
#include "eval.h"
#include "gc.h"
#include "lexer.h"
#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
/* LEAN and NOMINMAX keep windows.h from dragging in macros that could shadow
 * an identifier of ours; only SetConsoleOutputCP is wanted from it. */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#endif

static void print_version(void) { printf("bobshit %s\n", BS_VERSION); }

static void print_help(void) {
    printf(
        "bobshit %s — a soft, human first interpreted language\n"
        "\n"
        "usage:\n"
        "  bobshit <file.shit>        run a script\n"
        "  bobshit run <file.shit>   run a script (explicit form)\n"
        "  bobshit -e <code>          run a snippet\n"
        "  bobshit -i                 start the REPL\n"
        "  bobshit -s <file.shit>     strict mode: typos are errors\n"
        "\n"
        "options:\n"
        "  -e, --eval <code>   evaluate a string of BobShit\n"
        "  -i, --interactive   REPL\n"
        "  -s, --strict        disable soft mode\n"
        "      --soft          force soft mode (default)\n"
        "      --hold          wait for Enter before exiting\n"
        "      --ast           print the AST of the input and exit\n"
        "      --tokens        print the token stream of the input and exit\n"
        "  -v, --version       print the version\n"
        "  -h, --help          print this help\n",
        BS_VERSION);
}

/* --hold: keep the console open after the program. On Windows a double
 * clicked .shit would otherwise flash and vanish before the output is read. */
static void hold_open(void) {
#if defined(_WIN32)
    printf("\n[press Enter to close]");
    fflush(stdout);
    getchar();
#endif
}

/* Speculative parse: returns 1 when the source ends inside an open block or
 * an unclosed bracket, in which case the REPL should keep reading. */
static int repl_incomplete(const char *src) {
    bs_quiet = 1;
    TokenStream *ts = lex(src);
    Node *prog = parse_program(ts);
    int incomplete = parse_is_incomplete() || ts->unclosed > 0;
    tokens_free(ts);
    ast_free(prog);
    bs_quiet = 0;
    return incomplete;
}

static int run_repl(Env *env) {
    Str pending;
    str_init(&pending);
    /* ASTs of evaluated inputs must outlive the call: a `fun` defined in one
     * line is still callable on the next one, so they are kept until exit. */
    Node **asts = NULL;
    int nasts = 0, capasts = 0;

    printf("bobshit %s — type `exit` to quit, `help` for a reminder\n", BS_VERSION);
    for (;;) {
        fputs(pending.len ? "... " : ">>> ", stdout);
        fflush(stdout);
        char line[4096];
        if (!fgets(line, sizeof line, stdin)) {
            fputc('\n', stdout);
            break;
        }
        char *trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
        size_t tl = strlen(trimmed);
        while (tl > 0 && (trimmed[tl - 1] == '\n' || trimmed[tl - 1] == '\r')) trimmed[--tl] = '\0';

        if (pending.len == 0) {
            if (strcmp(trimmed, "exit") == 0 || strcmp(trimmed, "quit") == 0) break;
            if (strcmp(trimmed, "help") == 0) {
                printf(
                    "say <a b>       print values (commas optional)\n"
                    "x = 1           assignment, `:=` also works\n"
                    "if/elif/else    ... end\n"
                    "while/for       ... end\n"
                    "fun name(a, b)  ... end\n"
                    "multi line:     an unclosed block or bracket keeps reading\n");
                continue;
            }
        }
        str_add(&pending, trimmed);
        str_addc(&pending, '\n');

        if (repl_incomplete(pending.data)) continue; /* keep reading */

        TokenStream *ts = lex(pending.data);
        Node *prog = parse_program(ts);
        eval_run(prog, env);
        tokens_free(ts);
        if (nasts == capasts) {
            capasts = capasts ? capasts * 2 : 16;
            asts = bs_realloc(asts, sizeof(Node *) * (size_t)capasts);
        }
        asts[nasts++] = prog;
        str_clear(&pending);
    }
    for (int i = 0; i < nasts; i++) ast_free(asts[i]);
    free(asts);
    str_free(&pending);
    return 0;
}

/* On Windows the C runtime opens stdout in text mode and rewrites every "\n"
 * into "\r\n". Redirected output would then differ from every other platform,
 * which breaks the regression suite, pipes, and anything that diffs what the
 * interpreter printed. Binary mode keeps the bytes identical, but a console
 * would then step down one line without coming back to the first column, so
 * the translation is only disabled when the output is a file or a pipe and not
 * a console. The console code page is also switched to UTF-8, otherwise
 * accented characters come out mangled on a real terminal. */
static void use_native_output(void) {
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
    if (!_isatty(_fileno(stdout))) _setmode(_fileno(stdout), _O_BINARY);
    if (!_isatty(_fileno(stderr))) _setmode(_fileno(stderr), _O_BINARY);
#endif
}

int main(int argc, char **argv) {
    use_native_output();
    bs_soft = 1;

    const char *file = NULL;
    const char *code = NULL;
    int repl = 0, dump_ast = 0, dump_tokens = 0, hold = 0;
    int gc_flag = -1;
    Env *env = NULL;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        /* `bobshit run foo.shit`: the explicit form people type out of habit */
        if (i == 1 && strcmp(a, "run") == 0) continue;
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            print_help();
            return 0;
        }
        if (strcmp(a, "--gc") == 0) {
            gc_flag = 1;
            continue;
        }
        if (strcmp(a, "--no-gc") == 0) {
            gc_flag = 0;
            continue;
        }
        if (strcmp(a, "-v") == 0 || strcmp(a, "--version") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(a, "-s") == 0 || strcmp(a, "--strict") == 0) {
            bs_soft = 0;
            continue;
        }
        if (strcmp(a, "--soft") == 0) {
            bs_soft = 1;
            continue;
        }
        if (strcmp(a, "--hold") == 0) {
            hold = 1;
            continue;
        }
        if (strcmp(a, "-i") == 0 || strcmp(a, "--interactive") == 0) {
            repl = 1;
            continue;
        }
        if (strcmp(a, "--ast") == 0) {
            dump_ast = 1;
            continue;
        }
        if (strcmp(a, "--tokens") == 0) {
            dump_tokens = 1;
            continue;
        }
        if (strcmp(a, "-e") == 0 || strcmp(a, "--eval") == 0) {
            if (i + 1 >= argc) bs_fatal("%s needs a code argument", a);
            code = argv[++i];
            continue;
        }
        if (a[0] == '-' && a[1]) {
            bs_fatal("unknown option '%s' (try --help)", a);
        }
        file = a;
    }

    if (repl) {
        env = eval_global_env();
        gc_init(gc_flag == 1);
        return run_repl(env);
    }

    if (code && file) bs_fatal("give either a file or -e, not both");

    if (dump_tokens) {
        char *src;
        if (code) {
            src = bs_strdup(code);
        } else if (file) {
            src = eval_read_file(file);
            if (!src) bs_fatal("cannot read '%s'", file);
        } else {
            bs_fatal("--tokens needs a file or -e");
            return 1;
        }
        TokenStream *ts = lex(src);
        for (int i = 0; i < ts->count; i++) {
            Token *t = &ts->items[i];
            char *num = t->kind == T_NUM ? bs_dtoa(t->num) : NULL;
            const char *payload = t->text ? t->text : (num ? num : "");
            printf("%4d  %-14s %s\n", t->line, tok_kind_name(t->kind), payload);
            free(num);
        }
        tokens_free(ts);
        free(src);
        return 0;
    }

    char *src;
    if (code) {
        src = bs_strdup(code);
    } else if (file) {
        src = eval_read_file(file);
        if (!src) bs_fatal("cannot read '%s'", file);
    } else {
        print_help();
        return 0;
    }

    TokenStream *ts = lex(src);
    Node *prog = parse_program(ts);

    if (dump_ast) {
        ast_dump(prog, 0);
        tokens_free(ts);
        ast_free(prog);
        free(src);
        return 0;
    }

    env = eval_global_env();
    gc_init(gc_flag == 1);
    eval_run(prog, env);

    if (bs_warn_count > 0 && bs_soft)
        fprintf(stderr, "bobshit: %lu warning(s), run with -s to turn them into errors\n",
                bs_warn_count);

    tokens_free(ts);
    ast_free(prog);
    free(src);
    if (hold) hold_open();
    return 0;
}
