/*
    A very simple linked list implementation.

    This file is part of libscanmem.

    This library is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published
    by the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this library.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef LIST_H
#define LIST_H

typedef struct element {
    void *data;
    struct element *next;
} element_t;

typedef struct {
    size_t size;
    element_t *head;
    element_t *tail;
} list_t;

/* create a new list */
list_t *l_init(void);

/* destroy the whole list */
void l_destroy(list_t * list);

/* add a new element to the list */
int l_append(list_t * list, element_t * element, void *data);

/* remove the element at element->next */
void l_remove(list_t * list, element_t * element, void **data);

/* remove the nth element from head */
void l_remove_nth(list_t * list, size_t n, void **data);

/* remove all elements from *src, and append to dst */
int l_concat(list_t *dst, list_t **src);

#endif /* LIST_H */
