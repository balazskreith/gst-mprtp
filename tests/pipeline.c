#include "pipeline.h"

static gint32 _compare_types(gchar* type, ...)
{
  va_list arguments;
   gchar* token = NULL;
  gint32 result = 1;

  va_start ( arguments, type );
  for(token = va_arg( arguments,  gchar*); token; token = va_arg(arguments,  gchar*), ++result){
    if(!strcmp(type, token)){
      return result;
    }
  }
  va_end ( arguments );
  return -1;
}

gchar* _string_test(gchar *subject_str, gchar* failed_str)
{
  return subject_str == NULL ? failed_str : subject_str;
}


void setup_framerate(gchar *string, Framerate* framerate)
{
  gchar       **tokens = g_strsplit(string, "/", -1);
  framerate->numerator = atoi(tokens[0]);
  framerate->divider   = atoi(tokens[1]);
}


SourceParams*   make_source_params(gchar* params_rawstring)
{
  SourceParams* result = g_malloc0(sizeof(SourceParams));
  gchar       **tokens = g_strsplit(params_rawstring, ":", -1);

  result->type = _compare_types(tokens[0], "TESTVIDEO", "RAWPROXY", "LIVEFILE", "FILE", NULL);

  switch(result->type){
    case SOURCE_TYPE_TESTVIDEO:

      sprintf(result->to_string, "TestVideoSource");
      break;
    case SOURCE_TYPE_RAWPROXY:
      strcpy(result->rawproxy.width,      tokens[1]);
      strcpy(result->rawproxy.height,     tokens[2]);

      result->rawproxy.clock_rate = atoi(tokens[3]);
      result->rawproxy.port       = atoi(tokens[4]);

      sprintf(result->to_string, "RawProxy, W:%s, H:%s, ClockRate: %d, Port: %hu",
          result->rawproxy.width,
          result->rawproxy.height,
          result->rawproxy.clock_rate,
          result->rawproxy.port
      );

      break;

    case SOURCE_TYPE_FILE:
    case SOURCE_TYPE_LIVEFILE:
      strcpy(result->file.location,   tokens[1]);

      result->file.loop = atoi(tokens[2]);

      strcpy(result->file.width,      tokens[3]);
      strcpy(result->file.height,     tokens[4]);

      result->file.format = atoi(tokens[5]);
      setup_framerate(tokens[6], &result->file.framerate);

      sprintf(result->to_string, "%s: %s, loop: %d, W:%s, H:%s, Framerate: %d/%d, Format: %d",
          result->type == SOURCE_TYPE_FILE ? "File" : "LiveFile",
          result->file.location,
          result->file.loop,
          result->file.width,
          result->file.height,
          result->file.framerate.numerator,
          result->file.framerate.divider,
          result->file.format
      );

      break;
    default:
      g_print("Unrecognized type (%d) at make_source_params \n", result->type);
      break;
  };

  return result;
}

CodecParams*    make_codec_params (gchar* params_rawstring)
{
  CodecParams* result = g_malloc0(sizeof(CodecParams));
  gchar       **tokens = g_strsplit(params_rawstring, ":", -1);

  result->type = _compare_types(tokens[0], "THEORA",  "VP8", NULL);

  switch(result->type){
    case CODEC_TYPE_THEORA:
      sprintf(result->type_str,  "THEORA");
      sprintf(result->to_string, "THEORA");
      break;

    case CODEC_TYPE_VP8:
      sprintf(result->type_str,  "VP8");
      sprintf(result->to_string, "VP8");
    break;

    default:
      g_print("Unrecognized type (%d) at make_codec_params \n", result->type);
      break;
  };

  return result;
}

VideoParams*    make_video_params (gchar* params_rawstring)
{
  VideoParams* result = g_malloc0(sizeof(VideoParams));
  gchar       **tokens = g_strsplit(params_rawstring, ":", -1);

  strcpy(result->width,      tokens[0]);
  strcpy(result->height,     tokens[1]);

  result->clock_rate = atoi(tokens[2]);

  setup_framerate(tokens[3], &result->framerate);

  sprintf(result->to_string,
      "Width: %s, Height: %s, Clock-Rate: %d, FrameRate: %d/%d",
      result->width,
      result->height,
      result->clock_rate,
      result->framerate.numerator,
      result->framerate.divider
  );

  return result;
}

SenderParams*   make_sender_params(gchar* params_rawstring)
{
  SenderParams *result = g_malloc0(sizeof(SenderParams));
  gchar       **tokens = g_strsplit(params_rawstring, ":", -1);

  result->type = _compare_types(tokens[0], "RTPSIMPLE", "RTPSCREAM", "RTPFBRAP", NULL);

  switch(result->type){
    case TRANSFER_TYPE_RTPSIMPLE:

      result->dest_port = atoi(tokens[1]);
      strcpy(result->dest_ip, tokens[2]);

      sprintf(result->to_string, "Dest IP: %s:%d", result->dest_ip, result->dest_port);

      break;
    case TRANSFER_TYPE_RTPSCREAM:

      break;
    case TRANSFER_TYPE_RTPFBRAP:

      break;
    default:
      g_print("Unrecognized type (%d) at make_sender_params \n", result->type);
      break;
  };

  return result;
}

ReceiverParams* make_receiver_params(gchar* params_rawstring)
{
  ReceiverParams* result = g_malloc0(sizeof(ReceiverParams));
  gchar       **tokens = g_strsplit(params_rawstring, ":", -1);

  result->type = _compare_types(tokens[0], "RTPSIMPLE", "RTPSCREAM", "RTPFBRAP", NULL);

  result->bound_port = atoi(tokens[1]);

  switch(result->type){
    case TRANSFER_TYPE_RTPSIMPLE:
      sprintf(result->to_string, "RTPSIMPLE, Port: %d", result->bound_port);
      break;
    case TRANSFER_TYPE_RTPSCREAM:

      break;
    case TRANSFER_TYPE_RTPFBRAP:

      break;
    default:
      g_print("Unrecognized type (%d) at make_receiver_params \n", result->type);
      break;
  };

  return result;
}

SinkParams*     make_sink_params(gchar* params_rawstring)
{
  SinkParams* result = g_malloc0(sizeof(SinkParams));
  gchar       **tokens = g_strsplit(params_rawstring, ":", -1);

  result->type = _compare_types(tokens[0], "AUTOVIDEO", "RAWPROXY", NULL);

  switch(result->type){
    case SINK_TYPE_AUTOVIDEO:
      sprintf(result->to_string, "AUTOVIDEO");
      break;
    case SINK_TYPE_RAWPROXY:

      break;
    default:
      g_print("Unrecognized type (%d) at make_sink_params \n", result->type);
      break;
  };

  return result;
}

Notifier* notifier_ctor(void)
{
  return g_malloc0(sizeof(Notifier));
}

void notifier_dtor(Notifier* this)
{
  GSList* item = this->listeners;
  g_slist_free_full(this->listeners, g_free);
}

Notifier* make_notifier(const gchar* event_name)
{
  Notifier* this = notifier_ctor();
  strcpy(this->name, event_name);
  return this;
}

void notifier_add_listener(Notifier* this, listener listener_func, gpointer user_data)
{
  Listener* listener = g_malloc0(sizeof(Listener));
  listener->listener_func = listener_func;
  listener->listener_obj     = user_data;

  this->listeners = g_slist_prepend(this->listeners, listener);
}

static void _foreach_listeners(Listener* listener, gpointer user_data){
  listener->listener_func(listener->listener_obj, user_data);
}

void notifier_do(Notifier* this, gpointer user_data)
{
  g_slist_foreach(this->listeners, (GFunc)_foreach_listeners, user_data);
}


void
cb_eos (GstBus * bus, GstMessage * message, gpointer data)
{
  GMainLoop *loop = data;
  g_print ("Got EOS\n");
  g_main_loop_quit (loop);
}


void
cb_state (GstBus * bus, GstMessage * message, gpointer data)
{
  GstObject *pipe = GST_OBJECT (data);
  GstState old, new, pending;
  gst_message_parse_state_changed (message, &old, &new, &pending);
  if (message->src == pipe) {
    g_print ("Pipeline %s changed state from %s to %s\n",
        GST_OBJECT_NAME (message->src),
        gst_element_state_get_name (old), gst_element_state_get_name (new));
  }
}

void
cb_warning (GstBus * bus, GstMessage * message, gpointer data)
{
  GError *error = NULL;
  gst_message_parse_warning (message, &error, NULL);
  g_printerr ("Got warning from %s: %s\n", GST_OBJECT_NAME (message->src),
      error->message);
  g_error_free (error);
}

void
cb_error (GstBus * bus, GstMessage * message, gpointer data)
{
  GMainLoop *loop = data;
  GError *error = NULL;
  gst_message_parse_error (message, &error, NULL);
  g_printerr ("Got error from %s: %s\n", GST_OBJECT_NAME (message->src),
      error->message);
  g_error_free (error);
  g_main_loop_quit (loop);
}

void
setup_ghost_sink_by_padnames (GstElement * sink, const gchar* sinkpadName, GstBin * bin, const gchar *binPadName)
{
  GstPad *sinkPad = gst_element_get_static_pad (sink, sinkpadName);
  GstPad *binPad = gst_ghost_pad_new (binPadName, sinkPad);
  gst_element_add_pad (GST_ELEMENT (bin), binPad);
}

void
setup_ghost_sink (GstElement * sink, GstBin * bin)
{
  setup_ghost_sink_by_padnames(sink, "sink", bin, "sink");
}

void
setup_ghost_src_by_padnames (GstElement * src, const gchar* srcpadName, GstBin * bin, const gchar *binPadName)
{
  GstPad *srcPad = gst_element_get_static_pad (src, srcpadName);
  GstPad *binPad = gst_ghost_pad_new (binPadName, srcPad);
  gst_element_add_pad (GST_ELEMENT (bin), binPad);
}


void
setup_ghost_src (GstElement * src, GstBin * bin)
{
  setup_ghost_src_by_padnames(src, "src", bin, "src");
}

static SenderNotifiers* sender_notifiers = NULL;
SenderNotifiers* get_sender_eventers(void)
{
  if(sender_notifiers){
    return sender_notifiers;
  }
  sender_notifiers = g_malloc0(sizeof(SenderNotifiers));
  sender_notifiers->on_playing     = make_notifier("on--playing");
  sender_notifiers->on_destroy     = make_notifier("on-destroy");
  sender_notifiers->on_target_bitrate_change = make_notifier("on-target-bitrate-change");

  return sender_notifiers;
}

void sender_eventers_dtor(void)
{
  g_free(sender_notifiers);
}
