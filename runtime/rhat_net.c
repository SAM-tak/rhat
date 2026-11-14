// The native part of std.net and std.async: non-blocking sockets, and the waits a coroutine
// yields until a socket is ready or a timer is due. Each thread has its own table of waits, so
// each worker of a thread-per-core program runs its own loop.
//
// Sockets answer a handle (the OS socket plus one, so 0 means none) or a negative code:
// RH_NET_PENDING when the operation would block, else one of the failures below.
// -std=c11 hides POSIX: getaddrinfo, clock_gettime and nanosleep need it asked for.
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE
#endif
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
// clang-format off: winsock2.h must come before windows.h.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
// clang-format on
typedef SOCKET RhSocket;
#define RH_BAD_SOCKET INVALID_SOCKET
#define rh_poll WSAPoll
#define rh_close_socket closesocket
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#else
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
typedef int RhSocket;
#define RH_BAD_SOCKET (-1)
#define rh_poll poll
#define rh_close_socket close
#endif

#if defined(MSG_NOSIGNAL)
#define RH_SEND_FLAGS MSG_NOSIGNAL
#else
#define RH_SEND_FLAGS 0
#endif

enum {
    RH_NET_FAILED = -1,
    RH_NET_ADDRESS_IN_USE = -2,
    RH_NET_RESOLVE = -3,
    RH_NET_UNREACHABLE = -4,
    RH_NET_REFUSED = -5,
    RH_NET_RESET = -6,
    RH_NET_TOO_LARGE = -7,
    RH_NET_CLOSED = -8,
    RH_NET_PENDING = -100,
};

static RhSocket socket_of(int64_t handle)
{
    return (RhSocket)(handle - 1);
}

static int64_t handle_of(RhSocket s)
{
    return (int64_t)s + 1;
}

#if defined(_WIN32)
static BOOL CALLBACK net_startup(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    (void)once;
    (void)parameter;
    (void)context;
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
    return TRUE;
}
#endif

// Starts Winsock once; a thread that arrives while another starts it waits for it to finish.
static void net_start(void)
{
#if defined(_WIN32)
    static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
    InitOnceExecuteOnce(&once, net_startup, NULL, NULL);
#endif
}

// The failure the last socket call reported, or RH_NET_PENDING when it would block.
static int64_t last_failure(void)
{
#if defined(_WIN32)
    switch (WSAGetLastError()) {
    case WSAEWOULDBLOCK:
    case WSAEINPROGRESS:
        return RH_NET_PENDING;
    case WSAEADDRINUSE:
        return RH_NET_ADDRESS_IN_USE;
    case WSAENETUNREACH:
    case WSAEHOSTUNREACH:
    case WSAEADDRNOTAVAIL:
        return RH_NET_UNREACHABLE;
    case WSAECONNREFUSED:
        return RH_NET_REFUSED;
    case WSAECONNRESET:
    case WSAECONNABORTED:
    case WSAENETRESET:
        return RH_NET_RESET;
    case WSAEMSGSIZE:
        return RH_NET_TOO_LARGE;
    case WSAENOTSOCK:
    case WSAESHUTDOWN:
        return RH_NET_CLOSED;
    default:
        return RH_NET_FAILED;
    }
#else
    switch (errno) {
    case EAGAIN:
#if EWOULDBLOCK != EAGAIN
    case EWOULDBLOCK:
#endif
    case EINPROGRESS:
        return RH_NET_PENDING;
    case EADDRINUSE:
        return RH_NET_ADDRESS_IN_USE;
    case ENETUNREACH:
    case EHOSTUNREACH:
    case EADDRNOTAVAIL:
        return RH_NET_UNREACHABLE;
    case ECONNREFUSED:
        return RH_NET_REFUSED;
    case ECONNRESET:
    case ECONNABORTED:
    case EPIPE:
        return RH_NET_RESET;
    case EMSGSIZE:
        return RH_NET_TOO_LARGE;
    case EBADF:
    case ENOTSOCK:
        return RH_NET_CLOSED;
    default:
        return RH_NET_FAILED;
    }
#endif
}

static bool nonblocking(RhSocket s)
{
#if defined(_WIN32)
    u_long on = 1;
    return ioctlsocket(s, FIONBIO, &on) == 0;
#else
    int flags = fcntl(s, F_GETFL, 0);
    return flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

// A new non-blocking socket of the family of address, or RH_BAD_SOCKET.
static RhSocket open_socket(const struct sockaddr *address, int type)
{
    net_start();
    RhSocket s = socket(address->sa_family, type, type == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP);
    if (s == RH_BAD_SOCKET) {
        return s;
    }
    if (!nonblocking(s)) {
        rh_close_socket(s);
        return RH_BAD_SOCKET;
    }
#if defined(SO_NOSIGPIPE)
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
#if defined(_WIN32)
    if (type == SOCK_DGRAM) {
        // A datagram to a closed port must not fail the next receive.
        BOOL off = FALSE;
        DWORD ignored = 0;
        WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof(off), NULL, 0, &ignored, NULL, NULL);
    }
#endif
    return s;
}

// An address is kept by R^ in 32 bytes: the sockaddr itself, which says its family.
static socklen_t address_length(const struct sockaddr *address)
{
    return address->sa_family == AF_INET6 ? sizeof(struct sockaddr_in6)
                                          : sizeof(struct sockaddr_in);
}

int64_t rh_net_resolve(const uint8_t *host, int64_t length, int64_t port, uint8_t *address)
{
    net_start();
    if (port < 0 || port > 65535) {
        return RH_NET_FAILED;
    }
    char name[256], service[16];
    if (length < 0 || length >= (int64_t)sizeof(name)) {
        return RH_NET_RESOLVE;
    }
    memcpy(name, host, (size_t)length);
    name[length] = 0;
    snprintf(service, sizeof(service), "%d", (int)port);
    struct addrinfo hints = {0}, *found = NULL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_NUMERICSERV;
    if (getaddrinfo(name, service, &hints, &found) != 0 || !found) {
        return RH_NET_RESOLVE;
    }
    memset(address, 0, 32);
    memcpy(address, found->ai_addr,
           found->ai_addrlen <= 32 ? (size_t)found->ai_addrlen : (size_t)32);
    freeaddrinfo(found);
    return 0;
}

int64_t rh_net_address_port(const uint8_t *address)
{
    const struct sockaddr *a = (const struct sockaddr *)address;
    if (a->sa_family == AF_INET6) {
        return ntohs(((const struct sockaddr_in6 *)address)->sin6_port);
    }
    return ntohs(((const struct sockaddr_in *)address)->sin_port);
}

// Writes the numeric host of address to out and answers its length.
int64_t rh_net_address_host(const uint8_t *address, uint8_t *out, int64_t capacity)
{
    const struct sockaddr *a = (const struct sockaddr *)address;
    char text[64] = "";
    if (a->sa_family == AF_INET6) {
        inet_ntop(AF_INET6, &((const struct sockaddr_in6 *)address)->sin6_addr, text, sizeof(text));
    }
    else if (a->sa_family == AF_INET) {
        inet_ntop(AF_INET, &((const struct sockaddr_in *)address)->sin_addr, text, sizeof(text));
    }
    int64_t n = (int64_t)strlen(text);
    if (n > capacity) {
        n = capacity;
    }
    memcpy(out, text, (size_t)n);
    return n;
}

int64_t rh_net_listen(const uint8_t *address)
{
    const struct sockaddr *a = (const struct sockaddr *)address;
    RhSocket s = open_socket(a, SOCK_STREAM);
    if (s == RH_BAD_SOCKET) {
        return last_failure();
    }
#if !defined(_WIN32)
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#endif
    if (bind(s, a, address_length(a)) != 0 || listen(s, SOMAXCONN) != 0) {
        int64_t failure = last_failure();
        rh_close_socket(s);
        return failure;
    }
    return handle_of(s);
}

int64_t rh_net_accept(int64_t listener)
{
    RhSocket s = accept(socket_of(listener), NULL, NULL);
    if (s == RH_BAD_SOCKET) {
        return last_failure();
    }
    if (!nonblocking(s)) {
        rh_close_socket(s);
        return RH_NET_FAILED;
    }
#if defined(SO_NOSIGPIPE)
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
    int nodelay = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&nodelay, sizeof(nodelay));
    return handle_of(s);
}

// Starts connecting; the socket becomes writable when it is done, and rh_net_connected tells
// whether it succeeded.
int64_t rh_net_connect(const uint8_t *address)
{
    const struct sockaddr *a = (const struct sockaddr *)address;
    RhSocket s = open_socket(a, SOCK_STREAM);
    if (s == RH_BAD_SOCKET) {
        return last_failure();
    }
    int nodelay = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&nodelay, sizeof(nodelay));
    if (connect(s, a, address_length(a)) != 0) {
        int64_t failure = last_failure();
        if (failure != RH_NET_PENDING) {
            rh_close_socket(s);
            return failure;
        }
    }
    return handle_of(s);
}

int64_t rh_net_connected(int64_t handle)
{
    int code = 0;
    socklen_t length = sizeof(code);
    if (getsockopt(socket_of(handle), SOL_SOCKET, SO_ERROR, (char *)&code, &length) != 0) {
        return last_failure();
    }
    if (code == 0) {
        return 0;
    }
#if defined(_WIN32)
    WSASetLastError(code);
#else
    errno = code;
#endif
    int64_t failure = last_failure();
    return failure == RH_NET_PENDING ? RH_NET_FAILED : failure;
}

// Answers how many bytes were read; 0 means the peer closed the connection.
int64_t rh_net_read(int64_t handle, uint8_t *buffer, int64_t capacity)
{
    int n = (int)recv(socket_of(handle), (char *)buffer,
                      (int)(capacity > INT32_MAX ? INT32_MAX : capacity), 0);
    return n < 0 ? last_failure() : n;
}

int64_t rh_net_write(int64_t handle, const uint8_t *data, int64_t length)
{
    int n = (int)send(socket_of(handle), (const char *)data,
                      (int)(length > INT32_MAX ? INT32_MAX : length), RH_SEND_FLAGS);
    return n < 0 ? last_failure() : n;
}

void rh_net_close(int64_t handle)
{
    if (handle > 0) {
        rh_close_socket(socket_of(handle));
    }
}

int64_t rh_net_port(int64_t handle)
{
    struct sockaddr_storage local;
    socklen_t length = sizeof(local);
    if (getsockname(socket_of(handle), (struct sockaddr *)&local, &length) != 0) {
        return 0;
    }
    return rh_net_address_port((const uint8_t *)&local);
}

int64_t rh_net_udp_open(const uint8_t *address)
{
    RhSocket s = open_socket((const struct sockaddr *)address, SOCK_DGRAM);
    return s == RH_BAD_SOCKET ? last_failure() : handle_of(s);
}

int64_t rh_net_udp_bind(int64_t handle, const uint8_t *address)
{
    const struct sockaddr *a = (const struct sockaddr *)address;
    return bind(socket_of(handle), a, address_length(a)) == 0 ? 0 : last_failure();
}

int64_t rh_net_udp_peer(int64_t handle, const uint8_t *address)
{
    const struct sockaddr *a = (const struct sockaddr *)address;
    return connect(socket_of(handle), a, address_length(a)) == 0 ? 0 : last_failure();
}

int64_t rh_net_udp_broadcast(int64_t handle, bool on)
{
    int value = on;
    return setsockopt(socket_of(handle), SOL_SOCKET, SO_BROADCAST, (const char *)&value,
                      sizeof(value)) == 0
               ? 0
               : last_failure();
}

// A datagram that does not fit the send buffer is dropped, as UDP may; that is not a failure.
int64_t rh_net_udp_send(int64_t handle, const uint8_t *data, int64_t length, const uint8_t *address)
{
    const struct sockaddr *a = (const struct sockaddr *)address;
    int n = address ? (int)sendto(socket_of(handle), (const char *)data, (int)length, RH_SEND_FLAGS,
                                  a, address_length(a))
                    : (int)send(socket_of(handle), (const char *)data, (int)length, RH_SEND_FLAGS);
    if (n >= 0) {
        return 0;
    }
    int64_t failure = last_failure();
    return failure == RH_NET_PENDING ? 0 : failure;
}

int64_t rh_net_udp_send_peer(int64_t handle, const uint8_t *data, int64_t length)
{
    return rh_net_udp_send(handle, data, length, NULL);
}

int64_t rh_net_udp_receive(int64_t handle, uint8_t *buffer, int64_t capacity, uint8_t *from)
{
    struct sockaddr_storage peer;
    socklen_t length = sizeof(peer);
    int n = (int)recvfrom(socket_of(handle), (char *)buffer,
                          (int)(capacity > INT32_MAX ? INT32_MAX : capacity), 0,
                          (struct sockaddr *)&peer, &length);
    if (n < 0) {
        return last_failure();
    }
    memset(from, 0, 32);
    memcpy(from, &peer, length <= 32 ? (size_t)length : (size_t)32);
    return n;
}

// Waits: each is an id a coroutine yields, ready when its socket is, its time has come, or
// another thread completes it (an external wait).
typedef struct External External;

typedef struct {
    int64_t id;
    int64_t handle; // A socket; 0 for a timer or an external wait.
    bool write;
    double due;         // Seconds on the monotonic clock, for a timer.
    External *external; // An external wait, completed by another thread.
} Wait;

static _Thread_local Wait *waits;
static _Thread_local int64_t wait_count, wait_capacity, last_id;

// The socket a waiting thread polls so that another thread can wake it: a loopback UDP socket
// that sends to itself.
typedef struct {
    RhSocket socket;
    struct sockaddr_in address;
    atomic_int holds;
} Waker;

static _Thread_local Waker *waker;

// Its first member is what the Channel runtime calls; see RhWatch in rhat_thread.c.
struct External {
    void (*complete)(External *);
    atomic_int done, holds;
    Waker *waker;
};

static void waker_release(Waker *w)
{
    if (atomic_fetch_sub(&w->holds, 1) == 1) {
        rh_close_socket(w->socket);
        free(w);
    }
}

static void external_release(External *e)
{
    if (atomic_fetch_sub(&e->holds, 1) == 1) {
        waker_release(e->waker);
        free(e);
    }
}

// Completes e once, from any thread: its waiter finds it done and wakes from poll.
static void external_complete(External *e)
{
    if (atomic_exchange(&e->done, 1) == 0) {
        char byte = 1;
        sendto(e->waker->socket, &byte, 1, 0, (const struct sockaddr *)&e->waker->address,
               sizeof(e->waker->address));
    }
    external_release(e);
}

static Waker *thread_waker(void)
{
    if (waker) {
        return waker;
    }
    net_start();
    struct sockaddr_in loopback = {0};
    loopback.sin_family = AF_INET;
    loopback.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    RhSocket s = open_socket((const struct sockaddr *)&loopback, SOCK_DGRAM);
    socklen_t length = sizeof(loopback);
    if (s == RH_BAD_SOCKET || bind(s, (const struct sockaddr *)&loopback, sizeof(loopback)) != 0 ||
        getsockname(s, (struct sockaddr *)&loopback, &length) != 0) {
        if (s != RH_BAD_SOCKET) {
            rh_close_socket(s);
        }
        return NULL;
    }
    Waker *w = calloc(1, sizeof(*w));
    if (!w) {
        rh_close_socket(s);
        return NULL;
    }
    w->socket = s;
    w->address = loopback;
    atomic_init(&w->holds, 1); // The thread's own hold, kept while it lives.
    waker = w;
    return w;
}

static double now(void)
{
#if defined(_WIN32)
    static LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    if (!frequency.QuadPart) {
        QueryPerformanceFrequency(&frequency);
    }
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
#endif
}

static int64_t add_wait(Wait w)
{
    if (wait_count == wait_capacity) {
        int64_t capacity = wait_capacity ? wait_capacity * 2 : 16;
        Wait *grown = realloc(waits, (size_t)capacity * sizeof(Wait));
        if (!grown) {
            return 0;
        }
        waits = grown;
        wait_capacity = capacity;
    }
    w.id = ++last_id;
    waits[wait_count++] = w;
    return last_id;
}

static int64_t take_wait(int64_t k)
{
    int64_t id = waits[k].id;
    if (waits[k].external) {
        external_release(waits[k].external);
    }
    waits[k] = waits[--wait_count];
    return id;
}

// The id of the timer that came due first by t, or of an external wait another thread has
// completed, taken from the table; 0 if there is none.
static int64_t take_due(double t)
{
    int64_t first = -1;
    for (int64_t k = 0; k < wait_count; k++) {
        if (waits[k].external) {
            if (atomic_load(&waits[k].external->done)) {
                return take_wait(k);
            }
        }
        else if (!waits[k].handle && waits[k].due <= t &&
                 (first < 0 || waits[k].due < waits[first].due)) {
            first = k;
        }
    }
    return first < 0 ? 0 : take_wait(first);
}

int64_t rh_async_timer(double seconds)
{
    return add_wait((Wait){.due = now() + (seconds > 0 ? seconds : 0)});
}

int64_t rh_async_watch(int64_t handle, bool write)
{
    return add_wait((Wait){.handle = handle, .write = write});
}

// A wait another thread completes: *token goes to whoever completes it (rh_async_complete,
// or a Channel through rh_channel_watch), exactly once.
int64_t rh_async_external(int64_t *token)
{
    Waker *w = thread_waker();
    External *e = w ? calloc(1, sizeof(*e)) : NULL;
    if (!e) {
        *token = 0;
        return 0;
    }
    e->complete = external_complete;
    atomic_init(&e->done, 0);
    atomic_init(&e->holds, 2); // The table's and the completer's.
    atomic_fetch_add(&w->holds, 1);
    e->waker = w;
    *token = (int64_t)(uintptr_t)e;
    int64_t id = add_wait((Wait){.external = e});
    if (!id) {
        external_release(e);
    }
    return id;
}

void rh_async_complete(int64_t token)
{
    External *e = (External *)(uintptr_t)token;
    if (e) {
        e->complete(e);
    }
}

// The index of wait id in this thread's table, or -1.
static int64_t find_wait(int64_t id)
{
    for (int64_t k = 0; k < wait_count; k++) {
        if (waits[k].id == id) {
            return k;
        }
    }
    return -1;
}

void rh_async_drop(int64_t id)
{
    int64_t k = find_wait(id);
    if (k >= 0) {
        take_wait(k);
    }
}

int64_t rh_async_pending(void)
{
    return wait_count;
}

bool rh_async_waiting(int64_t id)
{
    return find_wait(id) >= 0;
}

double rh_async_next(void)
{
    double t = now(), next = -1;
    for (int64_t k = 0; k < wait_count; k++) {
        if (!waits[k].handle && !waits[k].external) {
            double left = waits[k].due > t ? waits[k].due - t : 0;
            next = next < 0 || left < next ? left : next;
        }
    }
    return next;
}

// Answers the id of one wait that is ready, sleeping up to timeout seconds for one; 0 when
// none became ready, or when nothing is waited for.
int64_t rh_async_wait(double timeout)
{
    double start = now(), limit = start + (timeout > 0 ? timeout : 0);
    for (;;) {
        double t = now(), next = limit;
        int64_t sockets = 0, due = take_due(t);
        if (due) {
            return due;
        }
        bool externals = false;
        for (int64_t k = 0; k < wait_count; k++) {
            if (waits[k].external) {
                externals = true;
            }
            else if (!waits[k].handle) {
                next = waits[k].due < next ? waits[k].due : next;
            }
            else {
                sockets++;
            }
        }
        if (!wait_count) {
            return 0;
        }
        // The waker is polled too, last, while any external wait is pending.
        int64_t slots = sockets + (externals && waker ? 1 : 0);
        struct pollfd *polled = slots ? malloc((size_t)slots * sizeof(struct pollfd)) : NULL;
        int64_t *which = slots ? malloc((size_t)slots * sizeof(int64_t)) : NULL;
        int64_t n = 0;
        for (int64_t k = 0; polled && which && k < wait_count; k++) {
            if (waits[k].handle) {
                polled[n] = (struct pollfd){.fd = socket_of(waits[k].handle),
                                            .events = waits[k].write ? POLLOUT : POLLIN};
                which[n++] = k;
            }
        }
        if (polled && which && externals && waker) {
            polled[n] = (struct pollfd){.fd = waker->socket, .events = POLLIN};
            which[n++] = -1;
        }
        int ms = next > t ? (int)((next - t) * 1000.0 + 0.999) : 0;
        int ready = 0;
        if (n) {
            ready = rh_poll(polled, (unsigned long)n, ms);
        }
        else if (ms > 0) {
#if defined(_WIN32)
            Sleep((DWORD)ms);
#else
            struct timespec pause = {ms / 1000, (long)(ms % 1000) * 1000000L};
            nanosleep(&pause, NULL);
#endif
        }
        int64_t found = 0;
        for (int64_t j = 0; ready > 0 && j < n && !found; j++) {
            if (!polled[j].revents) {
                continue;
            }
            if (which[j] < 0) {
                // Drain the wake-ups; take_due finds the completed waits below.
                char bytes[64];
                while (recv(waker->socket, bytes, sizeof(bytes), 0) > 0) {
                }
                continue;
            }
            found = take_wait(which[j]);
        }
        free(polled);
        free(which);
        if (found) {
            return found;
        }
        due = take_due(now());
        if (due || now() >= limit) {
            return due; // A completion, or a timer that came due meanwhile.
        }
    }
}
