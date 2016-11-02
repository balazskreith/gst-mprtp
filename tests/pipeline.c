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

SourceParams*   make_source_params(gchar* params_rawstring)
{
  SourceParams* result = g_malloc0(sizeof(SourceParams));
  gchar       **tokens = g_strsplit(params_rawstring, ":", -1);

  result->type = _compare_types(tokens[0], "TESTVIDEO", "RAWPROXY", NULL);

  switch(result->type){
    case SOURCE_TYPE_TESTVIDEO:
      result->testvideo.horizontal_speed = atoi(tokens[1]);

      sprintf(result->to_string, "TestVideoSource, horizontal-speed: %d", result->testvideo.horizontal_speed);
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

  result->type = _compare_types(tokens[0], "VP8", "THEORA", NULL);
  strcpy(result->width,      tokens[1]);
  strcpy(result->height,     tokens[2]);

  result->clock_rate = atoi(tokens[3]);

  switch(result->type){
    case CODEC_TYPE_VP8:

      sprintf(result->to_string, "VP8 Codec, W:%s, H:%s, ClockRate: %d",
        result->width,
        result->height,
        result->clock_rate
      );
    break;

    case CODEC_TYPE_THEORA:

      sprintf(result->to_string, "THEORA Codec, W:%s, H:%s, ClockRate: %d",
              result->width,
              result->height,
              result->clock_rate
            );
      break;

    default:
      g_print("Unrecognized type (%d) at make_codec_params \n", result->type);
      break;
  };

  return result;
}

SenderParams*   make_sender_params(gchar* params_rawstring)
{
  SenderParams *result = g_malloc0(sizeof(SenderParams));
  gchar       **tokens = g_strsplit(params_rawstring, ":", -1);

  result->type = _compare_types(tokens[0], "RTPSIMPLE", "RTPSCREAM", "RTPFBRAP", NULL);

  switch(result->type){
    case TRANSFER_TYPE_RTPSIMPLE:

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

  switch(result->type){
    case TRANSFER_TYPE_RTPSIMPLE:

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


