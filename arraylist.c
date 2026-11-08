#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include "arraylist.h"

int al_init (arraylist_t *list, unsigned cap) {

    assert (cap > 0);
    list->data = malloc (cap * sizeof(void *));
    if (!list->data) return -1;

    list->capacity = cap;
    list->length = 0;

    return 0;
}

int al_destroy (arraylist_t *list){
    if (list->length > 0) return -1;
    free (list->data);

    return 0;

}

unsigned al_length (arraylist_t *list) {
    return list->length;
}

int al_append (arraylist_t *list, void *item) {

    if (list->length == list->capacity) {
        unsigned newlen = list->capacity * 2;
        void **newdata = realloc (list->data, newlen * sizeof(void *));

        if (!newdata) return -1;

        list->capacity = newlen;
        list->data = newdata;
    }

    list->data[list->length] = item;
    list->length++;

    return 0;
}


void * al_pop (arraylist_t *list){

    if (list->length == 0) return NULL;

    list->length--;

    return list->data[list->length];
}