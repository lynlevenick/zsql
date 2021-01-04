bin_PROGRAMS = z
z_CFLAGS = $(SQLITE3_CFLAGS) $(UTF8PROC_CFLAGS)
z_LDADD = $(SQLITE3_LIBS) $(UTF8PROC_LIBS)
z_SOURCES = \
	src/args.c src/args.h src/error.c src/error.h src/fuzzy_search.c \
	src/fuzzy_search.h src/migrate.c src/migrate.h src/sqlh.c src/sqlh.h \
	src/zsql.c

man_MANS = docs/z.1
