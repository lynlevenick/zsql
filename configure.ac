AC_INIT([z], [1.0.0], [zsql@me.ash.dev])
AC_CONFIG_SRCDIR([src/zsql.c])

AM_INIT_AUTOMAKE([foreign subdir-objects])

AC_PROG_CC
AC_PROG_CC_C99

AC_ARG_ENABLE([tls],
  [AS_HELP_STRING([--enable-tls=yes|no|auto],
  [use thread-local preallocated buffers to avoid small mallocs (default: auto)])],
  [use_tls=$enableval],
  [use_tls=auto])

AS_IF([test "x$use_tls" != 'xno'],
 [ZSQL_C_THREAD_LOCAL([],
   [AS_IF([test "x$use_tls" != 'xauto'],
     [AC_MSG_ERROR([thread-local storage is enabled but not supported])])])])

AC_CHECK_FUNCS_ONCE([flockfile funlockfile fwrite_unlocked putc_unlocked])

AC_CHECK_HEADERS_ONCE([sqlite3.h utf8proc.h])
AS_IF([test "x$ac_cv_header_sqlite3_h" != 'xyes'], [AC_MSG_ERROR([cannot find sqlite3.h])])
AS_IF([test "x$ac_cv_header_utf8proc_h" != 'xyes'], [AC_MSG_ERROR([cannot find utf8proc.h])])

AC_SEARCH_LIBS([dlopen], [dl dld], [], [AC_MSG_ERROR([dlopen not found])])
AC_SEARCH_LIBS([sqlite3_value_frombind], [sqlite3], [], [AC_MSG_ERROR([sqlite3 version 3.28 required])])
AC_SEARCH_LIBS([utf8proc_isupper], [utf8proc], [], [AC_MSG_ERROR([utf8proc version 2.6 required])])

AC_CONFIG_FILES([Makefile docs/z.1])
AC_OUTPUT
