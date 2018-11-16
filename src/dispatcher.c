/*
 */
#include "dispatcher.h"
#include "pixie-timer.h"
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#if defined(_MSC_VER)
#pragma comment(lib, "Ws2_32.lib")
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
#define sockets_close(fd) close(fd)
#define sockets_poll poll
#endif


struct my_connection
{
    dispatch_handler handler;
    void *handledata;
    unsigned is_closing:1;
    struct sockaddr_storage sa;
    socklen_t sa_addrlen;
    ptrdiff_t len;
    char peeraddr[64];
    char peerport[8];
    char buf[512];
};

struct dispatcher
{
    struct my_connection **connections;
    struct pollfd *list;
    unsigned count;
    unsigned max;
};

#if defined(WIN32)
const char *sockets_strerror(int err)
{
    char *buf = NULL;
    if (buf)
        LocalFree(buf);

    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                  FORMAT_MESSAGE_FROM_SYSTEM |
                  FORMAT_MESSAGE_IGNORE_INSERTS,
                  NULL, err,
                  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                  (LPTSTR)&buf, 0, NULL);


    return buf;
}

#endif

/****************************************************************************
 ****************************************************************************/
struct dispatcher *dispatcher_create(void)
{
    struct dispatcher *d;
    d = malloc(sizeof(*d));
    
    /* [WINDOWS] Cruft needed by Windows at the start of a program */
#if defined(WIN32)
    {WSADATA x; WSAStartup(0x0201, &x);}
#endif

    /* Start with 1 entries, an empty one marking the end of he list */
    d->max = 1;
    d->list = malloc(d->max * sizeof(*d->list));
    d->count = 0;
    d->connections = malloc(d->max * sizeof(*d->connections));
    d->connections[0] = malloc(sizeof(*d->connections[0]));
    memset(d->connections[0], 0, sizeof(*d->connections[0]));
    return d;
}

/****************************************************************************
 ****************************************************************************/
void dispatcher_close_connection(struct dispatcher *d, unsigned index)
{
    /* Called by the plugin to request this connection be closed. We don't
     * actually do any closing to the connection here, but instead just
     * set a flag to indicate this connection should be closed after
     * the event has been processed */
    struct my_connection *c = d->connections[index];
    
    c->is_closing = 1;
}

/****************************************************************************
 ****************************************************************************/
void dispatcher_remove_at(struct dispatcher *d, size_t index)
{
    struct my_connection *c = d->connections[index];
    size_t end;
    
    /* close the socket if it's still open */
    if (d->list[index].fd > 0) {
        sockets_close(d->list[index].fd);
        d->list[index].fd = (SOCKET)~0;
    }
    
    /* Free the connection-specific structure */
    free(c);
    
    /* For efficiency, replace this entry with the one at the end of the list */
    end = d->count - 1;
    if (end > index) {
        memcpy(&d->list[index], &d->list[end], sizeof(d->list[0]));
        memcpy(&d->connections[index], &d->connections[end], sizeof(d->connections[0]));
    }
    d->count--;
}


/****************************************************************************
 ****************************************************************************/
int dispatcher_dispatch(struct dispatcher *d, int wait_milliseconds)
{
    unsigned i;
    int err;
    
    /* If there is nothing to do, then do a 'sleep()' instead */
    if (d == NULL || d->count == 0) {
        pixie_mssleep(wait_milliseconds);
        return -1;
        
    }
    
    /* wait for incoming event on any connection */
    err = sockets_poll(d->list, d->count, wait_milliseconds);
    if (err == -1) {
        switch (errno) {
            case EINTR:
                return 0;
            default:
                /* fatal program error, shouldn't be possible */
                fprintf(stderr, "[-] poll(): %s\n", sockets_strerror(sockets_errno));
                return -1;
        }
    } else if (err == 0) {
        /* timeout happened, nothing was recevied */
        return 0;
    }
    
    /* handle all the incoming events */
    for (i=0; i<d->count; i++) {
        struct my_connection *c = d->connections[i];
        
        if (d->list[i].revents == 0) {
            /* no events for this socket */
            continue;
        }
        
        if ((d->list[i].revents & POLLHUP) != 0) {
            /* other side hungup (i.e. sent FIN, closed socket) */
            c->handler(d, i, d->list[i].fd, DISPATCH_CLOSED, c->handledata);
            dispatcher_remove_at(d, i--);
            continue;
        }
        
        if ((d->list[i].revents & POLLERR) != 0) {
            /* error, probably RST sent by other side */
            c->handler(d, i, d->list[i].fd, DISPATCH_ERRORED, c->handledata);
            c->handler(d, i, d->list[i].fd, DISPATCH_CLOSED, c->handledata);
            dispatcher_remove_at(d, i--);
            continue;
        }
        
        if ((d->list[i].revents & POLLIN) != 0) {
            /* Data is ready to receive */
            c->handler(d, i, d->list[i].fd, DISPATCH_READABLE, c->handledata);
            if (c->is_closing)
                dispatcher_remove_at(d, i--);
            continue;
        }
        
        if ((d->list[i].revents & POLLOUT) != 0) {
            /* We are ready to transmit data */
            c->handler(d, i, d->list[i].fd, DISPATCH_WRITEABLE, c->handledata);
            if (c->is_closing)
                dispatcher_remove_at(d, i--);
            continue;
        }
        
        fprintf(stderr, "[-] poll([%s]:%s): unknown event[%d] 0x%x\n", c->peeraddr, c->peerport, (int)i, d->list[i].revents);
        c->handler(d, i, d->list[i].fd, DISPATCH_CLOSED, c->handledata);
        dispatcher_remove_at(d, i--);
    } /* end handling connections */
    
    return 0;
}

/****************************************************************************
 ****************************************************************************/
void dispatcher_set_event(struct dispatcher *d, unsigned index, int state, unsigned milliseconds)
{
    switch (state) {
        case DISPATCH_READABLE:
            d->list[index].events = POLLIN;
            d->list[index].revents = 0;
            break;
        case DISPATCH_WRITEABLE:
            d->list[index].events = POLLOUT;
            d->list[index].revents = 0;
            break;
        default:
            fprintf(stderr, "unknown set event %d\n", state);
            exit(1);
            break;
    }
}

/****************************************************************************
 ****************************************************************************/
void dispatcher_set_userdata(struct dispatcher *d, unsigned index, void *handledata)
{
    d->connections[index]->handledata = handledata;
}


/****************************************************************************
 ****************************************************************************/
void dispatcher_add_connection(
    struct dispatcher *d, 
    int fd, 
    struct sockaddr *sa,
    size_t sa_addrlen,
    dispatch_handler handler,
    void *handlerdata)
{
    struct my_connection *c;
    int err;
    
    /* add to the poll() list, set for reading */
    d->list[d->count].fd = fd;
    d->list[d->count].events = POLLIN;
    d->list[d->count].revents = 0;
    
    /* add per=connection info */
    c = d->connections[d->count];
    c->len = 0; /* init buffer */
    c->sa_addrlen = sa_addrlen;
    memcpy(&c->sa, sa, sa_addrlen);

    /* get print name of remote connection */
    err = getnameinfo(  (struct sockaddr *)&c->sa, c->sa_addrlen,
                        c->peeraddr, sizeof(c->peeraddr),
                        c->peerport, sizeof(c->peerport),
                        NI_NUMERICHOST | NI_NUMERICSERV);
    if (err) {
        fprintf(stderr, "[-] getnameinfo(): %s\n", gai_strerror(err));
        memcpy(c->peeraddr, "err", 4);
        memcpy(c->peerport, "err", 4);
    }

    /* grow the lists by 1, if needed*/
    d->count++;
    if (d->count >= d->max) {
        d->max++;
        d->list = realloc(d->list, d->max * sizeof(*d->list));
        d->connections = realloc(d->connections, d->max * sizeof(*d->connections));
    }
}

/****************************************************************************
 ****************************************************************************/
void dispatcher_destroy(struct dispatcher *d)
{
    while (d->count)
        dispatcher_remove_at(d, d->count-1);

    free(d->list);
    free(d->connections);
}
  

/****************************************************************************
 ****************************************************************************/
int dispatcher_register_server(
        struct dispatcher *d, 
        const char *addrname, 
        const char *portname,
        dispatch_handler handler,
        void *handledata)
{
    struct addrinfo *ai = NULL;
    struct addrinfo hints = {0};
    int err;
    char hostaddr[64];
    char hostport[8];
    int fd = -1;
    int yes = 1;
    int return_code = -1;

    /* Get an address structure for the port */
    hints.ai_flags = AI_PASSIVE;
    err = getaddrinfo(addrname, portname, &hints, &ai);
    if (err) {
        fprintf(stderr, "[-] getaddrinfo(): %s\n", gai_strerror(err));
        ai = NULL;
        goto cleanup;
    }

    /* And retrieve back again which addresses were assigned */
    err = getnameinfo(ai->ai_addr, ai->ai_addrlen,
                        hostaddr, sizeof(hostaddr),
                        hostport, sizeof(hostport),
                        NI_NUMERICHOST | NI_NUMERICSERV);
    if (err) {
        fprintf(stderr, "[-] getnameinfo(): %s\n", gai_strerror(err));
        goto cleanup;
    }
    
    /* Create a file handle for the kernel resources */
    fd = socket(ai->ai_family, SOCK_STREAM, 0);
    if (fd == -1) {
        fprintf(stderr, "[-] socket(): %s\n", sockets_strerror(sockets_errno));
        goto cleanup;
    }

    /* Allow multiple processes to share this IP address. This is needed for 
     * fast restart, when the previous process leaves the listening socket
     * in a waiting state. */
    err = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void*)&yes, sizeof(yes));
    if (err) {
        fprintf(stderr, "[-] SO_REUSEADDR([%s]:%s): %s\n", hostaddr, hostport, sockets_strerror(sockets_errno));
    }
    
#if defined(SO_REUSEPORT)
    /* Allow multiple processes to share this port */
    err = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
    if (err) {
        fprintf(stderr, "[-] SO_REUSEPORT([%s]:%s): %s\n", hostaddr, hostport, strerror(errno));
        goto cleanup;
    }
#endif

    /* Tell it to use the local port number (and optionally, address) */
    err = bind(fd, ai->ai_addr, ai->ai_addrlen);
    if (err) {
        fprintf(stderr, "[-] bind([%s]:%s): %s\n", hostaddr, hostport, sockets_strerror(sockets_errno));
        goto cleanup;
    }

    /* Configure the socket for listening (i.e. accepting incoming connections) */
    err = listen(fd, 10);
    if (err) {
        fprintf(stderr, "[-] listen([%s]:%s): %s\n", hostaddr, hostport, sockets_strerror(sockets_errno));
        goto cleanup;
    } else
        fprintf(stderr, "[+] listening on [%s]:%s\n", hostaddr, hostport);

    /* Add this to our list of things to poll */
    dispatcher_add_connection(d,
                              fd,
                              ai->ai_addr,
                              ai->ai_addrlen,
                              handler,
                              handledata);

    return 0;
    
cleanup:
    if (fd != -1)
        sockets_close(fd);
    if (ai)
        freeaddrinfo(ai);
    return return_code;
}

void dispatcher_print_error(struct dispatcher *d, unsigned index, int err, const char *msg, ...)
{
    
}

void dispatcher_print_info(struct dispatcher *d, unsigned index, int level, const char *msg, ...)
{
    
}
