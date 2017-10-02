noinst_PROGRAMS = statsrelayer 
                  
TARGETPATH = ../                  
# FIXME 0.11: ignore GValueArray warnings for now until this is sorted
ERROR_CFLAGS=

statsrelayer_SOURCES = buffer.c \
					   common.c \
					   mapper.c \
					   main.c \
					   sink.c \
					   source.c \
					   statsrelayer.c 
					   
statsrelayer_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
                        $(GST_CFLAGS)	$(GST_NET_CFLAGS) $(WARNING_CFLAGS) \
                        $(ERROR_CFLAGS)
statsrelayer_LDADD = $(GST_LIBS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
            $(GST_NET_LIBS) -lgstrtp-@GST_API_VERSION@ \
	        $(GST_BASE_LIBS) $(GST_LIBS_LIBS) \
	        -lgstapp-$(GST_API_VERSION)

