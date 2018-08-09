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
} list;

typedef int (*compare_func)(void *, void *);

list *list_create(void);
list *list_attach(list *, list_node *, unsigned long);
void list_release(list *);
list *list_head_insert(list *, void *);
list *list_tail_insert(list *, void *);
list_node *list_remove_node(list_node *, list_node *, compare_func);


#endif
