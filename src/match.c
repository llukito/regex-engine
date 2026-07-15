#include "parser.h"
#include "nfa.h"
#include "dfa.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * CLI full-match helper:
 *   match <pattern> <string>
 * prints MATCH or NOMATCH (or an error on stderr).
 *
 * Pipeline: pattern -> AST -> NFA (Thompson) -> DFA (subset) -> dfa_match.
 */
int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <pattern> <string>\n", argv[0]);
        return 2;
    }

    const char *pattern = argv[1];
    const char *input = argv[2];

    char err[256];
    AstNode *ast = regex_parse(pattern, err, sizeof err);
    if (!ast) {
        fprintf(stderr, "parse error: %s\n", err);
        return 1;
    }

    Nfa *nfa = nfa_from_ast(ast);
    ast_free(ast);
    if (!nfa) {
        fprintf(stderr, "NFA construction failed\n");
        return 1;
    }

    Dfa *dfa = dfa_from_nfa(nfa);
    nfa_free(nfa);
    if (!dfa) {
        fprintf(stderr, "DFA construction failed\n");
        return 1;
    }

    int ok = dfa_match(dfa, input);
    dfa_free(dfa);

    puts(ok ? "MATCH" : "NOMATCH");
    return ok ? 0 : 1;
}
