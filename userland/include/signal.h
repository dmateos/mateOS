#ifndef MATEOS_SIGNAL_H
#define MATEOS_SIGNAL_H

typedef unsigned int sigset_t;
typedef void (*sighandler_t)(int);

#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int signo);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int pthread_sigmask(int how, const sigset_t *set, sigset_t *oldset);

#endif
