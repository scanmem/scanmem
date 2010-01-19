/*
 $Id: list.c,v 1.12 2007-06-05 01:45:35+01 taviso Exp taviso $

 A simple linked list implementation.

 Copyright (C) 2006,2007,2009 Tavis Ormandy <taviso@sdf.lonestar.org>

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "config.h"

#include <stdlib.h>

#include "list.h"

/* create a new list */
list_t *l_init(void)
{
    return calloc(1, sizeof(list_t));
}

/* destroy the whole list */
void l_destroy(list_t * list)
{
    void *data;

    if (list == NULL)
        return;

    /* remove every element */
    while (list->size) {
        l_remove(list, NULL, &data);
        free(data);
    }

    free(list);
}

/*lint -e429 take custody of pointer data */

/* add a new element to the list */
int l_append(list_t * list, element_t * element, void *data)
{
    element_t *n = calloc(1, sizeof(element_t));

    if (n == NULL)
        return -1;

    n->data = data;

    /* insert at head or tail */
    if (element == NULL) {
        if (list->size == 0) {
            list->tail = n;
        }

        n->next = list->head;
        list->head = n;
    } else {

        /* insertion in the middle of a list */
        if (element->next == NULL) {
            list->tail = n;
        }

        n->next = element->next;
        element->next = n;
    }

    list->size++;

    return 0;
}

/* concatenate list src with list dst */
int l_concat(list_t *dst, list_t **src)
{
    void *data;
    element_t *n;

    n = (*src)->head;
    
    while (n) {
        l_remove(*src, NULL, &data);
        if (l_append(dst, NULL, data) == -1)
            return -1;
        
        n = (*src)->head;
    }
    
    l_destroy(*src);
    
    *src = NULL;
    
    return 0;
}

/* remove the element at element->next */
void l_remove(list_t * list, element_t * element, void **data)
{
    element_t *o;

    /* remove from head */
    if (element == NULL) {
        if (data) {
            *data = list->head->data;
        }

        o = list->head;

        list->head = o->next;

        if (list->size == 1) {
            list->tail = NULL;
        }
    } else {
        if (data) {
            *data = element->next->data;
        }

        o = element->next;


        if ((element->next = element->next->next) == NULL) {
            list->tail = element;
        }
    }

    if (data == NULL)
        free(o->data);

    free(o);

    list->size--;

    return;
}

/* remove the nth element */
void l_remove_nth(list_t * list, unsigned n, void **data)
{
    element_t *np = list->head;
    
    /* traverse to correct element */
    while (n--) {
        if ((np = np->next) == NULL)
            /* return */ abort();
    }
    
    l_remove(list, np, data);
}
