# regex-engine

A small regular-expression **parser** in C. It turns a pattern string into an
abstract syntax tree (AST); matching / compilation can be layered on top later.

## Supported syntax

| Feature            | Example        |
|--------------------|----------------|
| Literals           | `abc`          |
| Concatenation      | `ab`           |
| Alternation        | `a\|b`         |
| Quantifiers        | `a*` `a+` `a?` |
| Grouping           | `(ab)+`        |
| Dot wildcard       | `.`            |
| Anchors            | `^` `$`        |
| Character classes  | `[a-z]` `[^0-9]` |
| Escapes            | `\*` `\|`      |

### Precedence (tightest first)

1. `*` `+` `?` (postfix, apply to one atom)
2. Concatenation (juxtaposition)
3. `|` (alternation)

Parentheses override precedence.

## Layout

```
src/ast.h      AST node types, constructors, grammar comment
src/ast.c      AST construction, free, pretty-print
src/parser.h   regex_parse() API
src/parser.c   recursive-descent parser
tests/         unit tests and a small AST demo
```

## Usage

```c
#include "parser.h"

char err[128];
AstNode *ast = regex_parse("^[a-z]+|[0-9]+$", err, sizeof err);
if (!ast) {
    fprintf(stderr, "parse error: %s\n", err);
    return 1;
}
ast_print(ast, 0);   /* debug dump */
ast_free(ast);
```

Build and run tests:

```sh
make test
```

Pretty-print an AST from the shell:

```sh
make build/demo
./build/demo 'a(b|c)*d'
```
