#serial 1

dnl CHECK_MAIN_THREE_ARGS
dnl Test whether main() can be declared with three arguments:
dnl   int main(int argc, char *argv[], char *envp[])
dnl Sets HAVE_MAIN_THREE_ARGS if supported.

AC_DEFUN([CHECK_MAIN_THREE_ARGS],
[
  AC_MSG_CHECKING([whether main() accepts a third envp argument])

  AC_CACHE_VAL([ac_cv_main_three_args],
    [AC_COMPILE_IFELSE(
       [AC_LANG_SOURCE([[
         int main(int argc, char *argv[], char *envp[]) {
           (void)argc; (void)argv; (void)envp;
           return 0;
         }
       ]])],
       [ac_cv_main_three_args=yes],
       [ac_cv_main_three_args=no]
     )]
  )

  AC_MSG_RESULT([$ac_cv_main_three_args])

  if test "$ac_cv_main_three_args" = yes; then
    AC_DEFINE([HAVE_MAIN_THREE_ARGS], [1],
      [Define to 1 if main() can be declared with a third `char *envp[]` argument.])
  fi
])
