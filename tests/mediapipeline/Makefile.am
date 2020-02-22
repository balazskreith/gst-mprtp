noinst_PROGRAMS = snd_pipeline             \
                  rcv_pipeline             \
                  buffercollector            
                  
TARGETPATH = ../                  
# FIXME 0.11: ignore GValueArray warnings for now until this is sorted
ERROR_CFLAGS=

rcv_pipeline_SOURCES = rcv_pipeline.c \
					   decoder.c \
					   encoder.c \
					   owr_arrival_time_meta.c \
					   pipeline.c \
					   receiver.c \
					   rtpstatmaker.c \
					   sender.c \
					   sink.c \
					   source.c 
					   
rcv_pipeline_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
                        $(GST_CFLAGS)	$(GST_NET_CFLAGS) $(WARNING_CFLAGS) \
                        $(ERROR_CFLAGS)
                        
rcv_pipeline_LDADD = $(GST_LIBS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
            $(GST_NET_LIBS) -lgstrtp-@GST_API_VERSION@ \
	        $(GST_BASE_LIBS) $(GST_LIBS_LIBS) \
	        -lgstapp-$(GST_API_VERSION)

snd_pipeline_SOURCES = snd_pipeline.c \
					   decoder.c \
					   encoder.c \
					   owr_arrival_time_meta.c \
					   pipeline.c \
					   receiver.c \
					   rtpstatmaker.c \
					   sender.c \
					   sink.c \
					   source.c 
					   
snd_pipeline_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
                        $(GST_CFLAGS)	$(GST_NET_CFLAGS) $(WARNING_CFLAGS) \
                        $(ERROR_CFLAGS)
snd_pipeline_LDADD = $(GST_LIBS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
            $(GST_NET_LIBS) -lgstrtp-@GST_API_VERSION@ \
	        $(GST_BASE_LIBS) $(GST_LIBS_LIBS) \
	        -lgstapp-$(GST_API_VERSION)
	        
	        

buffercollector_SOURCES = buffercollector.c
					   
buffercollector_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
                        $(GST_CFLAGS)	$(GST_NET_CFLAGS) $(WARNING_CFLAGS) \
                        $(ERROR_CFLAGS)
                        
buffercollector_LDADD = $(GST_LIBS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
            $(GST_NET_LIBS) -lgstrtp-@GST_API_VERSION@ \
	        $(GST_BASE_LIBS) $(GST_LIBS_LIBS)
	        -lgstapp-$(GST_API_VERSION)

