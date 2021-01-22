AC_DEFUN([ZSQL_C_THREAD_LOCAL],
 [AC_MSG_CHECKING([for thread-local storage support])
  AC_CACHE_VAL([zsql_cv_thread_local],
   [zsql_cv_thread_local=none
    for zsql_thread_local_keyword in thread_local _Thread_local __thread '__declspec(thread)'; do
      AC_COMPILE_IFELSE(
        [AC_LANG_PROGRAM([], [static $zsql_thread_local_keyword int var;])],
        [zsql_cv_thread_local=$zsql_thread_local_keyword; break])
    done])
  AC_MSG_RESULT([$zsql_cv_thread_local])
  AS_IF([test "x$zsql_cv_thread_local" != 'xnone'],
   [AC_DEFINE_UNQUOTED([HAVE_THREAD_LOCAL], [1])
    AS_IF([test "x$zsql_cv_thread_local" != 'xthread_local'],
     [AC_DEFINE_UNQUOTED([thread_local], [$zsql_cv_thread_local])])
    m4_ifnblank([$1], [$1], [[:]])],
   [AC_DEFINE_UNQUOTED([thread_local], [])
    m4_ifnblank([$2], [$2], [[:]])])])
