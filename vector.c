#include "vector.h"
#include <malloc.h>

struct vector_t * vector_init(unsigned int initial_size, unsigned int initial_cap, unsigned int item_sz) {

    if (initial_size > initial_cap) {
        initial_cap = initial_size;
    }

    if (!initial_cap)
        initial_cap = initial_size;

    struct vector_t * rval = (struct vector_t *) malloc( sizeof(struct vector_t) );

    if (!rval)
        return NULL;

    rval->size = initial_size;
    rval->cap = initial_cap;
    rval->item_sz = item_sz;

    rval->data = malloc(item_sz * initial_cap);

    if (!rval->data) {
        free(rval);
        return NULL;
    }

    return rval;
}

void * vector_at(struct vector_t * v, unsigned int oft ) {
    if (oft > v->cap) {
        v->data = realloc(v->data, oft * v->item_sz);
        if (!v->data) {
            free(v);
            return NULL;
        }
        v->cap = oft;
    }
    return (char *)v->data + oft * v->item_sz;
}

void vector_destroy(struct vector_t * v) {
    if (!v)
        return;

    free(v->data);
    free(v);
}
