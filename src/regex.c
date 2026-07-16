#include "regex.h"

#include "parser.h"
#include "nfa.h"
#include "dfa.h"
#include "charset.h"

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
 * REGEX_ICASE rewrites the AST using compile-time BYTE_TOLOWER/BYTE_TOUPPER
 * maps and 256-bit bitmaps (see charset.h):
 *   - literal c     → class of {fold_lower(c), fold_upper(c)}
 *   - [ranges]      → class of every member plus both case folds
 *   - [^ranges]     → same positive expansion, keep negated flag
 *
 * Fold maps cover 0..255 (high bytes are identity). Membership for the
 * resulting class is later tested via branchless bitmap bit tests in the
 * NFA/DFA.
 *
 * On allocation failure the node is left unchanged and scratch is freed.
 * Sibling nodes already rewritten remain a valid tree for ast_free().
 */

/*
 * Expand a class's *positive* membership with case folds, then write
 * ranges back. The negated flag is left unchanged so NFA construction
 * applies negation only after the expanded positive set is built:
 *
 *   [^a] + ICASE → positive {a,A} kept, negated=1
 *               → NFA bitmap: all bytes except a and A
 *
 * On failure: leave the original node ranges intact.
 */
static int icase_expand_class(AstNode *node)
{
    CharRange *old = node->u.cclass.ranges;
    size_t nold = node->u.cclass.nranges;
    CharBitmap positive;
    CharBitmap expanded;
    CharRange *scratch = NULL;
    size_t nranges = 0;

    /* 1. Positive set only (ignore negated here). */
    char_bitmap_from_ranges(&positive, old, nold);
    /* 2. Case-expand that positive set (before any negation). */
    char_bitmap_expand_icase(&expanded, &positive);
    /* 3. Store expanded positive ranges; negated flag stays for NFA. */
    if (!char_bitmap_to_ranges(&expanded, &scratch, &nranges))
        return 0;

    free(old);
    node->u.cclass.ranges = scratch;
    node->u.cclass.nranges = nranges;
    /* node->u.cclass.negated unchanged */
    return 1;
}

/*
 * Convert a literal into a case-insensitive character class using fold maps.
 * On failure the node remains AST_LITERAL.
 */
static int icase_literal_to_class(AstNode *node)
{
    unsigned char c = node->u.literal;
    CharBitmap bm;
    CharRange *scratch = NULL;
    size_t nranges = 0;

    char_bitmap_clear(&bm);
    char_bitmap_set_icase(&bm, c);
    if (!char_bitmap_to_ranges(&bm, &scratch, &nranges))
        return 0;

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
