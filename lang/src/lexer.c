/* lexer.c — hand written scanner for BobShit.
 *
 * Design notes (soft mode):
 *  - hash, double slash and slash-star comments, keywords are
 *    case-insensitive, FR/EN aliases fold onto one token kind.
 *  - newlines are *soft* separators: a newline is emitted only when the
 *    previous line is complete (no dangling operator) and the next line
 *    cannot continue the expression (no leading operator, no unbalanced
 *    bracket). That lets you write
 *        x = 1 +
 *            2
 *    and still lets you write
 *        x = 1
 *        - 2      # two statements, not 1 - 2
 *  - an unterminated string is closed at the end of the line/file.
 *  - junk bytes are skipped with a warning instead of aborting.
 */
#include "lexer.h"

#include "common.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *word;
    TokKind kind;
} Keyword;

static const Keyword KEYWORDS[] = {
    /* control flow */
    {"if", T_KW_IF},        {"si", T_KW_IF},
    {"elif", T_KW_ELIF},    {"elsif", T_KW_ELIF},   {"sinon_si", T_KW_ELIF},
    {"else", T_KW_ELSE},    {"sinon", T_KW_ELSE},
    {"then", T_KW_THEN},    {"alors", T_KW_THEN},
    {"do", T_KW_DO},        {"faire", T_KW_DO},
    {"end", T_KW_END},      {"fin", T_KW_END},       {"done", T_KW_END},
    {"endif", T_KW_END},    {"finon", T_KW_END},
    {"while", T_KW_WHILE},  {"tantque", T_KW_WHILE},
    {"for", T_KW_FOR},      {"pour", T_KW_FOR},
    {"in", T_KW_IN},        {"dans", T_KW_IN},
    {"to", T_KW_TO},        {"jusqua", T_KW_TO},     {"jusqu'a", T_KW_TO},
    {"step", T_KW_STEP},    {"pas", T_KW_STEP},
    {"break", T_KW_BREAK},  {"casse", T_KW_BREAK},   {"stop", T_KW_BREAK},
    {"continue", T_KW_CONTINUE}, {"suivant", T_KW_CONTINUE},
    {"try", T_KW_TRY},      {"essaie", T_KW_TRY},
    {"catch", T_KW_CATCH},  {"attrape", T_KW_CATCH},

    /* functions */
    {"fun", T_KW_FUN},          {"fn", T_KW_FUN},
    {"def", T_KW_FUN},          {"func", T_KW_FUN},
    {"function", T_KW_FUN},     {"fonction", T_KW_FUN},
    {"return", T_KW_RETURN},    {"retour", T_KW_RETURN},
    {"renvoie", T_KW_RETURN},

    /* io */
    {"say", T_KW_SAY},      {"print", T_KW_SAY},   {"echo", T_KW_SAY},
    {"affiche", T_KW_SAY},  {"log", T_KW_SAY},

    /* operators / literals */
    {"and", T_KW_AND},      {"et", T_KW_AND},
    {"or", T_KW_OR},        {"ou", T_KW_OR},
    {"not", T_KW_NOT},      {"non", T_KW_NOT},
    {"true", T_KW_TRUE},    {"vrai", T_KW_TRUE},
    {"false", T_KW_FALSE},  {"faux", T_KW_FALSE},
    {"nil", T_KW_NIL},      {"null", T_KW_NIL},    {"none", T_KW_NIL},
    {"rien", T_KW_NIL},
    {NULL, T_EOF}
};

static TokenStream *ts_new(void) {
    TokenStream *ts = bs_calloc(1, sizeof(TokenStream));
    ts->items = NULL;
    ts->count = 0;
    return ts;
}

static void ts_push(TokenStream *ts, Token t) {
    ts->items = bs_realloc(ts->items, sizeof(Token) * (size_t)(ts->count + 1));
    ts->items[ts->count++] = t;
}

static Token mk(TokKind k, int line) {
    Token t;
    memset(&t, 0, sizeof t);
    t.kind = k;
    t.line = line;
    return t;
}

const char *tok_kind_name(TokKind k) {
    switch (k) {
        case T_EOF: return "end of file";
        case T_NL: return "end of line";
        case T_NUM: return "number";
        case T_STR: return "string";
        case T_IDENT: return "identifier";
        case T_KW_IF: return "if";
        case T_KW_ELIF: return "elif";
        case T_KW_ELSE: return "else";
        case T_KW_END: return "end";
        case T_KW_WHILE: return "while";
        case T_KW_FOR: return "for";
        case T_KW_IN: return "in";
        case T_KW_TO: return "to";
        case T_KW_STEP: return "step";
        case T_KW_FUN: return "fun";
        case T_KW_RETURN: return "return";
        case T_KW_BREAK: return "break";
        case T_KW_CONTINUE: return "continue";
        case T_KW_TRY: return "try";
        case T_KW_CATCH: return "catch";
        case T_KW_SAY: return "say";
        case T_KW_AND: return "and";
        case T_KW_OR: return "or";
        case T_KW_NOT: return "not";
        case T_KW_TRUE: return "true";
        case T_KW_FALSE: return "false";
        case T_KW_NIL: return "nil";
        case T_KW_THEN: return "then";
        case T_KW_DO: return "do";
        case T_PLUS: return "+";
        case T_MINUS: return "-";
        case T_STAR: return "*";
        case T_SLASH: return "/";
        case T_PERCENT: return "%";
        case T_EQ: return "==";
        case T_NE: return "!=";
        case T_LT: return "<";
        case T_GT: return ">";
        case T_LE: return "<=";
        case T_GE: return ">=";
        case T_ASSIGN: return "=";
        case T_DEFL: return ":=";
        case T_ADDEQ: return "+=";
        case T_SUBEQ: return "-=";
        case T_MULEQ: return "*=";
        case T_DIVEQ: return "/=";
        case T_MODEQ: return "%=";
        case T_LPAREN: return "(";
        case T_RPAREN: return ")";
        case T_LBRACKET: return "[";
        case T_RBRACKET: return "]";
        case T_LBRACE: return "{";
        case T_RBRACE: return "}";
        case T_COMMA: return ",";
        case T_DOT: return ".";
        case T_COLON: return ":";
        case T_SEMI: return ";";
    }
    return "?";
}

static TokKind keyword_kind(const char *word) {
    for (int i = 0; KEYWORDS[i].word; i++)
        if (bs_streq_ci(KEYWORDS[i].word, word)) return KEYWORDS[i].kind;
    return T_IDENT;
}

/* ------------------------------------------------------------------ */
/* newline heuristics                                                  */
/* ------------------------------------------------------------------ */

static int tok_is_dangling(TokKind k) {
    switch (k) {
        case T_PLUS: case T_MINUS: case T_STAR: case T_SLASH: case T_PERCENT:
        case T_COMMA: case T_COLON: case T_ASSIGN: case T_DEFL:
        case T_EQ: case T_NE: case T_LT: case T_GT: case T_LE: case T_GE:
        case T_ADDEQ: case T_SUBEQ: case T_MULEQ: case T_DIVEQ: case T_MODEQ:
        case T_LPAREN: case T_LBRACKET: case T_LBRACE:
        case T_KW_AND: case T_KW_OR: case T_KW_NOT:
        case T_KW_THEN: case T_KW_DO: case T_KW_IN: case T_KW_TO: case T_KW_STEP:
            return 1;
        default:
            return 0;
    }
}

static int char_starts_continuation(int c) {
    /* `+ - * / ...` may continue the previous expression; `(`/`[` are
     * handled by the bracket depth rule, and `-` alone is excluded so a
     * fresh line starting with a minus stays a new statement. */
    switch (c) {
        case '+': case '*': case '=': case '<': case '>':
        case '!': case '&': case '|': case '%': case '.': case ':':
            return 1;
        default:
            return 0;
    }
}

/* True when the line starting at `i` looks like an assignment
 * (`name = ...` or `name := ...`). Inside an unclosed bracket this is a
 * strong hint that the bracket was simply forgotten. */
static int line_starts_assignment(const char *s, size_t len, size_t i) {
    size_t start = i;
    while (i < len && bs_is_ident_char((unsigned char)s[i])) i++;
    if (i == start) return 0;
    while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
    if (i + 1 < len && s[i] == '=' && s[i + 1] != '=') return 1;
    if (i + 1 < len && s[i] == ':' && s[i + 1] == '=') return 1;
    return 0;
}

/* True when a new statement clearly starts at `i` (inside an unclosed
 * bracket). Emitting the newline there turns a forgotten `]` or `}` into a
 * one line recovery instead of a cascade of errors. */
static int word_is_statement_start(const char *s, size_t len, size_t i) {
    static const char *words[] = {
        "say", "print", "echo", "affiche", "log",
        "if", "si", "elif", "elsif", "sinon_si", "else", "sinon",
        "while", "tantque", "for", "pour",
        "fun", "fn", "def", "func", "function", "fonction",
        "return", "retour", "renvoie",
        "break", "casse", "stop", "continue", "suivant",
        "try", "essaie", "catch", "attrape",
        "end", "fin", "done", NULL
    };
    size_t start = i;
    while (i < len && bs_is_ident_char((unsigned char)s[i])) i++;
    size_t wlen = i - start;
    if (wlen == 0) return 0;
    for (int k = 0; words[k]; k++) {
        if (strlen(words[k]) != wlen) continue;
        size_t m = 0;
        while (m < wlen) {
            char a = (char)tolower((unsigned char)s[start + m]);
            char b = (char)tolower((unsigned char)words[k][m]);
            if (a != b) break;
            m++;
        }
        if (m == wlen) return 1;
    }
    return 0;
}

/* Skip spaces, blank lines and comments starting at `i`; return the index
 * of the next meaningful byte (or len). */
static size_t skip_filler(const char *s, size_t len, size_t i) {
    for (;;) {
        while (i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++;
        if (i + 1 < len && s[i] == '/' && s[i + 1] == '/') {
            while (i < len && s[i] != '\n') i++;
            continue;
        }
        if (i < len && s[i] == '#') {
            while (i < len && s[i] != '\n') i++;
            continue;
        }
        if (i + 1 < len && s[i] == '/' && s[i + 1] == '*') {
            i += 2;
            while (i + 1 < len && !(s[i] == '*' && s[i + 1] == '/')) i++;
            i = (i + 1 < len) ? i + 2 : len;
            continue;
        }
        return i;
    }
}

/* ------------------------------------------------------------------ */
/* scanner                                                             */
/* ------------------------------------------------------------------ */

static char read_escape(const char *s, size_t len, size_t *i) {
    size_t p = *i + 1; /* skip backslash */
    if (p >= len) {
        *i = len;
        return '\0';
    }
    char c = s[p];
    *i = p + 1;
    switch (c) {
        case 'n': return '\n';
        case 't': return '\t';
        case 'r': return '\r';
        case '0': return '\0';
        case 'a': return '\a';
        case 'b': return '\b';
        case 'f': return '\f';
        case 'v': return '\v';
        case 'e': return '\x1b';
        case '\n': return '\n'; /* line continuation inside a string */
        default: return c;
    }
}

static double scan_number(const char *s, size_t len, size_t *i) {
    if (s[*i] == '0' && *i + 1 < len) {
        char c = (char)tolower((unsigned char)s[*i + 1]);
        if (c == 'x' || c == 'b' || c == 'o') {
            size_t j = *i + 2;
            while (j < len && (isalnum((unsigned char)s[j]) || s[j] == '_')) j++;
            char *tmp = bs_strndup(s + *i, j - *i);
            double v;
            if (c == 'x') v = (double)strtoull(tmp + 2, NULL, 16);
            else if (c == 'b') v = (double)strtoull(tmp + 2, NULL, 2);
            else v = (double)strtoull(tmp + 2, NULL, 8);
            free(tmp);
            *i = j;
            return v;
        }
    }
    char *end = NULL;
    double v = strtod(s + *i, &end);
    size_t consumed = (size_t)(end - (s + *i));
    while (*i + consumed < len && s[*i + consumed] == '_') consumed++;
    *i += consumed;
    return v;
}

TokenStream *lex(const char *src) {
    TokenStream *ts = ts_new();
    size_t len = strlen(src);
    size_t i = 0;
    int line = 1;
    int depth = 0;  /* (), [] and {} nesting */
    int pending_nl = 0;
    int pending_nl_cont = 0;
    int seen_code = 0; /* no leading separator before the first real token */
    TokKind last = T_EOF;

    while (i < len) {
        char c = src[i];

        /* --- whitespace & comments --- */
        if (c == '\n') {
            i++;
            line++;
            if (tok_is_dangling(last)) {
                /* the previous line cannot end a statement: keep going, and
                 * remember that this newline is a continuation — unless the
                 * next line obviously starts a new statement, in which case a
                 * trailing comma really was trailing. */
                if (!pending_nl) {
                    size_t nxt = skip_filler(src, len, i);
                    pending_nl = 1;
                    pending_nl_cont = !(nxt < len && word_is_statement_start(src, len, nxt));
                }
            } else {
                size_t nxt = skip_filler(src, len, i);
                int suppressed = 0;
                if (nxt < len) {
                    if (depth == 0) {
                        suppressed = char_starts_continuation((unsigned char)src[nxt]);
                    } else {
                        /* inside brackets: only keep going when the next line
                         * does not obviously start a new statement */
                        int stmt = word_is_statement_start(src, len, nxt) ||
                                   line_starts_assignment(src, len, nxt);
                        suppressed = !stmt;
                        if (stmt) depth = 0; /* the bracket was forgotten */
                    }
                }
                if (!suppressed) pending_nl = 1;
            }
            continue;
        }
        if (bs_is_space(c)) { i++; continue; }
        if (c == '#') { while (i < len && src[i] != '\n') i++; continue; }
        if (c == '/' && i + 1 < len && src[i + 1] == '/') {
            while (i < len && src[i] != '\n') i++;
            continue;
        }
        if (c == '/' && i + 1 < len && src[i + 1] == '*') {
            int start_line = line;
            i += 2;
            while (i + 1 < len && !(src[i] == '*' && src[i + 1] == '/')) {
                if (src[i] == '\n') line++;
                i++;
            }
            if (i + 1 >= len) {
                if (bs_soft) bs_warn(start_line, "unterminated block comment, closed at end of file");
                i = len;
            } else {
                i += 2;
            }
            continue;
        }

        if (pending_nl && seen_code) {
            Token t = mk(T_NL, line - 1 > 0 ? line - 1 : 1);
            t.cont = pending_nl_cont;
            ts_push(ts, t);
        }
        pending_nl = 0;
        pending_nl_cont = 0;
        seen_code = 1;

        /* --- strings --- */
        if (c == '"' || c == '\'') {
            char quote = c;
            int start_line = line;
            i++;
            Str s;
            str_init(&s);
            int closed = 0;
            while (i < len) {
                char d = src[i];
                if (d == '\\') {
                    char e = read_escape(src, len, &i);
                    if (e == '\n') line++;
                    str_addc(&s, e);
                    continue;
                }
                if (d == quote) { i++; closed = 1; break; }
                /* soft: strings stop at EOL. A CR stops them too, otherwise a
                 * file saved by a Windows editor would end every implicitly
                 * closed string with a stray carriage return. */
                if (d == '\n' || d == '\r') break;
                str_addc(&s, d);
                i++;
            }
            if (!closed) {
                if (bs_soft) {
                    bs_warn(start_line, "unterminated string, closed implicitly at end of line");
                } else {
                    bs_warn(start_line, "unterminated string");
                }
            }
            Token t = mk(T_STR, start_line);
            t.len = s.len;
            t.text = str_release(&s);
            ts_push(ts, t);
            last = T_STR;
            continue;
        }

        /* --- numbers --- */
        if (bs_is_digit((unsigned char)c) ||
            (c == '.' && i + 1 < len && bs_is_digit((unsigned char)src[i + 1]))) {
            Token t = mk(T_NUM, line);
            t.num = scan_number(src, len, &i);
            ts_push(ts, t);
            last = T_NUM;
            continue;
        }

        /* --- identifiers & keywords --- */
        if (bs_is_ident_start((unsigned char)c)) {
            size_t start = i;
            while (i < len && bs_is_ident_char((unsigned char)src[i])) i++;
            char *word = bs_strndup(src + start, i - start);
            TokKind k = keyword_kind(word);
            Token t = mk(k, line);
            t.text = word;
            ts_push(ts, t);
            last = k;
            continue;
        }

        /* --- operators & punctuation --- */
        i++;
        Token t = mk(T_EOF, line);
        int emit = 1;
        switch (c) {
            case '+':
                if (i < len && src[i] == '=') { i++; t.kind = T_ADDEQ; }
                else t.kind = T_PLUS;
                break;
            case '-':
                if (i < len && src[i] == '=') { i++; t.kind = T_SUBEQ; }
                else t.kind = T_MINUS;
                break;
            case '*':
                if (i < len && src[i] == '=') { i++; t.kind = T_MULEQ; }
                else t.kind = T_STAR;
                break;
            case '/':
                if (i < len && src[i] == '=') { i++; t.kind = T_DIVEQ; }
                else t.kind = T_SLASH;
                break;
            case '%':
                if (i < len && src[i] == '=') { i++; t.kind = T_MODEQ; }
                else t.kind = T_PERCENT;
                break;
            case '=':
                if (i < len && src[i] == '=') { i++; t.kind = T_EQ; }
                else t.kind = T_ASSIGN;
                break;
            case ':':
                if (i < len && src[i] == '=') { i++; t.kind = T_DEFL; }
                else t.kind = T_COLON;
                break;
            case '!':
                if (i < len && src[i] == '=') { i++; t.kind = T_NE; }
                else t.kind = T_KW_NOT;
                break;
            case '<':
                if (i < len && src[i] == '=') { i++; t.kind = T_LE; }
                else if (i < len && src[i] == '>') { i++; t.kind = T_NE; }
                else t.kind = T_LT;
                break;
            case '>':
                if (i < len && src[i] == '=') { i++; t.kind = T_GE; }
                else t.kind = T_GT;
                break;
            case '&':
                if (i < len && src[i] == '&') i++;
                t.kind = T_KW_AND;
                break;
            case '|':
                if (i < len && src[i] == '|') i++;
                t.kind = T_KW_OR;
                break;
            case '(': t.kind = T_LPAREN; depth++; break;
            case ')': t.kind = T_RPAREN; if (depth > 0) depth--; break;
            case '[': t.kind = T_LBRACKET; depth++; break;
            case ']': t.kind = T_RBRACKET; if (depth > 0) depth--; break;
            case '{': t.kind = T_LBRACE; depth++; break;
            case '}': t.kind = T_RBRACE; if (depth > 0) depth--; break;
            case ',': t.kind = T_COMMA; break;
            case '.': t.kind = T_DOT; break;
            case ';': t.kind = T_NL; break; /* `;` is just a separator */
            case '?': case '`': case '~': case '^': case '\\': case '$':
                if (bs_soft) {
                    bs_warn(line, "ignored stray character '%c'", c);
                    emit = 0;
                } else {
                    bs_warn(line, "unexpected character '%c'", c);
                    emit = 0;
                }
                break;
            default:
                bs_warn(line, "unexpected byte 0x%02x, skipped", (unsigned char)c);
                emit = 0;
                break;
        }
        if (emit) {
            ts_push(ts, t);
            last = t.kind;
        }
    }

    if (depth > 0) {
        bs_warn(line, "%d unclosed bracket(s) at end of file", depth);
        ts->unclosed = depth;
    }
    if (pending_nl && seen_code) {
        Token t = mk(T_NL, line);
        t.cont = pending_nl_cont;
        ts_push(ts, t);
    }
    ts_push(ts, mk(T_EOF, line));
    return ts;
}

void tokens_free(TokenStream *ts) {
    if (!ts) return;
    for (int i = 0; i < ts->count; i++) free(ts->items[i].text);
    free(ts->items);
    free(ts);
}
