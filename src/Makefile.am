bin_PROGRAMS = z
z_CFLAGS = -I${top_srcdir}/deps/utf8proc -DSQLITE_DQS=0 -DSQLITE_THREADSAFE=0 -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_LIKE_DOESNT_MATCH_GLOBS -DSQLITE_MAX_EXPR_DEPTH=0 -DSQLITE_OMIT_DECLTYPE -DSQLITE_OMIT_DEPRECATED -DSQLITE_OMIT_PROGRESS_CALLBACK -DSQLITE_OMIT_SHARED_CACHE -DSQLITE_OMIT_AUTOINIT
z_LDADD = ${top_builddir}/deps/libutf8proc.a
z_SOURCES = args.c args.h error.c error.h fuzzy_search.c fuzzy_search.h migrate.c migrate.h sqlh.c sqlh.h sqlite3.c sqlite3.h wtf8.c wtf8.h zsql.c
