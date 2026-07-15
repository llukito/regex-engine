#ifndef REGEX_ENGINE_H
#define REGEX_ENGINE_H

#include <stddef.h>

/*
 * Public regular-expression API.
 *
 * Hides the internal pipeline:
 *   pattern → AST → NFA (Thompson) → DFA (subset) [→ minimize] → match
 *
 * Matching is always a full-string match (like re.fullmatch).
 */

typedef struct Regex Regex;

/* Compile-time options for regex_compile(). OR together as needed. */
enum {
    REGEX_DEFAULT  = 0,      /* compile to a DFA, no minimization */
    REGEX_MINIMIZE = 1 << 0  /* minimize the DFA after construction */
};

/*
 * Compile a null-terminated pattern.
 *
 * On success, returns an opaque Regex* that the caller must free with
 * regex_free().
 *
 * On failure, returns NULL and, if errmsg is non-NULL and errmsg_size > 0,
 * writes a short diagnostic into errmsg (always NUL-terminated).
 */
Regex *regex_compile(const char *pattern, int flags,
                     char *errmsg, size_t errmsg_size);

/*
 * Full-match: returns 1 if the entire input matches the compiled pattern,
 * 0 otherwise. Safe to call with NULL re or input (returns 0).
 */
int regex_match(const Regex *re, const char *input);

/* Free a compiled regex (safe with NULL). */
void regex_free(Regex *re);

#endif /* REGEX_ENGINE_H */
