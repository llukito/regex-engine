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
#define REGEX_DEFAULT  0u
#define REGEX_MINIMIZE (1u << 0)  /* minimize the DFA after construction */
#define REGEX_ICASE    (1u << 1)  /* case-insensitive literals and classes */

/*
 * Compile a null-terminated pattern.
 *
 * On success, returns an opaque Regex* that the caller must free with
 * regex_free(). Clears the last-error buffer.
 *
 * On failure, returns NULL, records a diagnostic in the last-error buffer
 * (see regex_last_error()), and, if errmsg is non-NULL and errmsg_size > 0,
 * also copies that diagnostic into errmsg (always NUL-terminated).
 */
Regex *regex_compile(const char *pattern, unsigned flags,
                     char *errmsg, size_t errmsg_size);

/*
 * Return 1 if pattern compiles successfully under REGEX_DEFAULT, else 0.
 * Does not retain the compiled form. Updates regex_last_error() on failure.
 */
int regex_valid(const char *pattern);

/*
 * Full-match: returns 1 if the entire input matches the compiled pattern,
 * 0 otherwise. Safe to call with NULL re or input (returns 0).
 */
int regex_match(const Regex *re, const char *input);

/* Free a compiled regex (safe with NULL). */
void regex_free(Regex *re);

/*
 * Most recent diagnostic from regex_compile() / regex_valid().
 * Empty string after a successful compile. Never NULL; valid until the
 * next compile/valid call.
 */
const char *regex_last_error(void);

/*
 * Debug: number of AST nodes produced while compiling this regex
 * (counted before the AST is discarded). 0 if re is NULL.
 */
size_t regex_ast_size(const Regex *re);

/* Debug: number of DFA states in the compiled machine. 0 if re is NULL. */
size_t regex_dfa_state_count(const Regex *re);

#endif /* REGEX_ENGINE_H */
