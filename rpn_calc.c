/* spawn NWORKERS children running RPN calculators that crashes on parameter
   stack (not process stack, mind you) under-/overflow and have a supervisor
   that restarts them when this happens.
   
   This code only makes sense as a demonstration of the relationship between
   workers and a supervisor process */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ctype.h>
#include <string.h>

#define PSTACK_DEPTH 10 
#define TOKEN_LENGTH 31 

#define TOKTYPE_OP  0
#define TOKTYPE_NBR 1

#define POP(state) state->psp == 0 ? \
        die("pop on an empty stack") : \
        state->pstack[--state->psp]
#define PUSH(s,v) if(s->psp == PSTACK_DEPTH-1) { \
        die("push on a full stack"); } else { \
        s->pstack[s->psp++] = v; }

#define ISDECDIGIT(x) (x >= '0' && x <= '9')

#define NWORKERS    2
#define CHILD_FD    0
#define PARENT_FD   1

struct worker {
    pid_t pid;
    int fd;
    FILE *fp;
};

typedef struct {
    int type, value;
} token_t;

typedef struct {
    struct operator_s *optbl;
    int psp;
    int pstack[PSTACK_DEPTH];
} state_t;


typedef void (*op_cb)(state_t *state);
struct operator_s {
    char *name;
    op_cb f;
};


/* die "returns" because it's used in expressions that are assumed to evaluate
   to integers (i.e., POP) */
int die(char *error)
{
    fprintf(stderr, "error: %s\n", error);
    exit(EXIT_FAILURE);
    return 0;
}

void op_add(state_t *state) 
{
    int arg2 = POP(state);
    int arg1 = POP(state);
    PUSH(state, arg1+arg2);
}

void op_sub(state_t *state) 
{
    int arg2 = POP(state);
    int arg1 = POP(state);
    PUSH(state, arg1-arg2);
}

void op_div(state_t *state) 
{
    int arg2 = POP(state);
    int arg1 = POP(state);
    PUSH(state, arg1/arg2);
}

void op_mul(state_t *state) 
{
    int arg2 = POP(state);
    int arg1 = POP(state);
    PUSH(state, arg1*arg2);
}

void op_print(state_t *state) 
{
    int val = POP(state);
    fprintf(stderr, "%d\n", val);
}


void init_state(state_t *state, struct operator_s *optbl)
{
    memset(state, 0, sizeof(state_t));
    state->optbl = optbl;
}

int find_op(struct operator_s *optbl, char *name)
{
    int ix=0;
    while (optbl && optbl->name) {
        if (strcmp(optbl->name, name) == 0) {
            return ix;
        }

        ix++;
        optbl++;
    }

    return -1;
}

int str_to_ntok(char *str, token_t *tok)
{
    int negate = 0;
    int nbr=0;

    if (*str == '-') {
        negate = 1;
        str++;
        if (!ISDECDIGIT(*str)) {
            return -1;
        }
    }

    while (*str) {
        if (!ISDECDIGIT(*str)) {
            return -1;
        }
        nbr = nbr*10;
        nbr += *str-'0';
        str++;
    }

    if (negate) {
        nbr = -(nbr);
    }

    tok->type = TOKTYPE_NBR;
    tok->value = nbr;
    return 0; 
}

int read_token(state_t *state, token_t *tok)
{
    char inbuf[TOKEN_LENGTH+1];
    int ix=0, ch;

    while ((ch = fgetc(stdin)) != EOF) {
        if (isspace(ch)) {
            inbuf[ix] = 0;
            ix=find_op(state->optbl, inbuf);
            if (ix < 0) {
                if (str_to_ntok(inbuf, tok) < 0) {
                    fprintf(stderr, "invalid token: \"%s\"\n",
                            inbuf);
                    ix=0;
                    continue;
                } 
            } else {
                tok->type = TOKTYPE_OP;
                tok->value = ix;
            }
            return 0;            
        }

        inbuf[ix++] = (char)ch;
        if (ix == TOKEN_LENGTH) {
            ix = 0;
        }
    }

    return -1;
}

void eval_token(state_t *state, token_t *tok)
{
    if (tok->type == TOKTYPE_NBR) {
        PUSH(state, tok->value);
    } else if(tok->type == TOKTYPE_OP) {
        state->optbl[tok->value].f(state);
    }

}

void start_rpn()
{
    token_t tok;
    state_t state;
    struct operator_s optbl[] = {
        {".", op_print},
        {"+", op_add},
        {"-", op_sub},
        {"/", op_div},
        {"*", op_mul},
        {NULL, NULL}
    };
    
    init_state(&state, optbl);
    while (read_token(&state, &tok) >= 0) {
        eval_token(&state, &tok);
    }
}

int start_worker(struct worker *worker, void(*cb)(void))
{
    int sv[2];
    pid_t pid;

    if (socketpair(PF_LOCAL, SOCK_STREAM, 0, sv)) {
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        return -1;
    } else if (pid == 0) {
        dup2(sv[CHILD_FD], STDIN_FILENO);
        dup2(sv[CHILD_FD], STDOUT_FILENO);
        dup2(sv[CHILD_FD], STDERR_FILENO);
        cb();
        exit(EXIT_SUCCESS);
    } else {
        worker->pid = pid;
        worker->fd = sv[PARENT_FD];
        worker->fp = NULL;
    }

    return 0;
}

void main_loop(struct worker *workers, size_t nworkers)
{
    fd_set fds;
    int ret, i, max_fd;
    struct timeval to;
    char readbuf[256];
    int nextworker = 0, status;
    pid_t pid;

    while (1) {
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        max_fd = STDIN_FILENO;
        for(i=0; i<nworkers; i++) {
            FD_SET(workers[i].fd, &fds);
            if (workers[i].fd > max_fd) {
                max_fd = workers[i].fd;
            }
        }

        to.tv_sec = 1;
        to.tv_usec = 0;
        ret = select(max_fd+1, &fds, NULL, NULL, &to);
        if (ret < 0) {
            break;
        } else if (ret > 0) {
            if (FD_ISSET(STDIN_FILENO, &fds)) {
                fgets(readbuf, sizeof(readbuf), stdin);
                readbuf[sizeof(readbuf)-1] = '\0';
                write(workers[nextworker].fd, readbuf, strlen(readbuf));
                nextworker = (nextworker + 1) % nworkers;
            }

            for(i=0; i<nworkers; i++) {
                if (FD_ISSET(workers[i].fd, &fds)) {
                    if (workers[i].fp == NULL) {
                        workers[i].fp = fdopen(workers[i].fd, "r");
                    }

                    if (fgets(readbuf, sizeof(readbuf), workers[i].fp) 
                            != NULL)  {
                        readbuf[sizeof(readbuf)-1] = '\0';
                        printf("%s", readbuf);
                    }
                }
            }
        }

        /* restart children if needed */
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            for(i=0; i<nworkers; i++) {
                if (workers[i].pid == pid) {
                    close(workers[i].fd);
                    if (workers[i].fp != NULL) {
                        fclose(workers[i].fp);
                        workers[i].fp = NULL;
                    }

                    start_worker(&workers[i], start_rpn);
                }
            }
        }
    }
}

int main()
{
    int i;
    struct worker workers[NWORKERS];

    for(i=0; i<NWORKERS; i++) {
        if (start_worker(&workers[i], start_rpn)) {
            perror("start_worker");
            exit(EXIT_FAILURE);
        }
    }

    main_loop(workers, NWORKERS);
    return EXIT_SUCCESS;
}
