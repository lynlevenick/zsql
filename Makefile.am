ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS = z
z_SOURCES = \
	src/env.c src/env.h src/error.c src/error.h src/fuzzy_search.c \
	src/fuzzy_search.h src/migrate.c src/migrate.h src/sqlh.c src/sqlh.h \
	src/zsql.c

man_MANS = docs/z.1

EXTRA_DIST = m4/zsql_c_thread_local.m4
