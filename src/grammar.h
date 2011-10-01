
#ifndef _BOO_GRAMMAR_H_
#define _BOO_GRAMMAR_H_

#include <limits.h>

#include "pool.h"
#include "list.h"
#include "symtab.h"

#define         BOO_TOKEN           0x80000000

#define boo_is_token(x) (((x) & BOO_TOKEN) != 0)

typedef struct {
    pool_t                  *pool;
    symtab_t                *symtab;
    boo_list_t              rules;
    boo_list_t              actions;
    boo_list_t              item_sets;

    boo_uint_t              num_item_sets;
    boo_uint_t              num_symbols;

    void                    **lhs_lookup;
} boo_grammar_t;

typedef struct boo_rule_s {
    boo_list_entry_t        entry;

    boo_uint_t              length;
    boo_uint_t              lhs;
    boo_uint_t              *rhs;

    boo_uint_t              action;

    struct boo_rule_s       *lhs_hash_next;
    void                    *booked_by_item_set;
} boo_rule_t;

typedef struct {
    boo_list_entry_t        entry;

    /*
     * Start and end offsets of the action code in the source file
     */
    off_t                   start, end;
} boo_action_t;

typedef struct {
    boo_list_entry_t        entry;
    boo_list_t              items;
    unsigned                closed:1;
} boo_lalr1_item_set_t;

typedef struct {
    boo_list_entry_t        entry;

    boo_uint_t              length;
    boo_uint_t              pos;
    boo_uint_t              lhs;
    boo_uint_t              *rhs; 

    unsigned                closed:1;
    unsigned                core:1;
} boo_lalr1_item_t;

#define grammar_lookup_rules_by_lhs(grammar,symbol) (boo_rule_t*)grammar->lhs_lookup[(symbol) - UCHAR_MAX - 1]

boo_grammar_t *grammar_create(pool_t*);
void grammar_add_rule(boo_grammar_t*, boo_rule_t*);
boo_int_t grammar_generate_lr_item_sets(boo_grammar_t*);

#endif
