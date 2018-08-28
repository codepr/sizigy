#ifndef LIST_H
#define LIST_H


struct _list_node {
    void *data;
    struct _list_node *next;
};


typedef struct _list_node list_node;


typedef struct {
    list_node *head;
    list_node *tail;
    unsigned long len;
} list_t;

typedef int (*compare_func)(void *, void *);

list_t *list_create(void);
list_t *list_attach(list_t *, list_node *, unsigned long);
void list_release(list_t *, int);
list_t *list_head_insert(list_t *, void *);
list_t *list_tail_insert(list_t *, void *);
list_node *list_remove_node(list_node *, list_node *, compare_func);


#endif
