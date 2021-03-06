
#include <stdio.h>
#include <getopt.h>
#include <string.h>

#include "boo.h"
#include "grammar.h"
#include "lookahead.h"
#include "output.h"
#include "string.h"
#include "pool.h"

boo_int_t bootstrap_parse_file(boo_grammar_t *grammar, pool_t *pool, boo_str_t *filename);

boo_str_t target_filename = boo_string("a.out");
boo_str_t source_suffix = boo_string(".boo");
boo_str_t dest_suffix = boo_string(".h");
boo_str_t debug_suffix = boo_string(".debug");

static boo_int_t replace_suffix(pool_t *pool, boo_str_t *source_filename,
    boo_str_t *dest_filename, boo_str_t *source_suffix, boo_str_t *dest_suffix)
{
    u_char *p;

    dest_filename->len = source_filename->len - source_suffix->len + dest_suffix->len;

    dest_filename->data = palloc(pool, dest_filename->len + 1);

    if(dest_filename->data == NULL) {
        return BOO_ERROR;
    }

    p = dest_filename->data;

    memcpy(p, source_filename->data, source_filename->len - source_suffix->len);

    p += source_filename->len - source_suffix->len;

    memcpy(p, dest_suffix->data, dest_suffix->len);

    p += dest_suffix->len;

    *p = '\0';

    return BOO_OK;
}

boo_int_t build_project(char **filenames, boo_uint_t num_filenames) {
    boo_int_t result, i;
    pool_t *p, *tmp_pool;
    boo_str_t *_filenames, *output_filenames, *debug_filenames;
    boo_grammar_t *grammar;
    boo_output_t *output;

    p = pool_create(NULL);

    if(p == NULL) {
        return BOO_ERROR;
    }

    tmp_pool = pool_create(NULL);

    if(tmp_pool == NULL) {
        result = BOO_ERROR;
        goto cleanup;
    }

    _filenames = palloc(tmp_pool, sizeof(boo_str_t)*num_filenames);

    if(filenames == NULL) {
        result = BOO_ERROR;
        goto cleanup;
    }

    output_filenames = palloc(p, sizeof(boo_str_t)*num_filenames);

    if(output_filenames == NULL) {
        result = BOO_ERROR;
        goto cleanup;
    }

    debug_filenames = palloc(tmp_pool, sizeof(boo_str_t)*num_filenames);

    if(debug_filenames == NULL) {
        result = BOO_ERROR;
        goto cleanup;
    }

    for(i = 0;i < num_filenames;i++) {
        _filenames[i].data = (u_char*)filenames[i];
        _filenames[i].len = strlen(filenames[i]);

        result = replace_suffix(p, &_filenames[i], &output_filenames[i],
            &source_suffix, &dest_suffix);

        if(result != BOO_OK) {
            goto cleanup;
        }

        result = replace_suffix(tmp_pool, &_filenames[i], &debug_filenames[i],
            &source_suffix, &debug_suffix);

        if(result != BOO_OK) {
            goto cleanup;
        }
    }

    grammar = grammar_create(p);

    if(grammar == NULL) {
        result = BOO_ERROR;
        goto cleanup1;
    }

    grammar->debug = fopen((const char*)debug_filenames[0].data, "w");

    if(grammar->debug == NULL) {
        result = BOO_ERROR;
        goto cleanup1;
    }

    p->debug = grammar->debug;
    tmp_pool->debug = grammar->debug;

    result = bootstrap_parse_file(grammar, tmp_pool, &_filenames[0]);

    if(result != BOO_OK) {
        result = BOO_ERROR;
        goto cleanup1;
    }

    pool_destroy(tmp_pool);

    result = grammar_wrapup(grammar);

    if(result != BOO_OK) {
        result = BOO_ERROR;
        goto cleanup;
    }

    result = grammar_generate_lr_item_sets(grammar, &grammar->item_sets);

    if(result != BOO_OK) {
        result = BOO_ERROR;
        goto cleanup;
    }

    fprintf(grammar->debug, "LR item sets:\n");
    grammar_dump_item_sets(grammar->debug, grammar, &grammar->item_sets);

    result = lookahead_generate_item_sets(grammar, &grammar->reverse_item_sets);

    if(result != BOO_OK) {
        result = BOO_ERROR;
        goto cleanup;
    }

    result = lookahead_build(grammar, &grammar->reverse_item_sets);

    if(result != BOO_OK) {
        result = BOO_ERROR;
        goto cleanup;
    }

    fprintf(grammar->debug, "Lookahead item sets:\n");
    grammar_dump_item_sets(grammar->debug, grammar, &grammar->reverse_item_sets);

    output = output_create(p);

    if(output == NULL) {
        result = BOO_ERROR;
        goto cleanup;
    }

    output->debug = grammar->debug;

    output->file = fopen((const char*)output_filenames[0].data, "w");

    if(output->file == NULL) {
        result = BOO_ERROR;
        goto cleanup;
    }

    result = output_add_grammar(output, grammar);

    if(result != BOO_OK) {
        result = BOO_ERROR;
        goto cleanup;
    }

    result = output_codes(output, grammar);

    if(result != BOO_OK) {
        result = BOO_ERROR;
        goto cleanup;
    }

    result = output_symbols(output, grammar);

    if(result != BOO_OK) {
        result = BOO_ERROR;
        goto cleanup;
    }

    result = output_lookup(output, grammar);

    if(result != BOO_OK) {
        result = BOO_ERROR;
        goto cleanup;
    }

    result = output_actions(output, grammar, filenames[0]);

    if(result != BOO_OK) {
        result = BOO_ERROR;
        goto cleanup;
    }

    result = output_lhs(output, grammar);

    if(result != BOO_OK) {
        result = BOO_ERROR;
        goto cleanup;
    }

    result = output_rhs(output, grammar);

    if(result != BOO_OK) {
        result = BOO_ERROR;
        goto cleanup;
    }

    result = output_rules(output, grammar);

    if(result != BOO_OK) {
        result = BOO_ERROR;
        goto cleanup;
    }

    result = BOO_OK;

cleanup:
    pool_destroy(p);

    return result;

cleanup1:
    pool_destroy(tmp_pool);
    goto cleanup;
}

static struct option long_options[] = {
    {"debug", 0, 0, 'd'},
    {0, 0, 0, 0}
};

int main(int argc, char *argv[]) {
    int                 c;
//    int                 this_option_optind;
    int                 option_index;

    pool_init();

    while(1) {
//        this_option_optind = optind ? optind : 1;
        option_index = 0;

        c = getopt_long(argc, argv, "b:do:v", long_options, &option_index);

        if(c == -1) {
             break;
        }

        switch(c) {
            case 0:
                printf("option %s", long_options[option_index].name);
                if (optarg)
                    printf (" with arg %s", optarg);
                printf ("\n");
                break;

            case 'b':
#if 0
                result = platform_set_platform(optarg);

                if(result != BOO_OK) {
                    fprintf(stderr, "No support for platform %s is found\n", optarg);
                    return 255;
                }
#endif
                break;

            case 'd':
#if 0
                boodebug = 1;
#endif
                break;

            case 'o':
                target_filename.data = (u_char*)optarg;
                target_filename.len = strlen(optarg);
                break;

            case 'v':
#if 0
                show_commands = 1;
#endif
                break;

            case '?':
                break;
        }
    }

    if(optind < argc) {
        if(build_project(argv + optind, argc - optind) != BOO_OK)
            return 255;
    }
    else {
        fprintf(stderr, "No input files.\n");
    }

    return 0;
}
