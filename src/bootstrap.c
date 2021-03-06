
/*
 * This is bootstrap code. It is a simple and stupid fscanf-based
 * implementation of grammar definition parser.
 */

#include <stdio.h>
#include <limits.h>

#include "boo.h"
#include "pool.h"
#include "vector.h"
#include "grammar.h"

#define BOO_LITERAL             1

typedef enum {
    s_lhs, s_rhs
} parser_state_t;

static symbol_t *
bootstrap_add_symbol(boo_grammar_t *grammar, boo_vector_t *lhs_lookup, boo_str_t *token, boo_uint_t flags)
{
    symbol_t *symbol;
    boo_lhs_lookup_t *lookup;
    boo_uint_t i, t;

    symbol = symtab_resolve(grammar->symtab, token);

    if(symbol == NULL) {
        token->data = pstrdup(grammar->pool, token->data, token->len);

        if(token->data == NULL) {
            fprintf(stderr, "insufficient memory\n");
            return NULL;
        }

        symbol = symtab_add(grammar->symtab, token, boo_symbol_to_code(grammar->num_symbols));

        if(symbol == NULL) {
            fprintf(stderr, "insufficient memory\n");
            return NULL;
        }

        /*
         * Treat symbols in capitals as tokens
         */
        t = 1;
        for(i = 0 ; i != token->len ; i++) {
            if(token->data[i] >= 'a' && token->data[i] <= 'z') {
                t = 0;
            }
        }

        if(t || (flags & BOO_LITERAL)) {
            symbol->value |= BOO_TOKEN;
        }

        lookup = vector_append(lhs_lookup);

        if(lookup == NULL) {
            fprintf(stderr, "insufficient memory\n");
            return NULL;
        }

        lookup->rules = NULL;
        lookup->name = symbol->name;
        lookup->literal = (flags & BOO_LITERAL) ? 1 : 0;
        lookup->token = (symbol->value & BOO_TOKEN) ? 1 : 0;

        grammar->num_symbols++;
    }

    return symbol;
}

static boo_int_t
bootstrap_process_type_directive(FILE *fin, boo_grammar_t *grammar, boo_vector_t *lhs_lookup, boo_str_t *token)
{
    boo_int_t rc;
    u_char *p = token->data;
    u_char *q = p + token->len;
    boo_str_t typename;
    char token_buffer[128];
    boo_uint_t flags;
    symbol_t *symbol;
    boo_type_t *type;
    boo_lhs_lookup_t *lookup;

    p += 5;

    if(p == q) {
        fprintf(stderr, "invalid type directive\n");
        return BOO_ERROR;
    }

    typename.data = p;
    typename.len = 0;

    while(p != q && *p != '>') {
        typename.len++;
        p++;
    }

    type = pcalloc(grammar->pool, sizeof(boo_type_t));

    if(type == NULL) {
        fprintf(stderr, "insufficient memory\n");
        return BOO_ERROR;
    }

    type->name.data = pstrdup(grammar->pool, typename.data, typename.len);

    if(type->name.data == NULL) {
        fprintf(stderr, "insufficient memory\n");
        return BOO_ERROR;
    }

    type->name.len = typename.len;

    boo_list_append(&grammar->types, &type->entry);

    while(!feof(fin)) {

        rc = fscanf(fin, "%128s", token_buffer);

        if(rc == EOF || rc != 1) {
            fprintf(stderr, "%%type directive broken at the end of file\n");
            return BOO_ERROR;
        }

        token->data = (u_char*)token_buffer;
        token->len = strlen(token_buffer);

        if(token->len == 1 && token->data[0] == ';') {
            return BOO_OK;
        }
        else {
            flags = 0;

            if(token->data[0] == '\'') {
                if(token->len != 3) {
                    fprintf(stderr, "Invalid token\n");
                    return BOO_ERROR;
                }

                token->data++;
                token->len -= 2;

                flags = BOO_LITERAL;
            }

            symbol = bootstrap_add_symbol(grammar, lhs_lookup, token, flags);

            if(symbol == NULL) {
                return BOO_ERROR;
            }

            symbol->type = type;

            lookup = lhs_lookup->elements;

            lookup += boo_code_to_symbol(symbol->value);

            lookup->type = type;
        }
    }
        
    return BOO_ERROR;
}

static boo_int_t
bootstrap_process_union_directive(FILE *fin, boo_grammar_t *grammar)
{
    boo_int_t rc;
    char token_buffer[128];
    boo_str_t token;
    boo_uint_t c, num_brakets, pos;

    if(feof(fin)) {
        return BOO_ERROR;
    }

    rc = fscanf(fin, "%128s", token_buffer);

    if(rc == EOF || rc != 1) {
        fprintf(stderr, "%%union directive broken at the end of file\n");
        return BOO_ERROR;
    }

    token.data = (u_char*)token_buffer;
    token.len = strlen(token_buffer);

    if(token.len != 1 || token.data[0] != '{') {
        fprintf(stderr, "unexpected token: \"%s\"\n", token_buffer);
        return BOO_ERROR;
    }

    pos = ftell(fin) - token.len;

    num_brakets = 1;

    for(;;) {
        c = fgetc(fin);

        if(c == EOF) {
            fprintf(stderr, "%%union directive broken at the end of file\n");
            return BOO_ERROR;
        }

        if(c == '{') {
            num_brakets++;
        }
        else if(c == '}') {
            num_brakets--;
        }

        if(num_brakets == 0) {
            break;
        }
    }

    grammar->union_code = pcalloc(grammar->pool, sizeof(boo_union_t));

    if(grammar->union_code == NULL) {
        fprintf(stderr, "insufficient memory\n");
        return BOO_ERROR;
    }

    grammar->union_code->start = pos;
    grammar->union_code->end = ftell(fin);

    return BOO_OK;
}

static boo_int_t
bootstrap_process_context_directive(FILE *fin, boo_grammar_t *grammar, boo_str_t *token)
{
    boo_int_t rc;
    char token_buffer[128];

    while(!feof(fin)) {

        rc = fscanf(fin, "%128s", token_buffer);

        if(rc == EOF || rc != 1) {
            fprintf(stderr, "%%context directive broken at the end of file\n");
            return BOO_ERROR;
        }

        token->data = (u_char*)token_buffer;
        token->len = strlen(token_buffer);

        if(token->len == 1 && token->data[0] == ';') {
            return BOO_OK;
        }
        else {
            if(grammar->context != NULL) {
                fprintf(stderr, "too many values in %%context directive\n");
                return BOO_ERROR;
            }

            grammar->context = pcalloc(grammar->pool, sizeof(boo_str_t));

            if(grammar->context == NULL) {
                return BOO_ERROR;
            }
        
            grammar->context->data = pstrdup(grammar->pool, token->data, token->len);

            if(grammar->context->data == NULL) {
                return BOO_ERROR;
            }

            grammar->context->len = token->len;
        }
    }
        
    return BOO_ERROR;
}

static boo_int_t
bootstrap_process_prefix_directive(FILE *fin, boo_grammar_t *grammar, boo_str_t *token)
{
    boo_int_t rc;
    char token_buffer[128];

    while(!feof(fin)) {

        rc = fscanf(fin, "%128s", token_buffer);

        if(rc == EOF || rc != 1) {
            fprintf(stderr, "%%prefix directive broken at the end of file\n");
            return BOO_ERROR;
        }

        token->data = (u_char*)token_buffer;
        token->len = strlen(token_buffer);

        if(token->len == 1 && token->data[0] == ';') {
            return BOO_OK;
        }
        else {
            if(grammar->prefix != NULL) {
                fprintf(stderr, "too many values in %%prefix directive\n");
                return BOO_ERROR;
            }

            grammar->prefix = pcalloc(grammar->pool, sizeof(boo_str_t));

            if(grammar->prefix == NULL) {
                return BOO_ERROR;
            }
        
            grammar->prefix->data = pstrdup(grammar->pool, token->data, token->len);

            if(grammar->prefix->data == NULL) {
                return BOO_ERROR;
            }

            grammar->prefix->len = token->len;
        }
    }
        
    return BOO_ERROR;
}

static boo_int_t
bootstrap_process_directive(FILE *fin, boo_grammar_t *grammar, boo_vector_t *lhs_lookup, boo_str_t *token)
{
    if(token->len > 4 && token->data[0] == 't' && token->data[1] == 'y' &&
        token->data[2] == 'p' && token->data[3] == 'e' && token->data[4] == '<')
    {
        return bootstrap_process_type_directive(fin, grammar, lhs_lookup, token);
    }
    else if(token->len == 5 && token->data[0] == 'u' && token->data[1] == 'n' &&
        token->data[2] == 'i' && token->data[3] == 'o' && token->data[4] == 'n')
    {
        return bootstrap_process_union_directive(fin, grammar);
    }
    else if(token->len == 7 && token->data[0] == 'c' && token->data[1] == 'o' &&
        token->data[2] == 'n' && token->data[3] == 't' && token->data[4] == 'e' &&
        token->data[5] == 'x' && token->data[6] == 't')
    {
        return bootstrap_process_context_directive(fin, grammar, token);
    }
    else if(token->len == 6 && token->data[0] == 'p' && token->data[1] == 'r' &&
        token->data[2] == 'e' && token->data[3] == 'f' && token->data[4] == 'i' &&
        token->data[5] == 'x')
    {
        return bootstrap_process_prefix_directive(fin, grammar, token);
    }

    return BOO_ERROR;
}

static boo_int_t
bootstrap_add_eof_symbol(boo_vector_t *rhs_vector)
{
    boo_uint_t *rhs;

    rhs = vector_append(rhs_vector);

    if(rhs == NULL) {
        fprintf(stderr, "insufficient memory\n");
        return BOO_ERROR;
    }

    *rhs = BOO_EOF | BOO_TOKEN;

    return BOO_OK;
}

boo_int_t bootstrap_parse_file(boo_grammar_t *grammar, pool_t *pool, boo_str_t *filename)
{
    FILE *fin;
    char token_buffer[128];
    boo_int_t rc;
    parser_state_t state;
    boo_str_t token;
    boo_uint_t lhs;
    boo_int_t has_lhs;
    boo_vector_t *rhs_vector;
    boo_vector_t *lhs_lookup;
    boo_vector_t *action_vector;
    symbol_t *symbol;
    boo_rule_t *rule;
    boo_action_t *action, **paction;
    boo_uint_t *rhs;
    boo_lhs_lookup_t *lookup;
    boo_uint_t flags, rule_no, pos, num_brakets, i;
    int c;
    boo_int_t seen_rules;

    state = s_lhs;
    has_lhs = 0;
    lhs = 0;
    rule_no = 1;
    seen_rules = 0;

    rhs_vector = vector_create(pool, sizeof(boo_uint_t), 8);

    if(rhs_vector == NULL) {
        fprintf(stderr, "insufficient memory\n");
        return BOO_ERROR;
    }

    lhs_lookup = vector_create(pool, sizeof(boo_lhs_lookup_t), (UCHAR_MAX + 1) * 2);

    if(lhs_lookup == NULL) {
        fprintf(stderr, "insufficient memory\n");
        return BOO_ERROR;
    }

    action_vector = vector_create(pool, sizeof(boo_action_t*), 8);

    if(action_vector == NULL) {
        fprintf(stderr, "insufficient memory\n");
        return BOO_ERROR;
    }

    fin = fopen((char*)filename->data, "r");

    if(fin == NULL) {
        fprintf(stderr, "cannot open input file %s\n", filename->data);
        return BOO_ERROR;
    }

    action = NULL;

    while(!feof(fin)) {

        rc = fscanf(fin, "%128s", token_buffer);

        if(rc == EOF && state == s_lhs && !has_lhs) {
            break;
        }

        if(rc != 1) {
            goto cleanup;
        }

        token.data = (u_char*)token_buffer;
        token.len = strlen(token_buffer);

        switch(state) {
            case s_lhs:
                if(token.len == 1 && token.data[0] == ':') {
                    action = NULL;
                    state = s_rhs;
                }
                else if(!has_lhs) {
                    if(token.len >= 1 && token.data[0] == '%') {
                        token.data++; token.len--;

                        rc = bootstrap_process_directive(fin, grammar, lhs_lookup, &token);

                        if(rc != BOO_OK) {
                            goto cleanup;
                        }
                    }
                    else {
                        symbol = bootstrap_add_symbol(grammar, lhs_lookup, &token, 0);

                        if(symbol == NULL) {
                            goto cleanup;
                        }

                        symbol->line = rule_no++;

                        lhs = symbol->value;
                        has_lhs = 1;

                        if(!seen_rules) {
                            grammar->root_symbol = lhs;
                            seen_rules = 1;
                        }
                    }
                }
                else {
                    fprintf(stderr, "only one symbol is allowed on the left-hand side");
                    goto cleanup;
                }
                break;
            case s_rhs:
                if(token.len != 0 && token.data[0] == '{') {

                    if(action != NULL) {
                        fprintf(stderr, "Duplicate action at position %d of a rule\n", rhs_vector->nelements);
                        goto cleanup;
                    }

                    if(rhs_vector->nelements == 0) {
                        fprintf(stderr, "Cannot instantiate an action at position 0 of a rule\n");
                        goto cleanup;
                    }

                    pos = ftell(fin) - token.len;

                    num_brakets = 0;

                    for(i = 0 ; i != token.len ; i++) {
                        if(token.data[i] == '{') {
                            num_brakets++;
                        }
                        else if(token.data[i] == '}') {
                            num_brakets--;
                        }
                    }            

                    if(num_brakets != 0) {
                        for(;;) {
                            c = fgetc(fin);

                            if(c == EOF) {
                                break;
                            }

                            if(c == '{') {
                                num_brakets++;
                            }
                            else if(c == '}') {
                                num_brakets--;
                            }

                            if(num_brakets == 0) {
                                break;
                            }
                        }
                    }

                    action = pcalloc(grammar->pool, sizeof(boo_action_t));

                    if(action == NULL) {
                        fprintf(stderr, "insufficient memory\n");
                        goto cleanup;
                    }

                    /*
                     * Determine action start and end positions in the source file
                     */
                    action->start = pos;

                    pos = ftell(fin);

                    action->end = pos;

                    /*
                     * Set the action's postion within the rule
                     */
                    action->pos = action_vector->nelements;
                }
                else if(token.len == 1 && (token.data[0] == '|' || token.data[0] == ';')) {

                    if(lhs == grammar->root_symbol) {
                        if(bootstrap_add_eof_symbol(rhs_vector) != BOO_OK)
                        {
                            goto cleanup;
                        }

                        paction = vector_append(action_vector);

                        if(paction == NULL) {
                            fprintf(stderr, "insufficient memory\n");
                            goto cleanup;
                        }

                        *paction = NULL;
                    }

                    paction = vector_append(action_vector);

                    if(paction == NULL) {
                        fprintf(stderr, "insufficient memory\n");
                        goto cleanup;
                    }

                    *paction = action;

                    // Consume action if there is any
                    action = NULL;

                    if(rhs_vector->nelements == 0) {
                        fprintf(stderr, "zero length rule\n");
                        goto cleanup;
                    }

                    if(rhs_vector->nelements + 1 != action_vector->nelements) {
                        fprintf(stderr, "action vector must be exactly one element longer than RHS vector\n");
                        goto cleanup;
                    }

                    rule = pcalloc(grammar->pool, sizeof(boo_rule_t));

                    if(rule == NULL) {
                        fprintf(stderr, "zero length rule\n");
                        goto cleanup;
                    }

                    rule->length = rhs_vector->nelements;
                    rule->lhs = lhs;

                    rule->rhs = palloc(grammar->pool, rule->length * sizeof(boo_uint_t));

                    if(rule->rhs == NULL) {
                        fprintf(stderr, "insufficient memory\n");
                        goto cleanup;
                    }

                    memcpy(rule->rhs, rhs_vector->elements, rule->length * sizeof(boo_uint_t));

                    rule->num_actions = action_vector->nelements;

                    rule->actions = palloc(grammar->pool, rule->num_actions * sizeof(boo_action_t*));

                    if(rule->actions == NULL) {
                        fprintf(stderr, "insufficient memory\n");
                        goto cleanup;
                    }

                    memcpy(rule->actions, action_vector->elements, rule->num_actions * sizeof(boo_action_t*));

                    grammar_add_rule(grammar, rule);

                    lookup = lhs_lookup->elements;

                    lookup += boo_code_to_symbol(lhs);

                    rule->lhs_hash_next = lookup->rules;
                    lookup->rules = rule;

                    if(token.data[0] == ';') {
                        state = s_lhs;
                        has_lhs = 0;
                    }

                    vector_clear(rhs_vector);
                    vector_clear(action_vector);
                }
                else {
                    flags = 0;

                    rhs = vector_append(rhs_vector);

                    if(rhs == NULL) {
                        fprintf(stderr, "insufficient memory\n");
                        goto cleanup;
                    }

                    paction = vector_append(action_vector);

                    if(paction == NULL) {
                        fprintf(stderr, "insufficient memory\n");
                        goto cleanup;
                    }

                    if(token.data[0] == '\'') {
                        if(token.len != 3) {
                            fprintf(stderr, "invalid token\n");
                            goto cleanup;
                        }

                        token.data++;
                        token.len -= 2;

                        flags = BOO_LITERAL;
                    }

                    symbol = bootstrap_add_symbol(grammar, lhs_lookup, &token, flags);

                    if(symbol == NULL) {
                        goto cleanup;
                    }

                    *rhs = symbol->value;
                    *paction = action;

                    // Consume action if there is any
                    action = NULL;
                }
                
                break;
        }
    }

    /*
     * Copy left-hand side symbol lookup table to a permanent place
     */
    grammar->lhs_lookup = palloc(grammar->pool, (grammar->num_symbols + 1) * sizeof(boo_lhs_lookup_t));    

    if(grammar->lhs_lookup == NULL) {
        fprintf(stderr, "insufficient memory\n");
        goto cleanup;
    }

    memcpy(grammar->lhs_lookup, lhs_lookup->elements, (grammar->num_symbols + 1) * sizeof(boo_lhs_lookup_t));

    fclose(fin);
    return BOO_OK;

cleanup:
    fclose(fin);
    return BOO_ERROR;
}
