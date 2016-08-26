noinst_PROGRAMS = sender             \
                  receiver           \
                  scream_sender      \
                  scream_receiver    \
                  v4l2_file_player   
                  
                  
# FIXME 0.11: ignore GValueArray warnings for now until this is sorted
ERROR_CFLAGS=

receiver_SOURCES = receiver.c
receiver_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
receiver_LDADD = $(GST_LIBS)

scream_receiver_SOURCES = scream_receiver.c
scream_receiver_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(AM_CFLAGS)
scream_receiver_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstrtp-$(GST_API_VERSION) $(GST_BASE_LIBS) $(LDADD)

sender_SOURCES = sender.c
sender_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
sender_LDADD = $(GST_LIBS)

scream_sender_SOURCES = scream_sender.c
scream_sender_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
scream_sender_LDADD = $(GST_LIBS)

v4l2_file_player_SOURCES = v4l2_file_player.c
v4l2_file_player_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
v4l2_file_player_LDADD = $(GST_LIBS)