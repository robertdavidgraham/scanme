#ifndef BUILTIN_ECHO_H
#define BUILTIN_ECHO_H

struct builtin_data_echo;
struct dispatcher;

int builtin_echo_listen(struct dispatcher *d, unsigned index, 
                                    int fd, int event, void *handledata);
                                    
#endif
