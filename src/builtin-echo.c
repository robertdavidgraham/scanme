#include "builtin-echo.h"
#include "dispatcher.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#if defined(WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#if defined(_MSC_VER)
#pragma comment(lib, "Ws2_32.lib")
typedef intptr_t ssize_t;
#endif
#define sockets_close(fd) closesocket(fd)
#define sockets_poll WSAPoll
#define sockets_errno ((int)WSAGetLastError())
const char *sockets_strerror(int err);
#else
#include <unistd.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <netdb.h>
#endif


struct echo_data
{
    unsigned char buf[2048];
    ssize_t len;
};

int builtin_handler_echo_connection(
        struct dispatcher *d, 
        unsigned index, 
        int fd, 
        int event, 
        void *handledata)
{
    struct echo_data *data = (struct echo_data *)handledata;
    int err;

    switch (event) {
        case 0:
        default:
            break;

        case DISPATCH_CREATED:
            /* This handler was just created, so allocate our data to work with */
            if (data == NULL) {
                data = malloc(sizeof(*data));
                data->len = 0;
                dispatcher_set_userdata(d, index, data);
            }
            break;

        case DISPATCH_CLOSED:
            /* This connection has been closed, so free resources*/
            dispatcher_print_info(d, fd, 0, "connection closed gracefully");
            if (data)
                free(data);
            break;

        case DISPATCH_ERRORED:
            /* An error has occured on this connection */
            {
                int opt;
                socklen_t opt_len = sizeof(opt);
                err = getsockopt(fd, SOL_SOCKET, SO_ERROR, &opt, &opt_len);
                if (err) {
                    dispatcher_print_error(d, index, errno, "getsockopt(impossible)");
                } else {
                    dispatcher_print_error(d, index, opt, "error");
                }
            }
            break;

        case DISPATCH_READABLE:
            /* Data is ready to receive */
            data->len = recv(fd, data->buf, sizeof(data->buf), 0);
            if (data->len == 0 ) {
                /* Shouldn't be possible, should've got POLLHUP instead */
                dispatcher_print_info(d, index, 0, "RECV(0)");
                dispatcher_close_connection(d, index);
            } else if (data->len < 0) {
                dispatcher_print_error(d, index, errno, "recv(-1)");
                dispatcher_close_connection(d, index);
            } else {
                return builtin_handler_echo_connection(
                    d, index, fd, DISPATCH_WRITEABLE, data
                    );
            }
            break;

        case DISPATCH_WRITEABLE:
            /* The socket is now writable */
            {                
                ssize_t count;

                count = send(fd, data->buf, data->len, 0);

                if (count < 0) {
                    dispatcher_print_error(d, index, errno, "send(-1)");
                    dispatcher_close_connection(d, index);
                } else if (count < data->len) {
                    memmove(data->buf, data->buf + count, data->len - count);
                    data->len -= count;
                    dispatcher_set_event(d, index, DISPATCH_WRITEABLE, 0);
                } else {
                    data->len = 0;
                    dispatcher_set_event(d, index, DISPATCH_READABLE, 0);
                }
                return 0;
            }
            break;
    }
    
    return 0;
}

int builtin_echo_listen(
        struct dispatcher *d, 
        unsigned index, 
        int fd, 
        int event, 
        void *handledata)
{
    struct sockaddr_storage sa;
    socklen_t sa_addrlen = sizeof(sa);
    int fd2;

    switch (event) {
    case DISPATCH_READABLE:
    case DISPATCH_WRITEABLE:
        /* accept a new connection */
        fd2 = accept(fd, (struct sockaddr *)&sa, &sa_addrlen);
        if (fd2 == -1) {
            dispatcher_print_error(d, fd, errno, "accept(echo)");
            switch (errno) {
                case EMFILE:
                    fprintf(stderr, "[-] HINT: use 'ulimit -n <n>' to raise limits\n");
                    break;
            }
            return 0;
        }
        break;
    }    
    /* Add the connection */
    dispatcher_add_connection(d, fd, (struct sockaddr *)&sa, sa_addrlen, builtin_handler_echo_connection, 0);
    
    return 0;
}
