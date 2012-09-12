%{
#include <sys/stat.h>
#include <sys/fcntl.h>

#include <err.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "buildmanager.h"

#define WANT_FILENAME 0
#define WANT_DIRNAME  1

int yyparse(void);
void yyerror(const char *, ...);
int yywrap(void);
FILE *yyin;

extern int yylineno;

void
yyerror(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	printf("Error at line %d\n", yylineno);
	va_end(ap);
	exit(EXIT_FAILURE);
}

int
yywrap(void)
{
	return 1;
}

int
word_is_fs(const char *word, int want)
{
	struct stat sb;

	if (stat(word, &sb) != 0) {
		yyerror("'%s': not a %s", word, want == WANT_DIRNAME ? "directory" : "file");
	}
	if (S_ISDIR(sb.st_mode) && want == WANT_DIRNAME)
		return (0);
	if (S_ISDIR(sb.st_mode) && want == WANT_FILENAME)
		return (0);
	yyerror("'%s': not a %s", word, want == WANT_DIRNAME ?  "directory" : "file");
	return (1);
}

void
parse_config(const char *filename)
{
	if ((yyin = fopen(filename, "r")) == NULL)
		err(EXIT_FAILURE, "%s", filename);

	yyparse();
	fclose(yyin);
}
%}

%{
int yylex(void);
%}

%token DATADIR CMDSOCKET PKGBUILDTIMEOUT PREPARINGBUILDTIMEOUT
%token NODE_HOST NODE_PORT

%union
{
	int number;
	char *string;
}
%token <number> STATE
%token <number> NUMBER
%token <string> WORD
%token <string> WORDS

%%
options: /* empty */
	| options option
	;

option: datadir | cmdsocket | pkgbuild_timeout | preparing_timeout |
      node_host | node_port;

datadir: DATADIR WORD {
	if (word_is_fs($2, WANT_DIRNAME) != 0)
		YYERROR;
	conf.datadir = $2;
};

cmdsocket: CMDSOCKET WORD {
	 conf.cmdsocket = $2;
};

pkgbuild_timeout: PKGBUILDTIMEOUT NUMBER {
	conf.pkgbuild_timeout = $2;
};

preparing_timeout: PREPARINGBUILDTIMEOUT NUMBER {
	conf.preparing_timeout = $2;
};

node_host: NODE_HOST WORD {
	 conf.nodehost = $2;
};

node_port: NODE_PORT WORD {
	 conf.nodeport = $2;
}

%%
