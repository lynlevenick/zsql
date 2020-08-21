AC_INIT([z], [1.0.0])
AC_CONFIG_MACRO_DIRS([m4])
AC_CONFIG_SRCDIR([src/zsql.c])

AM_INIT_AUTOMAKE([foreign])

AC_PROG_CC
AC_PROG_CC_C99

AX_CHECK_COMPILE_FLAG([-flto], [
  AX_CHECK_LINK_FLAG([-flto], [
    AX_APPEND_FLAG([-flto])
  ])
])

AC_CONFIG_FILES([Makefile docs/Makefile docs/z.1 m4/Makefile src/Makefile])
AC_OUTPUT
