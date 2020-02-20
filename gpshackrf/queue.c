#include "queue.h"
#include <stdlib.h>
#include <pthread.h>

queueHead* queueCreate() {
	queueHead *head = malloc(sizeof(queueHead));
	head->head = NULL;
	head->tail = NULL;
	head->numElements = 0;

	pthread_mutex_t *mutex = malloc(sizeof(*mutex));
	pthread_mutex_init(mutex, NULL);
	head->mutex = mutex;

	return head;
}

void queueEmpty(queueHead *header) {
	message *msg;
	while (NULL != (msg = queuePop(header))) {
		free(msg->buffer);
		free(msg);
	}
}

void queueDestroy(queueHead *header) {
	free(header->mutex);
	free(header);
	header = NULL;
}

void queuePush(queueHead *header, void *elem) {
	element *e = malloc(sizeof(*e));
	e->value = elem;
	e->next = NULL;

	pthread_mutex_lock(header->mutex);

	if (header->head == NULL) {
		header->head = e;
		header->tail = e;
		header->numElements++;
	}
	else {
		if (header->numElements > 100) {
			free(e);
			free(elem);
		}
		else {
			element* oldTail = header->tail;
			oldTail->next = e;
			header->tail = e;
			header->numElements++;
		}
	}

	pthread_mutex_unlock(header->mutex);
}

void* queuePop(queueHead *header) {
	pthread_mutex_lock(header->mutex);
	element *head = header->head;

	if (head == NULL) {
		pthread_mutex_unlock(header->mutex);
		return NULL;
	}
	else {
		header->numElements--;
		header->head = head->next;

		void *value = head->value;
		free(head);

		pthread_mutex_unlock(header->mutex);
		return value;
	}
}