noinst_PROGRAMS = client             \
                  client-mprtpaux    \
                  client-ratiotest   \
                  server-live-cctest \
                  server-cctest      \
                  server-mprtpaux    \
                  server-ratiodist

# FIXME 0.11: ignore GValueArray warnings for now until this is sorted
ERROR_CFLAGS=

server_live_cctest_SOURCES = server-live-cctest.c
server_live_cctest_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
server_live_cctest_LDADD = $(GST_LIBS)

server_cctest_SOURCES = server-cctest.c
server_cctest_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
server_cctest_LDADD = $(GST_LIBS)

client_rtpaux_SOURCES = client-rtpaux.c
client_rtpaux_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
client_rtpaux_LDADD = $(GST_LIBS)

server_mprtpaux_SOURCES = server-mprtpaux.c
server_mprtpaux_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
server_mprtpaux_LDADD = $(GST_LIBS)

server_ratiodist_SOURCES = server-ratiodist.c
server_ratiodist_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
server_ratiodist_LDADD = $(GST_LIBS)

client_mprtpaux_SOURCES = client-mprtpaux.c
client_mprtpaux_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
client_mprtpaux_LDADD = $(GST_LIBS)

client_ratiotest_SOURCES = client-ratiotest.c
client_ratiotest_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
client_ratiotest_LDADD = $(GST_LIBS)

client_SOURCES = client.c
client_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
client_LDADD = $(GST_LIBS)