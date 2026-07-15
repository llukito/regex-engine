#include "dfa.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Subset construction
 * -------------------
 * Each DFA state is a set of NFA states (bitvector of length nfa->nstates).
 *
 *   start  =  eps_close({nfa->start}, follow EPSILON | ANCHOR_START)
 *   delta(S, c) = eps_close(move(S, c), follow EPSILON only)
 *   accept(S)   = nfa->accept ∈ eps_close(S, follow EPSILON | ANCHOR_END)
 *
 * This separates position-dependent anchors from the character step so
 * the DFA transition table is a pure function of (state, byte), while
 * still matching the NFA simulator in nfa.c.
 */

typedef struct {
    unsigned char *bits; /* length = nnfa, 0/1 membership */
} NfaSet;

typedef struct {
    const Nfa *nfa;
    size_t nnfa;
    NfaSet *sets;        /* parallel to DFA states during build */
    size_t nsets;
    size_t cap_sets;
    int failed;
} Builder;

/* ---- bitset helpers --------------------------------------------------- */

static void set_clear(unsigned char *bits, size_t n)
{
    memset(bits, 0, n);
}

static void set_copy(unsigned char *dst, const unsigned char *src, size_t n)
{
    memcpy(dst, src, n);
}

static int set_equal(const unsigned char *a, const unsigned char *b, size_t n)
{
    return memcmp(a, b, n) == 0;
}

static int set_empty(const unsigned char *bits, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (bits[i])
            return 0;
    }
    return 1;
}

/* ---- NFA move / closure (local copies; nfa.c internals are private) --- */

static int class_matches(const NfaTrans *t, unsigned char c)
{
    int in = 0;
    for (size_t i = 0; i < t->nranges; i++) {
        if (c >= t->ranges[i].lo && c <= t->ranges[i].hi) {
            in = 1;
            break;
        }
    }
    return t->negated ? !in : in;
}

static int trans_matches_char(const NfaTrans *t, unsigned char c)
{
    switch (t->type) {
    case NFA_TRANS_CHAR:
        return t->ch == c;
    case NFA_TRANS_DOT:
        return 1;
    case NFA_TRANS_CLASS:
        return class_matches(t, c);
    default:
        return 0;
    }
}

typedef enum {
    CLOSE_EPS = 1,
    CLOSE_START = 2, /* also follow ^ */
    CLOSE_END = 4    /* also follow $ */
} CloseFlags;

static void epsilon_closure(const Nfa *nfa, unsigned char *set, int flags)
{
    size_t nnfa = nfa->nstates;
    int *stack = malloc(nnfa * sizeof(int));
    if (!stack)
        return;

    int sp = 0;
    for (size_t i = 0; i < nnfa; i++) {
        if (set[i])
            stack[sp++] = (int)i;
    }

    while (sp > 0) {
        int id = stack[--sp];
        const NfaState *s = &nfa->states[id];
        for (size_t j = 0; j < s->ntrans; j++) {
            const NfaTrans *t = &s->trans[j];
            int follow = 0;
            switch (t->type) {
            case NFA_TRANS_EPSILON:
                follow = (flags & CLOSE_EPS) != 0;
                break;
            case NFA_TRANS_ANCHOR_START:
                follow = (flags & CLOSE_START) != 0;
                break;
            case NFA_TRANS_ANCHOR_END:
                follow = (flags & CLOSE_END) != 0;
                break;
            default:
                break;
            }
            if (follow && t->to >= 0 && (size_t)t->to < nnfa && !set[t->to]) {
                set[t->to] = 1;
                stack[sp++] = t->to;
            }
        }
    }

    free(stack);
}

static void nfa_move(const Nfa *nfa, const unsigned char *cur,
                     unsigned char *next, unsigned char c)
{
    size_t nnfa = nfa->nstates;
    set_clear(next, nnfa);
    for (size_t i = 0; i < nnfa; i++) {
        if (!cur[i])
            continue;
        const NfaState *s = &nfa->states[i];
        for (size_t j = 0; j < s->ntrans; j++) {
            const NfaTrans *t = &s->trans[j];
            if (trans_matches_char(t, c) && t->to >= 0
                && (size_t)t->to < nnfa)
                next[t->to] = 1;
        }
    }
}

static int set_accepts(const Nfa *nfa, const unsigned char *bits)
{
    /* Copy, then close through $ at end-of-input. */
    size_t nnfa = nfa->nstates;
    unsigned char *tmp = malloc(nnfa);
    if (!tmp)
        return 0;
    set_copy(tmp, bits, nnfa);
    epsilon_closure(nfa, tmp, CLOSE_EPS | CLOSE_END);
    int ok = (nfa->accept >= 0 && (size_t)nfa->accept < nnfa
              && tmp[nfa->accept])
                 ? 1
                 : 0;
    free(tmp);
    return ok;
}

/* ---- builder: map NFA-sets to DFA state ids --------------------------- */

static int find_set(const Builder *b, const unsigned char *bits)
{
    for (size_t i = 0; i < b->nsets; i++) {
        if (set_equal(b->sets[i].bits, bits, b->nnfa))
            return (int)i;
    }
    return -1;
}

static int add_set(Builder *b, Dfa *dfa, const unsigned char *bits)
{
    int existing = find_set(b, bits);
    if (existing >= 0)
        return existing;

    if (b->nsets >= b->cap_sets) {
        size_t ncap = b->cap_sets ? b->cap_sets * 2 : 8;
        NfaSet *ns = realloc(b->sets, ncap * sizeof(*ns));
        if (!ns) {
            b->failed = 1;
            return -1;
        }
        b->sets = ns;
        b->cap_sets = ncap;
    }

    if (dfa->nstates >= dfa->cap_states) {
        size_t ncap = dfa->cap_states ? dfa->cap_states * 2 : 8;
        DfaState *ns = realloc(dfa->states, ncap * sizeof(*ns));
        if (!ns) {
            b->failed = 1;
            return -1;
        }
        dfa->states = ns;
        dfa->cap_states = ncap;
    }

    unsigned char *copy = malloc(b->nnfa);
    if (!copy) {
        b->failed = 1;
        return -1;
    }
    set_copy(copy, bits, b->nnfa);

    int id = (int)b->nsets;
    b->sets[b->nsets].bits = copy;
    b->nsets++;

    DfaState *st = &dfa->states[dfa->nstates++];
    st->id = id;
    st->accept = set_accepts(b->nfa, copy);
    for (int c = 0; c < DFA_ALPHABET; c++)
        st->trans[c] = -1; /* filled later */

    return id;
}

static void builder_free_sets(Builder *b)
{
    if (!b->sets)
        return;
    for (size_t i = 0; i < b->nsets; i++)
        free(b->sets[i].bits);
    free(b->sets);
    b->sets = NULL;
    b->nsets = 0;
}

/* ---- public API ------------------------------------------------------- */

Dfa *dfa_from_nfa(const Nfa *nfa)
{
    if (!nfa || nfa->nstates == 0 || nfa->start < 0)
        return NULL;

    Dfa *dfa = calloc(1, sizeof(*dfa));
    if (!dfa)
        return NULL;
    dfa->start = -1;
    dfa->dead = -1;

    Builder b = {
        .nfa = nfa,
        .nnfa = nfa->nstates,
        .sets = NULL,
        .nsets = 0,
        .cap_sets = 0,
        .failed = 0,
    };

    unsigned char *start_bits = calloc(b.nnfa, 1);
    unsigned char *move_bits = calloc(b.nnfa, 1);
    unsigned char *work_bits = calloc(b.nnfa, 1);
    if (!start_bits || !move_bits || !work_bits) {
        free(start_bits);
        free(move_bits);
        free(work_bits);
        dfa_free(dfa);
        return NULL;
    }

    /* Start set: NFA start, close through epsilon and ^. */
    start_bits[nfa->start] = 1;
    epsilon_closure(nfa, start_bits, CLOSE_EPS | CLOSE_START);

    dfa->start = add_set(&b, dfa, start_bits);
    if (b.failed) {
        free(start_bits);
        free(move_bits);
        free(work_bits);
        builder_free_sets(&b);
        dfa_free(dfa);
        return NULL;
    }

    /* Dead state (empty subset). May already exist if start is empty. */
    set_clear(work_bits, b.nnfa);
    dfa->dead = add_set(&b, dfa, work_bits);
    if (b.failed) {
        free(start_bits);
        free(move_bits);
        free(work_bits);
        builder_free_sets(&b);
        dfa_free(dfa);
        return NULL;
    }

    /*
     * Worklist: process each DFA state once for every alphabet symbol.
     * New states discovered via delta are appended and processed later.
     */
    for (size_t si = 0; si < b.nsets; si++) {
        const unsigned char *S = b.sets[si].bits;

        for (int c = 0; c < DFA_ALPHABET; c++) {
            nfa_move(nfa, S, move_bits, (unsigned char)c);
            set_copy(work_bits, move_bits, b.nnfa);
            epsilon_closure(nfa, work_bits, CLOSE_EPS);

            int dest;
            if (set_empty(work_bits, b.nnfa))
                dest = dfa->dead;
            else
                dest = add_set(&b, dfa, work_bits);

            if (b.failed || dest < 0) {
                free(start_bits);
                free(move_bits);
                free(work_bits);
                builder_free_sets(&b);
                dfa_free(dfa);
                return NULL;
            }
            dfa->states[si].trans[c] = dest;
        }
    }

    free(start_bits);
    free(move_bits);
    free(work_bits);
    builder_free_sets(&b);

    if (b.failed) {
        dfa_free(dfa);
        return NULL;
    }
    return dfa;
}

void dfa_free(Dfa *dfa)
{
    if (!dfa)
        return;
    free(dfa->states);
    free(dfa);
}

void dfa_print(const Dfa *dfa)
{
    if (!dfa) {
        puts("(null DFA)");
        return;
    }

    printf("DFA: start=s%d dead=s%d  (%zu states)\n",
           dfa->start, dfa->dead, dfa->nstates);

    for (size_t i = 0; i < dfa->nstates; i++) {
        const DfaState *s = &dfa->states[i];
        printf("  s%d:%s\n", s->id, s->accept ? "  (accept)" : "");

        /* Collapse runs of identical targets for readability. */
        int c = 0;
        while (c < DFA_ALPHABET) {
            int dest = s->trans[c];
            int end = c;
            while (end + 1 < DFA_ALPHABET && s->trans[end + 1] == dest)
                end++;

            /* Skip dead self-loops in the dump to reduce noise, but
             * always show transitions that leave the dead state or
             * go somewhere interesting. */
            int boring = (dest == dfa->dead && (int)i == dfa->dead);
            if (!boring) {
                if (c == end) {
                    if (c >= 32 && c < 127)
                        printf("      '%c'", c);
                    else
                        printf("      0x%02x", c);
                } else {
                    if (c >= 32 && c < 127 && end >= 32 && end < 127)
                        printf("      '%c'-'%c'", c, end);
                    else
                        printf("      0x%02x-0x%02x", c, end);
                }
                printf(" -> s%d\n", dest);
            }
            c = end + 1;
        }
    }
}

int dfa_match(const Dfa *dfa, const char *input)
{
    if (!dfa || !input || dfa->nstates == 0 || dfa->start < 0)
        return 0;

    int s = dfa->start;
    for (const unsigned char *p = (const unsigned char *)input; *p; p++) {
        s = dfa->states[s].trans[*p];
        if (s < 0 || (size_t)s >= dfa->nstates)
            return 0;
    }
    return dfa->states[s].accept;
}

/*
 * DFA minimization
 * ----------------
 * 1. Drop states unreachable from the start state.
 * 2. Refine the partition {accepting, non-accepting} until it is stable:
 *    two states stay equivalent only if, for every byte c, their c-
 *    successors lie in the same block (Moore-style refinement).
 * 3. Emit one DFA state per block.
 *
 * The result is a smallest DFA for the same language (unique up to
 * renaming of states for a complete DFA over a fixed alphabet).
 */

Dfa *dfa_minimize(const Dfa *src)
{
    if (!src || src->nstates == 0 || src->start < 0
        || (size_t)src->start >= src->nstates)
        return NULL;

    const size_t n_old = src->nstates;

    /* ---- reachable from start --------------------------------------- */
    unsigned char *reach = calloc(n_old, 1);
    int *queue = malloc(n_old * sizeof(int));
    if (!reach || !queue) {
        free(reach);
        free(queue);
        return NULL;
    }

    int qh = 0, qt = 0;
    reach[src->start] = 1;
    queue[qt++] = src->start;
    while (qh < qt) {
        int s = queue[qh++];
        for (int c = 0; c < DFA_ALPHABET; c++) {
            int t = src->states[s].trans[c];
            if (t < 0 || (size_t)t >= n_old)
                continue;
            if (!reach[t]) {
                reach[t] = 1;
                queue[qt++] = t;
            }
        }
    }
    free(queue);

    size_t nr = 0;
    for (size_t i = 0; i < n_old; i++) {
        if (reach[i])
            nr++;
    }
    if (nr == 0) {
        free(reach);
        return NULL;
    }

    int *old_of = malloc(nr * sizeof(int));       /* compact -> old */
    int *compact_of = malloc(n_old * sizeof(int)); /* old -> compact or -1 */
    int *accept = malloc(nr * sizeof(int));
    int *trans = malloc(nr * (size_t)DFA_ALPHABET * sizeof(int));
    if (!old_of || !compact_of || !accept || !trans) {
        free(reach);
        free(old_of);
        free(compact_of);
        free(accept);
        free(trans);
        return NULL;
    }

    for (size_t i = 0; i < n_old; i++)
        compact_of[i] = -1;

    size_t k = 0;
    for (size_t i = 0; i < n_old; i++) {
        if (!reach[i])
            continue;
        compact_of[i] = (int)k;
        old_of[k] = (int)i;
        k++;
    }

    for (size_t ci = 0; ci < nr; ci++) {
        int oi = old_of[ci];
        accept[ci] = src->states[oi].accept ? 1 : 0;
        for (int c = 0; c < DFA_ALPHABET; c++) {
            int t = src->states[oi].trans[c];
            int ct = (t >= 0 && (size_t)t < n_old) ? compact_of[t] : -1;
            /* Targets of reachable states are reachable in a total DFA;
             * if not, point at self as a safe fallback. */
            if (ct < 0)
                ct = (int)ci;
            trans[ci * DFA_ALPHABET + c] = ct;
        }
    }
    free(reach);

    /* ---- partition refinement --------------------------------------- */
    int *block = malloc(nr * sizeof(int));
    int *nblock = malloc(nr * sizeof(int));
    if (!block || !nblock) {
        free(old_of);
        free(compact_of);
        free(accept);
        free(trans);
        free(block);
        free(nblock);
        return NULL;
    }

    for (size_t i = 0; i < nr; i++)
        block[i] = accept[i]; /* 0 = reject, 1 = accept */

    int changed = 1;
    int nblocks = 0;
    while (changed) {
        changed = 0;
        for (size_t i = 0; i < nr; i++)
            nblock[i] = -1;

        int nid = 0;
        for (size_t s = 0; s < nr; s++) {
            if (nblock[s] >= 0)
                continue;
            nblock[s] = nid;
            for (size_t t = s + 1; t < nr; t++) {
                if (nblock[t] >= 0)
                    continue;
                if (block[t] != block[s])
                    continue;
                int same = 1;
                for (int c = 0; c < DFA_ALPHABET; c++) {
                    int ds = trans[s * DFA_ALPHABET + c];
                    int dt = trans[t * DFA_ALPHABET + c];
                    if (block[ds] != block[dt]) {
                        same = 0;
                        break;
                    }
                }
                if (same)
                    nblock[t] = nid;
            }
            nid++;
        }

        /* Stable iff the equivalence relation is unchanged. */
        for (size_t s = 0; s < nr && !changed; s++) {
            for (size_t t = s + 1; t < nr; t++) {
                int same_old = (block[s] == block[t]);
                int same_new = (nblock[s] == nblock[t]);
                if (same_old != same_new) {
                    changed = 1;
                    break;
                }
            }
        }

        memcpy(block, nblock, nr * sizeof(int));
        nblocks = nid;
    }

    /* ---- build minimized DFA ---------------------------------------- */
    int *rep = malloc((size_t)nblocks * sizeof(int));
    if (!rep) {
        free(old_of);
        free(compact_of);
        free(accept);
        free(trans);
        free(block);
        free(nblock);
        return NULL;
    }
    for (int b = 0; b < nblocks; b++)
        rep[b] = -1;
    for (size_t s = 0; s < nr; s++) {
        int b = block[s];
        if (rep[b] < 0)
            rep[b] = (int)s;
    }

    Dfa *out = calloc(1, sizeof(*out));
    if (!out) {
        free(old_of);
        free(compact_of);
        free(accept);
        free(trans);
        free(block);
        free(nblock);
        free(rep);
        return NULL;
    }

    out->states = calloc((size_t)nblocks, sizeof(DfaState));
    if (!out->states) {
        free(out);
        free(old_of);
        free(compact_of);
        free(accept);
        free(trans);
        free(block);
        free(nblock);
        free(rep);
        return NULL;
    }

    out->nstates = (size_t)nblocks;
    out->cap_states = (size_t)nblocks;
    out->start = block[compact_of[src->start]];

    for (int b = 0; b < nblocks; b++) {
        int r = rep[b];
        out->states[b].id = b;
        out->states[b].accept = accept[r];
        for (int c = 0; c < DFA_ALPHABET; c++) {
            int dest = trans[r * DFA_ALPHABET + c];
            out->states[b].trans[c] = block[dest];
        }
    }

    /* Image of the old dead state, or a non-accepting total sink. */
    out->dead = -1;
    if (src->dead >= 0 && (size_t)src->dead < n_old
        && compact_of[src->dead] >= 0)
        out->dead = block[compact_of[src->dead]];
    if (out->dead < 0) {
        for (int b = 0; b < nblocks; b++) {
            if (out->states[b].accept)
                continue;
            int sink = 1;
            for (int c = 0; c < DFA_ALPHABET; c++) {
                if (out->states[b].trans[c] != b) {
                    sink = 0;
                    break;
                }
            }
            if (sink) {
                out->dead = b;
                break;
            }
        }
    }
    if (out->dead < 0)
        out->dead = 0;

    free(old_of);
    free(compact_of);
    free(accept);
    free(trans);
    free(block);
    free(nblock);
    free(rep);
    return out;
}
