#ifndef MATEOS_SEMAPHORE_H
#define MATEOS_SEMAPHORE_H

typedef struct {
    volatile int value;
} sem_t;

int sem_init(sem_t *sem, int pshared, unsigned int value);
int sem_wait(sem_t *sem);
int sem_post(sem_t *sem);

#endif
