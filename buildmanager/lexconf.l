%option nounput
%option noinput
%option yylineno

%{
#include <err.h>
#include <sys/param.h>
#include <stdlib.h>
#include <string.h>
#include "parseconf.h"

static char *words(char *);
static int parse_number(const char *);

static char *
words(char *s)
{
	char *p;

	p = strrchr(s, '"');
	*p = '\0';
	p = s;
	p++;

	return (p);
}

static int
parse_number(const char *buf)
{
	int i;
	const char *errstr;

	i = strtonum(buf, 0, INT_MAX, &errstr);
	if (errstr)
		errx(1, "Unknown number %s: %s", buf, errstr);

	return (i);
}
%}

%%
datadir			return DATADIR;
cmdsocket		return CMDSOCKET;
pkgbuild_timeout	return PKGBUILDTIMEOUT;
preparing_timeout	return PREPARINGBUILDTIMEOUT;
node_host_interface	return NODE_HOST;
node_port		return NODE_PORT;

on|off			yylval.number =! strcmp(yytext, "on"); return STATE;
\"[^\"]*\"		yylval.string = strdup(words(yytext)); return WORDS;
[[:graph:]]+		yylval.string = strdup(yytext); return WORD;
[0-9]+			yylval.number = parse_number(yytext); return NUMBER;
[ \t]+			;
#.*$			yylineno--;
\n			;
%%
