#ifndef DISPATCHER_H
#define DISPATCHER_H
#include <sys/types.h>
struct sockaddr_storage;
struct dispatcher;

/* Flags for what type of server to add with `dispatcher_add_server` */
#define DISPATCH_TCP    1
#define DISPATCH_UDP    2
#define DISPATCH_SCTP   4



#define DISPATCH_READABLE   0x01
#define DISPATCH_WRITEABLE  0x02
#define DISPATCH_TIMEOUT    0x04
#define DISPATCH_ERRORED    0x08
#define DISPATCH_CLOSED     0x10
#define DISPATCH_CREATED    0x20
#define DISPATCH_SLEEP      0x40


/**
 * Creates a new instance of the dispatcher object and initializes it.
 */
struct dispatcher *dispatcher_create(void);

/**
 * Deletes, frees, cleans up a dispatcher object. All the pending handlers
 * will first be closed with DISPATCH_CLOSED events.
 * @param d
 *      An object created by `dispatcher_create()`.
 */
void dispatcher_destroy(struct dispatcher *d);

/**
 * Called to dispatch events. Essentially, the program spends all its time
 * in a tight loop repeatedly calling this function.
 * @param d
 *      A dispatcher instance creaed by dispatcher_create()
 * @param wait_milliseconds
 *      The number of milliseconds to wait for an incoming event, or 0 to
 *      return immediately, or -1 in order to wait indefinitely.
 * @return
 *      0 on success, -1 on failure. If -1 is returned, then the dispatcher
 *      can no longer function and should be deleted.
 */
int dispatcher_dispatch(struct dispatcher *d, int wait_milliseconds);


/**
 * Called by the dispatcher to handle incoming events.
 * @param d
 *      The main dispatcher object.
 * @param index
 *      An opaque pointer to the dispatcher's internal data structure
 *      for this connection.
 * @param fd
 *      A handle for the operating system's internal data structure for
 *      this connection. Use this for 'send()' and 'recv().
 * @param event
 *      The event that was triggered (readable, writable, timeout).
 * @param handledata
 *      Opaque pointer to user data
 */
typedef int (*dispatch_handler)(struct dispatcher *d, unsigned index, 
                                    int fd, int event, void *handledata);


/**
 * Add a TCP connection to the dispatcher
 */
void dispatcher_add_connection(
    struct dispatcher *d, 
    int fd, 
    struct sockaddr *sa, 
    socklen_t sa_addrlen,
    dispatch_handler handler,
    void *handlerdata);

/**
 * The dispatcher should close this connection. This will immediately call
 * back into the handler with DISPATCH_CLOSED, then free up the resources.
 */
void dispatcher_close_connection(
    struct dispatcher *d,
    unsigned index);

/**
 * Create a socket to listen for incoming connections.
 */
int dispatcher_register_server(
    struct dispatcher *d, 
    const char *addrname, 
    const char *portname,
    dispatch_handler handler,
    void *handledata);

/**
 * The part that sets the state we are waiting for
 */
void dispatcher_set_event(struct dispatcher *d, unsigned index, int state, unsigned milliseconds);

/**
 * Called to set the handler data on a connection, if it wasn't already set.
 * Typically called from DISPATCH_CREATED in a handler to create new handler
 * data.
 * */
void dispatcher_set_userdata(struct dispatcher *d, unsigned index, void *handledata);

void dispatcher_print_error(struct dispatcher *d, unsigned index, int err, const char *msg, ...);

void dispatcher_print_info(struct dispatcher *d, unsigned index, int level, const char *msg, ...);


#endif
