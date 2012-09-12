#ifndef _BUILDMANAGER_H
#define _BUILDMANAGER_H

struct bm_conf {
	char *datadir;
	char *cmdsocket;
	int cmdfd;
	int pkgbuild_timeout;
	int preparing_timeout;
	char *nodehost;
	char *nodeport;
	int nodefd;
};

extern struct bm_conf conf;

void parse_config(const char *);

#endif
