#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Recursive-descent parser for the grammar in ast.h.
 *
 *   regex         ::=  alternation
 *   alternation   ::=  concatenation ( '|' concatenation )*
 *   concatenation ::=  repetition+
 *   repetition    ::=  atom ( '*' | '+' | '?' )?
 *   atom          ::=  '(' regex ')' | char_class | '.' | '^' | '$'
 *                   |  '\' any_char | literal_char
 *
 * Precedence:  (* + ?)  >  concatenation  >  |
 */

typedef struct {
    const char *src;   /* full pattern */
    const char *pos;   /* current read position */
    char *errmsg;
    size_t errmsg_size;
    int failed;
} Parser;

/* ---- cursor helpers --------------------------------------------------- */

static int peek(const Parser *p)
{
    return (unsigned char)*p->pos;
}

static int at_end(const Parser *p)
{
    return *p->pos == '\0';
}

static int advance(Parser *p)
{
    int c = (unsigned char)*p->pos;
    if (c != '\0')
        p->pos++;
    return c;
}

static int match(Parser *p, char expected)
{
    if (peek(p) == (unsigned char)expected) {
        advance(p);
        return 1;
    }
    return 0;
}

/* Characters that end a concatenation sequence. */
static int is_concat_stop(int c)
{
    return c == '\0' || c == '|' || c == ')';
}

/* ---- error helpers ---------------------------------------------------- */

/* Human-readable description of a single input byte / EOF. */
static void format_token(int c, char *buf, size_t bufsz)
{
    if (c == '\0')
        snprintf(buf, bufsz, "end of pattern");
    else if (c == '\n')
        snprintf(buf, bufsz, "'\\n'");
    else if (c == '\r')
        snprintf(buf, bufsz, "'\\r'");
    else if (c == '\t')
        snprintf(buf, bufsz, "'\\t'");
    else if (c >= 32 && c < 127)
        snprintf(buf, bufsz, "'%c'", (char)c);
    else
        snprintf(buf, bufsz, "byte 0x%02x", c);
}

/* Generic message with offset (used for out-of-memory, etc.). */
static void parse_error(Parser *p, const char *msg)
{
    if (p->failed)
        return;
    p->failed = 1;
    if (p->errmsg && p->errmsg_size > 0) {
        size_t offset = (size_t)(p->pos - p->src);
        snprintf(p->errmsg, p->errmsg_size, "%s at offset %zu", msg, offset);
    }
}

/* "expected X, found Y at offset N" — Y is the character at pos. */
static void parse_error_expected(Parser *p, const char *expected)
{
    if (p->failed)
        return;
    p->failed = 1;
    if (p->errmsg && p->errmsg_size > 0) {
        char found[40];
        format_token(peek(p), found, sizeof found);
        size_t offset = (size_t)(p->pos - p->src);
        snprintf(p->errmsg, p->errmsg_size,
                 "expected %s, found %s at offset %zu",
                 expected, found, offset);
    }
}

/* "unexpected Y (expected X) at offset N" */
static void parse_error_unexpected(Parser *p, const char *expected)
{
    if (p->failed)
        return;
    p->failed = 1;
    if (p->errmsg && p->errmsg_size > 0) {
        char found[40];
        format_token(peek(p), found, sizeof found);
        size_t offset = (size_t)(p->pos - p->src);
        snprintf(p->errmsg, p->errmsg_size,
                 "unexpected %s (expected %s) at offset %zu",
                 found, expected, offset);
    }
}

/* ---- forward declarations --------------------------------------------- */

static AstNode *parse_alternation(Parser *p);
static AstNode *parse_concatenation(Parser *p);
static AstNode *parse_repetition(Parser *p);
static AstNode *parse_atom(Parser *p);
static AstNode *parse_char_class(Parser *p);

/* ---- char class ------------------------------------------------------- */

/*
 * Parse one character inside a class. Backslash escapes the next char.
 * Returns the character value, or -1 on error / EOF.
 */
static int class_char(Parser *p)
{
    if (at_end(p)) {
        parse_error_expected(p, "']' or class character");
        return -1;
    }
    if (peek(p) == '\\') {
        advance(p);
        if (at_end(p)) {
            parse_error_expected(p, "character after '\\' in character class");
            return -1;
        }
        return advance(p);
    }
    return advance(p);
}

static int push_range(CharRange **ranges, size_t *nranges, size_t *cap,
                      unsigned char lo, unsigned char hi)
{
    if (lo > hi) {
        unsigned char tmp = lo;
        lo = hi;
        hi = tmp;
    }
    if (*nranges >= *cap) {
        size_t ncap = *cap ? *cap * 2 : 4;
        CharRange *n = realloc(*ranges, ncap * sizeof(*n));
        if (!n)
            return 0;
        *ranges = n;
        *cap = ncap;
    }
    (*ranges)[*nranges].lo = lo;
    (*ranges)[*nranges].hi = hi;
    (*nranges)++;
    return 1;
}

static AstNode *parse_char_class(Parser *p)
{
    /* Caller has already consumed '['. */
    int negated = 0;
    if (match(p, '^'))
        negated = 1;

    CharRange *ranges = NULL;
    size_t nranges = 0;
    size_t cap = 0;
    int first = 1;

    /*
     * Special cases (common POSIX-style rules):
     *   - ']' immediately after '[' or '[^' is a literal ']'
     *   - '-' at either end of the class body is a literal '-'
     *   - otherwise 'a-z' forms an inclusive range
     */
    for (;;) {
        if (at_end(p)) {
            free(ranges);
            parse_error_expected(p, "']' to close character class");
            return NULL;
        }

        /* Closing bracket — except when it is the first item (literal). */
        if (peek(p) == ']' && !first)
            break;

        if (peek(p) == ']' && first) {
            if (!push_range(&ranges, &nranges, &cap, ']', ']')) {
                free(ranges);
                parse_error(p, "out of memory");
                return NULL;
            }
            advance(p);
            first = 0;
            continue;
        }

        int lo = class_char(p);
        if (lo < 0) {
            free(ranges);
            return NULL;
        }
        first = 0;

        /* Range form: lo '-' hi, provided hi is not the closing bracket. */
        if (peek(p) == '-' && p->pos[1] != '\0' && p->pos[1] != ']') {
            advance(p); /* consume '-' */
            int hi = class_char(p);
            if (hi < 0) {
                free(ranges);
                return NULL;
            }
            if (!push_range(&ranges, &nranges, &cap,
                            (unsigned char)lo, (unsigned char)hi)) {
                free(ranges);
                parse_error(p, "out of memory");
                return NULL;
            }
        } else {
            if (!push_range(&ranges, &nranges, &cap,
                            (unsigned char)lo, (unsigned char)lo)) {
                free(ranges);
                parse_error(p, "out of memory");
                return NULL;
            }
        }
    }

    if (!match(p, ']')) {
        free(ranges);
        parse_error_expected(p, "']' to close character class");
        return NULL;
    }

    if (nranges == 0) {
        free(ranges);
        parse_error_unexpected(p, "non-empty character class body");
        return NULL;
    }

    AstNode *node = ast_char_class(negated, ranges, nranges);
    if (!node)
        parse_error(p, "out of memory");
    return node;
}

/* ---- atoms / repetitions / concat / alt ------------------------------- */

static AstNode *parse_atom(Parser *p)
{
    if (p->failed)
        return NULL;

    int c = peek(p);

    if (c == '(') {
        advance(p);
        AstNode *inner = parse_alternation(p);
        if (!inner)
            return NULL;
        if (!match(p, ')')) {
            ast_free(inner);
            parse_error_expected(p, "')' to close group");
            return NULL;
        }
        return inner;
    }

    if (c == '[') {
        advance(p);
        return parse_char_class(p);
    }

    if (c == '.') {
        advance(p);
        AstNode *n = ast_dot();
        if (!n)
            parse_error(p, "out of memory");
        return n;
    }

    if (c == '^') {
        advance(p);
        AstNode *n = ast_anchor_start();
        if (!n)
            parse_error(p, "out of memory");
        return n;
    }

    if (c == '$') {
        advance(p);
        AstNode *n = ast_anchor_end();
        if (!n)
            parse_error(p, "out of memory");
        return n;
    }

    if (c == '\\') {
        advance(p);
        if (at_end(p)) {
            parse_error_expected(p, "character after '\\'");
            return NULL;
        }
        AstNode *n = ast_literal((unsigned char)advance(p));
        if (!n)
            parse_error(p, "out of memory");
        return n;
    }

    /* Metacharacters that are not valid atoms on their own. */
    if (c == '*' || c == '+' || c == '?' || c == '|' || c == ')' || c == '\0') {
        parse_error_unexpected(p,
            "atom (literal, class, group, '.', '^', or '$')");
        return NULL;
    }

    /* Ordinary literal. */
    AstNode *n = ast_literal((unsigned char)advance(p));
    if (!n)
        parse_error(p, "out of memory");
    return n;
}

static AstNode *parse_repetition(Parser *p)
{
    AstNode *atom = parse_atom(p);
    if (!atom)
        return NULL;

    int c = peek(p);
    AstNode *result = atom;

    if (c == '*') {
        advance(p);
        result = ast_star(atom);
    } else if (c == '+') {
        advance(p);
        result = ast_plus(atom);
    } else if (c == '?') {
        advance(p);
        result = ast_question(atom);
    }

    if (!result)
        parse_error(p, "out of memory");
    return result;
}

static AstNode *parse_concatenation(Parser *p)
{
    if (is_concat_stop(peek(p))) {
        /* Empty branch, e.g. "a|" or "|b" or "()". */
        AstNode *n = ast_empty();
        if (!n)
            parse_error(p, "out of memory");
        return n;
    }

    AstNode *left = parse_repetition(p);
    if (!left)
        return NULL;

    while (!p->failed && !is_concat_stop(peek(p))) {
        AstNode *right = parse_repetition(p);
        if (!right) {
            ast_free(left);
            return NULL;
        }
        AstNode *joined = ast_concat(left, right);
        if (!joined) {
            parse_error(p, "out of memory");
            return NULL;
        }
        left = joined;
    }
    return left;
}

static AstNode *parse_alternation(Parser *p)
{
    AstNode *left = parse_concatenation(p);
    if (!left)
        return NULL;

    while (match(p, '|')) {
        AstNode *right = parse_concatenation(p);
        if (!right) {
            ast_free(left);
            return NULL;
        }
        AstNode *joined = ast_alt(left, right);
        if (!joined) {
            parse_error(p, "out of memory");
            return NULL;
        }
        left = joined;
    }
    return left;
}

/* ---- public entry point ----------------------------------------------- */

AstNode *regex_parse(const char *pattern, char *errmsg, size_t errmsg_size)
{
    if (!pattern) {
        if (errmsg && errmsg_size > 0)
            snprintf(errmsg, errmsg_size, "NULL pattern");
        return NULL;
    }

    Parser p = {
        .src = pattern,
        .pos = pattern,
        .errmsg = errmsg,
        .errmsg_size = errmsg_size,
        .failed = 0,
    };

    if (errmsg && errmsg_size > 0)
        errmsg[0] = '\0';

    AstNode *root = parse_alternation(&p);
    if (!root)
        return NULL;

    if (!at_end(&p)) {
        parse_error_expected(&p, "end of pattern");
        ast_free(root);
        return NULL;
    }

    if (p.failed) {
        ast_free(root);
        return NULL;
    }

    return root;
}
