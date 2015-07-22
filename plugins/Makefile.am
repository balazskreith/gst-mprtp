plugin_LTLIBRARIES = libgstmprtp.la

# sources used to compile this plug-in
libgstmprtp_la_SOURCES = gstmprtpplugin.c    \
                         gstmprtcpbuffer.c   \
                         gstmprtpsender.c    \
                         mprtpssubflow.c     \
                         gstmprtpreceiver.c  \
                         mprtprsubflow.c     \
                         schtree.c           \
                         gstmprtcpreceiver.c \
                         gstmprtcpsender.c   \
                         gsttry.c 
                         
noinst_HEADERS = mprtpssubflow.h schtree.h mprtprsubflow.h

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstmprtp_la_CFLAGS = $(GST_CFLAGS) -I/usr/local/include/gstreamer-1.0 -I/home/balazs/gst/master/gst-plugins-base/gst-libs
libgstmprtp_la_LIBADD = $(GST_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstrtp-1.0 
libgstmprtp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmprtp_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

