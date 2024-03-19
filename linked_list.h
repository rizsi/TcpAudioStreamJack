#ifndef LINKED_LIST_H_
#define LINKED_LIST_H_

/// Simple linked list implementation

#include <stdbool.h>
typedef struct linked_list_str {
  struct linked_list_str * next;
} linked_list;

bool linked_list_remove(linked_list ** head, linked_list * toremove);
void linked_list_add(linked_list ** head, linked_list * toadd);

#endif /* LINKED_LIST_H_ */
