AC_INIT([z], [1.0.0], [zsql@me.ash.dev])
AC_CONFIG_SRCDIR([src/zsql.c])

AM_INIT_AUTOMAKE([foreign subdir-objects])

AC_PROG_CC
AC_PROG_CC_C99

AC_SEARCH_LIBS([dlopen], [dl dld], [], [AC_MSG_ERROR([dlopen not found])])
AC_SEARCH_LIBS([sqlite3_initialize], [sqlite3], [], [AC_MSG_ERROR([sqlite3_initialize not found])])
AC_SEARCH_LIBS([utf8proc_version], [utf8proc], [], [AC_MSG_ERROR([utf8proc_version not found])])

AC_CONFIG_FILES([Makefile docs/z.1])
AC_OUTPUT
