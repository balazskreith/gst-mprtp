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
                         gstmprtpplugin.c      \
                         gstmprtpscheduler.c   \
                         gstmprtpsender.c      \
                         gstbufferpacer.c      \
                         gsttransceiver.c      \
                         jitterbuffer.c        \
                         lib_bintree.c         \
                         lib_datapuffer.c      \
                         lib_swplugins.c       \
                         mediator.c            \
                         messenger.c           \
                         mprtpdefs.c           \
                         mprtplogger.c         \
                         notifier.c            \
                         rcvctrler.c           \
                         rcvpackets.c          \
                         rcvsubflows.c         \
                         rcvtracker.c          \
                         recycle.c             \
                         reportproc.c          \
                         reportprod.c          \
                         ricalcer.c            \
                         rtpfecbuffer.c        \
                         signalreport.c        \
                         slidingwindow.c       \
                         sndctrler.c           \
                         sndsubflows.c         \
                         sndtracker.c          \
                         sndpackets.c          \
                         streamjoiner.c        \
                         streamsplitter.c      
                    
                    
noinst_HEADERS = fbrafbproc.h          \
                 fbrafbprod.h          \
                 fbrasubctrler.h       \
                 fecdec.h              \
                 fecenc.h              \
                 gstmprtcpbuffer.h     \
                 jitterbuffer.h        \
                 lib_bintree.h         \
                 lib_datapuffer.h      \
                 lib_swplugins.h       \
                 mediator.h            \
                 messenger.h           \
                 mprtpdefs.h           \
                 mprtplogger.h         \
                 notifier.h            \
                 rcvctrler.h           \
                 rcvsubflows.h         \
                 rcvpackets.h          \
                 rcvtracker.h          \
                 recycle.h             \
                 reportproc.h          \
                 reportprod.h          \
                 ricalcer.h            \
                 rtpfecbuffer.h        \
                 signalreport.h        \
                 slidingwindow.h       \
                 sndctrler.h           \
                 sndsubflows.h         \
                 sndpackets.h          \
                 sndtracker.h          \
                 streamjoiner.h        \
                 streamsplitter.h      

                          

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
