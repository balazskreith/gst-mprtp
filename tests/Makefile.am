noinst_PROGRAMS = client-rtpaux \
                  server-rtpaux \
                  client-mprtpaux \
                  server-mprtpaux 

# FIXME 0.11: ignore GValueArray warnings for now until this is sorted
ERROR_CFLAGS=

server_rtpaux_SOURCES = server-rtpaux.c
server_rtpaux_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
server_rtpaux_LDADD = $(GST_LIBS)

client_rtpaux_SOURCES = client-rtpaux.c
client_rtpaux_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
client_rtpaux_LDADD = $(GST_LIBS)

server_mprtpaux_SOURCES = server-mprtpaux.c
server_mprtpaux_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
server_mprtpaux_LDADD = $(GST_LIBS)

client_mprtpaux_SOURCES = client-mprtpaux.c
client_mprtpaux_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
client_mprtpaux_LDADD = $(GST_LIBS)