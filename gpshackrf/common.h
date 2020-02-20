#pragma once

#include <pthread.h>

volatile int running;

int gettimeofday(struct timeval * tp, struct timezone * tzp);
long int timem();

typedef struct hackrfThreadArgs {
	int argc;
	char **argv;
	void *queue;
} hackrfThreadArgs;

typedef struct socketThreadArgs {
	pthread_mutex_t *mutex;
	double llhr[3];
	char update;
} socketThreadArgs;