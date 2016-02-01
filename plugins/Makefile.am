plugin_LTLIBRARIES = libgstmprtp.la

# sources used to compile this plug-in
libgstmprtp_la_SOURCES = gstmprtpplugin.c           \
                         gstmprtcpbuffer.c          \
                         gstmprtpbuffer.c           \
                         gstmprtpscheduler.c        \
                         gstmprtpplayouter.c        \
                         gstmprtpreceiver.c         \
                         gstmprtpsender.c           \
                         mprtprpath.c               \
                         mprtpspath.c               \
                         streamjoiner.c             \
                         streamsplitter.c           \
                         sndctrler.c                \
                         rcvctrler.c                \
                         bintree.c                  \
                         bintree2.c                 \
                         packetsrcvqueue.c          \
                         packetssndqueue.c          \
                         pointerpool.c              \
                         ricalcer.c                 \
                         percentiletracker.c        \
                         percentiletracker2.c       \
                         sndratedistor.c            \
                         numstracker.c              \
                         floatsbuffer.c             \
                         subratectrler.c            
                    
                    
noinst_HEADERS = mprtpspath.h           \
                 sndctrler.h            \
                 rcvctrler.c            \
                 mprtprpath.h           \
                 streamsplitter.h       \
                 streamjoiner.h         \
                 bintree.h              \
                 bintree2.h             \
                 packetsrcvqueue.h      \
                 packetssndqueue.h      \
                 pointerpool.h          \
                 ricalcer.h             \
                 percentiletracker.h    \
                 percentiletracker2.h   \
                 sndratedistor.h        \
                 numstracker.h          \
                 floatsbuffer.h         \
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
