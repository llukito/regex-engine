#include "regex.h"

#include <stdio.h>
#include <string.h>

/*
 * CLI full-match helper:
 *   match [--min] <pattern> <string>
 * prints MATCH or NOMATCH (or an error on stderr).
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
    int flags = do_min ? REGEX_MINIMIZE : REGEX_DEFAULT;

    char err[256];
    Regex *re = regex_compile(pattern, flags, err, sizeof err);
    if (!re) {
        fprintf(stderr, "compile error: %s\n", err);
        return 1;
    }

    int ok = regex_match(re, input);
    regex_free(re);

    puts(ok ? "MATCH" : "NOMATCH");
    return ok ? 0 : 1;
}
