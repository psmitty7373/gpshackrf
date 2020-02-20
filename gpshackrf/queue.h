#pragma once
#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>

typedef struct _message {
	size_t length;
	char *buffer;
} message;

typedef struct _element {
	void *next;
	void *value;
} element;

typedef struct _queueHead {
	element *head;
	element *tail;
	pthread_mutex_t *mutex;
	size_t numElements;
} queueHead;

queueHead* queueCreate();
void queueEmpty(queueHead *header);
void queueDestroy(queueHead *header);
void queuePush(queueHead *header, void *elem);
void* queuePop(queueHead *header);

#endif
