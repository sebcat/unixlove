# unixlove

Snippets of code showing some unix love

### rpn_calc.c

Spawn N worker processes running RPN calculators that crashes on parameter stack under-/overflow. When this happens the supervisor process spawns new workers.

The communication is line buffered and round-robin, so the lines are evenly distributed across the worker pool, which can be interesting.

example: 
```
$ gcc -o rpn rpn_calc.c
$ ./rpn
10 10 + .                    
20                           
10                           
20                           
1 +                          
4 +                          
.                            
11                           
.                            
24
```

### passer.c
Allocates messages on the heap and passes a pointer to these messages over a socketpair, using the socketpair as a thread queue. The producer allocates and inits the messages and the consumer frees them.
