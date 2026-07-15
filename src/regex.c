#include "regex.h"

#include "parser.h"
#include "nfa.h"
#include "dfa.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Regex {
    Dfa *dfa;
};

static void set_err(char *errmsg, size_t errmsg_size, const char *msg)
{
    if (errmsg && errmsg_size > 0) {
        snprintf(errmsg, errmsg_size, "%s", msg);
        errmsg[errmsg_size - 1] = '\0';
    }
}

Regex *regex_compile(const char *pattern, int flags,
                     char *errmsg, size_t errmsg_size)
{
    if (errmsg && errmsg_size > 0)
        errmsg[0] = '\0';

    if (!pattern) {
        set_err(errmsg, errmsg_size, "NULL pattern");
        return NULL;
    }

    char parse_err[256];
    AstNode *ast = regex_parse(pattern, parse_err, sizeof parse_err);
    if (!ast) {
        if (errmsg && errmsg_size > 0) {
            if (parse_err[0])
                snprintf(errmsg, errmsg_size, "parse error: %s", parse_err);
            else
                set_err(errmsg, errmsg_size, "parse error");
            errmsg[errmsg_size - 1] = '\0';
        }
        return NULL;
    }

    Nfa *nfa = nfa_from_ast(ast);
    ast_free(ast);
    if (!nfa) {
        set_err(errmsg, errmsg_size, "NFA construction failed");
        return NULL;
    }

    Dfa *dfa = dfa_from_nfa(nfa);
    nfa_free(nfa);
    if (!dfa) {
        set_err(errmsg, errmsg_size, "DFA construction failed");
        return NULL;
    }

    if (flags & REGEX_MINIMIZE) {
        Dfa *min = dfa_minimize(dfa);
        dfa_free(dfa);
        if (!min) {
            set_err(errmsg, errmsg_size, "DFA minimization failed");
            return NULL;
        }
        dfa = min;
    }

    Regex *re = malloc(sizeof(*re));
    if (!re) {
        dfa_free(dfa);
        set_err(errmsg, errmsg_size, "out of memory");
        return NULL;
    }
    re->dfa = dfa;
    return re;
}

int regex_match(const Regex *re, const char *input)
{
    if (!re || !re->dfa || !input)
        return 0;
    return dfa_match(re->dfa, input);
}

void regex_free(Regex *re)
{
    if (!re)
        return;
    dfa_free(re->dfa);
    free(re);
}
