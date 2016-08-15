noinst_PROGRAMS = sender             \
                  receiver           \
                  v4l2_file_player

# FIXME 0.11: ignore GValueArray warnings for now until this is sorted
ERROR_CFLAGS=

receiver_SOURCES = receiver.c
receiver_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
receiver_LDADD = $(GST_LIBS)

sender_SOURCES = sender.c
sender_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
sender_LDADD = $(GST_LIBS)

v4l2_file_player_SOURCES = v4l2_file_player.c
v4l2_file_player_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
v4l2_file_player_LDADD = $(GST_LIBS)