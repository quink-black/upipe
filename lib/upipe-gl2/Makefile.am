lib_LTLIBRARIES = libupipe_gl2.la

libupipe_gl2_la_SOURCES = \
    upipe_glfw_sink.c \
    uprobe_gl2_sink.c

libupipe_gl2_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_gl2_la_CFLAGS = $(AM_CFLAGS) $(GLFW_CFLAGS) $(EPOXY_CFLAGS)
libupipe_gl2_la_LIBADD = $(AM_LIBS) $(GLFW_LIBS) $(EPOXY_LIBS)

libupipe_gl2_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_gl2.pc
