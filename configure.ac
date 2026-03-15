AC_PREREQ([2.71])

AC_INIT([migash], [0.0.1], [mike@lonelycactus.com], [migash],
        [https://github.com/spk121/migash])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIRS([m4])
AC_CONFIG_AUX_DIR([build-aux])

# 'foreign' skips GNU standards files (NEWS, COPYING, etc.)
AM_INIT_AUTOMAKE([foreign -Wall -Werror subdir-objects tar-pax])

# Checks for programs
AC_PROG_CC
AC_PROG_INSTALL
AM_PROG_AR
AC_CANONICAL_HOST

# Libtool for shared/static libraries
# Enables --enable-shared / --disable-shared
# The 'win32-dll' option says we're using __declspec(dllexport)
LT_INIT([shared disable-static win32-dll])

# Expose the shared/static choice to Makefile.am conditionals
AM_CONDITIONAL([ENABLE_SHARED], [test "x$enable_shared" = xyes])
AM_CONDITIONAL([ENABLE_WIN32_DLL], [test "x$enable_win32_dll" = xyes])

dnl ------------------------------------------------------------------------------
dnl - library configuration options - for migaconf.h
dnl ------------------------------------------------------------------------------

dnl Tell autoheader about API-selection symbols used in config.h
AH_TEMPLATE([MIGA_POSIX_API], [Define to 1 when building with POSIX API mode])
AH_TEMPLATE([MIGA_UCRT_API],  [Define to 1 when building with UCRT API mode])
AH_TEMPLATE([MIGA_ISO_C_API], [Define to 1 when building with ISO C API mode])

AC_ARG_ENABLE([configuration],
  [AS_HELP_STRING([--enable-configuration={posix,ucrt,iso_c}],
                  [Select API configuration])],
  [configuration="$enableval"],
  [configuration=""])

if test -z "$configuration"; then
  AC_MSG_CHECKING([configuration based on host system])
  case $host_os in
    mingw* | msys* | win*)
      configuration="ucrt"
      result="'ucrt' ($host)"
      ;;
    linux* | darwin* | *bsd* | solaris* | cygwin* | gnu*)
      configuration="posix"
      result="'posix' ($host)"
      ;;
    *)
      configuration="iso_c"
      result="'iso_c' (unrecognized host '$host')"
      ;;
  esac

  AC_MSG_RESULT([$result])
fi

case "$configuration" in
  posix)
    CONFIG_DEFINE="#define MIGA_POSIX_API 1"
    AC_DEFINE([MIGA_POSIX_API], [1], [Build with POSIX API])
    ;;
  ucrt)
    CONFIG_DEFINE="#define MIGA_UCRT_API 1"
    AC_DEFINE([MIGA_UCRT_API], [1], [Build with UCRT API])
    ;;
  iso_c)
    CONFIG_DEFINE="#define MIGA_ISO_C_API 1"
    AC_DEFINE([MIGA_ISO_C_API], [1], [Build with ISO C API])
    ;;
  *)
    AC_MSG_ERROR([Invalid value for --enable-configuration: $configuration])
    ;;
esac

AC_SUBST([CONFIG_DEFINE])

dnl ------------------------------------------------------------------------------
dnl - platform feature checks
dnl ------------------------------------------------------------------------------

CHECK_MAIN_THREE_ARGS

dnl ------------------------------------------------------------------------------
dnl - output files
dnl ------------------------------------------------------------------------------

AC_CONFIG_FILES([Makefile
                 src/Makefile
                 include/Makefile
                 include/miga/migaconf.h
                 migash.pc
                 packaging/migash.spec])
AC_OUTPUT
