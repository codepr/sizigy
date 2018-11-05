/*
 * BSD 2-Clause License
 *
 * Copyright (c) 2018, Andrea Giacomo Baldan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "list.h"


/*
 * Create a list, initializing all fields
 */
List *list_create(void) {
    List *l = malloc(sizeof(List));
    if (!l) {
        perror("malloc(3) failed");
        exit(EXIT_FAILURE);
    }
    // set default values to the List structure fields
    l->head = l->tail = NULL;
    l->len = 0L;
    return l;
}


/*
 * Attach a node to the head of a new list
 */
List *list_attach(List *l, list_node *head, unsigned long len) {
    // set default values to the List structure fields
    l->head = head;
    l->len = len;
    return l;
}

/*
 * Destroy a list, releasing all allocated memory
 */
void list_release(List *l, int deep) {
    if (!l) return;
    list_node *h = l->head;
    list_node *tmp;
    // free all nodes
    while (l->len--) {
        tmp = h->next;
        if (h) {
            if (h->data && deep == 1) free(h->data);
            free(h);
        }
        h = tmp;
    }
    // free List structure pointer
    free(l);
}


/*
 * Insert value at the front of the list
 * Complexity: O(1)
 */
List *list_head_insert(List *l, void *val) {
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
List *Listail_insert(List *l, void *val) {
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
