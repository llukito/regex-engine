#ifndef REGEX_DFA_H
#define REGEX_DFA_H

#include "nfa.h"

#include <stddef.h>

/*
 * Deterministic finite automaton produced from an NFA by subset
 * construction.
 *
 * Anchors are compiled into the DFA as follows (matching nfa_match):
 *   - '^' is applied only when forming the start state
 *   - '$' is applied only when deciding whether a DFA state accepts
 *     at end-of-input
 *   - on-character steps use pure epsilon-closure (no anchors)
 *
 * The transition function is total: every state has a next state for
 * every byte 0..255 (unknown symbols go to a dead state).
 */

#define DFA_ALPHABET 256

typedef struct DfaState {
    int id;
    int accept;                 /* 1 if end-of-input is accepting here */
    int trans[DFA_ALPHABET];    /* next state for each byte */
} DfaState;

typedef struct Dfa {
    DfaState *states;
    size_t nstates;
    size_t cap_states;
    int start;
    int dead;                   /* empty-subset sink; may equal start */
} Dfa;

/*
 * Subset-construct a DFA from an NFA. The NFA is not modified and must
 * remain valid for the duration of this call only. Returns NULL on
 * failure. Free with dfa_free().
 */
Dfa *dfa_from_nfa(const Nfa *nfa);

void dfa_free(Dfa *dfa);

/* Print the transition table (accepting states marked) to stdout. */
void dfa_print(const Dfa *dfa);

/*
 * Full-match: 1 iff the DFA consumes the entire input and ends in an
 * accepting state. Same language as nfa_match for NFAs built by this
 * engine.
 */
int dfa_match(const Dfa *dfa, const char *input);

#endif /* REGEX_DFA_H */
