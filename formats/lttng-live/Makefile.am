AM_CFLAGS = $(PACKAGE_CFLAGS) -I$(top_srcdir)/include -I$(top_builddir)/include

lib_LTLIBRARIES = libbabeltrace-lttng-live.la

libbabeltrace_lttng_live_la_SOURCES = \
	lttng-live.c lttng-live-functions.c

libbabeltrace_lttng_live_la_LIBADD = \
	$(top_builddir)/lib/libbabeltrace.la
