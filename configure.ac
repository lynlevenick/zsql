AC_INIT([z], [1.0.0])
AC_CONFIG_MACRO_DIRS([m4])
AC_CONFIG_SRCDIR([src/zsql.c])

AM_INIT_AUTOMAKE([foreign subdir-objects])

AC_PROG_CC
AC_PROG_CC_C99
AC_PROG_RANLIB

dnl AX_CHECK_COMPILE_FLAG([-flto], [
dnl   AX_CHECK_LINK_FLAG([-flto], [
dnl     AX_APPEND_FLAG([-flto])
dnl   ])
dnl ])

AC_CONFIG_FILES([Makefile deps/Makefile docs/Makefile docs/z.1 m4/Makefile src/Makefile])
AC_OUTPUT
