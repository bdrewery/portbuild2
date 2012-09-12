#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <err.h>
#include <netdb.h>
#include <signal.h>
#define _WITH_DPRINTF
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "buildmanager.h"

struct bm_conf conf;

static void
close_socket(int dummy) {
	close(conf.nodefd);
	close(conf.cmdfd);
	if (conf.cmdsocket != NULL)
		unlink(conf.cmdsocket);

	exit(EXIT_SUCCESS);
}


static int
bind_cmd_socket(void)
{
	struct sockaddr_un un;

	memset(&un, 0, sizeof(struct sockaddr_un));
	if ((conf.cmdfd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		err(EXIT_FAILURE, "socket()");

	un.sun_family = AF_UNIX;
	strlcpy(un.sun_path, conf.cmdsocket, sizeof(un.sun_path));

	if (setsockopt(conf.cmdfd, SOL_SOCKET, SO_REUSEADDR, (int[]){1}, sizeof(int)) < 0)
		err(EXIT_FAILURE, "setsockopt()");

	if (bind(conf.cmdfd, (struct sockaddr *) &un, sizeof(struct sockaddr_un)) == -1)
		err(EXIT_FAILURE, "bind()");

	if (listen(conf.cmdfd, 1024) < 0)
		err(EXIT_FAILURE, "listen()");


	return (0);
}

static int
bind_node_socket(void)
{
	struct addrinfo *ai;
	struct addrinfo hints;
	int er;

	memset(&hints, 0, sizeof(hints));

	hints.ai_family = PF_UNSPEC;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_socktype = SOCK_STREAM;

	if ((er = getaddrinfo(conf.nodehost, conf.nodeport, &hints, &ai)) != 0)
		err(EXIT_FAILURE, "getaddrinfo(): %s", gai_strerror(er));

	if ((conf.nodefd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) == -1)
		err(EXIT_FAILURE, "socket()");

	if (setsockopt(conf.nodefd, SOL_SOCKET, SO_REUSEADDR, (int[]){1}, sizeof(int)) < 0)
		err(EXIT_FAILURE, "setsockopt()");

	if (bind(conf.nodefd, ai->ai_addr, ai->ai_addrlen) == -1)
		err(EXIT_FAILURE, "bind()");

	if (listen(conf.nodefd, 1024) < 0)
		err(EXIT_FAILURE, "listen()");

	return (0);
}

static void
parse_cmd(char *buf, int fd)
{
	printf("Got cmd: %s", buf);
	dprintf(fd, "OK\n");
	close(fd);
}

static void
parse_node(char *buf, int fd)
{
	printf("Got node: %s", buf);
	dprintf(fd, "OK\n");
	close(fd);
}

int
main(int argc, char **argv)
{
	int kq;
	struct kevent ke;
	struct kevent *evlist = NULL;
	int nbevq = 0;
	int max_list_queues = 20;
	int nev, i;
	int fd;
	char *buf;

	memset(&conf, 0, sizeof(struct bm_conf));

	parse_config("/usr/local/etc/bm.conf");
	if (conf.cmdsocket == NULL)
		errx(EXIT_FAILURE, "Command socket not defined");
	if (conf.datadir ==  NULL)
		errx(EXIT_FAILURE, "Datadir not defined");
	if (conf.nodehost == NULL)
		conf.nodehost = "127.0.0.1";
	if (conf.nodeport == NULL)
		conf.nodeport = "4444";

	bind_cmd_socket();
	bind_node_socket();

	signal(SIGINT, close_socket);
	signal(SIGKILL, close_socket);
	signal(SIGQUIT, close_socket);
	signal(SIGTERM, close_socket);

	if ((kq = kqueue()) == -1)
		err(EXIT_FAILURE, "kqueue");

	EV_SET(&ke, conf.cmdfd, EVFILT_READ, EV_ADD, 0, 0, NULL);
	kevent(kq, &ke, 1, NULL, 0, NULL);
	nbevq++;
	EV_SET(&ke, conf.nodefd, EVFILT_READ, EV_ADD, 0, 0, NULL);
	kevent(kq, &ke, 1, NULL, 0, NULL);
	nbevq++;

	for (;;) {
		if (evlist == NULL) {
			if ((evlist = malloc(max_list_queues * sizeof(struct kevent))) == NULL)
				errx(1, "Unable to allocate memory");
		}
		else if (nbevq > max_list_queues) {
			max_list_queues += 20;
			free(evlist);
			if ((evlist = malloc(max_list_queues * sizeof(struct kevent))) == NULL)
				errx(1, "Unable to allocate memory");
		}
		nev = kevent(kq, NULL, 0, evlist, max_list_queues, NULL);
		for (i = 0; i < nev; i++) {
			if (evlist[i].ident == conf.cmdfd ||
			    evlist[i].ident == conf.nodefd) {
				fd = accept(evlist[i].ident, NULL, 0);
				EV_SET(&ke, fd, EVFILT_READ, EV_ADD, 0, 0,
				    evlist[i].ident == conf.cmdfd ? &conf.cmdfd : &conf.nodefd);
				kevent(kq, &ke, 1, NULL, 0, NULL);
				nbevq += 5;
			} else {
				if (evlist[i].flags & (EV_ERROR | EV_EOF)) {
					printf("closed\n");
					EV_SET(&ke, evlist[i].ident, EVFILT_READ, EV_DELETE, 0, 0, 0);
					kevent(kq, &ke, 1, NULL, 0, NULL);
					nbevq -= 5;
					close(evlist[i].ident);
					continue;
				}
				if (evlist[i].filter == EVFILT_READ) {
					buf = malloc(evlist[i].data + 1);
					read(evlist[i].ident, buf, evlist[i].data);
					buf[evlist[i].data] = '\0';
					if (*(int *)evlist[i].udata == conf.cmdfd)
						parse_cmd(buf, evlist[i].ident);
					else
						parse_node(buf, evlist[i].ident);
					free(buf);
				}
			}
		}
	}

	return (EXIT_SUCCESS);
}
