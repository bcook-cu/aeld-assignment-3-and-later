#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <unistd.h>

#define FILE_NAME "/var/tmp/aesdsocketdata"
int s_fd = -1;
int c_fd = -1;
FILE *file = NULL; 

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
    (void)s; // unused
    syslog(LOG_INFO, "Caught signal, exiting"); 
    if (c_fd >= 0) close(c_fd);
    if (s_fd >= 0) close(s_fd);
    if (file != NULL) fclose(file);
    if (remove(FILE_NAME) != 0) exit(1);
    closelog();
    exit(0);
}

int append(char **pkt, size_t *len, size_t *cap, char byte)
{
    if (*len == *cap) {
        size_t new_cap;

        if (*cap == 0) {
            new_cap = 256;
        } else {
            if (*cap > SIZE_MAX / 2)
                return -1;
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
        ssize_t sent = send(fd, data, len, MSG_NOSIGNAL); // no sigpipe
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

int handle_client(int fd, FILE *file)
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
            if (errno == EINTR) continue;
            rc = -1;
            break;
        }
        
        for (ssize_t i = 0; i < n; i++)
        {
            if(append(&pkt, &pkt_len, &cap, buf[i]) < 0) {
                syslog(LOG_ERR, "append:: %s", strerror(errno));
                rc = -1;
                break;
            }

            if (buf[i] == '\n')
            {
                if (fwrite(pkt, 1, pkt_len, file) != pkt_len)
                {
                    syslog(LOG_ERR, "fwrite: %s", strerror(errno));
                    rc = -1;
                    break;
                }
                if (fflush(file) != 0)
                {
                    syslog(LOG_ERR, "fflush: %s", strerror(errno));
                    rc = -1;
                    break;
                }
                // Send file to client
                if (send_file(fd, file) != 0)
                {
                    syslog(LOG_ERR, "send_file: %s", strerror(errno));
                    rc = -1;
                    break;
                }
                pkt_len = 0;
            }
        }
    }
    free(pkt);
    return rc;
}

int main(int argc, char *argv[])
{
    (void)argc; // unused
    openlog(argv[0], LOG_PID, LOG_DAEMON);

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    int daemon = 0;
    if (argc == 2 && strcmp(argv[1], "-d") == 0)
    {
        daemon = 1;
    } else if (argc != 1) {
        syslog(LOG_ERR, "Usage: %s [-d]", argv[0]);
        closelog();
        return -1;
    }

    struct addrinfo hints = {0};
    struct addrinfo *info = NULL;
    hints.ai_family = AF_INET;       // IPv4
    hints.ai_socktype = SOCK_STREAM; // TCP
    hints.ai_flags = AI_PASSIVE;     // suitable for bind

    struct sockaddr_in c_addr;
    socklen_t addr_len = sizeof(c_addr);

    if (getaddrinfo(NULL, "9000", &hints, &info) != 0) {
        syslog(LOG_ERR, "getaddrinfo: %s", strerror(errno));
        perror("getaddrinfo");
        closelog();
        return -1;
    }

    if ((s_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        syslog(LOG_ERR, "socket: %s", strerror(errno));
        freeaddrinfo(info);
        closelog();
        return -1;
    }

    if (bind(s_fd, info->ai_addr, info->ai_addrlen) < 0) {
        syslog(LOG_ERR, "bind: %s", strerror(errno));
        close(s_fd);
        freeaddrinfo(info);
        closelog();
        return -1;
    }

    if (listen(s_fd, 1) < 0) {
        syslog(LOG_ERR, "listen: %s", strerror(errno));
        close(s_fd);
        freeaddrinfo(info);
        closelog();
        return -1;
    }

    freeaddrinfo(info);

    file = fopen(FILE_NAME, "a+");
    if (file == NULL) {
        syslog(LOG_ERR, "fopen: %s", strerror(errno));
        close(s_fd);
        closelog();
        return -1;
    }

    if (daemon) start_daemon();

    for (;;) {
        if ((c_fd = accept(s_fd, (struct sockaddr *)&c_addr, &addr_len)) < 0) {
            syslog(LOG_ERR, "accept: %s", strerror(errno));
            fclose(file);
            close(s_fd);
            closelog();
            return -1;
        }

        char c_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &c_addr.sin_addr, c_ip, sizeof(c_ip));
        syslog(LOG_INFO, "Accepted connection from %s", c_ip);

        handle_client(c_fd, file);

        syslog(LOG_INFO, "Closed connection from %s", c_ip);
        close(c_fd);
    }

    fclose(file);
    close(s_fd);
    closelog();
    return 0;
}

