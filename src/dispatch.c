#include "dispatch.h"
#include "event-timeout.h"
#include "util-malloc.h"
#include "util-socket.h"

#include <errno.h>
#include <string.h>
#include <time.h>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <netdb.h> /* getaddrinfo() et al. */
#include <fcntl.h> /* fcntl(), F_GETFL, O_NONBLOCK */

/* Portability: this is defined only for Linux, on other platforms,
 * we need to handle the SIGPIPE error using a different technique */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

struct dispatcher_service
{
    int fd;
    DISPATCHER_RECV recv;
    DISPATCHER_SEND send;
    DISPATCHER_EVENT event;
    
    /** The number of seconds that we should wait for inactivity before
     * closing a connection */
    unsigned cfg_timeout_inactivity;

    void *userdata;
    char *hostaddr;
    char *hostport;
};

/***************************************************************************
 * This is the record for each TCP (or UDP or SCTP) connection.
 ***************************************************************************/
struct dispatcher_connection
{
    size_t index;
    int fd;
    void *userdata;
    struct dispatcher_service *svc;
    char *send_buf;
    size_t send_length;
    
    /**
     * The timers that this connection can have.
     */
    struct {
        struct TimeoutEntry inactivity;
        struct TimeoutEntry sleep;
        struct TimeoutEntry receive;
    } timeouts;
    
    char *peeraddr;
    char *peerport;
};

/***************************************************************************
 * The core dispatcher subsystem. While the intent is that there should
 * be only a single dispatcher for the program, in practice there may
 * be multiple dispatchers.
 ***************************************************************************/
struct dispatcher
{
    struct dispatcher_service **services;
    size_t service_count;
    
    struct dispatcher_connection **connections;
    size_t connection_count;
    
    /** Timeout connections that have been inactive for too long */
    struct {
        struct Timeouts *inactivity;
        struct Timeouts *sleep;
        struct Timeouts *receive;
    } timeouts;
};

/***************************************************************************
 ***************************************************************************/
struct dispatcher *
dispatcher_create(void)
{
    struct dispatcher *d;
    
    d = CALLOC(1, sizeof(*d));
    
    /* Initialize the timeouts for connections */
    d->timeouts.inactivity = timeouts_create(time(0), 0);
    
    return d;
}

/***************************************************************************
 ***************************************************************************/
void dispatcher_destroy(struct dispatcher *d)
{
    FREE(d);
}

/***************************************************************************
 ***************************************************************************/
int dispatcher_add_listener(
                            struct dispatcher *d,
                            void *servicedata,
                            int port,
                            DISPATCHER_RECV recv,
                            DISPATCHER_SEND send,
                            DISPATCHER_EVENT event)
{
    int err;
    
    /*
     * Port must be value between [0..65535]. The operating system may not
     * like ports below 1023, or the port 0, but we make no check for this
     * in our code.
     */
    if (port < 0 || 65535 < port) {
        fprintf(stderr, "[-] %s: invalid port number %d\n", __func__, port);
        return -1;
    }
    char num[32];
    snprintf(num, sizeof(num), "%d", port);

    /*
     * Get an address structure for the port. This is the way to do it
     * portably for IPv4/IPv6, replacing older Sockets functions.
     */
    struct addrinfo *ai = NULL;
    struct addrinfo hints = {0};
    hints.ai_flags = AI_PASSIVE;
    err = getaddrinfo(0,            /* local address*/
                      num,          /* local port number */
                      &hints,       /* hints */
                      &ai);         /* result */
    if (err) {
        fprintf(stderr, "[-] %s: getaddrinfo(): %s\n", __func__, gai_strerror(err));
        return -1;
    }
    
    /*
     * And retrieve back again which addresses were assigned, for
     * printing error messages and such. In particular, the addr
     * that we get back is often different than the address
     * that was configured.
     */
    char hostaddr[NI_MAXHOST];
    char hostport[NI_MAXSERV];
    err = getnameinfo(ai->ai_addr, ai->ai_addrlen,
                      hostaddr, sizeof(hostaddr),
                      hostport, sizeof(hostport),
                      NI_NUMERICHOST | NI_NUMERICSERV);
    if (err) {
        fprintf(stderr, "[-] %s:getnameinfo(): %s\n", __func__, gai_strerror(err));
        freeaddrinfo(ai);
        return -1;
    }

    
    /*
     * Create a file handle for the kernel resources
    */
    int fd;
    fd = socket(ai->ai_family, SOCK_STREAM, 0);
    if (fd == -1) {
        fprintf(stderr, "[-] %s:socket(): %s\n", __func__, strerror(errno));
        freeaddrinfo(ai);
        return -1;
    }
    socket_set_nonblocking(fd);
    
    /*
     * Allow multiple processes to share this IP address. This is needed
     * for when we quickly restart the service before timeouts complete.
     * This is also needed when we want to create multiple listening
     * sockets (For different processes/threads) for the same port.
     */
    int yes = 1;
    err = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (err) {
        fprintf(stderr, "[-] %s:SO_REUSEADDR([%s]:%s): %s\n", __func__, hostaddr, hostport, strerror(errno));
        close(fd);
        freeaddrinfo(ai);
        return -1;
    }
    
    /* Allow multiple processes/threads to share this port */
#if defined(SO_REUSEPORT)
    err = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
    if (err) {
        fprintf(stderr, "[-] %s:SO_REUSEPORT([%s]:%s): %s\n", __func__, hostaddr, hostport, strerror(errno));
        close(fd);
        freeaddrinfo(ai);
        return -1;
    }
#endif
    
    /* Tell it to use the local port number (and optionally, address) */
    err = bind(fd, ai->ai_addr, ai->ai_addrlen);
    if (err) {
        fprintf(stderr, "[-] %s:bind([%s]:%s): %s\n", __func__, hostaddr, hostport, strerror(errno));
        close(fd);
        freeaddrinfo(ai);
        return -1;
    }
    
    /* Configure the socket for listening (i.e. accepting incoming connections) */
    err = listen(fd, 10);
    if (err) {
        fprintf(stderr, "[-] %s:listen([%s]:%s): %s\n", __func__, hostaddr, hostport, strerror(errno));
        close(fd);
        freeaddrinfo(ai);
        return -1;
    } else
        fprintf(stderr, "[+] %s: listening on [%s]:%s\n", __func__, hostaddr, hostport);

    /* Create a service structure for this. The most important part of this is the
     * socket descriptor */
    struct dispatcher_service *svc;
    svc = CALLOC(1, sizeof(*svc));
    svc->recv = recv;
    svc->send = send;
    svc->event = event;
    svc->hostaddr = STRDUP(hostaddr);
    svc->hostport = STRDUP(hostport);
    svc->fd = fd;
    svc->userdata = servicedata;
    svc->cfg_timeout_inactivity = 5; /* default: 60 seconds timeout */
    
    /* Add the structure to our list of services */
    d->services = REALLOCARRAY(d->services, d->service_count+1, sizeof(d->services[0]));
    d->services[d->service_count] = svc;
    d->service_count += 1;

    return 0;
}


/***************************************************************************
 * For pretty-printing error messages, this gets both sides of a TCP
 * connection. Note these are the formated addresses, not a lookup
 * of the DNS names associated with those addresses.
 ***************************************************************************/
void
dispatcher_get_addrs(struct dispatcher_connection *conn,
                     const char **peeraddr,
                     const char **peerport,
                     const char **hostaddr,
                     const char **hostport)
{
    if (peeraddr)
        *peeraddr = conn->peeraddr;
    if (peerport)
        *peerport = conn->peerport;
    if (hostaddr)
        *hostaddr = conn->svc->hostaddr;
    if (hostport)
        *hostport = conn->svc->hostport;
}

/***************************************************************************
 ***************************************************************************/
static void
connection_remove(struct dispatcher *d, size_t i)
{
    struct dispatcher_connection *conn = d->connections[i];
    struct dispatcher_service *svc = conn->svc;

    /* Remove the connection from our list */
    d->connections[i] = d->connections[d->connection_count-1];
    d->connections[i]->index = i;
    d->connection_count -= 1;
    printf("connection count-- = %u\n", (unsigned)d->connection_count);

    /* Create an event notifying the user that the event has been
     * removed, so they can cleanup their resources */
    struct dispatcher_new_connection event = {0};
    event.sizeof_struct = sizeof(event);
    event.servicedata = svc->userdata;
    event.peeraddr = conn->peeraddr;
    event.peerport = conn->peerport;
    event.hostaddr = svc->hostaddr;
    event.hostport = svc->hostport;
    
    svc->event(conn, &conn->userdata, DISPATCH_END_CONNECTION, &event);

    /* Close the socket handle */
    if (conn->fd > 0)
        close(conn->fd);
    conn->fd = -1;
    
    /* Remove timeouts associated with this connection. This should be among
     * the very last things we do before freeing the data structure. */
    timeout_unlink(&conn->timeouts.inactivity);
    timeout_unlink(&conn->timeouts.sleep);
    timeout_unlink(&conn->timeouts.receive);
    
    /* lastly, free up the memory we use */
    FREE(conn);
}

/***************************************************************************
 ***************************************************************************/
static struct dispatcher_connection *
connection_add(struct dispatcher *d, struct dispatcher_service *svc,
               int fd2, const char *peeraddr, const char *peerport)
{
    /* Append to our connection list */
    struct dispatcher_connection *conn;
    conn = CALLOC(1, sizeof(*conn));
    conn->fd = fd2;
    conn->svc = svc;
    conn->peeraddr = STRDUP(peeraddr);
    conn->peerport = STRDUP(peerport);
 
    /* This timer gets updated for every send/recv on the connection. If it
     * goes too long without activity, the connection will be closed */
    timeouts_add(   d->timeouts.inactivity,
                    &conn->timeouts.inactivity,
                    offsetof(struct dispatcher_connection, timeouts.inactivity),
                    timestamp_from_tv(time(0)+svc->cfg_timeout_inactivity, 0));
    
    d->connections = REALLOCARRAY(d->connections, d->connection_count+1, sizeof(d->connections[0]));
    d->connections[d->connection_count] = conn;
    conn->index = d->connection_count;
    d->connection_count += 1;
    printf("connection count++ = %u\n", (unsigned)d->connection_count);

    /* Notify client that a new connection has been created */
    struct dispatcher_new_connection n = {0};
    n.sizeof_struct = sizeof(n);
    n.servicedata = svc->userdata;
    n.hostaddr = svc->hostaddr;
    n.hostport = svc->hostport;
    n.peeraddr = conn->peeraddr;
    n.peerport = conn->peerport;
    svc->event(conn, &conn->userdata, DISPATCH_NEW_CONNECTION, &n);

    return conn;
}


/***************************************************************************
 * Do the connection inactivity timeouts, which will close inactive TCP
 * connections.
 ***************************************************************************/
static void
timeout_inactivity(struct dispatcher *d, time_t secs, unsigned usecs)
{
    uint64_t timestamp = TICKS_FROM_TV(secs, usecs);

    /* Continue processing timeouts until there are none left */
    for (;;) {
        struct dispatcher_connection *conn;
        struct dispatcher_service *svc;
        
        /*
         * Get the next event that is older than the current time
         */
        conn = (struct dispatcher_connection *)timeouts_remove(d->timeouts.inactivity,
                                                          timestamp);
        
        /*
         * If everything up to the current time has already been processed,
         * then exit this loop
         */
        if (conn == NULL)
            break;
        
        /*
         * Process this timeout
         */
        svc = conn->svc;
        svc->event(conn, &conn->userdata, DISPATCH_TIMEOUT_INACTIVITY, 0);

        /*
         * Close the connection.
         */
        connection_remove(d, conn->index);
    }
}

/***************************************************************************
 ***************************************************************************/
static void
timeout_sleep(struct dispatcher *d, time_t secs, unsigned usecs)
{
    uint64_t timestamp = TICKS_FROM_TV(secs, usecs);
    
    /* Continue processing timeouts until there are none left */
    for (;;) {
        struct dispatcher_connection *conn;
        struct dispatcher_service *svc;
        
        /*
         * Get the next event that is older than the current time
         */
        conn = (struct dispatcher_connection *)timeouts_remove(d->timeouts.sleep,
                                                               timestamp);
        
        /*
         * If everything up to the current time has already been processed,
         * then exit this loop
         */
        if (conn == NULL)
            break;
        
        /*
         * Process this timeout
         */
        svc = conn->svc;
        svc->event(conn, &conn->userdata, DISPATCH_TIMEOUT_SLEEP, 0);
    }
}

/***************************************************************************
 ***************************************************************************/
static void
timeout_receive(struct dispatcher *d, time_t secs, unsigned usecs)
{
    uint64_t timestamp = TICKS_FROM_TV(secs, usecs);
    
    /* Continue processing timeouts until there are none left */
    for (;;) {
        struct dispatcher_connection *conn;
        struct dispatcher_service *svc;
        
        /*
         * Get the next event that is older than the current time
         */
        conn = (struct dispatcher_connection *)timeouts_remove(d->timeouts.sleep,
                                                               timestamp);
        
        /*
         * If everything up to the current time has already been processed,
         * then exit this loop
         */
        if (conn == NULL)
            break;
        
        /*
         * Process this timeout
         */
        svc = conn->svc;
        svc->event(conn, &conn->userdata, DISPATCH_TIMEOUT_RECEIVE, 0);
    }
}

/***************************************************************************
 ***************************************************************************/
int dispatcher_dispatch(struct dispatcher *d, unsigned milliseconds)
{
    size_t i;
    
    fd_set readset;
    fd_set writeset;
    fd_set errset;
    int nfds = 0;
    
    FD_ZERO(&readset);
    FD_ZERO(&writeset);
    FD_ZERO(&errset);

    /* Add listening services */
    for (i=0; i<d->service_count; i++) {
        int fd = d->services[i]->fd;
        if (nfds < fd + 1)
            nfds = fd + 1;
        FD_SET(fd, &readset);
        FD_SET(fd, &writeset);
        FD_SET(fd, &errset);
    }
    
    /* Add current connections */
    for (i=0; i<d->connection_count; i++) {
        int fd = d->connections[i]->fd;
        if (nfds < fd + 1)
            nfds = fd + 1;
        FD_SET(fd, &readset);
        FD_SET(fd, &writeset);
        FD_SET(fd, &errset);
    }
    

    /*
     * Do the select()
     */
    int err;
    struct timeval timeout = {0,0};
    timeout.tv_usec = milliseconds * 1000;
    err = select(nfds, &readset, &writeset, &errset, &timeout);

    
    /* There should never be an error (socket errors are reported with 'errset' isntead).
     * But in case there is, print a message and write the error to the terminal */
    if (err < 0) {
        fprintf(stderr, "[-] %s:select(%d) %s\n", __func__, nfds, strerror(errno));
        sleep(milliseconds * 2 / 1000);
        //return 0;
    }
    
    /* If there is nothing to do, then just return */
    if (err == 0)
        return 0;

    /* Process incoming data */
    for (i=0; i<d->connection_count; i++) {
        struct dispatcher_connection *conn = d->connections[i];
        int fd = conn->fd;
        struct dispatcher_service *svc = conn->svc;
        
        /* If nothing to do, then loop to the next one */
        if (!FD_ISSET(fd, &readset))
            continue;

        /* Receive the incoming data. Since UDP packets can be as big
         * as ~65536 bytes, we have to use that as the read buffer size */
        unsigned char buf[65536];
        ssize_t bytes_read;
        bytes_read = recv(fd, (char*)buf, sizeof(buf), MSG_NOSIGNAL);
        if (bytes_read < 0) {
            switch (errno) {
                default:
                    fprintf(stderr, "[-] [%s]:%s -> [%s]:%s: recv(): %s\n",
                            conn->peeraddr, conn->peerport,
                            svc->hostaddr, svc->hostport,
                            strerror(errno));
                    break;
            }
            continue;
        }
        
        /* If the connection has been closed, then delete it from our list */
        if (bytes_read == 0) {
            connection_remove(d, i);
            i--;
            continue;
        }
        
        /* Dispatch the incoming data */
        if (svc->recv)
            svc->recv(conn, conn->userdata, buf, bytes_read);
    }
    
    /* Handle any errors on a service */
    /* Accept incoming connections */
    for (i=0; i<d->connection_count; i++) {
        struct dispatcher_connection *conn = d->connections[i];
        int fd = conn->fd;
        struct dispatcher_service *svc = conn->svc;

        /* If nothing to do, then loop to the next one */
        if (!FD_ISSET(fd, &errset))
            continue;
        
        /* Retrieve the pending error for the socket */
        int err2 = 0;
        socklen_t err2_size = sizeof(err2);
        err = getsockopt(fd, SOL_SOCKET, SO_ERROR, &err2, &err2_size);
        if (err) {
            fprintf(stderr, "[-] %s:getsockopt(SO_ERROR) %s\n", __func__, strerror(errno));
            continue;
        }
        
        /* Pretty print the error */
        fprintf(stderr, "[-] [%s]:%s -> [%s]:%s: error: %s\n",
                conn->peeraddr, conn->peerport,
                svc->hostaddr, svc->hostport, strerror(err2));
    }

    /* Accept incoming connections */
    for (i=0; i<d->service_count; i++) {
        struct dispatcher_service *svc = d->services[i];
        int fd = svc->fd;
        
        /* If nothing to do, then loop to the next one */
        if (!FD_ISSET(fd, &readset) && !FD_ISSET(fd, &writeset))
            continue;
        
        /* Accept the connection */
        int fd2;
        struct sockaddr_storage peer;
        socklen_t peer_addrlen = sizeof(peer);
        fd2 = accept(fd, (struct sockaddr*)&peer, &peer_addrlen);
        if (fd2 == -1) {
            fprintf(stderr, "[-] %s:accept([%s]:%s): %s\n", __func__,
                    svc->hostaddr, svc->hostport, strerror(errno));
            continue;
        }
        
        /* Pretty print the incoming address/port */
        char peeraddr[NI_MAXHOST];
        char peerport[NI_MAXSERV];
        err = getnameinfo((struct sockaddr*)&peer, peer_addrlen,
                          peeraddr, sizeof(peeraddr),
                          peerport, sizeof(peerport),
                          NI_NUMERICHOST | NI_NUMERICSERV);
        if (err) {
            fprintf(stderr, "[-] %s:getnameinfo(): %s\n", __func__, gai_strerror(err));
            close(fd2);
            continue;
        }

        fprintf(stderr, "[+] accept([%s]:%s) from [%s]:%s\n",
                svc->hostaddr, svc->hostport, peeraddr, peerport);
        
        connection_add(d, svc, fd2, peeraddr, peerport);
    }

    /* Handle any errors on a service */
    /* Accept incoming connections */
    for (i=0; i<d->service_count; i++) {
        struct dispatcher_service *svc = d->services[i];
        int fd = svc->fd;
        
        /* If nothing to do, then loop to the next one */
        if (!FD_ISSET(fd, &errset))
            continue;
        
        /* Retrieve the pending error for the socket */
        int err2 = 0;
        socklen_t err2_size = sizeof(err2);
        err = getsockopt(fd, SOL_SOCKET, SO_ERROR, &err2, &err2_size);
        if (err) {
            fprintf(stderr, "[-] %s:getsockopt(SO_ERROR) %s\n", __func__, strerror(errno));
            continue;
        }
        
        /* Pretty print the error */
        fprintf(stderr, "[-] [%s]:%s: error: %s\n",
                svc->hostaddr, svc->hostport, strerror(err2));
    }

    /*
     * Handle timeouts associated with connections
     */
    timeout_inactivity(d, time(0), 0);
    timeout_sleep(d, time(0), 0);
    timeout_receive(d, time(0), 0);

    
    return 1;
}

