#include <stdlib.h>
#include "linked_list.h"
/// Simple linked list implementation
bool linked_list_remove(linked_list ** head, linked_list * toremove)
{
	if((*head) == toremove)
	{
	  *head = (*head)->next;
	  toremove->next=NULL;
	  return true;
	} else if((*head) == NULL)
	{
		return false;
	}else
	{
		return linked_list_remove(&((*head)->next), toremove);
	}
}
void linked_list_add(linked_list ** head, linked_list * toadd)
{
	toadd->next=*head;
	*head = toadd;
}

