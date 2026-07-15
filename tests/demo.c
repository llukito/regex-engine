#include "parser.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <pattern>\n", argv[0]);
        return 2;
    }

    char err[256];
    AstNode *ast = regex_parse(argv[1], err, sizeof err);
    if (!ast) {
        fprintf(stderr, "parse error: %s\n", err);
        return 1;
    }

    printf("AST for /%s/:\n", argv[1]);
    ast_print(ast, 0);
    ast_free(ast);
    return 0;
}
