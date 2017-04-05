noinst_PROGRAMS = snd_pipeline             \
                  rcv_pipeline             \
                  make_delta_statlogs      \
                  bcex                     \
                  bwcsv                    \
                  statmaker                \
                  logsplitter              \
                  tablemaker 
                  
                  
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


make_delta_statlogs_SOURCES = make_delta_statlogs.c 
make_delta_statlogs_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
make_delta_statlogs_LDADD = $(GST_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstapp-$(GST_API_VERSION) $(GST_BASE_LIBS) $(LDADD)

bcex_SOURCES = bcex.c 
bcex_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
bcex_LDADD = $(GST_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstapp-$(GST_API_VERSION) $(GST_BASE_LIBS) $(LDADD)

bwcsv_SOURCES = bwcsv.c 
bwcsv_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
bwcsv_LDADD = $(GST_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstapp-$(GST_API_VERSION) $(GST_BASE_LIBS) $(LDADD)

statmaker_SOURCES = statmaker.c 
statmaker_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
statmaker_LDADD = $(GST_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstapp-$(GST_API_VERSION) $(GST_BASE_LIBS) $(LDADD) -lpcap

logsplitter_SOURCES = logsplitter.c 
logsplitter_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
logsplitter_LDADD = $(GST_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstapp-$(GST_API_VERSION) $(GST_BASE_LIBS) $(LDADD)

tablemaker_SOURCES = tablemaker.c 
tablemaker_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
tablemaker_LDADD = $(GST_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstapp-$(GST_API_VERSION) $(GST_BASE_LIBS) $(LDADD)