#include "nfa.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Thompson's construction
 * -----------------------
 * Build a fragment (start, out) for each sub-expression and combine:
 *
 *   literal / class / dot:
 *       s --sym--> o
 *
 *   empty / anchor:
 *       s --ε|anchor--> o
 *
 *   concat(e1, e2):
 *       e1.out --ε--> e2.start
 *       fragment = (e1.start, e2.out)
 *
 *   alt(e1, e2):
 *       s --ε--> e1.start , s --ε--> e2.start
 *       e1.out --ε--> o   , e2.out --ε--> o
 *
 *   star(e):
 *       s --ε--> e.start , s --ε--> o
 *       e.out --ε--> e.start , e.out --ε--> o
 *
 *   plus(e):
 *       s --ε--> e.start
 *       e.out --ε--> e.start , e.out --ε--> o
 *
 *   question(e):
 *       s --ε--> e.start , s --ε--> o
 *       e.out --ε--> o
 */

typedef struct {
    int start;
    int out;
} Frag;

typedef struct {
    Nfa *nfa;
    int failed;
} Builder;

/* ---- state / transition plumbing -------------------------------------- */

static int nfa_add_state(Builder *b)
{
    Nfa *nfa = b->nfa;
    if (nfa->nstates >= nfa->cap_states) {
        size_t ncap = nfa->cap_states ? nfa->cap_states * 2 : 8;
        NfaState *ns = realloc(nfa->states, ncap * sizeof(*ns));
        if (!ns) {
            b->failed = 1;
            return -1;
        }
        nfa->states = ns;
        nfa->cap_states = ncap;
    }

    int id = (int)nfa->nstates;
    NfaState *s = &nfa->states[nfa->nstates++];
    s->id = id;
    s->trans = NULL;
    s->ntrans = 0;
    s->cap_trans = 0;
    return id;
}

static NfaTrans *add_trans_slot(Builder *b, int from)
{
    if (from < 0 || b->failed)
        return NULL;

    NfaState *s = &b->nfa->states[from];
    if (s->ntrans >= s->cap_trans) {
        size_t ncap = s->cap_trans ? s->cap_trans * 2 : 2;
        NfaTrans *nt = realloc(s->trans, ncap * sizeof(*nt));
        if (!nt) {
            b->failed = 1;
            return NULL;
        }
        s->trans = nt;
        s->cap_trans = ncap;
    }

    NfaTrans *t = &s->trans[s->ntrans++];
    memset(t, 0, sizeof(*t));
    t->to = -1;
    return t;
}

static void add_epsilon(Builder *b, int from, int to)
{
    NfaTrans *t = add_trans_slot(b, from);
    if (!t)
        return;
    t->type = NFA_TRANS_EPSILON;
    t->to = to;
}

static void add_char(Builder *b, int from, int to, unsigned char ch)
{
    NfaTrans *t = add_trans_slot(b, from);
    if (!t)
        return;
    t->type = NFA_TRANS_CHAR;
    t->to = to;
    t->ch = ch;
}

static void add_dot(Builder *b, int from, int to)
{
    NfaTrans *t = add_trans_slot(b, from);
    if (!t)
        return;
    t->type = NFA_TRANS_DOT;
    t->to = to;
}

static void add_anchor(Builder *b, int from, int to, NfaTransType type)
{
    NfaTrans *t = add_trans_slot(b, from);
    if (!t)
        return;
    t->type = type;
    t->to = to;
}

static void add_class(Builder *b, int from, int to,
                      int negated, const CharRange *ranges, size_t nranges)
{
    NfaTrans *t = add_trans_slot(b, from);
    if (!t)
        return;

    CharRange *copy = NULL;
    if (nranges > 0) {
        copy = malloc(nranges * sizeof(*copy));
        if (!copy) {
            /* roll back the slot */
            b->nfa->states[from].ntrans--;
            b->failed = 1;
            return;
        }
        memcpy(copy, ranges, nranges * sizeof(*copy));
    }

    t->type = NFA_TRANS_CLASS;
    t->to = to;
    t->negated = negated;
    t->ranges = copy;
    t->nranges = nranges;
}

static Frag make_symbol_frag(Builder *b, NfaTransType type,
                             unsigned char ch,
                             int negated, const CharRange *ranges, size_t nranges)
{
    Frag f = {-1, -1};
    f.start = nfa_add_state(b);
    f.out = nfa_add_state(b);
    if (b->failed)
        return f;

    switch (type) {
    case NFA_TRANS_CHAR:
        add_char(b, f.start, f.out, ch);
        break;
    case NFA_TRANS_DOT:
        add_dot(b, f.start, f.out);
        break;
    case NFA_TRANS_CLASS:
        add_class(b, f.start, f.out, negated, ranges, nranges);
        break;
    case NFA_TRANS_EPSILON:
        add_epsilon(b, f.start, f.out);
        break;
    case NFA_TRANS_ANCHOR_START:
    case NFA_TRANS_ANCHOR_END:
        add_anchor(b, f.start, f.out, type);
        break;
    }
    return f;
}

/* ---- recursive Thompson build ----------------------------------------- */

static Frag build(Builder *b, const AstNode *node)
{
    Frag f = {-1, -1};
    if (!node || b->failed) {
        b->failed = 1;
        return f;
    }

    switch (node->type) {
    case AST_EMPTY:
        return make_symbol_frag(b, NFA_TRANS_EPSILON, 0, 0, NULL, 0);

    case AST_LITERAL:
        return make_symbol_frag(b, NFA_TRANS_CHAR, node->u.literal, 0, NULL, 0);

    case AST_DOT:
        return make_symbol_frag(b, NFA_TRANS_DOT, 0, 0, NULL, 0);

    case AST_ANCHOR_START:
        return make_symbol_frag(b, NFA_TRANS_ANCHOR_START, 0, 0, NULL, 0);

    case AST_ANCHOR_END:
        return make_symbol_frag(b, NFA_TRANS_ANCHOR_END, 0, 0, NULL, 0);

    case AST_CHAR_CLASS:
        return make_symbol_frag(b, NFA_TRANS_CLASS, 0,
                                node->u.cclass.negated,
                                node->u.cclass.ranges,
                                node->u.cclass.nranges);

    case AST_CONCAT: {
        Frag left = build(b, node->u.binary.left);
        Frag right = build(b, node->u.binary.right);
        if (b->failed)
            return f;
        add_epsilon(b, left.out, right.start);
        f.start = left.start;
        f.out = right.out;
        return f;
    }

    case AST_ALT: {
        Frag left = build(b, node->u.binary.left);
        Frag right = build(b, node->u.binary.right);
        if (b->failed)
            return f;
        f.start = nfa_add_state(b);
        f.out = nfa_add_state(b);
        if (b->failed)
            return f;
        add_epsilon(b, f.start, left.start);
        add_epsilon(b, f.start, right.start);
        add_epsilon(b, left.out, f.out);
        add_epsilon(b, right.out, f.out);
        return f;
    }

    case AST_STAR: {
        Frag inner = build(b, node->u.child);
        if (b->failed)
            return f;
        f.start = nfa_add_state(b);
        f.out = nfa_add_state(b);
        if (b->failed)
            return f;
        add_epsilon(b, f.start, inner.start); /* enter body */
        add_epsilon(b, f.start, f.out);       /* skip (zero reps) */
        add_epsilon(b, inner.out, inner.start); /* loop */
        add_epsilon(b, inner.out, f.out);     /* exit after body */
        return f;
    }

    case AST_PLUS: {
        Frag inner = build(b, node->u.child);
        if (b->failed)
            return f;
        f.start = nfa_add_state(b);
        f.out = nfa_add_state(b);
        if (b->failed)
            return f;
        add_epsilon(b, f.start, inner.start);   /* must enter body */
        add_epsilon(b, inner.out, inner.start); /* loop */
        add_epsilon(b, inner.out, f.out);       /* exit */
        return f;
    }

    case AST_QUESTION: {
        Frag inner = build(b, node->u.child);
        if (b->failed)
            return f;
        f.start = nfa_add_state(b);
        f.out = nfa_add_state(b);
        if (b->failed)
            return f;
        add_epsilon(b, f.start, inner.start); /* one */
        add_epsilon(b, f.start, f.out);       /* zero */
        add_epsilon(b, inner.out, f.out);
        return f;
    }
    }

    b->failed = 1;
    return f;
}

/* ---- public: build / free / print ------------------------------------- */

Nfa *nfa_from_ast(const AstNode *ast)
{
    if (!ast)
        return NULL;

    Nfa *nfa = calloc(1, sizeof(*nfa));
    if (!nfa)
        return NULL;

    nfa->start = -1;
    nfa->accept = -1;

    Builder b = {.nfa = nfa, .failed = 0};
    Frag frag = build(&b, ast);

    if (b.failed || frag.start < 0 || frag.out < 0) {
        nfa_free(nfa);
        return NULL;
    }

    nfa->start = frag.start;
    nfa->accept = frag.out;
    return nfa;
}

void nfa_free(Nfa *nfa)
{
    if (!nfa)
        return;
    for (size_t i = 0; i < nfa->nstates; i++) {
        NfaState *s = &nfa->states[i];
        for (size_t j = 0; j < s->ntrans; j++)
            free(s->trans[j].ranges);
        free(s->trans);
    }
    free(nfa->states);
    free(nfa);
}

static void print_trans(const NfaTrans *t)
{
    switch (t->type) {
    case NFA_TRANS_EPSILON:
        fputs("ε", stdout);
        break;
    case NFA_TRANS_CHAR:
        if (t->ch >= 32 && t->ch < 127)
            printf("'%c'", t->ch);
        else
            printf("0x%02x", t->ch);
        break;
    case NFA_TRANS_DOT:
        fputs(".", stdout);
        break;
    case NFA_TRANS_ANCHOR_START:
        fputs("^", stdout);
        break;
    case NFA_TRANS_ANCHOR_END:
        fputs("$", stdout);
        break;
    case NFA_TRANS_CLASS:
        putchar('[');
        if (t->negated)
            putchar('^');
        for (size_t i = 0; i < t->nranges; i++) {
            CharRange r = t->ranges[i];
            if (i > 0)
                putchar(' ');
            if (r.lo == r.hi)
                printf("%c", r.lo);
            else
                printf("%c-%c", r.lo, r.hi);
        }
        putchar(']');
        break;
    }
}

void nfa_print(const Nfa *nfa)
{
    if (!nfa) {
        puts("(null NFA)");
        return;
    }

    printf("NFA: start=q%d accept=q%d  (%zu states)\n",
           nfa->start, nfa->accept, nfa->nstates);

    for (size_t i = 0; i < nfa->nstates; i++) {
        const NfaState *s = &nfa->states[i];
        printf("  q%d:", s->id);
        if (s->id == nfa->accept)
            fputs("  (accept)", stdout);
        if (s->ntrans == 0) {
            putchar('\n');
            continue;
        }
        putchar('\n');
        for (size_t j = 0; j < s->ntrans; j++) {
            const NfaTrans *t = &s->trans[j];
            fputs("      --", stdout);
            print_trans(t);
            printf("--> q%d\n", t->to);
        }
    }
}

/* ---- simulation ------------------------------------------------------- */

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

/*
 * Epsilon-closure of `set` at string position `pos` (0..len).
 * Anchor transitions are treated like conditional epsilons.
 * `set` is a byte array of length nstates (0/1).
 */
static void epsilon_closure(const Nfa *nfa, unsigned char *set,
                            size_t pos, size_t len)
{
    int *stack = malloc(nfa->nstates * sizeof(int));
    if (!stack)
        return;

    int sp = 0;
    for (size_t i = 0; i < nfa->nstates; i++) {
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
                follow = 1;
                break;
            case NFA_TRANS_ANCHOR_START:
                follow = (pos == 0);
                break;
            case NFA_TRANS_ANCHOR_END:
                follow = (pos == len);
                break;
            default:
                break;
            }
            if (follow && t->to >= 0 && !set[t->to]) {
                set[t->to] = 1;
                stack[sp++] = t->to;
            }
        }
    }

    free(stack);
}

static void move(const Nfa *nfa, const unsigned char *cur, unsigned char *next,
                 unsigned char c)
{
    memset(next, 0, nfa->nstates);
    for (size_t i = 0; i < nfa->nstates; i++) {
        if (!cur[i])
            continue;
        const NfaState *s = &nfa->states[i];
        for (size_t j = 0; j < s->ntrans; j++) {
            const NfaTrans *t = &s->trans[j];
            if (trans_matches_char(t, c) && t->to >= 0)
                next[t->to] = 1;
        }
    }
}

int nfa_match(const Nfa *nfa, const char *input)
{
    if (!nfa || !input || nfa->nstates == 0 || nfa->start < 0)
        return 0;

    size_t len = strlen(input);
    unsigned char *cur = calloc(nfa->nstates, 1);
    unsigned char *next = calloc(nfa->nstates, 1);
    if (!cur || !next) {
        free(cur);
        free(next);
        return 0;
    }

    cur[nfa->start] = 1;
    epsilon_closure(nfa, cur, 0, len);

    for (size_t i = 0; i < len; i++) {
        move(nfa, cur, next, (unsigned char)input[i]);
        /* swap */
        unsigned char *tmp = cur;
        cur = next;
        next = tmp;
        epsilon_closure(nfa, cur, i + 1, len);
    }

    int ok = cur[nfa->accept] ? 1 : 0;
    free(cur);
    free(next);
    return ok;
}
