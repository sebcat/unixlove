/* passer.c - allocate data messages (integers) and pass the pointer to the
              messages over a socketpair, using it as a thread queue */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <pthread.h>

#define MSGCOUNT    1000000
#define NTHREADS    4 

struct msg {
    unsigned int data;
};

/* The worker produces integers between [0, MSGCOUNT) */
void *worker(void *data)
{
    unsigned int i;
    struct msg *msg;
    int fd = (int)(long)data;
    
    for(i=0; i<MSGCOUNT; i++) {
        msg = malloc(sizeof(struct msg));
        if (msg == NULL) {
            break;
        }

        msg->data = i;
        write(fd, &msg, sizeof(struct msg*)); 
    }
    
    close(fd);
    pthread_exit(NULL);
}

/* -1 on error, fd on success */
int start_worker()
{
    pthread_t thread;
    int pair[2];

    if (socketpair(PF_LOCAL, SOCK_STREAM, 0, pair)) {
        return -1;
    }

    pthread_create(&thread, NULL, worker, (void*)(long)pair[0]);
    return pair[1];
}

void consume_worker_data(int nthreads, int *fds) 
{
    struct msg *msg;
    struct timeval tv = {5, 0};
    fd_set bset;
    int i, maxfd=0;
    

    while(1) {
        FD_ZERO(&bset);
        maxfd = -1;
        for(i=0; i<nthreads; i++) {
            if (fds[i] != -1) {
                FD_SET(fds[i], &bset);
                if (fds[i] > maxfd) {
                    maxfd = fds[i];
                }
            }
        }

        if (maxfd == -1) {
            /* all fds closed */
            break;
        }

        if (select(maxfd+1, &bset, NULL, NULL, &tv) <= 0) {
            /* error or timeout */
            break;
        } 

        for(i=0; i<nthreads; i++) {
            if (fds[i] >= 0 && FD_ISSET(fds[i], &bset)) {
                if (read(fds[i], &msg, sizeof(msg)) == sizeof(msg)) {
                    printf("%4d %u\n", fds[i], msg->data);
                    free(msg);
                } else {
                    close(fds[i]);
                    fds[i] = -1;
                }
            }
        }
    }

    for(i=0; i<nthreads; i++) {
        if (fds[i] >= 0) {
            close(fds[i]);
        }
    }
}

int main()
{
    int thread_fds[NTHREADS], i, ret;

    for(i=0; i<NTHREADS; i++) {
        ret = start_worker();
        if (i < 0) {
            exit(EXIT_FAILURE);
        } else {
            thread_fds[i] = ret;
        }
    }
    
    consume_worker_data(NTHREADS, thread_fds);
    return EXIT_SUCCESS;
}
