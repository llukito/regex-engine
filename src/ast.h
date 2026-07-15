#ifndef REGEX_AST_H
#define REGEX_AST_H

#include <stddef.h>

/*
 * Abstract syntax tree for regular expressions.
 *
 * Grammar (operators listed from lowest to highest precedence):
 *
 *   regex         ::=  alternation
 *   alternation   ::=  concatenation ( '|' concatenation )*
 *   concatenation ::=  repetition+
 *   repetition    ::=  atom ( '*' | '+' | '?' )?
 *   atom          ::=  '(' regex ')'
 *                   |  char_class
 *                   |  '.'
 *                   |  '^'
 *                   |  '$'
 *                   |  '\' any_char          (escaped literal)
 *                   |  literal_char          (non-metacharacter)
 *   char_class    ::=  '[' '^'? class_item+ ']'
 *   class_item    ::=  class_char ( '-' class_char )?
 *
 * Precedence summary:
 *   * + ?   bind tightest  (postfix, apply to a single atom)
 *   concat  binds next     (juxtaposition of repetitions)
 *   |       binds loosest  (alternation of concatenations)
 *
 * Parentheses override precedence by introducing a new regex nonterminal.
 */

typedef enum {
    AST_EMPTY,         /* epsilon (empty match) */
    AST_LITERAL,       /* single character */
    AST_DOT,           /* .  — any character */
    AST_ANCHOR_START,  /* ^  — start of string */
    AST_ANCHOR_END,    /* $  — end of string */
    AST_CHAR_CLASS,    /* [a-z] / [^a-z] */
    AST_CONCAT,        /* left then right */
    AST_ALT,           /* left | right */
    AST_STAR,          /* child* */
    AST_PLUS,          /* child+ */
    AST_QUESTION       /* child? */
} AstType;

/* Inclusive character range used inside character classes. */
typedef struct {
    unsigned char lo;
    unsigned char hi;
} CharRange;

typedef struct AstNode {
    AstType type;
    union {
        unsigned char literal;          /* AST_LITERAL */
        struct {
            int negated;                /* 1 if [^...] */
            CharRange *ranges;
            size_t nranges;
        } cclass;                       /* AST_CHAR_CLASS */
        struct {
            struct AstNode *left;
            struct AstNode *right;
        } binary;                       /* AST_CONCAT, AST_ALT */
        struct AstNode *child;          /* AST_STAR, AST_PLUS, AST_QUESTION */
    } u;
} AstNode;

/* Constructors — all return NULL on allocation failure. */
AstNode *ast_empty(void);
AstNode *ast_literal(unsigned char c);
AstNode *ast_dot(void);
AstNode *ast_anchor_start(void);
AstNode *ast_anchor_end(void);
AstNode *ast_char_class(int negated, CharRange *ranges, size_t nranges);
AstNode *ast_concat(AstNode *left, AstNode *right);
AstNode *ast_alt(AstNode *left, AstNode *right);
AstNode *ast_star(AstNode *child);
AstNode *ast_plus(AstNode *child);
AstNode *ast_question(AstNode *child);

/* Free an entire AST (safe to call with NULL). */
void ast_free(AstNode *node);

/* Pretty-print the AST to stdout (for debugging / tests). */
void ast_print(const AstNode *node, int indent);

/* Human-readable name for a node type. */
const char *ast_type_name(AstType type);

#endif /* REGEX_AST_H */
