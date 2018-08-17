#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "list.h"


/*
 * Create a list, initializing all fields
 */
list_t *list_create(void) {
    list_t *l = malloc(sizeof(list_t));
    if (!l) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }
    // set default values to the list_t structure fields
    l->head = l->tail = NULL;
    l->len = 0L;
    return l;
}


/*
 * Attach a node to the head of a new list
 */
list_t *list_attach(list_t *l, list_node *head, unsigned long len) {
    // set default values to the list_t structure fields
    l->head = head;
    l->len = len;
    return l;
}

/*
 * Destroy a list, releasing all allocated memory
 */
void list_release(list_t *l) {
    if (!l) return;
    list_node *h = l->head;
    list_node *tmp;
    // free all nodes
    while (l->len--) {
        tmp = h->next;
        if (h) {
            if (h->data) free(h->data);
            free(h);
        }
        h = tmp;
    }
    // free list_t structure pointer
    free(l);
}


/*
 * Insert value at the front of the list
 * Complexity: O(1)
 */
list_t *list_head_insert(list_t *l, void *val) {
    list_node *new_node = malloc(sizeof(list_node));
    if (!new_node) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }
    new_node->data = val;
    if (l->len == 0) {
        l->head = l->tail = new_node;
        new_node->next = NULL;
    } else {
        new_node->next = l->head;
        l->head = new_node;
    }
    l->len++;
    return l;
}


/*
 * Insert value at the back of the list
 * Complexity: O(1)
 */
list_t *list_tail_insert(list_t *l, void *val) {
    list_node *new_node = malloc(sizeof(list_node));
    if (!new_node) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }
    new_node->data = val;
    new_node->next = NULL;
    if (l->len == 0) l->head = l->tail = new_node;
    else {
        l->tail->next = new_node;
        l->tail = new_node;
    }
    l->len++;
    return l;
}


list_node *list_remove_node(list_node *head, list_node *node, compare_func cmp) {
    if (!head)
        return NULL;
    if (cmp(head, node) == 0) {
        list_node *tmp_next = head->next;
        free(head);
        return tmp_next;
    }

    head->next = list_remove_node(head->next, node, cmp);

    return head;
}
