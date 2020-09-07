AC_INIT([z], [1.0.0])
AC_CONFIG_MACRO_DIRS([m4])
AC_CONFIG_SRCDIR([src/zsql.c])

AM_INIT_AUTOMAKE([foreign subdir-objects])

AC_PROG_CC
AC_PROG_CC_C99

dnl AX_CHECK_COMPILE_FLAG([-flto], [
dnl   AX_CHECK_LINK_FLAG([-flto], [
dnl     AX_APPEND_FLAG([-flto])
dnl   ])
dnl ])

AC_CONFIG_FILES([Makefile docs/z.1])
AC_OUTPUT
