# regex-engine

A small regular-expression engine in C:

1. **Parse** a pattern into an AST  
2. **Thompson construction** → NFA with ε-transitions  
3. **Subset construction** → DFA  
4. **Match** by running the DFA (full-string match)

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
src/dfa.h      DFA types, subset construction, match, print
src/dfa.c      subset construction + DFA matcher
src/match.c    CLI: pattern + string → MATCH / NOMATCH
tests/         unit tests and a small demo
```

## Usage

### Library

```c
#include "parser.h"
#include "nfa.h"
#include "dfa.h"

char err[128];
AstNode *ast = regex_parse("a(b|c)*d", err, sizeof err);
Nfa *nfa = nfa_from_ast(ast);
ast_free(ast);
Dfa *dfa = dfa_from_nfa(nfa);
nfa_free(nfa);

dfa_print(dfa);                   /* debug: transition table */
int ok = dfa_match(dfa, "abbd");  /* 1 = full match */
dfa_free(dfa);
```

### CLI matcher

```sh
make build/match
./build/match 'a(b|c)*d' abbd    # prints MATCH
./build/match '^[0-9]+$' 42a     # prints NOMATCH
```

### Tests

```sh
make test
```
