#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "list.h"


/*
 * Create a list, initializing all fields
 */
list *list_create(void) {
    list *l = malloc(sizeof(list));
    // set default values to the list structure fields
    l->head = l->tail = NULL;
    l->len = 0L;
    return l;
}


/*
 * Attach a node to the head of a new list
 */
list *list_attach(list *l, list_node *head, unsigned long len) {
    // set default values to the list structure fields
    l->head = head;
    l->len = len;
    return l;
}

/*
 * Destroy a list, releasing all allocated memory
 */
void list_release(list *l) {
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
    // free list structure pointer
    free(l);
}


/*
 * Insert value at the front of the list
 * Complexity: O(1)
 */
list *list_head_insert(list *l, void *val) {
    list_node *new_node = malloc(sizeof(list_node));
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
list *list_tail_insert(list *l, void *val) {
    list_node *new_node = malloc(sizeof(list_node));
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
