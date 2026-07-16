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
    size_t ast_size; /* AST node count at compile time (debug) */
};

/* ---- last-error buffer ------------------------------------------------ */

#define REGEX_ERRBUF 256

static char g_last_error[REGEX_ERRBUF] = "";

const char *regex_last_error(void)
{
    return g_last_error;
}

/*
 * Record a diagnostic in g_last_error and optionally copy it into the
 * caller-supplied buffer. Always leaves both destinations NUL-terminated
 * when they are written (caller buffer only when errmsg_size > 0).
 */
static void set_err(char *errmsg, size_t errmsg_size, const char *msg)
{
    if (!msg)
        msg = "unknown error";

    snprintf(g_last_error, sizeof g_last_error, "%s", msg);
    g_last_error[sizeof g_last_error - 1] = '\0';

    if (errmsg && errmsg_size > 0) {
        snprintf(errmsg, errmsg_size, "%s", g_last_error);
        errmsg[errmsg_size - 1] = '\0';
    }
}

static void clear_err(char *errmsg, size_t errmsg_size)
{
    g_last_error[0] = '\0';
    if (errmsg && errmsg_size > 0)
        errmsg[0] = '\0';
}

/* ---- case-insensitive AST expansion ----------------------------------- */

/*
 * REGEX_ICASE rewrites the AST so existing NFA/DFA code stays unchanged:
 *   - literal c     → character class containing tolower(c) and toupper(c)
 *   - [ranges]      → same class with every character's case variants added
 *   - [^ranges]     → expand the positive set the same way, keep negation
 *
 * On allocation failure the node being rewritten is left unchanged and any
 * scratch buffers are freed. Sibling nodes already rewritten remain valid
 * AST so the caller can free the whole tree with ast_free().
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

/*
 * Expand a class's ranges to include case variants.
 * Builds into a scratch buffer; only commits to the node on full success.
 * On failure: frees scratch, leaves node (and its original ranges) intact.
 */
static int icase_expand_class(AstNode *node)
{
    CharRange *old = node->u.cclass.ranges;
    size_t nold = node->u.cclass.nranges;
    CharRange *scratch = NULL;
    size_t nranges = 0;
    size_t cap = 0;

    for (size_t i = 0; i < nold; i++) {
        unsigned char lo = old[i].lo;
        unsigned char hi = old[i].hi;

        if (!icase_push_range(&scratch, &nranges, &cap, lo, hi)) {
            free(scratch);
            return 0;
        }
        for (unsigned c = lo;; c++) {
            if (!icase_add_char(&scratch, &nranges, &cap, (unsigned char)c)) {
                free(scratch);
                return 0;
            }
            if (c == hi)
                break;
        }
    }

    /* Commit: replace old ranges only after scratch is complete. */
    free(old);
    node->u.cclass.ranges = scratch;
    node->u.cclass.nranges = nranges;
    return 1;
}

/*
 * Convert a literal node into a case-insensitive character class.
 * On failure the node remains AST_LITERAL and scratch is freed.
 */
static int icase_literal_to_class(AstNode *node)
{
    unsigned char c = node->u.literal;
    CharRange *scratch = NULL;
    size_t nranges = 0;
    size_t cap = 0;

    if (!icase_add_char(&scratch, &nranges, &cap, c)) {
        free(scratch);
        return 0;
    }

    node->type = AST_CHAR_CLASS;
    node->u.cclass.negated = 0;
    node->u.cclass.ranges = scratch;
    node->u.cclass.nranges = nranges;
    return 1;
}

/*
 * Walk the AST and apply case folding. Returns 0 on allocation failure.
 * The tree remains freeable with ast_free() after a partial rewrite.
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
        if (!ast_apply_icase(node->u.binary.left))
            return 0;
        if (!ast_apply_icase(node->u.binary.right))
            return 0;
        return 1;

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
    clear_err(errmsg, errmsg_size);

    if (!pattern) {
        set_err(errmsg, errmsg_size, "NULL pattern");
        return NULL;
    }

    char parse_err[REGEX_ERRBUF];
    parse_err[0] = '\0';
    AstNode *ast = regex_parse(pattern, parse_err, sizeof parse_err);
    if (!ast) {
        char buf[REGEX_ERRBUF];
        if (parse_err[0])
            snprintf(buf, sizeof buf, "parse error: %s", parse_err);
        else
            snprintf(buf, sizeof buf, "parse error");
        buf[sizeof buf - 1] = '\0';
        set_err(errmsg, errmsg_size, buf);
        return NULL;
    }

    if (flags & REGEX_ICASE) {
        if (!ast_apply_icase(ast)) {
            /* Partial rewrites (if any) are still a valid tree. */
            ast_free(ast);
            set_err(errmsg, errmsg_size, "out of memory");
            return NULL;
        }
    }

    /* Capture AST size before the tree is discarded. */
    size_t ast_size = ast_node_count(ast);

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
    re->ast_size = ast_size;
    clear_err(NULL, 0); /* success: last error is empty; leave caller's buf alone */
    if (errmsg && errmsg_size > 0)
        errmsg[0] = '\0';
    return re;
}

int regex_valid(const char *pattern)
{
    Regex *re = regex_compile(pattern, REGEX_DEFAULT, NULL, 0);
    if (!re)
        return 0;
    regex_free(re);
    /* Successful compile cleared last error; restore a neutral empty. */
    return 1;
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

size_t regex_ast_size(const Regex *re)
{
    return re ? re->ast_size : 0;
}

size_t regex_dfa_state_count(const Regex *re)
{
    return re ? dfa_state_count(re->dfa) : 0;
}
