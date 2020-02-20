#pragma once
#include <stdlib.h>

typedef struct listNode {
	struct listNode *next;
	struct listNode *prev;
	int data;
} listNode;

typedef struct listHead {
	struct listNode *first;
	struct listNode *last;
} listHead;

void listInit(listHead *head);
listNode* listAdd(listHead *head, int data);
void listRemove(listHead *head, listNode *node);