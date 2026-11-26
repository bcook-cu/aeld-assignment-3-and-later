#define _DEFAULT_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <time.h>
#include <unistd.h>

#define FILE_NAME "/var/tmp/aesdsocketdata"
#define SERVER_PORT 9000
#define TIMER_INTERVAL_SECONDS 10
#define TIME_FORMAT "%a, %d %b %Y %H:%M:%S %z"

int s_fd = -1;
FILE *file = NULL;
volatile sig_atomic_t stop_requested = 0;
int timer_stop = 0;

pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t timer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t timer_cond = PTHREAD_COND_INITIALIZER;

struct client {
    int fd;
    pthread_t thread;
    int done;
    char ip[INET_ADDRSTRLEN];
    SLIST_ENTRY(client) next;
};

SLIST_HEAD(client_head, client);
struct client_head clients = SLIST_HEAD_INITIALIZER(clients);

void start_daemon()
{
    pid_t pid;
    pid = fork();
    if (pid < 0) exit(1);
    if (pid > 0) exit(0);
    if (setsid() < 0) exit(1);
}

void handle_signal(int s)
{
    (void)s;
    syslog(LOG_INFO, "Caught signal, exiting");
    stop_requested = 1;
}

int install_signals()
{
    struct sigaction action;

    memset(&action, 0, sizeof action);
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);

    if (sigaction(SIGINT, &action, NULL) != 0)
        return -1;
    if (sigaction(SIGTERM, &action, NULL) != 0)
        return -1;

    return 0;
}

int append(char **pkt, size_t *len, size_t *cap, char byte)
{
    if (*len == *cap) {
        size_t new_cap;

        if (*cap == 0) {
            new_cap = 256;
        } else {
            if (*cap > SIZE_MAX / 2) {
                errno = EOVERFLOW;
                return -1;
            }
            new_cap = *cap * 2;
        }

        char *new_pkt = realloc(*pkt, new_cap);
        if (new_pkt == NULL)
            return -1;

        *pkt = new_pkt;
        *cap = new_cap;
    }

    (*pkt)[(*len)++] = byte;
    return 0;
}

int send_chunk(int fd, const char *data, size_t len)
{
    while (len > 0)
    {
        ssize_t sent = send(fd, data, len, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (sent == 0) return -1;
        data += sent;
        len -= (size_t)sent;
    }
    return 0;
}

int send_file(int fd, FILE *file)
{
    char buf[4096];
    size_t bytes_read;

    if (fflush(file) != 0) return -1;
    if (fseek(file, 0, SEEK_SET) != 0) return -1;

    while ((bytes_read = fread(buf, 1, sizeof(buf), file)) > 0)
    {
        if(send_chunk(fd, buf, bytes_read) != 0) return -1;
    }

    if (ferror(file)) return -1;
    if (fseek(file, 0, SEEK_END) != 0) return -1;
    return 0;
}

int write_packet(int fd, char *pkt, size_t len)
{
    int rc = 0;

    pthread_mutex_lock(&file_mutex);

    if (fwrite(pkt, 1, len, file) != len)
    {
        syslog(LOG_ERR, "fwrite: %s", strerror(errno));
        rc = -1;
    }
    else if (fflush(file) != 0)
    {
        syslog(LOG_ERR, "fflush: %s", strerror(errno));
        rc = -1;
    }
    else if (send_file(fd, file) != 0)
    {
        syslog(LOG_ERR, "send_file: %s", strerror(errno));
        rc = -1;
    }

    clearerr(file);
    if (fseek(file, 0, SEEK_END) != 0)
    {
        syslog(LOG_ERR, "fseek: %s", strerror(errno));
        rc = -1;
    }

    pthread_mutex_unlock(&file_mutex);
    return rc;
}

int handle_client(int fd)
{
    char buf[4096];
    char *pkt = NULL;
    size_t pkt_len = 0;
    size_t cap = 0;
    int rc = 0;

    for(;;)
    {
        ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n == 0) break;
        if (n < 0)
        {
            if (errno == EINTR)
            {
                if (stop_requested) break;
                continue;
            }
            syslog(LOG_ERR, "recv: %s", strerror(errno));
            rc = -1;
            break;
        }

        for (ssize_t i = 0; i < n; i++)
        {
            if(append(&pkt, &pkt_len, &cap, buf[i]) < 0) {
                syslog(LOG_ERR, "append: %s", strerror(errno));
                rc = -1;
                break;
            }

            if (buf[i] == '\n')
            {
                if (write_packet(fd, pkt, pkt_len) != 0)
                {
                    rc = -1;
                    break;
                }
                pkt_len = 0;
            }
        }

        if (rc != 0) break;
    }

    free(pkt);
    return rc;
}

void *client_thread(void *arg)
{
    struct client *c = arg;

    if (handle_client(c->fd) != 0)
        syslog(LOG_INFO, "Client connection ended with an error");

    pthread_mutex_lock(&client_mutex);
    close(c->fd);
    c->fd = -1;
    c->done = 1;
    pthread_mutex_unlock(&client_mutex);

    syslog(LOG_INFO, "Closed connection from %s", c->ip);
    return NULL;
}

int add_client(int fd, struct sockaddr_in *addr)
{
    struct client *c = calloc(1, sizeof *c);
    int err;

    if (c == NULL)
        return -1;

    c->fd = fd;
    c->done = 0;
    if (inet_ntop(AF_INET, &addr->sin_addr, c->ip, sizeof c->ip) == NULL)
        strcpy(c->ip, "unknown");

    syslog(LOG_INFO, "Accepted connection from %s", c->ip);

    pthread_mutex_lock(&client_mutex);
    SLIST_INSERT_HEAD(&clients, c, next);
    err = pthread_create(&c->thread, NULL, client_thread, c);
    if (err != 0) {
        SLIST_REMOVE(&clients, c, client, next);
        pthread_mutex_unlock(&client_mutex);
        free(c);
        errno = err;
        return -1;
    }
    pthread_mutex_unlock(&client_mutex);

    return 0;
}

void reap_clients()
{
    struct client *c;
    struct client *tmp;
    struct client_head finished;

    SLIST_INIT(&finished);

    pthread_mutex_lock(&client_mutex);
    for (c = SLIST_FIRST(&clients); c != NULL; c = tmp) {
        tmp = SLIST_NEXT(c, next);
        if (c->done) {
            SLIST_REMOVE(&clients, c, client, next);
            SLIST_INSERT_HEAD(&finished, c, next);
        }
    }
    pthread_mutex_unlock(&client_mutex);

    for (c = SLIST_FIRST(&finished); c != NULL; c = tmp) {
        tmp = SLIST_NEXT(c, next);
        SLIST_REMOVE(&finished, c, client, next);
        pthread_join(c->thread, NULL);
        free(c);
    }
}

void stop_clients()
{
    struct client *c;
    struct client *tmp;
    struct client_head remaining;

    SLIST_INIT(&remaining);

    pthread_mutex_lock(&client_mutex);
    for (c = SLIST_FIRST(&clients); c != NULL; c = tmp) {
        tmp = SLIST_NEXT(c, next);
        if (c->fd >= 0)
            shutdown(c->fd, SHUT_RDWR);
        SLIST_REMOVE(&clients, c, client, next);
        SLIST_INSERT_HEAD(&remaining, c, next);
    }
    pthread_mutex_unlock(&client_mutex);

    for (c = SLIST_FIRST(&remaining); c != NULL; c = tmp) {
        tmp = SLIST_NEXT(c, next);
        SLIST_REMOVE(&remaining, c, client, next);
        pthread_join(c->thread, NULL);
        free(c);
    }
}

int append_timestamp()
{
    time_t now;
    struct tm current;
    char date[128];
    char line[160];
    int len;
    int rc = 0;

    now = time(NULL);
    if (now == (time_t)-1)
        return -1;
    if (localtime_r(&now, &current) == NULL)
        return -1;
    if (strftime(date, sizeof date, TIME_FORMAT, &current) == 0) {
        errno = EINVAL;
        return -1;
    }

    len = snprintf(line, sizeof line, "timestamp:%s\n", date);
    if (len < 0 || (size_t)len >= sizeof line) {
        errno = EOVERFLOW;
        return -1;
    }

    pthread_mutex_lock(&file_mutex);

    if (fwrite(line, 1, (size_t)len, file) != (size_t)len || fflush(file) != 0)
        rc = -1;

    clearerr(file);
    if (fseek(file, 0, SEEK_END) != 0)
        rc = -1;

    pthread_mutex_unlock(&file_mutex);
    return rc;
}

void *timer_thread(void *arg)
{
    (void)arg;

    pthread_mutex_lock(&timer_mutex);

    while (!timer_stop) {
        struct timespec deadline;
        int result;

        if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) {
            syslog(LOG_ERR, "clock_gettime: %s", strerror(errno));
            break;
        }
        deadline.tv_sec += TIMER_INTERVAL_SECONDS;

        result = 0;
        while (!timer_stop && result != ETIMEDOUT) {
            result = pthread_cond_timedwait(&timer_cond, &timer_mutex, &deadline);
            if (result != 0 && result != ETIMEDOUT) {
                syslog(LOG_ERR, "pthread_cond_timedwait: %s", strerror(result));
                timer_stop = 1;
            }
        }

        if (timer_stop)
            break;

        pthread_mutex_unlock(&timer_mutex);
        if (append_timestamp() != 0)
            syslog(LOG_ERR, "append_timestamp: %s", strerror(errno));
        pthread_mutex_lock(&timer_mutex);
    }

    pthread_mutex_unlock(&timer_mutex);
    return NULL;
}

void stop_timer(pthread_t tid)
{
    pthread_mutex_lock(&timer_mutex);
    timer_stop = 1;
    pthread_cond_signal(&timer_cond);
    pthread_mutex_unlock(&timer_mutex);

    pthread_join(tid, NULL);
}

int create_socket()
{
    int fd;
    int reuse = 1;
    struct sockaddr_in address;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse) != 0) {
        close(fd);
        return -1;
    }

    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(SERVER_PORT);

    if (bind(fd, (struct sockaddr *)&address, sizeof address) != 0) {
        close(fd);
        return -1;
    }

    if (listen(fd, 10) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

int main(int argc, char *argv[])
{
    int daemon = 0;
    pthread_t timer_tid;
    int err;
    struct pollfd pfd;

    openlog(argv[0], LOG_PID, LOG_DAEMON);

    if (argc == 2 && strcmp(argv[1], "-d") == 0)
    {
        daemon = 1;
    } else if (argc != 1) {
        syslog(LOG_ERR, "Usage: %s [-d]", argv[0]);
        closelog();
        return -1;
    }

    if (daemon) start_daemon();

    if (install_signals() != 0) {
        syslog(LOG_ERR, "sigaction: %s", strerror(errno));
        closelog();
        return -1;
    }

    s_fd = create_socket();
    if (s_fd < 0) {
        syslog(LOG_ERR, "create_socket: %s", strerror(errno));
        closelog();
        return -1;
    }

    file = fopen(FILE_NAME, "a+");
    if (file == NULL) {
        syslog(LOG_ERR, "fopen: %s", strerror(errno));
        close(s_fd);
        closelog();
        return -1;
    }

    err = pthread_create(&timer_tid, NULL, timer_thread, NULL);
    if (err != 0) {
        syslog(LOG_ERR, "pthread_create timer: %s", strerror(err));
        fclose(file);
        file = NULL;
        remove(FILE_NAME);
        close(s_fd);
        closelog();
        return -1;
    }

    pfd.fd = s_fd;
    pfd.events = POLLIN;

    while (!stop_requested) {
        struct sockaddr_in c_addr;
        socklen_t addr_len;

        pfd.revents = 0;
        err = poll(&pfd, 1, 500);

        if (stop_requested)
            break;

        if (err < 0) {
            if (errno == EINTR)
                continue;
            syslog(LOG_ERR, "poll: %s", strerror(errno));
            break;
        }

        if (err == 0) {
            reap_clients();
            continue;
        }

        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            syslog(LOG_ERR, "poll: listening socket error");
            break;
        }

        if (pfd.revents & POLLIN) {
            addr_len = sizeof c_addr;
            int c_fd = accept(s_fd, (struct sockaddr *)&c_addr, &addr_len);
            if (c_fd < 0) {
                if (errno == EINTR)
                    continue;
                if (!stop_requested)
                    syslog(LOG_ERR, "accept: %s", strerror(errno));
            } else if (add_client(c_fd, &c_addr) != 0) {
                syslog(LOG_ERR, "add_client: %s", strerror(errno));
                close(c_fd);
            }
        }

        reap_clients();
    }

    close(s_fd);
    s_fd = -1;

    stop_clients();
    stop_timer(timer_tid);

    pthread_mutex_lock(&file_mutex);
    if (file != NULL) {
        fclose(file);
        file = NULL;
    }
    pthread_mutex_unlock(&file_mutex);

    if (remove(FILE_NAME) != 0 && errno != ENOENT)
        syslog(LOG_ERR, "remove: %s", strerror(errno));

    syslog(LOG_INFO, "Server exiting");
    closelog();
    return 0;
}
