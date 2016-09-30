plugin_LTLIBRARIES = libgstmprtp.la

# sources used to compile this plug-in
libgstmprtp_la_SOURCES = fbrafbproc.c          \
                         fbrafbprod.c          \
                         fbrasubctrler.c       \
                         fecdec.c              \
                         fecenc.c              \
                         gstmprtcpbuffer.c     \
                         gstmprtpplayouter.c   \
                         gstmprtpreceiver.c    \
                         gstmprtpscheduler.c   \
                         gstmprtpsender.c      \
                         lib_bintree.c         \
                         lib_swplugins.c       \
                         mprtpdefs.c           \
                         mprtplogger.c         \
                         observer.c            \
                         packetforwarder.c     \
                         rcvctrler.c           \
                         rcvsubflows.c         \
                         rcvtracker.c          \
                         reportproc.c          \
                         reportprod.c          \
                         ricalcer.c            \
                         rtpfecbuffer.c        \
                         rtppackets.c          \
                         signalreport.c        \
                         slidingwindow.c       \
                         sndctrler.c           \
                         sndsubflows.c         \
                         sndtracker.c          \
                         streamjoiner.c        \
                         streamsplitter.c          
                    
                    
noinst_HEADERS = mprtpspath.h           \
                 sndctrler.h            \
                 rcvctrler.c            \
                 streamsplitter.h       \
                 streamjoiner.h         \
                 mprtplogger.h          \
                 ricalcer.h             \
                 reportproc.h           \
                 reportprod.h           \
                 sndratedistor.h        \
                 fbrasubctrler.h        \
	             fbrafbproc.h           \
	             fbrafbprod.h           \
	             screamsubctrler.h      \
                 fecdec.h               \
                 fecenc.h               \
                 rtpfecbuffer.h         \
                 signalreport.h         \
                 slidingwindow.h        \
                 subratectrler.h
                          

# compiler and linker flags used to compile this plugin, set in configure.ac
# libgstmprtp_la_CFLAGS = $(GST_CFLAGS) -I/usr/local/include/gstreamer-1.0 -I/home/balazs/gst/master/gst-plugins-base/gst-libs
libgstmprtp_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
                        $(GST_CFLAGS)	$(GST_NET_CFLAGS) $(WARNING_CFLAGS) \
                        $(ERROR_CFLAGS)
libgstmprtp_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
            $(GST_NET_LIBS) -lgstrtp-@GST_API_VERSION@ \
	        $(GST_BASE_LIBS) $(GST_LIBS_LIBS) 
libgstmprtp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmprtp_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)
