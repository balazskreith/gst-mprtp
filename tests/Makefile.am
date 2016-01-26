noinst_PROGRAMS = client             \
                  server      

# FIXME 0.11: ignore GValueArray warnings for now until this is sorted
ERROR_CFLAGS=

server_SOURCES = server.c
server_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
server_LDADD = $(GST_LIBS)

client_SOURCES = client.c
client_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
client_LDADD = $(GST_LIBS)