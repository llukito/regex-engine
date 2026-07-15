# regex-engine

A small regular-expression engine in C. Today it parses a pattern into an AST
and compiles that AST to an NFA via Thompson's construction. Matching is done
by simulating the NFA with epsilon-closure (a DFA layer can sit on top later).

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
src/nfa.h      NFA types, Thompson build, match, print
src/nfa.c      fragment-based NFA construction + simulation
tests/         unit tests and a small demo
```

## Usage

```c
#include "parser.h"
#include "nfa.h"

char err[128];
AstNode *ast = regex_parse("a(b|c)*d", err, sizeof err);
if (!ast) {
    fprintf(stderr, "parse error: %s\n", err);
    return 1;
}

Nfa *nfa = nfa_from_ast(ast);
ast_free(ast);
if (!nfa)
    return 1;

nfa_print(nfa);                      /* debug: state graph */
int ok = nfa_match(nfa, "abbd");     /* 1 = full match */
nfa_free(nfa);
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
