/*
 * crypto-server.c
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
        unsigned char   in[DATA_SIZE],
                        encrypted[DATA_SIZE],
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

    memset(buf, '\0', sizeof(buf));
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
        unsigned char   in[DATA_SIZE],
                        decrypted[DATA_SIZE],
                        iv[BLOCK_SIZE];
    } data;

    memset(&cryp, 0, sizeof(cryp));

    cryp.ses = sess.ses;
    cryp.len = sizeof(data.in);
    cryp.src = buf;
    cryp.dst = data.decrypted;
    cryp.iv = inv;
    cryp.op = COP_DECRYPT;\

    if(ioctl(cfd, CIOCCRYPT, &cryp))
    {
        perror("ioctl CIOCCRYPT decrypt");
        return 1;
    }

    memset(buf, '\0', sizeof(buf));
    i=0;
    while (data.decrypted[i] != '\0')
    {
        buf[i] = data.decrypted[i];
        i++;
    }
    return 0;
}


int main() 
{
	char addrstr[INET_ADDRSTRLEN];
	int sd, newsd, cfd;
	ssize_t n;
	socklen_t len;
	struct sockaddr_in sa;
	struct pollfd fds[2];

    memset(&sess, 0, sizeof(sess));

    /* Make sure a broken connection doesn't kill us */
	signal(SIGPIPE, SIG_IGN);

	/* Create TCP/IP socket, used as main chat channel */
	if ((sd = socket(PF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("socket");
		exit(1);
	}
	fprintf(stderr, "Created TCP socket\n");

	/* Bind to a well-known port */
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(TCP_PORT);
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(sd, (struct sockaddr *)&sa, sizeof(sa)) < 0)
	{
		perror("bind");
		exit(1);
	}
	fprintf(stderr, "Bound TCP socket to port %d\n", TCP_PORT);

	/* Listen for incoming connections */
	if (listen(sd, TCP_BACKLOG) < 0)
	{
		perror("listen");
		exit(1);
	}

	/* Loop forever, accept()ing connections */
	for (;;)
	{
		fprintf(stderr, "Waiting for an incoming connection...\n");

		/* Accept an incoming connection */
		len = sizeof(struct sockaddr_in);
		if ((newsd = accept(sd, (struct sockaddr *)&sa, &len)) < 0)
		{
			perror("accept");
			exit(1);
		}
		if (!inet_ntop(AF_INET, &sa.sin_addr, addrstr, sizeof(addrstr)))
		{
			perror("could not format IP address");
			exit(1);
		}
		fprintf(stderr, "Incoming connection from %s:%d\n",
				addrstr, ntohs(sa.sin_port));

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

		fds[0].fd = 0;
		fds[1].fd = newsd;

		fds[0].events = POLLIN;
		fds[1].events = POLLIN;

		/* We break out of the loop when the remote peer goes away */
		for (;;)
		{
            memset(buf, '\0', sizeof(buf));

			poll(fds, 2, -1);
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
				if (insist_write(newsd, buf, sizeof(buf)) != sizeof(buf))
				{
					perror("write to peer");
					exit(1);
				}
                bzero(buf, sizeof(buf));
			}
			else if (fds[1].revents & POLLIN)
			{
                //fprintf(stdout, "read from peer\n");
                memset(buf, '\0', sizeof(buf));
				n = read(newsd, buf, sizeof(buf));
				if (n <= 0)
				{
					if (n < 0)
						perror("read from remote peer failed");
					else
						fprintf(stderr, "Peer went away\n");
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
					break;
				}
                bzero(buf, sizeof(buf));
			}
		}
		/* Make sure we don't leak open files */
		if (close(newsd) < 0)
			perror("close");
        
        /* close crypto session */
        if (close(cfd) < 0)
            perror("close crypto fd (cfd)");
	}
	/* This will never happen */
	return 1;
}

