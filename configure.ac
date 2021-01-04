AC_INIT([z], [1.0.0], [zsql@me.ash.dev])
AC_CONFIG_SRCDIR([src/zsql.c])

AM_INIT_AUTOMAKE([foreign subdir-objects])

AC_PROG_CC
AC_PROG_CC_C99

AC_SEARCH_LIBS([dlopen], [dl dld], [], [AC_MSG_ERROR([dlopen not found])])

AC_PATH_TOOL([PKG_CONFIG], [pkg-config])
if test -n "$PKG_CONFIG"; then
        AC_MSG_CHECKING([for sqlite3])
	if "$PKG_CONFIG" --atleast-version=3.28 sqlite3; then
                AC_MSG_RESULT([yes])
                AC_SUBST([SQLITE3_CFLAGS])
                AC_SUBST([SQLITE3_LIBS])
                SQLITE3_CFLAGS="$(pkg-config --cflags sqlite3)"
                SQLITE3_LIBS="$(pkg-config --libs sqlite3)"
        else
                AC_MSG_RESULT([no])
                AC_MSG_ERROR([sqlite3 version 3.28 required])
        fi

        AC_MSG_CHECKING([for libutf8proc])
        if "$PKG_CONFIG" --atleast-version=2.4 libutf8proc; then
                AC_MSG_RESULT([yes])
                AC_SUBST([UTF8PROC_CFLAGS])
                AC_SUBST([UTF8PROC_LIBS])
                UTF8PROC_CFLAGS="$(pkg-config --cflags libutf8proc)"
                UTF8PROC_LIBS="$(pkg-config --libs libutf8proc)"
        else
                AC_MSG_RESULT([no])
                AC_MSG_ERROR([libutf8proc version 2.6 required])
        fi
else
	AC_MSG_WARN([pkg-config not found, using fallback])

        dnl sqlite3_value_frombind is the newest sqlite function we require
	AC_SEARCH_LIBS([sqlite3_value_frombind], [sqlite3], [], [AC_MSG_ERROR([sqlite3 version 3.28 required])])

        dnl utf8proc_isupper is only available in newer versions of utf8proc
	AC_SEARCH_LIBS([utf8proc_isupper], [utf8proc], [], [AC_MSG_ERROR([utf8proc version 2.6 required])])
fi

AC_CONFIG_FILES([Makefile docs/z.1])
AC_OUTPUT
