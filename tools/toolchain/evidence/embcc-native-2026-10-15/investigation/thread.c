#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include <stddef.h>
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static int ready;
static void *worker(void *argument)
{
    (void)pthread_mutex_lock(&mutex);
    ready = *(int *)argument;
    (void)pthread_cond_signal(&condition);
    (void)pthread_mutex_unlock(&mutex);
    return argument;
}
int main(void)
{
    pthread_t thread;
    int value = 42;
    void *result = NULL;
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 1;
    if (pthread_create(&thread, NULL, worker, &value) != 0) return 2;
    (void)pthread_mutex_lock(&mutex);
    while (!ready) (void)pthread_cond_wait(&condition, &mutex);
    (void)pthread_mutex_unlock(&mutex);
    if (pthread_join(thread, &result) != 0) return 3;
    if (result != &value || ready != 42) return 4;
    (void)printf("pthread OK: mutex=%zu cond=%zu attr=%zu time=%zu/%zu\n", sizeof(pthread_mutex_t), sizeof(pthread_cond_t), sizeof(pthread_attr_t), sizeof(struct timespec), offsetof(struct timespec, tv_nsec));
    return 0;
}
