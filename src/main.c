#include "dispatcher.h"
#include "builtin-echo.h"
#include "stub-lua.h"

#include <signal.h>
#include <stdio.h>

#if defined(WIN32)
#endif



int main(int argc, char *argv[])
{
    struct dispatcher *d = NULL;
    
    /* Ignore the send() problem */
#if defined(SIGPIPE)
    signal(SIGPIPE, SIG_IGN);
#endif

    /* Initialize the Lua subsystem */
    stublua_init();
    fprintf(stderr, "Lua version = %d.%d\n", 
                (int)*lua_version(0)/100, (int)*lua_version(0)%100);

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

