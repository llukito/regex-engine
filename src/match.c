#include "parser.h"
#include "nfa.h"
#include "dfa.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * CLI full-match helper:
 *   match [--min] <pattern> <string>
 * prints MATCH or NOMATCH (or an error on stderr).
 *
 * Pipeline: pattern -> AST -> NFA (Thompson) -> DFA (subset)
 *           [-> dfa_minimize if --min] -> dfa_match.
 */
static void usage(const char *argv0)
{
    fprintf(stderr, "usage: %s [--min] <pattern> <string>\n", argv0);
    fprintf(stderr, "  --min   minimize the DFA before matching\n");
}

int main(int argc, char **argv)
{
    int do_min = 0;
    int argi = 1;

    if (argi < argc && strcmp(argv[argi], "--min") == 0) {
        do_min = 1;
        argi++;
    }

    if (argc - argi != 2) {
        usage(argv[0]);
        return 2;
    }

    const char *pattern = argv[argi];
    const char *input = argv[argi + 1];

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

    if (do_min) {
        Dfa *min = dfa_minimize(dfa);
        dfa_free(dfa);
        if (!min) {
            fprintf(stderr, "DFA minimization failed\n");
            return 1;
        }
        dfa = min;
    }

    int ok = dfa_match(dfa, input);
    dfa_free(dfa);

    puts(ok ? "MATCH" : "NOMATCH");
    return ok ? 0 : 1;
}
