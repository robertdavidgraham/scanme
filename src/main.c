#include "dispatcher.h"
#include "builtin-echo.h"

#include <stdio.h>
#include <signal.h>

int main(int argc, char *argv[])
{
    struct dispatcher *d = NULL;
    
    /* Ignore the send() problem */
    signal(SIGPIPE, SIG_IGN);

    /* create an instance of our polling object */
    d = dispatcher_create();
    
    /* add an echo listener */
    dispatcher_register_server(d, 0, "1313", builtin_echo_listen, 0);


    for (;;) {
        int err;
        
        err = dispatcher_dispatch(d, 10);
        if (err != 0)
            break;
    }
    
    if (d)
        dispatcher_destroy(d);
    return 0;
}

