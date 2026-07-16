#include "regex.h"

#include "parser.h"
#include "nfa.h"
#include "dfa.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Regex {
    Dfa *dfa;
};

static void set_err(char *errmsg, size_t errmsg_size, const char *msg)
{
    if (errmsg && errmsg_size > 0) {
        snprintf(errmsg, errmsg_size, "%s", msg);
        errmsg[errmsg_size - 1] = '\0';
    }
}

/* ---- case-insensitive AST expansion ----------------------------------- */

/*
 * REGEX_ICASE rewrites the AST so existing NFA/DFA code stays unchanged:
 *   - literal c     → character class containing tolower(c) and toupper(c)
 *   - [ranges]      → same class with every character's case variants added
 *   - [^ranges]     → expand the positive set the same way, keep negation
 *
 * ASCII case folding via tolower/toupper (cast through unsigned char).
 */

static int icase_push_range(CharRange **ranges, size_t *nranges, size_t *cap,
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

static int icase_add_char(CharRange **ranges, size_t *nranges, size_t *cap,
                          unsigned char c)
{
    unsigned char lower = (unsigned char)tolower(c);
    unsigned char upper = (unsigned char)toupper(c);
    if (!icase_push_range(ranges, nranges, cap, lower, lower))
        return 0;
    if (upper != lower
        && !icase_push_range(ranges, nranges, cap, upper, upper))
        return 0;
    return 1;
}

/* Expand a class's ranges in place to include case variants. Returns 0 on OOM. */
static int icase_expand_class(AstNode *node)
{
    CharRange *old = node->u.cclass.ranges;
    size_t nold = node->u.cclass.nranges;
    CharRange *ranges = NULL;
    size_t nranges = 0;
    size_t cap = 0;

    for (size_t i = 0; i < nold; i++) {
        unsigned char lo = old[i].lo;
        unsigned char hi = old[i].hi;
        /* Keep the original range (covers non-letters and the base set). */
        if (!icase_push_range(&ranges, &nranges, &cap, lo, hi)) {
            free(ranges);
            return 0;
        }
        for (unsigned c = lo;; c++) {
            if (!icase_add_char(&ranges, &nranges, &cap, (unsigned char)c)) {
                free(ranges);
                return 0;
            }
            if (c == hi)
                break;
        }
    }

    free(old);
    node->u.cclass.ranges = ranges;
    node->u.cclass.nranges = nranges;
    return 1;
}

/* Convert a literal node into a case-insensitive character class. */
static int icase_literal_to_class(AstNode *node)
{
    unsigned char c = node->u.literal;
    CharRange *ranges = NULL;
    size_t nranges = 0;
    size_t cap = 0;

    if (!icase_add_char(&ranges, &nranges, &cap, c)) {
        free(ranges);
        return 0;
    }

    node->type = AST_CHAR_CLASS;
    node->u.cclass.negated = 0;
    node->u.cclass.ranges = ranges;
    node->u.cclass.nranges = nranges;
    return 1;
}

/*
 * Walk the AST and apply case folding. Returns 0 on allocation failure
 * (AST may be partially rewritten; caller must free it).
 */
static int ast_apply_icase(AstNode *node)
{
    if (!node)
        return 1;

    switch (node->type) {
    case AST_LITERAL:
        return icase_literal_to_class(node);

    case AST_CHAR_CLASS:
        return icase_expand_class(node);

    case AST_CONCAT:
    case AST_ALT:
        return ast_apply_icase(node->u.binary.left)
            && ast_apply_icase(node->u.binary.right);

    case AST_STAR:
    case AST_PLUS:
    case AST_QUESTION:
        return ast_apply_icase(node->u.child);

    case AST_EMPTY:
    case AST_DOT:
    case AST_ANCHOR_START:
    case AST_ANCHOR_END:
        return 1;
    }
    return 1;
}

/* ---- public API ------------------------------------------------------- */

Regex *regex_compile(const char *pattern, unsigned flags,
                     char *errmsg, size_t errmsg_size)
{
    if (errmsg && errmsg_size > 0)
        errmsg[0] = '\0';

    if (!pattern) {
        set_err(errmsg, errmsg_size, "NULL pattern");
        return NULL;
    }

    char parse_err[256];
    AstNode *ast = regex_parse(pattern, parse_err, sizeof parse_err);
    if (!ast) {
        if (errmsg && errmsg_size > 0) {
            if (parse_err[0])
                snprintf(errmsg, errmsg_size, "parse error: %s", parse_err);
            else
                set_err(errmsg, errmsg_size, "parse error");
            errmsg[errmsg_size - 1] = '\0';
        }
        return NULL;
    }

    if (flags & REGEX_ICASE) {
        if (!ast_apply_icase(ast)) {
            ast_free(ast);
            set_err(errmsg, errmsg_size, "out of memory");
            return NULL;
        }
    }

    Nfa *nfa = nfa_from_ast(ast);
    ast_free(ast);
    if (!nfa) {
        set_err(errmsg, errmsg_size, "NFA construction failed");
        return NULL;
    }

    Dfa *dfa = dfa_from_nfa(nfa);
    nfa_free(nfa);
    if (!dfa) {
        set_err(errmsg, errmsg_size, "DFA construction failed");
        return NULL;
    }

    if (flags & REGEX_MINIMIZE) {
        Dfa *min = dfa_minimize(dfa);
        dfa_free(dfa);
        if (!min) {
            set_err(errmsg, errmsg_size, "DFA minimization failed");
            return NULL;
        }
        dfa = min;
    }

    Regex *re = malloc(sizeof(*re));
    if (!re) {
        dfa_free(dfa);
        set_err(errmsg, errmsg_size, "out of memory");
        return NULL;
    }
    re->dfa = dfa;
    return re;
}

int regex_match(const Regex *re, const char *input)
{
    if (!re || !re->dfa || !input)
        return 0;
    return dfa_match(re->dfa, input);
}

void regex_free(Regex *re)
{
    if (!re)
        return;
    dfa_free(re->dfa);
    free(re);
}
