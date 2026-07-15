#include "parser.h"
#include "nfa.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0;
static int g_fail = 0;

static Nfa *compile(const char *pattern)
{
    char err[128];
    AstNode *ast = regex_parse(pattern, err, sizeof err);
    if (!ast) {
        fprintf(stderr, "parse failed for \"%s\": %s\n", pattern, err);
        return NULL;
    }
    Nfa *nfa = nfa_from_ast(ast);
    ast_free(ast);
    if (!nfa)
        fprintf(stderr, "nfa_from_ast failed for \"%s\"\n", pattern);
    return nfa;
}

static void expect_states(const char *pattern, size_t expected)
{
    Nfa *nfa = compile(pattern);
    if (!nfa) {
        g_fail++;
        return;
    }
    if (nfa->nstates != expected) {
        fprintf(stderr, "FAIL  \"%s\" states=%zu expected=%zu\n",
                pattern, nfa->nstates, expected);
        nfa_print(nfa);
        g_fail++;
    } else {
        printf("ok    \"%s\" has %zu states\n", pattern, expected);
        g_pass++;
    }
    nfa_free(nfa);
}

static size_t count_trans(const Nfa *nfa, NfaTransType type)
{
    size_t n = 0;
    for (size_t i = 0; i < nfa->nstates; i++) {
        for (size_t j = 0; j < nfa->states[i].ntrans; j++) {
            if (nfa->states[i].trans[j].type == type)
                n++;
        }
    }
    return n;
}

/* Literal 'a': s --'a'--> o  => 2 states, 1 char edge, 0 epsilon */
static void test_literal_structure(void)
{
    Nfa *nfa = compile("a");
    if (!nfa) {
        g_fail++;
        return;
    }
    int ok = nfa->nstates == 2
          && count_trans(nfa, NFA_TRANS_CHAR) == 1
          && count_trans(nfa, NFA_TRANS_EPSILON) == 0
          && nfa->states[nfa->start].ntrans == 1
          && nfa->states[nfa->start].trans[0].type == NFA_TRANS_CHAR
          && nfa->states[nfa->start].trans[0].ch == 'a'
          && nfa->states[nfa->start].trans[0].to == nfa->accept
          && nfa->states[nfa->accept].ntrans == 0;
    if (!ok) {
        fprintf(stderr, "FAIL  literal structure\n");
        nfa_print(nfa);
        g_fail++;
    } else {
        printf("ok    literal 'a' structure\n");
        g_pass++;
    }
    nfa_free(nfa);
}

/* Concat ab: two lit frags + 1 epsilon bridge => 4 states, 2 char, 1 eps */
static void test_concat_structure(void)
{
    Nfa *nfa = compile("ab");
    if (!nfa) {
        g_fail++;
        return;
    }
    int ok = nfa->nstates == 4
          && count_trans(nfa, NFA_TRANS_CHAR) == 2
          && count_trans(nfa, NFA_TRANS_EPSILON) == 1;
    if (!ok) {
        fprintf(stderr, "FAIL  concat structure\n");
        nfa_print(nfa);
        g_fail++;
    } else {
        printf("ok    concat 'ab' structure (4 states, 2 char, 1 ε)\n");
        g_pass++;
    }
    nfa_free(nfa);
}

/*
 * Alt a|b: 2 lits (4 states) + start + accept = 6
 * edges: 2 char + 4 epsilon (start->L, start->R, Lout->acc, Rout->acc)
 */
static void test_alt_structure(void)
{
    Nfa *nfa = compile("a|b");
    if (!nfa) {
        g_fail++;
        return;
    }
    int ok = nfa->nstates == 6
          && count_trans(nfa, NFA_TRANS_CHAR) == 2
          && count_trans(nfa, NFA_TRANS_EPSILON) == 4
          && nfa->states[nfa->start].ntrans == 2
          && nfa->states[nfa->accept].ntrans == 0;
    if (!ok) {
        fprintf(stderr, "FAIL  alt structure\n");
        nfa_print(nfa);
        g_fail++;
    } else {
        printf("ok    alt 'a|b' structure (6 states, 2 char, 4 ε)\n");
        g_pass++;
    }
    nfa_free(nfa);
}

/*
 * Star a*: lit (2) + start + accept = 4
 * epsilon: enter, skip, loop, exit = 4
 */
static void test_star_structure(void)
{
    Nfa *nfa = compile("a*");
    if (!nfa) {
        g_fail++;
        return;
    }
    int ok = nfa->nstates == 4
          && count_trans(nfa, NFA_TRANS_CHAR) == 1
          && count_trans(nfa, NFA_TRANS_EPSILON) == 4;
    if (!ok) {
        fprintf(stderr, "FAIL  star structure\n");
        nfa_print(nfa);
        g_fail++;
    } else {
        printf("ok    star 'a*' structure (4 states, 1 char, 4 ε)\n");
        g_pass++;
    }
    nfa_free(nfa);
}

/* Plus a+: lit (2) + start + accept = 4; eps: enter, loop, exit = 3 */
static void test_plus_structure(void)
{
    Nfa *nfa = compile("a+");
    if (!nfa) {
        g_fail++;
        return;
    }
    int ok = nfa->nstates == 4
          && count_trans(nfa, NFA_TRANS_CHAR) == 1
          && count_trans(nfa, NFA_TRANS_EPSILON) == 3;
    if (!ok) {
        fprintf(stderr, "FAIL  plus structure\n");
        nfa_print(nfa);
        g_fail++;
    } else {
        printf("ok    plus 'a+' structure (4 states, 1 char, 3 ε)\n");
        g_pass++;
    }
    nfa_free(nfa);
}

/* Optional a?: lit (2) + start + accept = 4; eps: enter, skip, exit = 3 */
static void test_question_structure(void)
{
    Nfa *nfa = compile("a?");
    if (!nfa) {
        g_fail++;
        return;
    }
    int ok = nfa->nstates == 4
          && count_trans(nfa, NFA_TRANS_CHAR) == 1
          && count_trans(nfa, NFA_TRANS_EPSILON) == 3;
    if (!ok) {
        fprintf(stderr, "FAIL  question structure\n");
        nfa_print(nfa);
        g_fail++;
    } else {
        printf("ok    question 'a?' structure (4 states, 1 char, 3 ε)\n");
        g_pass++;
    }
    nfa_free(nfa);
}

/* Class [a-z]: 2 states, one CLASS transition with range a-z */
static void test_class_structure(void)
{
    Nfa *nfa = compile("[a-z]");
    if (!nfa) {
        g_fail++;
        return;
    }
    int ok = 0;
    if (nfa->nstates == 2 && count_trans(nfa, NFA_TRANS_CLASS) == 1) {
        const NfaTrans *t = &nfa->states[nfa->start].trans[0];
        ok = t->type == NFA_TRANS_CLASS
          && !t->negated
          && t->nranges == 1
          && t->ranges[0].lo == 'a'
          && t->ranges[0].hi == 'z'
          && t->to == nfa->accept;
    }
    if (!ok) {
        fprintf(stderr, "FAIL  class structure\n");
        nfa_print(nfa);
        g_fail++;
    } else {
        printf("ok    class [a-z] structure\n");
        g_pass++;
    }
    nfa_free(nfa);
}

static void expect_match(const char *pattern, const char *input, int want)
{
    Nfa *nfa = compile(pattern);
    if (!nfa) {
        g_fail++;
        return;
    }
    int got = nfa_match(nfa, input);
    if (got != want) {
        fprintf(stderr, "FAIL  match /%s/ against \"%s\": got %d want %d\n",
                pattern, input, got, want);
        nfa_print(nfa);
        g_fail++;
    } else {
        printf("ok    /%s/ %s \"%s\"\n",
               pattern, want ? "matches" : "rejects", input);
        g_pass++;
    }
    nfa_free(nfa);
}

int main(void)
{
    /* --- state counts (Thompson fragment sizes) --- */
    expect_states("", 2);       /* empty: s --ε--> o */
    expect_states("a", 2);
    expect_states(".", 2);
    expect_states("^", 2);
    expect_states("$", 2);
    expect_states("[a-z]", 2);
    expect_states("ab", 4);
    expect_states("a|b", 6);
    expect_states("a*", 4);
    expect_states("a+", 4);
    expect_states("a?", 4);

    /* --- edge structure --- */
    test_literal_structure();
    test_concat_structure();
    test_alt_structure();
    test_star_structure();
    test_plus_structure();
    test_question_structure();
    test_class_structure();

    /* --- simulation sanity checks --- */
    expect_match("a", "a", 1);
    expect_match("a", "b", 0);
    expect_match("a", "", 0);
    expect_match("a", "aa", 0);          /* full match only */

    expect_match("ab", "ab", 1);
    expect_match("ab", "a", 0);
    expect_match("ab", "ba", 0);

    expect_match("a|b", "a", 1);
    expect_match("a|b", "b", 1);
    expect_match("a|b", "c", 0);
    expect_match("a|b", "ab", 0);

    expect_match("a*", "", 1);
    expect_match("a*", "a", 1);
    expect_match("a*", "aaa", 1);
    expect_match("a*", "b", 0);

    expect_match("a+", "", 0);
    expect_match("a+", "a", 1);
    expect_match("a+", "aa", 1);
    expect_match("a+", "b", 0);

    expect_match("a?", "", 1);
    expect_match("a?", "a", 1);
    expect_match("a?", "aa", 0);

    expect_match(".", "x", 1);
    expect_match(".", "", 0);
    expect_match(".", "xy", 0);

    expect_match("[a-z]", "m", 1);
    expect_match("[a-z]", "0", 0);
    expect_match("[^0-9]", "a", 1);
    expect_match("[^0-9]", "5", 0);

    expect_match("^a", "a", 1);
    expect_match("^a", "ba", 0);
    expect_match("a$", "a", 1);
    expect_match("a$", "ab", 0);
    expect_match("^a$", "a", 1);
    expect_match("^a$", "aa", 0);
    expect_match("^a$", "", 0);

    expect_match("a(b|c)*d", "ad", 1);
    expect_match("a(b|c)*d", "abd", 1);
    expect_match("a(b|c)*d", "acbcd", 1);
    expect_match("a(b|c)*d", "aXd", 0);
    expect_match("a(b|c)*d", "ab", 0);

    expect_match("[a-z]+", "hello", 1);
    expect_match("[a-z]+", "Hello", 0);
    expect_match("[a-zA-Z]+", "Hello", 1);
    expect_match("^[0-9]+$", "42", 1);
    expect_match("^[0-9]+$", "42a", 0);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
