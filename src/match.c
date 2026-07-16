#include "regex.h"

#include <stdio.h>
#include <string.h>

/*
 * CLI full-match helper:
 *   match [--min] [--icase] <pattern> <string>
 * prints MATCH or NOMATCH (or an error on stderr).
 *
 * Flags may appear in any order before the pattern.
 */
static void usage(const char *argv0)
{
    fprintf(stderr, "usage: %s [--min] [--icase] <pattern> <string>\n", argv0);
    fprintf(stderr, "  --min    minimize the DFA before matching\n");
    fprintf(stderr, "  --icase  case-insensitive literals and character classes\n");
}

int main(int argc, char **argv)
{
    unsigned flags = REGEX_DEFAULT;
    int argi = 1;

    while (argi < argc && strncmp(argv[argi], "--", 2) == 0) {
        if (strcmp(argv[argi], "--min") == 0) {
            flags |= REGEX_MINIMIZE;
            argi++;
        } else if (strcmp(argv[argi], "--icase") == 0) {
            flags |= REGEX_ICASE;
            argi++;
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[argi]);
            usage(argv[0]);
            return 2;
        }
    }

    if (argc - argi != 2) {
        usage(argv[0]);
        return 2;
    }

    const char *pattern = argv[argi];
    const char *input = argv[argi + 1];

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
