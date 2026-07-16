#include "regex.h"

#include <stdio.h>
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

static void expect_match(const char *pattern, const char *input, int want)
{
    char err[128];
    Regex *re = regex_compile(pattern, REGEX_DEFAULT, err, sizeof err);
    if (!re) {
        fprintf(stderr, "FAIL  compile /%s/: %s\n", pattern, err);
        g_fail++;
        return;
    }
    int got = regex_match(re, input);
    if (got != want) {
        fprintf(stderr, "FAIL  /%s/ on \"%s\": got %d want %d\n",
                pattern, input, got, want);
        g_fail++;
    } else {
        printf("ok    /%s/ %s \"%s\"\n",
               pattern, want ? "matches" : "rejects", input);
        g_pass++;
    }
    regex_free(re);
}

static void expect_match_flags(const char *pattern, unsigned flags,
                               const char *input, int want, const char *tag)
{
    char err[128];
    Regex *re = regex_compile(pattern, flags, err, sizeof err);
    if (!re) {
        fprintf(stderr, "FAIL  compile %s /%s/: %s\n", tag, pattern, err);
        g_fail++;
        return;
    }
    int got = regex_match(re, input);
    if (got != want) {
        fprintf(stderr, "FAIL  %s /%s/ on \"%s\": got %d want %d\n",
                tag, pattern, input, got, want);
        g_fail++;
    } else {
        printf("ok    %s /%s/ %s \"%s\"\n",
               tag, pattern, want ? "matches" : "rejects", input);
        g_pass++;
    }
    regex_free(re);
}

static void expect_match_min(const char *pattern, const char *input, int want)
{
    expect_match_flags(pattern, REGEX_MINIMIZE, input, want, "min");
}

static void expect_match_icase(const char *pattern, const char *input, int want)
{
    expect_match_flags(pattern, REGEX_ICASE, input, want, "icase");
}

static void expect_compile_fail(const char *pattern)
{
    char err[128];
    memset(err, 0, sizeof err);
    Regex *re = regex_compile(pattern, REGEX_DEFAULT, err, sizeof err);
    if (re) {
        fprintf(stderr, "FAIL  compile /%s/ should have failed\n",
                pattern ? pattern : "(null)");
        regex_free(re);
        g_fail++;
    } else {
        check(err[0] != '\0', "error message set on bad pattern");
        printf("ok    compile rejects %s (%s)\n",
               pattern ? pattern : "NULL", err);
        g_pass++;
    }
}

static void test_reuse(void)
{
    char err[128];
    Regex *re = regex_compile("a(b|c)*d", REGEX_DEFAULT, err, sizeof err);
    if (!re) {
        fprintf(stderr, "FAIL  reuse compile: %s\n", err);
        g_fail++;
        return;
    }
    int ok = regex_match(re, "ad")
          && regex_match(re, "abd")
          && regex_match(re, "acbcd")
          && !regex_match(re, "aXd")
          && !regex_match(re, "ab");
    check(ok, "one compiled regex, multiple matches");
    regex_free(re);
}

static void test_null_safety(void)
{
    regex_free(NULL); /* must not crash */
    check(regex_match(NULL, "a") == 0, "regex_match(NULL, ...) is 0");

    char err[64];
    Regex *re = regex_compile("a", REGEX_DEFAULT, err, sizeof err);
    if (!re) {
        g_fail++;
        return;
    }
    check(regex_match(re, NULL) == 0, "regex_match(re, NULL) is 0");
    regex_free(re);
}

static void test_default_vs_minimize_agree(void)
{
    const char *pattern = "a(b|c)*d";
    const char *inputs[] = {"", "ad", "abd", "acd", "acbcd", "aXd", "ab", "x"};
    char err[128];

    Regex *raw = regex_compile(pattern, REGEX_DEFAULT, err, sizeof err);
    Regex *min = regex_compile(pattern, REGEX_MINIMIZE, err, sizeof err);
    if (!raw || !min) {
        fprintf(stderr, "FAIL  default vs min compile\n");
        regex_free(raw);
        regex_free(min);
        g_fail++;
        return;
    }

    int ok = 1;
    for (size_t i = 0; i < sizeof inputs / sizeof inputs[0]; i++) {
        if (regex_match(raw, inputs[i]) != regex_match(min, inputs[i])) {
            ok = 0;
            fprintf(stderr, "FAIL  disagree on \"%s\"\n", inputs[i]);
        }
    }
    check(ok, "REGEX_DEFAULT and REGEX_MINIMIZE agree on language");
    regex_free(raw);
    regex_free(min);
}

static void test_regex_valid(void)
{
    check(regex_valid("a") == 1, "regex_valid(\"a\")");
    check(regex_valid("a(b|c)*d") == 1, "regex_valid(\"a(b|c)*d\")");
    check(regex_valid("") == 1, "regex_valid(\"\") empty pattern");
    check(regex_valid("[a-z]+") == 1, "regex_valid(\"[a-z]+\")");
    check(regex_valid(NULL) == 0, "regex_valid(NULL)");
    check(regex_valid("*") == 0, "regex_valid(\"*\")");
    check(regex_valid("(") == 0, "regex_valid(\"(\")");
    check(regex_valid("[a-z") == 0, "regex_valid(\"[a-z\")");
    check(regex_valid("\\") == 0, "regex_valid(\"\\\")");
}

static void test_last_error(void)
{
    char err[128];

    /* Successful compile clears last error. */
    Regex *re = regex_compile("abc", REGEX_DEFAULT, err, sizeof err);
    check(re != NULL, "compile abc succeeds");
    check(err[0] == '\0', "errmsg empty on success");
    check(regex_last_error()[0] == '\0', "last_error empty on success");
    regex_free(re);

    /* Failed compile fills both errmsg and last_error. */
    re = regex_compile("*", REGEX_DEFAULT, err, sizeof err);
    check(re == NULL, "compile * fails");
    check(err[0] != '\0', "errmsg set on failure");
    check(err[strlen(err)] == '\0', "errmsg is NUL-terminated");
    check(regex_last_error()[0] != '\0', "last_error set on failure");
    check(strstr(regex_last_error(), "parse error") != NULL,
          "last_error mentions parse error");
    check(strcmp(err, regex_last_error()) == 0,
          "errmsg matches last_error");

    /* NULL errmsg still updates last_error. */
    re = regex_compile("(", REGEX_DEFAULT, NULL, 0);
    check(re == NULL, "compile ( fails with NULL errmsg");
    check(regex_last_error()[0] != '\0',
          "last_error set when errmsg is NULL");
    check(strstr(regex_last_error(), "expected") != NULL
          || strstr(regex_last_error(), "parse error") != NULL,
          "last_error has useful text for unclosed group");

    /* regex_valid failure also sets last_error. */
    check(regex_valid("[") == 0, "regex_valid(\"[\") fails");
    check(regex_last_error()[0] != '\0',
          "last_error set after regex_valid failure");

    /* Tiny errmsg buffer is still NUL-terminated. */
    char tiny[2];
    memset(tiny, 'X', sizeof tiny);
    re = regex_compile("*", REGEX_DEFAULT, tiny, sizeof tiny);
    check(re == NULL, "compile fails into tiny buffer");
    check(tiny[sizeof tiny - 1] == '\0', "tiny errmsg is NUL-terminated");
}

static void test_ast_size_and_dfa_count(void)
{
    char err[128];
    Regex *re = regex_compile("a", REGEX_DEFAULT, err, sizeof err);
    if (!re) {
        g_fail++;
        return;
    }
    /* Single literal → one AST node; DFA has at least start + accept (+ maybe dead). */
    check(regex_ast_size(re) == 1, "regex_ast_size(\"a\") == 1");
    check(regex_dfa_state_count(re) >= 2, "regex_dfa_state_count(\"a\") >= 2");
    check(regex_ast_size(NULL) == 0, "regex_ast_size(NULL) == 0");
    check(regex_dfa_state_count(NULL) == 0, "regex_dfa_state_count(NULL) == 0");
    regex_free(re);

    re = regex_compile("ab", REGEX_DEFAULT, err, sizeof err);
    if (!re) {
        g_fail++;
        return;
    }
    /* CONCAT(a, b) → 3 nodes */
    check(regex_ast_size(re) == 3, "regex_ast_size(\"ab\") == 3");
    regex_free(re);

    re = regex_compile("a*", REGEX_DEFAULT, err, sizeof err);
    if (!re) {
        g_fail++;
        return;
    }
    /* STAR(LITERAL) → 2 nodes */
    check(regex_ast_size(re) == 2, "regex_ast_size(\"a*\") == 2");
    regex_free(re);

    re = regex_compile("a|b", REGEX_MINIMIZE, err, sizeof err);
    if (!re) {
        g_fail++;
        return;
    }
    /* ALT(a, b) → 3 nodes; minimize may shrink DFA but AST size is pre-DFA. */
    check(regex_ast_size(re) == 3, "regex_ast_size(\"a|b\") == 3 with MINIMIZE");
    check(regex_dfa_state_count(re) >= 1, "minimized DFA still has states");
    regex_free(re);
}

/* ICASE: multi-node trees still free cleanly after a full apply. */
static void test_icase_tree_frees(void)
{
    char err[128];
    /* Nested structure exercises concat/alt/star paths under ICASE. */
    Regex *re = regex_compile("a(b|[c-e])*F", REGEX_ICASE, err, sizeof err);
    if (!re) {
        fprintf(stderr, "FAIL  icase tree compile: %s\n", err);
        g_fail++;
        return;
    }
    int ok = regex_match(re, "aBcf")
          && regex_match(re, "ABCF")
          && !regex_match(re, "aX");
    check(ok, "icase tree matches after full rewrite");
    regex_free(re); /* must not leak rewritten class ranges */
    check(1, "icase tree frees without crash");
}

int main(void)
{
    /* --- compile success + match --- */
    expect_match("a", "a", 1);
    expect_match("a", "b", 0);
    expect_match("a", "", 0);
    expect_match("a", "aa", 0);
    expect_match("ab", "ab", 1);
    expect_match("a|b", "a", 1);
    expect_match("a|b", "b", 1);
    expect_match("a|b", "c", 0);
    expect_match("a*", "", 1);
    expect_match("a*", "aaa", 1);
    expect_match("a+", "", 0);
    expect_match("a+", "aa", 1);
    expect_match("a?", "", 1);
    expect_match("a?", "a", 1);
    expect_match("a?", "aa", 0);
    expect_match(".", "x", 1);
    expect_match(".", "xy", 0);
    expect_match("[a-z]+", "hello", 1);
    expect_match("[a-z]+", "Hello", 0);
    expect_match("^[0-9]+$", "42", 1);
    expect_match("^[0-9]+$", "42a", 0);
    expect_match("a(b|c)*d", "ad", 1);
    expect_match("a(b|c)*d", "acbcd", 1);
    expect_match("a(b|c)*d", "aXd", 0);
    expect_match("^$", "", 1);
    expect_match("^$", "a", 0);
    expect_match(".*", "anything", 1);

    /* --- minimized path --- */
    expect_match_min("a*", "", 1);
    expect_match_min("a*", "aaa", 1);
    expect_match_min("a(b|c)*d", "abd", 1);
    expect_match_min("a(b|c)*d", "aXd", 0);
    expect_match_min("^[0-9]+$", "42", 1);

    /* --- case-insensitive: literals --- */
    expect_match_icase("a", "a", 1);
    expect_match_icase("a", "A", 1);
    expect_match_icase("A", "a", 1);
    expect_match_icase("A", "A", 1);
    expect_match_icase("a", "b", 0);
    expect_match_icase("abc", "AbC", 1);
    expect_match_icase("abc", "ABC", 1);
    expect_match_icase("abc", "abc", 1);
    expect_match_icase("abc", "ab", 0);
    expect_match_icase("Hello", "hello", 1);
    expect_match_icase("Hello", "HELLO", 1);
    expect_match_icase("Hello", "hElLo", 1);
    expect_match_icase("Hello", "hell", 0);

    /* case-insensitive: character classes */
    expect_match_icase("[a-z]", "m", 1);
    expect_match_icase("[a-z]", "M", 1);
    expect_match_icase("[A-Z]", "m", 1);
    expect_match_icase("[A-Z]", "M", 1);
    expect_match_icase("[a-z]+", "Hello", 1);
    expect_match_icase("[a-z]+", "HELLO", 1);
    expect_match_icase("[a-c]", "B", 1);
    expect_match_icase("[a-c]", "d", 0);
    expect_match_icase("[a-c]", "D", 0);
    expect_match_icase("[abc]", "A", 1);
    expect_match_icase("[abc]", "B", 1);
    expect_match_icase("[abc]", "D", 0);

    /* case-insensitive: negated classes */
    expect_match_icase("[^a]", "b", 1);
    expect_match_icase("[^a]", "B", 1);
    expect_match_icase("[^a]", "a", 0);
    expect_match_icase("[^a]", "A", 0);
    expect_match_icase("[^a-z]", "0", 1);
    expect_match_icase("[^a-z]", "5", 1);
    expect_match_icase("[^a-z]", "m", 0);
    expect_match_icase("[^a-z]", "M", 0);
    expect_match_icase("[^abc]", "d", 1);
    expect_match_icase("[^abc]", "D", 1);
    expect_match_icase("[^abc]", "A", 0);
    expect_match_icase("[^abc]", "b", 0);

    /* case-insensitive with quantifiers / groups */
    expect_match_icase("a+", "AaA", 1);
    expect_match_icase("a(b|c)*d", "AbD", 1);
    expect_match_icase("a(b|c)*d", "ACBCD", 1);
    expect_match_icase("a(b|c)*d", "aXd", 0);

    /* ICASE + MINIMIZE together */
    expect_match_flags("Hello", REGEX_ICASE | REGEX_MINIMIZE, "hello", 1,
                       "icase+min");
    expect_match_flags("[a-z]+", REGEX_ICASE | REGEX_MINIMIZE, "Hello", 1,
                       "icase+min");
    expect_match_flags("[^a]", REGEX_ICASE | REGEX_MINIMIZE, "A", 0,
                       "icase+min");

    /* without ICASE, case still matters (sanity) */
    expect_match("a", "A", 0);
    expect_match("[a-z]", "M", 0);
    expect_match("[^a]", "A", 1); /* 'A' is not 'a' when case-sensitive */

    /* --- bad patterns / error handling --- */
    expect_compile_fail(NULL);
    expect_compile_fail("*");
    expect_compile_fail("+");
    expect_compile_fail("?");
    expect_compile_fail("(");
    expect_compile_fail(")");
    expect_compile_fail("[");
    expect_compile_fail("[a-z");
    expect_compile_fail("\\");
    expect_compile_fail("(a|b");

    /* errmsg optional */
    {
        Regex *re = regex_compile("*", REGEX_DEFAULT, NULL, 0);
        check(re == NULL, "compile fails with NULL errmsg buffer");
        regex_free(re);
    }

    /* empty errmsg_size should still fail cleanly */
    {
        char err[1] = {'x'};
        Regex *re = regex_compile("*", REGEX_DEFAULT, err, 0);
        check(re == NULL, "compile fails with errmsg_size 0");
        check(err[0] == 'x', "errmsg_size 0 does not write buffer");
    }

    test_reuse();
    test_null_safety();
    test_default_vs_minimize_agree();
    test_regex_valid();
    test_last_error();
    test_ast_size_and_dfa_count();
    test_icase_tree_frees();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
