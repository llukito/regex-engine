#include "ast.h"

#include <stdio.h>
#include <stdlib.h>

static AstNode *ast_alloc(AstType type)
{
    AstNode *node = calloc(1, sizeof(*node));
    if (node)
        node->type = type;
    return node;
}

AstNode *ast_empty(void)
{
    return ast_alloc(AST_EMPTY);
}

AstNode *ast_literal(unsigned char c)
{
    AstNode *node = ast_alloc(AST_LITERAL);
    if (node)
        node->u.literal = c;
    return node;
}

AstNode *ast_dot(void)
{
    return ast_alloc(AST_DOT);
}

AstNode *ast_anchor_start(void)
{
    return ast_alloc(AST_ANCHOR_START);
}

AstNode *ast_anchor_end(void)
{
    return ast_alloc(AST_ANCHOR_END);
}

AstNode *ast_char_class(int negated, CharRange *ranges, size_t nranges)
{
    AstNode *node = ast_alloc(AST_CHAR_CLASS);
    if (!node) {
        free(ranges);
        return NULL;
    }
    node->u.cclass.negated = negated;
    node->u.cclass.ranges = ranges;
    node->u.cclass.nranges = nranges;
    return node;
}

AstNode *ast_concat(AstNode *left, AstNode *right)
{
    AstNode *node = ast_alloc(AST_CONCAT);
    if (!node) {
        ast_free(left);
        ast_free(right);
        return NULL;
    }
    node->u.binary.left = left;
    node->u.binary.right = right;
    return node;
}

AstNode *ast_alt(AstNode *left, AstNode *right)
{
    AstNode *node = ast_alloc(AST_ALT);
    if (!node) {
        ast_free(left);
        ast_free(right);
        return NULL;
    }
    node->u.binary.left = left;
    node->u.binary.right = right;
    return node;
}

AstNode *ast_star(AstNode *child)
{
    AstNode *node = ast_alloc(AST_STAR);
    if (!node) {
        ast_free(child);
        return NULL;
    }
    node->u.child = child;
    return node;
}

AstNode *ast_plus(AstNode *child)
{
    AstNode *node = ast_alloc(AST_PLUS);
    if (!node) {
        ast_free(child);
        return NULL;
    }
    node->u.child = child;
    return node;
}

AstNode *ast_question(AstNode *child)
{
    AstNode *node = ast_alloc(AST_QUESTION);
    if (!node) {
        ast_free(child);
        return NULL;
    }
    node->u.child = child;
    return node;
}

void ast_free(AstNode *node)
{
    if (!node)
        return;

    switch (node->type) {
    case AST_CHAR_CLASS:
        free(node->u.cclass.ranges);
        break;
    case AST_CONCAT:
    case AST_ALT:
        ast_free(node->u.binary.left);
        ast_free(node->u.binary.right);
        break;
    case AST_STAR:
    case AST_PLUS:
    case AST_QUESTION:
        ast_free(node->u.child);
        break;
    default:
        break;
    }
    free(node);
}

const char *ast_type_name(AstType type)
{
    switch (type) {
    case AST_EMPTY:        return "EMPTY";
    case AST_LITERAL:      return "LITERAL";
    case AST_DOT:          return "DOT";
    case AST_ANCHOR_START: return "ANCHOR_START";
    case AST_ANCHOR_END:   return "ANCHOR_END";
    case AST_CHAR_CLASS:   return "CHAR_CLASS";
    case AST_CONCAT:       return "CONCAT";
    case AST_ALT:          return "ALT";
    case AST_STAR:         return "STAR";
    case AST_PLUS:         return "PLUS";
    case AST_QUESTION:     return "QUESTION";
    }
    return "UNKNOWN";
}

static void print_indent(int indent)
{
    for (int i = 0; i < indent; i++)
        fputs("  ", stdout);
}

void ast_print(const AstNode *node, int indent)
{
    if (!node) {
        print_indent(indent);
        puts("(null)");
        return;
    }

    print_indent(indent);
    fputs(ast_type_name(node->type), stdout);

    switch (node->type) {
    case AST_LITERAL:
        if (node->u.literal >= 32 && node->u.literal < 127)
            printf(" '%c'\n", node->u.literal);
        else
            printf(" 0x%02x\n", node->u.literal);
        break;

    case AST_CHAR_CLASS:
        printf(" %s", node->u.cclass.negated ? "negated " : "");
        putchar('[');
        for (size_t i = 0; i < node->u.cclass.nranges; i++) {
            CharRange r = node->u.cclass.ranges[i];
            if (i > 0)
                putchar(' ');
            if (r.lo == r.hi)
                printf("%c", r.lo);
            else
                printf("%c-%c", r.lo, r.hi);
        }
        puts("]");
        break;

    case AST_CONCAT:
    case AST_ALT:
        putchar('\n');
        ast_print(node->u.binary.left, indent + 1);
        ast_print(node->u.binary.right, indent + 1);
        break;

    case AST_STAR:
    case AST_PLUS:
    case AST_QUESTION:
        putchar('\n');
        ast_print(node->u.child, indent + 1);
        break;

    default:
        putchar('\n');
        break;
    }
}
