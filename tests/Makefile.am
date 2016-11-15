noinst_PROGRAMS = snd_pipeline             \
                  rcv_pipeline             \
                  make_owd_statlogs        
                  
                  
# FIXME 0.11: ignore GValueArray warnings for now until this is sorted
ERROR_CFLAGS=

rcv_pipeline_SOURCES = rcv_pipeline.c \
					   decoder.c \
					   encoder.c \
					   pipeline.c \
					   receiver.c \
					   rtpstatmaker.c \
					   sender.c \
					   sink.c \
					   source.c \
					   transceiver.c 
					   
rcv_pipeline_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
rcv_pipeline_LDADD = $(GST_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstapp-$(GST_API_VERSION) $(GST_BASE_LIBS) $(LDADD)

snd_pipeline_SOURCES = snd_pipeline.c \
					   decoder.c \
					   encoder.c \
					   pipeline.c \
					   receiver.c \
					   rtpstatmaker.c \
					   sender.c \
					   sink.c \
					   source.c \
					   transceiver.c
					   
snd_pipeline_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
snd_pipeline_LDADD = $(GST_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstapp-$(GST_API_VERSION) $(GST_BASE_LIBS) $(LDADD)


make_owd_statlogs_SOURCES = make_owd_statlogs.c owds.c 
make_owd_statlogs_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
make_owd_statlogs_LDADD = $(GST_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstapp-$(GST_API_VERSION) $(GST_BASE_LIBS) $(LDADD)
