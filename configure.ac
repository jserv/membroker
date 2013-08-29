#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.
AC_PREREQ([2.68])
AC_INIT([membroker], [1.0])

AM_INIT_AUTOMAKE(subdir-objects foreign)
LT_INIT

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O

# systemd
PKG_PROG_PKG_CONFIG
AC_ARG_WITH([systemdsystemunitdir],
            AS_HELP_STRING([--with-systemdsystemunitdir=DIR], [Directory for systemd service files]),
            [], [with_systemdsystemunitdir=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)])
if test "x$with_systemdsystemunitdir" != xno; then
AC_SUBST([systemdsystemunitdir], [$with_systemdsystemunitdir])
fi
AM_CONDITIONAL(HAVE_SYSTEMD, [test -n "$with_systemdsystemunitdir" -a "x$with_systemdsystemunitdir" != xno])

# Checks for libraries.
AC_CHECK_LIB(pthread, pthread_create)
# Reset LIBS. Don't want pthread on every link line
LIBS=

AC_CONFIG_FILES(Makefile membroker.pc src/membroker.service)
AC_OUTPUT
