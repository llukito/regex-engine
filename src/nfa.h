#ifndef REGEX_NFA_H
#define REGEX_NFA_H

#include "ast.h"

#include <stddef.h>

/*
 * NFA with epsilon transitions, built from an AstNode via Thompson's
 * construction (fragment-based).
 *
 * Each state has an integer id and a list of outgoing transitions.
 * A transition is one of:
 *   - epsilon          (no input consumed)
 *   - character        (exactly one byte)
 *   - character class  (ranges, optional negation; one byte consumed)
 *   - dot              (any byte consumed)
 *   - ^ / $ anchors    (zero-width; checked against input position)
 */

typedef enum {
    NFA_TRANS_EPSILON,
    NFA_TRANS_CHAR,
    NFA_TRANS_CLASS,
    NFA_TRANS_DOT,
    NFA_TRANS_ANCHOR_START, /* ^ — follow only at offset 0 */
    NFA_TRANS_ANCHOR_END    /* $ — follow only at end of input */
} NfaTransType;

typedef struct NfaTrans {
    NfaTransType type;
    int to;                 /* destination state id */
    unsigned char ch;       /* NFA_TRANS_CHAR */
    int negated;            /* NFA_TRANS_CLASS */
    CharRange *ranges;      /* NFA_TRANS_CLASS; owned by the NFA */
    size_t nranges;
} NfaTrans;

typedef struct NfaState {
    int id;
    NfaTrans *trans;
    size_t ntrans;
    size_t cap_trans;
} NfaState;

typedef struct Nfa {
    NfaState *states;
    size_t nstates;
    size_t cap_states;
    int start;
    int accept;
} Nfa;

/*
 * Convert an AST into an NFA. The AST is left untouched and may be freed
 * after this returns. Returns NULL on allocation failure or unexpected
 * node types. Free the result with nfa_free().
 */
Nfa *nfa_from_ast(const AstNode *ast);

/* Free an NFA (safe with NULL). */
void nfa_free(Nfa *nfa);

/* Pretty-print the state graph to stdout. */
void nfa_print(const Nfa *nfa);

/*
 * Full-match simulation: returns 1 iff the NFA can consume the entire
 * input string and reach the accept state. Uses epsilon-closure (with
 * anchor checks) and the standard subset-style step on each character.
 */
int nfa_match(const Nfa *nfa, const char *input);

#endif /* REGEX_NFA_H */
