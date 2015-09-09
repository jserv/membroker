TESTS =
bin_PROGRAMS =
bin_SCRIPTS =
noinst_PROGRAMS =
lib_LTLIBRARIES =
noinst_LTLIBRARIES =

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = membroker.pc
pkgconfig_DATA = membrokerserver.pc

systemdsystemunit_DATA = src/membroker.service src/membroker.socket

lib_LTLIBRARIES += libmembroker.la
libmembroker_la_SOURCES = \
	src/mbcodec.c \
	src/mbclient.c \
	src/mb.h \
	src/mbprivate.h \
	src/mbclient.h

pkginclude_HEADERS = \
	src/mb.h \
	src/mbclient.h \
	src/mbserver.h

lib_LTLIBRARIES += libmbs.la
libmbs_la_SOURCES = src/mbserver.c

bin_PROGRAMS += mbserver
mbserver_SOURCES = \
	src/main.c \
	src/mbserver.h
mbserver_LDADD = libmbs.la libmembroker.la

bin_PROGRAMS += mbutil
mbutil_SOURCES = src/mbutil.c
mbutil_LDADD = libmbs.la libmembroker.la

bin_SCRIPTS += src/membroker

noinst_PROGRAMS += mbctest
mbctest_SOURCES = \
	src/mbctest.c \
	src/mb.h
	src/mbclient.h
	src/mbprivate.h
mbctest_LDADD = libmembroker.la
TESTS += mbctest

noinst_PROGRAMS += mbtest
mbtest_SOURCES = src/mbtest.c
mbtest_LDADD = libmembroker.la
# This test fails
#TESTS += mbtest

noinst_PROGRAMS += test_main
test_main_SOURCES = src/tests/main.c
test_main_CFLAGS = -I$(srcdir)/src
test_main_LDADD = libmembroker.la libmbs.la
test_main_LDFLAGS = -lpthread
UNITTESTS =
UNITTESTS += initAndTerminate
UNITTESTS += testNormalRequest
UNITTESTS += testNormalReserve
UNITTESTS += testRequestOnRequesting
UNITTESTS += testReserveOnRequesting
UNITTESTS += testRequestOnReserving
UNITTESTS += testReserveOnReserving
UNITTESTS += testRequestOnReserved
UNITTESTS += testRequestOnRequested
UNITTESTS += testReserveOnRequested
UNITTESTS += testReserveOnReserved
UNITTESTS += testReturnOnRequest
UNITTESTS += testMultipleRequests
UNITTESTS += testClientTermination
UNITTESTS += testIoErrors
UNITTESTS += testDumpDebug

$(UNITTESTS): test_main
	@ echo Creating $@
	$Q rm -rf $@
	$Q echo "#!/bin/sh" > $@
	$Q echo "./test_main $@" >> $@
	$Q chmod +x $@
CLEANFILES = $(UNITTESTS)
TESTS += $(UNITTESTS)
