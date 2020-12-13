AC_INIT([z], [1.0.0], [zsql@me.ash.dev])
AC_CONFIG_MACRO_DIRS([m4])
AC_CONFIG_SRCDIR([src/zsql.c])

AM_INIT_AUTOMAKE([foreign subdir-objects])

AC_PROG_CC
AC_PROG_CC_C99

AX_CHECK_COMPILE_FLAG([-flto], [
  AX_CHECK_LINK_FLAG([-flto], [
    AX_APPEND_FLAG([-flto])
  ])
])

AC_CONFIG_FILES([Makefile docs/z.1])
AC_OUTPUT
