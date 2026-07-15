#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0;
static int g_fail = 0;

static void expect_ok(const char *pattern, AstType root_type)
{
    char err[128];
    AstNode *ast = regex_parse(pattern, err, sizeof err);
    if (!ast) {
        fprintf(stderr, "FAIL  parse(\"%s\") -> error: %s\n", pattern, err);
        g_fail++;
        return;
    }
    if (ast->type != root_type) {
        fprintf(stderr, "FAIL  parse(\"%s\") root type %s, expected %s\n",
                pattern, ast_type_name(ast->type), ast_type_name(root_type));
        g_fail++;
    } else {
        printf("ok    parse(\"%s\") -> %s\n", pattern, ast_type_name(root_type));
        g_pass++;
    }
    ast_free(ast);
}

static void expect_fail(const char *pattern)
{
    char err[128];
    AstNode *ast = regex_parse(pattern, err, sizeof err);
    if (ast) {
        fprintf(stderr, "FAIL  parse(\"%s\") should have failed\n", pattern);
        ast_free(ast);
        g_fail++;
    } else {
        printf("ok    parse(\"%s\") fails as expected (%s)\n", pattern, err);
        g_pass++;
    }
}

/* Structural checks for precedence / shape. */

static void test_precedence_star_over_concat(void)
{
    char err[128];
    /* ab*  =>  CONCAT( LITERAL 'a', STAR(LITERAL 'b') ) */
    AstNode *ast = regex_parse("ab*", err, sizeof err);
    if (!ast || ast->type != AST_CONCAT
        || !ast->u.binary.left || ast->u.binary.left->type != AST_LITERAL
        || !ast->u.binary.right || ast->u.binary.right->type != AST_STAR) {
        fprintf(stderr, "FAIL  precedence ab*\n");
        ast_free(ast);
        g_fail++;
        return;
    }
    printf("ok    precedence: ab* is CONCAT(a, STAR(b))\n");
    g_pass++;
    ast_free(ast);
}

static void test_precedence_concat_over_alt(void)
{
    char err[128];
    /* a|bc  =>  ALT( LITERAL 'a', CONCAT(b, c) ) */
    AstNode *ast = regex_parse("a|bc", err, sizeof err);
    if (!ast || ast->type != AST_ALT
        || !ast->u.binary.left || ast->u.binary.left->type != AST_LITERAL
        || !ast->u.binary.right || ast->u.binary.right->type != AST_CONCAT) {
        fprintf(stderr, "FAIL  precedence a|bc\n");
        ast_free(ast);
        g_fail++;
        return;
    }
    printf("ok    precedence: a|bc is ALT(a, CONCAT(b, c))\n");
    g_pass++;
    ast_free(ast);
}

static void test_grouping(void)
{
    char err[128];
    /* (a|b)*  =>  STAR( ALT(a, b) ) */
    AstNode *ast = regex_parse("(a|b)*", err, sizeof err);
    if (!ast || ast->type != AST_STAR
        || !ast->u.child || ast->u.child->type != AST_ALT) {
        fprintf(stderr, "FAIL  grouping (a|b)*\n");
        ast_free(ast);
        g_fail++;
        return;
    }
    printf("ok    grouping: (a|b)* is STAR(ALT(a, b))\n");
    g_pass++;
    ast_free(ast);
}

static void test_char_class(void)
{
    char err[128];
    AstNode *ast = regex_parse("[a-z]", err, sizeof err);
    if (!ast || ast->type != AST_CHAR_CLASS
        || ast->u.cclass.negated
        || ast->u.cclass.nranges != 1
        || ast->u.cclass.ranges[0].lo != 'a'
        || ast->u.cclass.ranges[0].hi != 'z') {
        fprintf(stderr, "FAIL  [a-z]\n");
        ast_free(ast);
        g_fail++;
        return;
    }
    printf("ok    [a-z] range\n");
    g_pass++;
    ast_free(ast);

    ast = regex_parse("[^0-9]", err, sizeof err);
    if (!ast || ast->type != AST_CHAR_CLASS
        || !ast->u.cclass.negated
        || ast->u.cclass.nranges != 1
        || ast->u.cclass.ranges[0].lo != '0'
        || ast->u.cclass.ranges[0].hi != '9') {
        fprintf(stderr, "FAIL  [^0-9]\n");
        ast_free(ast);
        g_fail++;
        return;
    }
    printf("ok    [^0-9] negated range\n");
    g_pass++;
    ast_free(ast);
}

static void test_anchors_and_dot(void)
{
    char err[128];
    AstNode *ast = regex_parse("^a.b$", err, sizeof err);
    /* CONCAT tree: ((((^ a) .) b) $) left-associated */
    if (!ast || ast->type != AST_CONCAT) {
        fprintf(stderr, "FAIL  ^a.b$\n");
        ast_free(ast);
        g_fail++;
        return;
    }
    /* Walk right spine for $ */
    if (ast->u.binary.right->type != AST_ANCHOR_END) {
        fprintf(stderr, "FAIL  ^a.b$ missing end anchor\n");
        ast_free(ast);
        g_fail++;
        return;
    }
    /* Leftmost should eventually be ^ */
    AstNode *n = ast;
    while (n->type == AST_CONCAT)
        n = n->u.binary.left;
    if (n->type != AST_ANCHOR_START) {
        fprintf(stderr, "FAIL  ^a.b$ missing start anchor\n");
        ast_free(ast);
        g_fail++;
        return;
    }
    printf("ok    ^a.b$ anchors and concat\n");
    g_pass++;
    ast_free(ast);
}

int main(void)
{
    expect_ok("", AST_EMPTY);
    expect_ok("a", AST_LITERAL);
    expect_ok(".", AST_DOT);
    expect_ok("^", AST_ANCHOR_START);
    expect_ok("$", AST_ANCHOR_END);
    expect_ok("a*", AST_STAR);
    expect_ok("a+", AST_PLUS);
    expect_ok("a?", AST_QUESTION);
    expect_ok("ab", AST_CONCAT);
    expect_ok("a|b", AST_ALT);
    expect_ok("[a-z]", AST_CHAR_CLASS);
    expect_ok("[^a-z]", AST_CHAR_CLASS);
    expect_ok("(ab)+", AST_PLUS);
    expect_ok("\\*", AST_LITERAL);
    expect_ok("a|b|c", AST_ALT);

    expect_fail("*");
    expect_fail("+");
    expect_fail("?");
    expect_fail("(");
    expect_fail(")");
    expect_fail("[");
    expect_fail("[a-z");
    expect_fail("\\");

    test_precedence_star_over_concat();
    test_precedence_concat_over_alt();
    test_grouping();
    test_char_class();
    test_anchors_and_dot();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
