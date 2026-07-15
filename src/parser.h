#ifndef REGEX_PARSER_H
#define REGEX_PARSER_H

#include "ast.h"

/*
 * Parse a null-terminated regular expression pattern into an AST.
 *
 * On success, returns the root of a newly allocated AST that the caller
 * must free with ast_free().
 *
 * On failure, returns NULL and, if errmsg is non-NULL, writes a short
 * diagnostic into errmsg (up to errmsg_size bytes, always NUL-terminated
 * when errmsg_size > 0).
 */
AstNode *regex_parse(const char *pattern, char *errmsg, size_t errmsg_size);

#endif /* REGEX_PARSER_H */
