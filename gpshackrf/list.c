#include "list.h"

void listInit(listHead *head) {
	head->first = NULL;
	head->last = NULL;
}

listNode* listAdd(listHead *head, int data) {
	struct listNode *newNode = malloc(sizeof(struct listNode));

	newNode->data = data;
	// first node
	if (NULL == head->first) {
		head->first = newNode;
		head->last = newNode;
		newNode->next = NULL;
		newNode->prev = NULL;
	}
	else {
		head->last->next = newNode;
		newNode->prev = head->last;
		newNode->next = NULL;
		head->last = newNode;
	}

	return newNode;
}

void listRemove(listHead *head, listNode *node) {
	if (NULL == head || NULL == node) {
		return;
	}

	void *temp = node->next;
	// only node
	if (head->first == node && head->last == node) {
		head->first = NULL;
		head->last = NULL;
		goto done;
	}

	// unlink node
	node->prev->next = node->next;
	node->next->prev = node->prev;

	// was first
	if (head->first == node) {
		head->first = node->next;
	}

	// was last
	if (head->last == node) {
		head->last = node->prev;
	}

done:
	free(node);
}