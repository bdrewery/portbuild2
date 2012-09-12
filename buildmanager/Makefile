PROG=	buildmanager

CFLAGS+=	-std=c99 \
		-I${.CURDIR}/../external/libyaml/include \
		-I${.CURDIR}/../external/uthash

LDADD+=		-L${.OBJDIR}/../external/libyaml \
		-lyaml

NO_MAN=	yes

.include <bsd.prog.mk>
