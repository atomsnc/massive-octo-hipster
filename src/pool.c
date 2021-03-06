
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/user.h>
#include <sys/types.h>

#include <malloc.h>
#include <string.h>

#include "pool.h"

static size_t pagesize;

static void *palloc_large(pool_t *pool, size_t size);

static u_char *align_hi(u_char *ptr, size_t align) {
    return (u_char*)(((uintptr_t)(ptr + (align - 1))) & ~(align - 1));
}

void pool_init() {
    pagesize = sysconf(_SC_PAGESIZE);
}

pool_t *pool_create(FILE *debug) {
    pool_t *p;
    pchunk_t *c;
    u_char *start;

    p = malloc(pagesize);

    if(p == NULL) {
        perror("malloc");
        return NULL;
    }

    p->debug = debug != NULL ? debug : stderr;

    c = (pchunk_t*)(p + 1);

    p->chunks = p->current = c;
    p->large = NULL;
    c->next = NULL;
    
    start = (u_char*)p;

    c->allocated = (u_char*)(c + 1);
    c->end = start + pagesize;

    return p;
}

void pool_destroy(pool_t *pool) {
    plarge_t *l;
    pchunk_t *c,*f;

    l = pool->large;

    while(l != NULL) {
        free(l->mem);
        l = l->next;
    }

    c = pool->chunks;
    c = c->next;

    while(c != NULL) {
        f = c;
        c = c->next;

        free(f);
    }

    free(pool);
}

void *palloc(pool_t *pool, size_t size) {
    pchunk_t *c;
    void *p;
    u_char *start, *next;

    if(size == 0) {
        fprintf(stderr, "zero size allocation\n");
        abort();
    }

    if(size > (pagesize - sizeof(pchunk_t) - sizeof(pool_t))) {
        return palloc_large(pool, size);
    }

    c = pool->chunks;

    while(c != NULL) {
        next = align_hi(c->allocated + size, 16);

        if(next <= c->end) {
            p = next - size;
            c->allocated = next;
            return p;
        }

        c = c->next;
    }

    c = malloc(pagesize);

    if(c == NULL) {
        perror("malloc");
        return NULL;
    }

    start = (u_char*)c;

    c->next = NULL;
    c->allocated = align_hi(start + sizeof(pchunk_t), 16);
    c->end = start + pagesize;

    p = c->allocated;
    c->allocated += size;

    pool->current->next = c;
    pool->current = c;

    return p;
}

void *pcalloc(pool_t *pool, size_t size) {
    void *p;

    p = palloc(pool, size);

    bzero(p, size);

    return p;
}

u_char *pstrdup(pool_t *pool, u_char *str, size_t size) {
    u_char *p;

    p = palloc(pool, size);
    memcpy(p, str, size);
    
    return p;
}

static void *palloc_large(pool_t *pool, size_t size) {
    void *mem;
    plarge_t *large;

    mem = malloc(size);

    if(mem == NULL) {
        perror("malloc");
        return NULL;
    }

    large = palloc(pool, sizeof(plarge_t));

    if(large == NULL) {
        free(mem);
        return NULL;
    }

    large->mem = mem;
    large->next = pool->large;
    pool->large = large;

    return mem;
}
