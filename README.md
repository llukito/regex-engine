# regex-engine

A small regular-expression engine in C:

1. **Parse** a pattern into an AST  
2. **Thompson construction** → NFA with ε-transitions  
3. **Subset construction** → DFA  
4. **Minimize** the DFA (optional; same language, fewer states)  
5. **Match** by running the DFA (full-string match)

Most callers only need the public API in `regex.h`.

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
src/regex.h    public API: compile / match / free
src/regex.c    pipeline wrapper (AST → NFA → DFA → match)
src/ast.h      AST node types, constructors, grammar comment
src/ast.c      AST construction, free, pretty-print
src/parser.h   regex_parse() API
src/parser.c   recursive-descent parser
src/nfa.h      NFA types, Thompson build, match, print
src/nfa.c      fragment-based NFA construction + simulation
src/dfa.h      DFA types, subset construction, minimize, match, print
src/dfa.c      subset construction, minimization, DFA matcher
src/match.c    CLI: pattern + string → MATCH / NOMATCH
tests/         unit tests and a small demo
```

## Usage

### Library (recommended)

```c
#include "regex.h"

#include <stdio.h>

int main(void)
{
    char err[128];
    Regex *re = regex_compile("a(b|c)*d", REGEX_DEFAULT, err, sizeof err);
    if (!re) {
        fprintf(stderr, "compile error: %s\n", err);
        return 1;
    }

    if (regex_match(re, "abbd"))
        puts("MATCH");
    else
        puts("NOMATCH");

    regex_free(re);

    /* Quick syntax check without retaining a compiled form: */
    if (!regex_valid("a(b|c)*d"))
        fprintf(stderr, "%s\n", regex_last_error());

    return 0;
}
```

Flags can be OR'd together:

| Flag | Effect |
|------|--------|
| `REGEX_DEFAULT` | no special options |
| `REGEX_MINIMIZE` | minimize the DFA after construction |
| `REGEX_ICASE` | case-insensitive literals and character classes |

```c
Regex *re = regex_compile("a(b|c)*d", REGEX_MINIMIZE, err, sizeof err);
Regex *ci = regex_compile("Hello", REGEX_ICASE, err, sizeof err);
/* ci matches "hello", "HELLO", "hElLo", … */
```

### CLI matcher

```sh
make build/match
./build/match 'a(b|c)*d' abbd              # prints MATCH
./build/match '^[0-9]+$' 42a               # prints NOMATCH
./build/match --min 'a(b|c)*d' abbd        # minimized DFA
./build/match --icase 'Hello' hElLo        # case-insensitive MATCH
./build/match --min --icase '[a-z]+' Hi    # both flags
```

### Tests

```sh
make test
```
