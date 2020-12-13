ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS = z
z_CFLAGS = \
	-I${top_srcdir}/utf8proc \
	-DSQLITE_DQS=0 -DSQLITE_THREADSAFE=0 -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_LIKE_DOESNT_MATCH_GLOBS -DSQLITE_MAX_EXPR_DEPTH=0 -DSQLITE_OMIT_DECLTYPE -DSQLITE_OMIT_DEPRECATED -DSQLITE_OMIT_PROGRESS_CALLBACK -DSQLITE_OMIT_SHARED_CACHE -DSQLITE_OMIT_AUTOINIT \
	-DSQLITE_OMIT_LOAD_EXTENSION
z_SOURCES = \
	utf8proc/utf8proc.c utf8proc/utf8proc.h \
	src/args.c src/args.h src/error.c src/error.h src/fuzzy_search.c src/fuzzy_search.h src/migrate.c src/migrate.h src/sqlh.c src/sqlh.h src/sqlite3.c src/sqlite3.h src/zsql.c

man_MANS = docs/z.1

EXTRA_DIST = \
	m4/ax_append_flag.m4 m4/ax_check_compile_flag.m4 m4/ax_check_link_flag.m4 \
	utf8proc/utf8proc_data.c
