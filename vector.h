#ifndef __VECTOR_H
#define __VECTOR_H

struct vector_t {
    unsigned int size;
    unsigned int cap;
    unsigned int item_sz;
    void * data;
};

struct vector_t * vector_init(unsigned int initial_size, unsigned int initial_cap, unsigned int item_sz);
void * vector_at(struct vector_t * v, unsigned int oft );
void vector_destroy(struct vector_t * v);

#endif
