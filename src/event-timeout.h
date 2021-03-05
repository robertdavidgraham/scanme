#ifndef EVENT_TIMEOUT_H
#define EVENT_TIMEOUT_H
#include <stdint.h>
#include <stdio.h>
#include <stddef.h> /* offsetof*/
#if defined(_MSC_VER)
#undef inline
#define inline _inline
#endif
struct Timeouts;

typedef uint64_t timestamp_t;

/***************************************************************************
 ***************************************************************************/
struct TimeoutEntry {
    /**
     * In units of 1/16384 of a second. We use power-of-two units here
     * to make the "modulus" operatation a simple binary "and".
     * See the TICKS_FROM_TV() macro for getting the timestamp from
     * the current time.
     */
    timestamp_t timestamp;

    /** we build a doubly-linked list */
    struct TimeoutEntry *next;
    struct TimeoutEntry **prev;

    /** The timeout entry is never allocated by itself, but instead
     * lives inside another data structure. This stores the value of
     * 'offsetof()', so given a pointer to this structure, we can find
     * the original structure that contains it */
    size_t offset;
};

/***************************************************************************
 ***************************************************************************/
static inline void
timeout_unlink(struct TimeoutEntry *entry)
{
    if (entry->prev == 0 && entry->next == 0)
        return;
    *(entry->prev) = entry->next;
    if (entry->next)
        entry->next->prev = entry->prev;
    entry->next = 0;
    entry->prev = 0;
    entry->timestamp = 0;
}

/***************************************************************************
 ***************************************************************************/
static inline void
timeout_init(struct TimeoutEntry *entry)
{
    entry->next = 0;
    entry->prev = 0;
}

/**
 * Create a timeout subsystem.
 * @param now
 *      The current timestamp indicating "now" when the thing starts,
 *      or time(0).
 * @param usec
 *      The number of microseconds since the start of the current
 *      second. This can be an accurate number of the precise
 *      timestamp, but usually should just be zero, since we aren't
 *      concerned with sub-second timings at startup.
 *
 */
struct Timeouts *
timeouts_create(time_t now, unsigned usec);

/**
 * Insert the timeout 'entry' into the future location in the timeout
 * ring, as determined by the timestamp. This must be removed either
 * with 'timeout_remove()' at the normal time, or "timeout_unlink()'
 * on cleanup.
 * @param timeouts
 *      A ring of timeouts, with each slot corresponding to a specific
 *      time in the future.
 * @param entry
 *      The entry that we are going to insert into the ring. If it's
 *      already in the ring, it'll be removed from the old location
 *      first before inserting into the new location.
 * @param offset
 *      The 'entry' field above is part of an existing structure. This
 *      tells the offset_of() from the begining of that structure. 
 *      In other words, this tells us the pointer to the object that
 *      that is the subject of the timeout.
 * @param expires
 *      When this timeout will expire. This is in terms of internal
 *      ticks, which in units of TICKS_PER_SECOND.
 */
void
timeouts_add(struct Timeouts *timeouts, struct TimeoutEntry *entry,
                  size_t offset, timestamp_t expires);

/**
 * Remove an object from the timestamp system that is older than than
 * the specified timestamp. This function must be called repeatedly
 * until it returns NULL to remove all the objects that are older
 * than the given timestamp.
 * @param timeouts
 *      A ring of timeouts. We'll walk the ring until we've caught
 *      up with the current time.
 * @param now
 *      Usually, this timestmap will be "now", the current time,
 *      and anything older than this will be aged out.
 * @return
 *      an object older than the specified timestamp, or NULL
 *      if there are no more objects to be found
 */
void *
timeouts_remove(struct Timeouts *timeouts, timestamp_t now);

/*
 * This macros convert a normal "timeval" structure into the timestamp
 * that we use for timeouts. The timeval structure probably will come
 * from the packets that we are capturing.
 */
#define TICKS_PER_SECOND (16384ULL)
#define TICKS_FROM_SECS(secs) ((secs)*16384ULL)
#define TICKS_FROM_USECS(usecs) ((usecs)/16384ULL)
#define TICKS_FROM_TV(secs,usecs) (TICKS_FROM_SECS(secs)+TICKS_FROM_USECS(usecs))

static inline timestamp_t
timestamp_from_tv(time_t secs, unsigned usecs)
{
    return TICKS_FROM_TV((timestamp_t)secs, (timestamp_t)usecs);
}

#endif
