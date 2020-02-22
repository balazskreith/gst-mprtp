noinst_PROGRAMS = statmaker \
				  statlistener
                  
                  
# FIXME 0.11: ignore GValueArray warnings for now until this is sorted
ERROR_CFLAGS=

statmaker_SOURCES = statmaker.c 
statmaker_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
statmaker_LDADD = $(GST_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstapp-$(GST_API_VERSION) $(GST_BASE_LIBS) $(LDADD) -lpcap -lm


statlistener_SOURCES = statlistener.c 
statlistener_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
statlistener_LDADD = $(GST_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstapp-$(GST_API_VERSION) $(GST_BASE_LIBS) $(LDADD) -lpcap -lm

