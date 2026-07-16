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

static int compile_both(const char *pattern, Nfa **out_nfa, Dfa **out_dfa)
{
    char err[128];
    AstNode *ast = regex_parse(pattern, err, sizeof err);
    if (!ast) {
        fprintf(stderr, "parse failed /%s/: %s\n", pattern, err);
        return 0;
    }
    Nfa *nfa = nfa_from_ast(ast);
    ast_free(ast);
    if (!nfa) {
        fprintf(stderr, "nfa_from_ast failed /%s/\n", pattern);
        return 0;
    }
    Dfa *dfa = dfa_from_nfa(nfa);
    if (!dfa) {
        fprintf(stderr, "dfa_from_nfa failed /%s/\n", pattern);
        nfa_free(nfa);
        return 0;
    }
    *out_nfa = nfa;
    *out_dfa = dfa;
    return 1;
}

/* Assert DFA and NFA agree on a single input. */
static void expect_agree(const char *pattern, const char *input)
{
    Nfa *nfa = NULL;
    Dfa *dfa = NULL;
    if (!compile_both(pattern, &nfa, &dfa)) {
        g_fail++;
        return;
    }
    int nm = nfa_match(nfa, input);
    int dm = dfa_match(dfa, input);
    if (nm != dm) {
        fprintf(stderr,
                "FAIL  /%s/ on \"%s\": nfa=%d dfa=%d\n",
                pattern, input, nm, dm);
        nfa_print(nfa);
        dfa_print(dfa);
        g_fail++;
    } else {
        printf("ok    /%s/ on \"%s\" -> %s (nfa=dfa)\n",
               pattern, input, nm ? "MATCH" : "NOMATCH");
        g_pass++;
    }
    dfa_free(dfa);
    nfa_free(nfa);
}

/* Assert both engines give `want` (and therefore agree). */
static void expect_match(const char *pattern, const char *input, int want)
{
    Nfa *nfa = NULL;
    Dfa *dfa = NULL;
    if (!compile_both(pattern, &nfa, &dfa)) {
        g_fail++;
        return;
    }
    int nm = nfa_match(nfa, input);
    int dm = dfa_match(dfa, input);
    if (nm != want || dm != want) {
        fprintf(stderr,
                "FAIL  /%s/ on \"%s\": nfa=%d dfa=%d want=%d\n",
                pattern, input, nm, dm, want);
        g_fail++;
    } else {
        printf("ok    /%s/ %s \"%s\"\n",
               pattern, want ? "matches" : "rejects", input);
        g_pass++;
    }
    dfa_free(dfa);
    nfa_free(nfa);
}

static void test_literal_dfa_shape(void)
{
    Nfa *nfa = NULL;
    Dfa *dfa = NULL;
    if (!compile_both("a", &nfa, &dfa)) {
        g_fail++;
        return;
    }
    /* At least start + dead; start accepts only after 'a'. */
    int ok = dfa->nstates >= 2
          && dfa_state_count(dfa) == dfa->nstates
          && dfa->start >= 0
          && dfa->dead >= 0
          && !dfa->states[dfa->start].accept
          && dfa->states[dfa->states[dfa->start].trans[(unsigned char)'a']].accept
          && !dfa->states[dfa->states[dfa->start].trans[(unsigned char)'b']].accept;
    check(ok, "literal 'a' DFA start-'a'->accept, 'b' not");
    check(dfa_state_count(NULL) == 0, "dfa_state_count(NULL) == 0");
    dfa_free(dfa);
    nfa_free(nfa);
}

static void test_total_transition_function(void)
{
    Nfa *nfa = NULL;
    Dfa *dfa = NULL;
    if (!compile_both("a|b", &nfa, &dfa)) {
        g_fail++;
        return;
    }
    int ok = 1;
    for (size_t i = 0; i < dfa->nstates && ok; i++) {
        for (int c = 0; c < DFA_ALPHABET; c++) {
            int t = dfa->states[i].trans[c];
            if (t < 0 || (size_t)t >= dfa->nstates) {
                ok = 0;
                break;
            }
        }
    }
    check(ok, "transition function is total (0..255 for every state)");
    dfa_free(dfa);
    nfa_free(nfa);
}

static void test_star_accepts_empty(void)
{
    Nfa *nfa = NULL;
    Dfa *dfa = NULL;
    if (!compile_both("a*", &nfa, &dfa)) {
        g_fail++;
        return;
    }
    check(dfa->states[dfa->start].accept, "a* start state accepts (empty match)");
    dfa_free(dfa);
    nfa_free(nfa);
}

int main(void)
{
    test_literal_dfa_shape();
    test_total_transition_function();
    test_star_accepts_empty();

    /* --- core operators: DFA == NFA language --- */
    const char *patterns[] = {
        "a",
        "ab",
        "a|b",
        "a*",
        "a+",
        "a?",
        ".",
        "[a-z]",
        "[^0-9]",
        "^a",
        "a$",
        "^a$",
        "^$",
        "$",
        "^",
        "a(b|c)*d",
        "[a-z]+",
        "[a-zA-Z0-9_]+",
        "a*",
        "(a|b)*c",
        "ab?c+",
        "^[0-9]+$",
        ".*",
        "a|b|c",
        "(ab)+",
        "",
        "a|",
        "|b",
    };
    const char *inputs[] = {
        "",
        "a",
        "b",
        "c",
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
        "hello_1",
        "42",
        "42a",
        "0",
        "5",
        "m",
        "xyz",
        "abab",
        "x",
    };

    size_t np = sizeof patterns / sizeof patterns[0];
    size_t ni = sizeof inputs / sizeof inputs[0];

    for (size_t p = 0; p < np; p++) {
        for (size_t i = 0; i < ni; i++)
            expect_agree(patterns[p], inputs[i]);
    }

    /* Spot checks with explicit expected results (both engines). */
    expect_match("a", "a", 1);
    expect_match("a", "b", 0);
    expect_match("a", "aa", 0);
    expect_match("ab", "ab", 1);
    expect_match("a|b", "a", 1);
    expect_match("a|b", "c", 0);
    expect_match("a*", "", 1);
    expect_match("a*", "aaa", 1);
    expect_match("a+", "", 0);
    expect_match("a+", "aa", 1);
    expect_match("a?", "", 1);
    expect_match("a?", "aa", 0);
    expect_match(".", "x", 1);
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
    expect_match("^$", "", 1);
    expect_match("^$", "a", 0);
    expect_match("a(b|c)*d", "ad", 1);
    expect_match("a(b|c)*d", "acbcd", 1);
    expect_match("a(b|c)*d", "aXd", 0);
    expect_match("^[0-9]+$", "42", 1);
    expect_match("^[0-9]+$", "42a", 0);
    expect_match(".*", "anything", 1);
    expect_match(".*", "", 1);
    expect_match("(ab)+", "ab", 1);
    expect_match("(ab)+", "abab", 1);
    expect_match("(ab)+", "aba", 0);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
