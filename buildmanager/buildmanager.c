#include <sys/param.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <netdb.h>
#include <paths.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#define _WITH_DPRINTF
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <yaml.h>
#include <uthash.h>

extern char **environ;

struct bm_conf {
	char cmdsocket[MAXPATHLEN];
	char datadir[MAXPATHLEN];
	int cmdfd;
	int pkgbuild_timeout;
	int preparing_timeout;
	char *nodehost;
	char *nodeport;
	int nodefd;
} conf;

struct cmd {
	char *name;
	char *script;
	bool background;
	UT_hash_handle hh;
};

struct cmd *local_cmds = NULL;
struct cmd *node_cmds = NULL;
typedef enum {
	LOCAL,
	NODE,
} cmdtype;

static void
close_socket(int dummy) {
	close(conf.nodefd);
	close(conf.cmdfd);
	if (conf.cmdsocket != NULL)
		unlink(conf.cmdsocket);

	exit(EXIT_SUCCESS);
}

static void
parse_command(yaml_document_t *doc, yaml_node_t *node, cmdtype t)
{
	yaml_node_item_t *item;
	yaml_node_t *val;
	struct cmd *c;
	item = node->data.sequence.items.start;
	while (item < node->data.sequence.items.top) {
		yaml_node_pair_t *pair;
		val = yaml_document_get_node(doc, *item);
		if (val->type != YAML_MAPPING_NODE)
			err(EXIT_FAILURE, "Error parsing commands");
		pair = val->data.mapping.pairs.start;
		c = calloc(1, sizeof(struct cmd));
		c->background = false;
		while (pair < val->data.mapping.pairs.top) {
			yaml_node_t *key = yaml_document_get_node(doc, pair->key);
			yaml_node_t *value = yaml_document_get_node(doc, pair->value);
			if (strcasecmp((char *)key->data.scalar.value, "name") == 0) {
				c->name = strdup((char *)value->data.scalar.value);
			} else if (strcasecmp((char *)key->data.scalar.value, "script") == 0) {
				c->script = strdup((char *)value->data.scalar.value);
			} else if (strcasecmp((char *)key->data.scalar.value, "background") == 0) {
				c->background = true;
			}
			++pair;
		}
		if (c->name != NULL && c->script != NULL) {
			if (t == LOCAL)
				HASH_ADD_KEYPTR(hh, local_cmds, c->name, strlen(c->name), c);
			else
				HASH_ADD_KEYPTR(hh, node_cmds, c->name, strlen(c->name), c);
		} else {
			free(c);
		}
		++item;
	}
}

static void
parse_env(yaml_document_t *doc, yaml_node_t *node)
{
	yaml_node_pair_t *pair;

	pair = node->data.mapping.pairs.start;
	while (pair < node->data.mapping.pairs.top) {
		yaml_node_t *key = yaml_document_get_node(doc, pair->key);
		yaml_node_t *val = yaml_document_get_node(doc, pair->value);

		if (val->type != YAML_SCALAR_NODE)
			errx(EXIT_FAILURE, "wrong environement values");
		setenv((char *)key->data.scalar.value, (char *)val->data.scalar.value, 0);
		++pair;
	}
}

static void
parse_configuration(yaml_document_t *doc, yaml_node_t *node)
{
	yaml_node_pair_t *pair;

	pair = node->data.mapping.pairs.start;
	while (pair < node->data.mapping.pairs.top) {
		yaml_node_t *key = yaml_document_get_node(doc, pair->key);
		yaml_node_t *val = yaml_document_get_node(doc, pair->value);

		if (key->data.scalar.length <= 0) {
			/*
			 * ignoring silently empty keys (empty lines or user
			 * mistakes
			 */
			++pair;
			continue;
		}
		if (val->type == YAML_NO_NODE ||
		    (val->type == YAML_SCALAR_NODE &&
		     val->data.scalar.length <= 0)) {
			/*
			 * silently skip on purpose to allow user to leave
			 * empty lines for examples without complaining
			 */
			++pair;
			continue;
		}
		if (strcasecmp((char *)key->data.scalar.value, "cmd_socket") == 0) {
			if (val->type != YAML_SCALAR_NODE)
				err(EXIT_FAILURE, "expecting a string for cmd_socket key");

			strlcpy(conf.cmdsocket, (char *)val->data.scalar.value, sizeof(conf.cmdsocket));
		} else if (strcasecmp((char *)key->data.scalar.value, "data_dir") == 0) {
			if (val->type != YAML_SCALAR_NODE)
				err(EXIT_FAILURE, "expecting a string for data_dir key");

			strlcpy(conf.datadir, (char *)val->data.scalar.value, sizeof(conf.datadir));
		} else if (strcasecmp((char *)key->data.scalar.value, "pkgbuild_timeout") == 0) {
			if (val->type != YAML_SCALAR_NODE)
				errx(EXIT_FAILURE, "expecting a string for pkgbuild_timeout key");
		} else if (strcasecmp((char *)key->data.scalar.value, "preparing_timeout") == 0) {
			if (val->type != YAML_SCALAR_NODE)
				errx(EXIT_FAILURE, "expecting a string for preparing_timeout key");
		} else if (strcasecmp((char *)key->data.scalar.value, "node_host") == 0) {
			if (val->type != YAML_SCALAR_NODE)
				errx(EXIT_FAILURE, "expecting a string for node_host key");
		} else if (strcasecmp((char *)key->data.scalar.value, "node_port") == 0) {
			if (val->type != YAML_SCALAR_NODE)
				errx(EXIT_FAILURE, "expecting a string for node_port key");
		} else if (strcasecmp((char *)key->data.scalar.value, "local_commands") == 0) {
			if (val->type != YAML_SEQUENCE_NODE)
				errx(EXIT_FAILURE, "expecting a sequence for local commands key");

			parse_command(doc, val, LOCAL);
		} else if (strcasecmp((char *)key->data.scalar.value, "nodes_commands") == 0) {
			if (val->type != YAML_SEQUENCE_NODE)
				errx(EXIT_FAILURE, "expecting a sequence for nodes commands key");

			parse_command(doc, val, NODE);
		} else if (strcasecmp((char *)key->data.scalar.value, "env") == 0) {
			if (val->type != YAML_MAPPING_NODE)
				errx(EXIT_FAILURE, "expecting a mapping for env");

			parse_env(doc, val);
		}
		++pair;
	}
}

static void
parse_conf(void)
{
	FILE *fp;
	yaml_parser_t parser;
	yaml_document_t doc;
	yaml_node_t *node;

	if ((fp = fopen("/usr/local/etc/bm.yml", "r")) == NULL)
		err(EXIT_FAILURE, "fopen()");

	yaml_parser_initialize(&parser);
	yaml_parser_set_input_file(&parser, fp);
	yaml_parser_load(&parser, &doc);

	node = yaml_document_get_root_node(&doc);
	if (node == NULL || node->type != YAML_MAPPING_NODE)
		err(EXIT_FAILURE, "Invalid configuration format, ignoring the configuration file");

	parse_configuration(&doc, node);

	yaml_document_delete(&doc);
	yaml_parser_delete(&parser);
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
	struct cmd *c;
	char *cmd;
	posix_spawn_file_actions_t action;
	int error, pstat;
	pid_t pid;
	const char *argv[4];

	cmd = buf;
	while (!isspace(*cmd))
		cmd++;
	cmd[0] = '\0';
	cmd++;
	HASH_FIND_STR(local_cmds, buf, c);
	if (c == NULL) {
		dprintf(fd, "KO");
		close(fd);
		return;
	}
	argv[0] = "sh";
	argv[1] = c->script;
	argv[2] = cmd;
	argv[3] = NULL;

	posix_spawn_file_actions_init(&action);
	posix_spawn_file_actions_adddup2(&action, fd, STDOUT_FILENO);
	posix_spawn_file_actions_adddup2(&action, fd, STDERR_FILENO);
	posix_spawn_file_actions_addclose(&action, fd);

	if ((error == posix_spawn(&pid, _PATH_BSHELL, &action, NULL,
	    __DECONST(char **, argv), environ)) != 0) {
	}

	if (c->background)
		return;

	while (waitpid(pid, &pstat, 0) == -1) {
		if (errno != EINTR)
			return;
	}
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

	parse_conf();
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

					close(fd);
					EV_SET(&ke, evlist[i].ident, EVFILT_READ, EV_DELETE, 0, 0, 0);
					kevent(kq, &ke, 1, NULL, 0, NULL);
					nbevq -= 5;
					free(buf);
				}
			}
		}
	}

	return (EXIT_SUCCESS);
}
