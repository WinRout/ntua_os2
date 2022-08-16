/*
 * crypto-client.c
 * TCP/IP encrypted communication using sockets
 * and cryptodev
 *
 * Nikitas Tsinnas <el18187@mail.ntua.gr>
 *
 */

#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/poll.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <crypto/cryptodev.h>

#include "socket-common.h"

#define DATA_SIZE 256
#define BLOCK_SIZE 16
#define KEY_SIZE 16

unsigned char buf[256];
unsigned char key[] = "aldkfjghqpwoecmm";
unsigned char inv[] = "allfjdhebwpxalty";
struct session_op sess;

/* Insist until all of the data has been written */
ssize_t insist_write(int fd, const void *buf, size_t cnt)
{
    ssize_t ret;
    size_t orig_cnt = cnt;

    while (cnt > 0)
    {
        ret = write(fd, buf, cnt);
        if (ret < 0)
            return ret;
        buf += ret;
        cnt -= ret;
    }

    return orig_cnt;
}

int encrypt(int cfd)
{
    int i;
    struct crypt_op cryp;
    struct
    {
        unsigned char encrypted[DATA_SIZE],
            iv[BLOCK_SIZE];
    } data;

    memset(&cryp, 0, sizeof(cryp));

    cryp.ses = sess.ses;
    cryp.len = sizeof(data.in);
    cryp.src = buf;
    cryp.dst = data.encrypted;
    cryp.iv = inv;
    cryp.op = COP_ENCRYPT;

    if (ioctl(cfd, CIOCCRYPT, &cryp))
    {
        perror("ioctl CIOCCRYPT encrypt");
        return 1;
    }

    i=0;
    while (data.encrypted[i] != '\0')
    {
        buf[i] = data.encrypted[i];
        i++;
    }
    return 0;
}

int decrypt(int cfd)
{
    int i;
    struct crypt_op cryp;
    struct
    {
        unsigned char in[DATA_SIZE],
            decrypted[DATA_SIZE],
            iv[BLOCK_SIZE];
    } data;

    memset(&cryp, 0, sizeof(crypt));

    cryp.ses = sess.ses;
    cryp.len = sizeof(data.in);
    cryp.src = buf;
    cryp.dst = data.decrypted;
    cryp.iv = inv;
    cryp.op = COP_DECRYPT;

    if (ioctl(cfd, CIOCCRYPT, &cryp))
    {
        perror("ioctl CIOCCRYPT decrypt");
        return 1;
    }

    i = 0;
    while (data.decrypted[i] != '\0')
    {
        buf[i] = data.decrypted[i];
        i++;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    int sd, port, cfd;
    ssize_t n;
    char *hostname;
    struct hostent *hp;
    struct sockaddr_in sa;
    struct pollfd fds[2];

    memset(&sess, 0, sizeof(sess));

    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s hostname port\n", argv[0]);
        exit(1);
    }
    hostname = argv[1];
    port = atoi(argv[2]); /* Needs better error checking */

    /* Create TCP/IP socket, used as main chat channel */
    if ((sd = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket");
        exit(1);
    }
    fprintf(stderr, "Created TCP socket\n");

    /* Look up remote hostname on DNS */
    if (!(hp = gethostbyname(hostname)))
    {
        printf("DNS lookup failed for host %s\n", hostname);
        exit(1);
    }

    /* Connect to remote TCP port */
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    memcpy(&sa.sin_addr.s_addr, hp->h_addr, sizeof(struct in_addr));
    fprintf(stderr, "Connecting to remote host... ");
    fflush(stderr);
    if (connect(sd, (struct sockaddr *)&sa, sizeof(sa)) < 0)
    {
        perror("connect");
        exit(1);
    }
    fprintf(stderr, "Connected.\n");

    /*
     * open crypto device
     */
    cfd = open("/dev/crypto", O_RDWR);
    if (cfd < 0)
    {
        perror("open crypto device");
        return 1;
    }

    /*
     * Get crypto sessioin for AES128
     */

    sess.cipher = CRYPTO_AES_CBC;
    sess.keylen = KEY_SIZE;
    sess.key = key;

    if (ioctl(cfd, CIOCGSESSION, &sess))
    {
        perror("ioctl CIOCGSESSION");
        return 1;
    }

    fds[0].fd = 0;  // stdin file descriptor
    fds[1].fd = sd; // socket file descriptor

    fds[0].events = POLLIN;
    fds[1].events = POLLIN;

    for (;;)
    {
        poll(fds, 2, -1); // block until an event happens to either one of fds

        if (fds[0].revents & POLLIN)
        {
            //fprintf(stdout, "read from stdin\n");
            memset(buf, '\0', sizeof(buf));
            n = read(0, buf, sizeof(buf));
            if (n < 0)
            {
                perror("read from standard input");
                exit(1);
            }
            if (n <= 0)
                break;

            if (encrypt(cfd))
            {
                perror("encrypt");
                exit(1);
            }
            if (insist_write(sd, buf, sizeof(buf)) != sizeof(buf))
            {
                perror("write to peer");
                exit(1);
            }
            bzero(buf, sizeof(buf));
        }

        else if (fds[1].revents & POLLIN)
        {
            //fprintf(stdout, "read from server\n");
            memset(buf, '\0', sizeof(buf));
            n = read(sd, buf, sizeof(buf));
            if (n < 0)
            {
                perror("read from peer");
            }
            if (n <= 0)
            {
                fprintf(stdout, "\nServer closed connection\n");
                break;
            }
            if (decrypt(cfd))
            {
                perror("decrypt");
                exit(1);
            }
            if (insist_write(1, buf, sizeof(buf)) != sizeof(buf))
            {
                perror("write to standard output");
                exit(1);
            }
            bzero(buf, sizeof(buf));
        }
    }

    /* close crypto session */
    if (close(cfd) < 0)
        perror("close crypto fd (cfd)");
    /*
     * Let the remote know we're not going to write anything else.
     * Try removing the shutdown() call and see what happens.
     */
    if (shutdown(sd, SHUT_WR) < 0)
    {
        perror("shutdown");
        exit(1);
    }

    fprintf(stderr, "\nDone.\n");
    return 0;
}
