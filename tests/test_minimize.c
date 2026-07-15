#include "parser.h"
#include "nfa.h"
#include "dfa.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0;
static int g_fail = 0;

static void check(int cond, const char *msg)
{
    if (cond) {
        printf("ok    %s\n", msg);
        g_pass++;
    } else {
        fprintf(stderr, "FAIL  %s\n", msg);
        g_fail++;
    }
}

static Dfa *compile_dfa(const char *pattern)
{
    char err[128];
    AstNode *ast = regex_parse(pattern, err, sizeof err);
    if (!ast) {
        fprintf(stderr, "parse failed /%s/: %s\n", pattern, err);
        return NULL;
    }
    Nfa *nfa = nfa_from_ast(ast);
    ast_free(ast);
    if (!nfa) {
        fprintf(stderr, "nfa_from_ast failed /%s/\n", pattern);
        return NULL;
    }
    Dfa *dfa = dfa_from_nfa(nfa);
    nfa_free(nfa);
    if (!dfa)
        fprintf(stderr, "dfa_from_nfa failed /%s/\n", pattern);
    return dfa;
}

/* Minimized DFA must not have more states than the original. */
static void test_size_reduced_or_equal(const char *pattern)
{
    Dfa *dfa = compile_dfa(pattern);
    if (!dfa) {
        g_fail++;
        return;
    }
    Dfa *min = dfa_minimize(dfa);
    if (!min) {
        fprintf(stderr, "FAIL  dfa_minimize returned NULL for /%s/\n", pattern);
        dfa_free(dfa);
        g_fail++;
        return;
    }
    int ok = min->nstates > 0 && min->nstates <= dfa->nstates;
    if (!ok) {
        fprintf(stderr, "FAIL  /%s/ min states %zu > original %zu\n",
                pattern, min->nstates, dfa->nstates);
        g_fail++;
    } else {
        printf("ok    /%s/ states %zu -> %zu\n",
               pattern, dfa->nstates, min->nstates);
        g_pass++;
    }
    dfa_free(min);
    dfa_free(dfa);
}

/* Original and minimized must agree on `input`. */
static void expect_agree(const char *pattern, const char *input)
{
    Dfa *dfa = compile_dfa(pattern);
    if (!dfa) {
        g_fail++;
        return;
    }
    Dfa *min = dfa_minimize(dfa);
    if (!min) {
        fprintf(stderr, "FAIL  minimize NULL for /%s/\n", pattern);
        dfa_free(dfa);
        g_fail++;
        return;
    }

    int a = dfa_match(dfa, input);
    int b = dfa_match(min, input);
    if (a != b) {
        fprintf(stderr,
                "FAIL  /%s/ on \"%s\": raw=%d min=%d\n",
                pattern, input, a, b);
        dfa_print(dfa);
        dfa_print(min);
        g_fail++;
    } else {
        printf("ok    /%s/ on \"%s\" -> %s (raw=min)\n",
               pattern, input, a ? "MATCH" : "NOMATCH");
        g_pass++;
    }
    dfa_free(min);
    dfa_free(dfa);
}

static void expect_match_min(const char *pattern, const char *input, int want)
{
    Dfa *dfa = compile_dfa(pattern);
    if (!dfa) {
        g_fail++;
        return;
    }
    Dfa *min = dfa_minimize(dfa);
    dfa_free(dfa);
    if (!min) {
        g_fail++;
        return;
    }
    int got = dfa_match(min, input);
    if (got != want) {
        fprintf(stderr, "FAIL  min /%s/ on \"%s\": got %d want %d\n",
                pattern, input, got, want);
        g_fail++;
    } else {
        printf("ok    min /%s/ %s \"%s\"\n",
               pattern, want ? "matches" : "rejects", input);
        g_pass++;
    }
    dfa_free(min);
}

/* Minimizing twice (or thrice) must not change the state count. */
static void test_idempotent(const char *pattern)
{
    Dfa *dfa = compile_dfa(pattern);
    if (!dfa) {
        g_fail++;
        return;
    }
    Dfa *m1 = dfa_minimize(dfa);
    dfa_free(dfa);
    if (!m1) {
        g_fail++;
        return;
    }
    Dfa *m2 = dfa_minimize(m1);
    if (!m2) {
        dfa_free(m1);
        g_fail++;
        return;
    }
    Dfa *m3 = dfa_minimize(m2);
    if (!m3) {
        dfa_free(m1);
        dfa_free(m2);
        g_fail++;
        return;
    }
    int ok = m1->nstates == m2->nstates && m2->nstates == m3->nstates;
    if (!ok) {
        fprintf(stderr,
                "FAIL  minimize not idempotent /%s/: %zu -> %zu -> %zu\n",
                pattern, m1->nstates, m2->nstates, m3->nstates);
        g_fail++;
    } else {
        printf("ok    minimize idempotent /%s/ (%zu states)\n",
               pattern, m1->nstates);
        g_pass++;
    }
    dfa_free(m1);
    dfa_free(m2);
    dfa_free(m3);
}

/* a|a should collapse compared to a naive construction that keeps both arms. */
static void test_alt_same_literal_shrinks(void)
{
    Dfa *dfa = compile_dfa("a|a");
    if (!dfa) {
        g_fail++;
        return;
    }
    Dfa *min = dfa_minimize(dfa);
    if (!min) {
        dfa_free(dfa);
        g_fail++;
        return;
    }
    /* Language is {a}; minimized should be small (start + accept + maybe dead). */
    int ok = min->nstates <= dfa->nstates
          && dfa_match(min, "a") == 1
          && dfa_match(min, "b") == 0
          && dfa_match(min, "aa") == 0
          && dfa_match(min, "") == 0;
    check(ok, "a|a minimizes and matches {a}");
    printf("      (states %zu -> %zu)\n", dfa->nstates, min->nstates);
    dfa_free(min);
    dfa_free(dfa);
}

int main(void)
{
    const char *patterns[] = {
        "a",
        "ab",
        "a|b",
        "a|a",
        "a*",
        "a+",
        "a?",
        ".",
        ".*",
        "[a-z]",
        "[^0-9]",
        "^a",
        "a$",
        "^a$",
        "^$",
        "a(b|c)*d",
        "[a-z]+",
        "(a|b)*c",
        "ab?c+",
        "^[0-9]+$",
        "(ab)+",
        "a|b|c",
        "",
        "a|",
        "|b",
        "(a|b|c|d|e)*",
        "a*b*",
        "[a-zA-Z0-9_]+",
    };

    const char *inputs[] = {
        "",
        "a",
        "b",
        "c",
        "d",
        "ab",
        "ba",
        "aa",
        "bb",
        "abc",
        "abd",
        "acd",
        "ad",
        "acbcd",
        "aXd",
        "hello",
        "Hello",
        "42",
        "42a",
        "0",
        "5",
        "m",
        "xyz",
        "abab",
        "x",
        "aaabbb",
        "bbb",
        "aaa",
    };

    size_t np = sizeof patterns / sizeof patterns[0];
    size_t ni = sizeof inputs / sizeof inputs[0];

    /* Size never grows. */
    for (size_t p = 0; p < np; p++)
        test_size_reduced_or_equal(patterns[p]);

    test_alt_same_literal_shrinks();

    /* Idempotence on every pattern in the suite. */
    for (size_t p = 0; p < np; p++)
        test_idempotent(patterns[p]);

    /* Language equivalence: raw DFA vs minimized, full cross product. */
    for (size_t p = 0; p < np; p++) {
        for (size_t i = 0; i < ni; i++)
            expect_agree(patterns[p], inputs[i]);
    }

    /* Explicit expected results on the minimized machine. */
    expect_match_min("a", "a", 1);
    expect_match_min("a", "b", 0);
    expect_match_min("a", "aa", 0);
    expect_match_min("ab", "ab", 1);
    expect_match_min("a|b", "a", 1);
    expect_match_min("a|b", "c", 0);
    expect_match_min("a|a", "a", 1);
    expect_match_min("a|a", "aa", 0);
    expect_match_min("a*", "", 1);
    expect_match_min("a*", "aaa", 1);
    expect_match_min("a+", "", 0);
    expect_match_min("a+", "aa", 1);
    expect_match_min("a?", "", 1);
    expect_match_min("a?", "aa", 0);
    expect_match_min(".", "x", 1);
    expect_match_min(".", "xy", 0);
    expect_match_min(".*", "anything", 1);
    expect_match_min(".*", "", 1);
    expect_match_min("[a-z]", "m", 1);
    expect_match_min("[a-z]", "0", 0);
    expect_match_min("[^0-9]", "a", 1);
    expect_match_min("[^0-9]", "5", 0);
    expect_match_min("^a", "a", 1);
    expect_match_min("^a", "ba", 0);
    expect_match_min("a$", "a", 1);
    expect_match_min("a$", "ab", 0);
    expect_match_min("^a$", "a", 1);
    expect_match_min("^a$", "aa", 0);
    expect_match_min("^$", "", 1);
    expect_match_min("^$", "a", 0);
    expect_match_min("a(b|c)*d", "ad", 1);
    expect_match_min("a(b|c)*d", "acbcd", 1);
    expect_match_min("a(b|c)*d", "aXd", 0);
    expect_match_min("^[0-9]+$", "42", 1);
    expect_match_min("^[0-9]+$", "42a", 0);
    expect_match_min("(ab)+", "ab", 1);
    expect_match_min("(ab)+", "abab", 1);
    expect_match_min("(ab)+", "aba", 0);
    expect_match_min("a*b*", "aaabbb", 1);
    expect_match_min("a*b*", "ba", 0);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
