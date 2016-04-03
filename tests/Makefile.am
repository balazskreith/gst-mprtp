noinst_PROGRAMS = sender             \
                  receiver

# FIXME 0.11: ignore GValueArray warnings for now until this is sorted
ERROR_CFLAGS=

receiver_SOURCES = receiver.c
receiver_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
receiver_LDADD = $(GST_LIBS)

sender_SOURCES = sender.c
sender_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
sender_LDADD = $(GST_LIBS)