#ifndef DISPATCH_H
#define DISPATCH_H
#include <stdio.h>

struct dispatcher;
struct dispatcher_connection;

/**
 * Structure sent to event handler to notify it that a new TCP connection
 * had arrived.
 */
struct dispatcher_new_connection
{
    /* The version of this structure, expressed as the size of the structure */
    size_t sizeof_struct;
    
    /* User-data that originally passed to dispatcher_create_listener() */
    void *servicedata;

    /* The stringified address of the local host IP address, which
     * is usually "[::]" */
    const char *hostaddr;
    
    /* The stringifyed port number of the listening service */
    const char *hostport;
    
    /* The stringified address of the remote computer, in either
     * IPv4 or IPv6 format */
    const char *peeraddr;
    
    /* The strinfieid port number of the remote computer */
    const char *peerport;
};


enum DISPATCH_EVENT {
    DISPATCH_NEW_CONNECTION = 1,
    DISPATCH_END_CONNECTION = 2,
    DISPATCH_TIMEOUT_INACTIVITY = 3,
    DISPATCH_TIMEOUT_SLEEP = 4,
    DISPATCH_TIMEOUT_RECEIVE = 5,
};
enum DISPATCH_ERR {
    DISPATCH_ERR_OK = 0,
    DISPATCH_ERR_INVAL = -1, /* function input parameter invalid */
};

typedef void (*DISPATCHER_RECV)(struct dispatcher_connection *conn, void *userdata, const unsigned char *buf, size_t length);
typedef void (*DISPATCHER_SEND)(struct dispatcher_connection *conn, void *userdata);
typedef void (*DISPATCHER_EVENT)(struct dispatcher_connection *conn, void **userdata, int event, void *eventdata);

struct dispatcher *dispatcher_create(void);
void dispatcher_destroy(struct dispatcher *d);
int dispatcher_dispatch(struct dispatcher *d, unsigned milliseconds);

void dispatcher_get_addrs(struct dispatcher_connection *conn, const char **peeraddr, const char **peerport, const char **hostaddr, const char **hostport);

int dispatcher_add_listener(
    struct dispatcher *d,
    void *servicedata,
    int port,
    DISPATCHER_RECV recv,
    DISPATCHER_SEND send,
    DISPATCHER_EVENT event);

#endif

