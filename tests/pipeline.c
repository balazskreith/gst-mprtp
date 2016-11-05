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

gchar* _null_test(gchar *subject_str, gchar* failed_str)
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

StatParams*     make_stat_params (gchar* params_rawstring)
{
  StatParams  *result = g_malloc0(sizeof(StatParams));
  gchar       **tokens = g_strsplit(params_rawstring, ":", -1);

  strcpy(result->touched_sync,     tokens[0]);

  result->csv = atoi(tokens[1]);

  sprintf(result->to_string,
      "Touched syncronization: %s, CSV logging: %d",
      result->touched_sync,
      result->csv
  );

  return result;
}

SndTransferParams*  make_snd_transfer_params(gchar* params_rawstring)
{
  SndTransferParams *result = g_malloc0(sizeof(SndTransferParams));
  gchar             **tokens = g_strsplit(params_rawstring, ":", -1);

  result->type = _compare_types(tokens[0], "RTP", "MPRTP", NULL);
  switch(result->type){
     case TRANSFER_TYPE_RTP:

       strcpy(result->rtp.dest_ip, tokens[1]);
       result->rtp.dest_port = atoi(tokens[2]);

       sprintf(result->to_string, "RTP, Dest.: %s:%d", result->rtp.dest_ip, result->rtp.dest_port);

       break;
     case TRANSFER_TYPE_MPRTP:
       {
         gint offset = 1;
         gint i;

         sprintf(result->to_string, "MPRTP Subflows (%d); ", result->mprtp.subflows_num);
         result->mprtp.subflows_num = atoi(tokens[1]);

         for(i = 0; i < result->mprtp.subflows_num; ++i){
           gchar subflow_info[255];
           SenderSubflow* subflow = g_malloc0(sizeof(SenderSubflow));
           subflow->id        = atoi(tokens[++offset]);
           strcpy(subflow->dest_ip, tokens[++offset]);
           subflow->dest_port = atoi(tokens[++offset]);

           result->mprtp.subflows = g_slist_append(result->mprtp.subflows, subflow);
           sprintf(subflow_info, "%d Dest.: %s:%d; ",
               subflow->id,
               subflow->dest_ip,
               subflow->dest_port
           );
           strcat(result->to_string, subflow_info);
         }
       }
       break;
     default:
       g_print("Unrecognized type (%d) at make_snd_transfer_params \n", result->type);
       break;
   };

  return result;
}

void free_snd_transfer_params(SndTransferParams *snd_transfer_params)
{
  if(snd_transfer_params->type == TRANSFER_TYPE_MPRTP){
    g_slist_free_full(snd_transfer_params->mprtp.subflows, g_free);
  }
  g_free(snd_transfer_params);
}


CCSenderSideParams*   make_cc_sender_side_params(gchar* params_rawstring)
{
  CCSenderSideParams *result = g_malloc0(sizeof(CCSenderSideParams));
  gchar                     **tokens = g_strsplit(params_rawstring, ":", -1);

  result->type = _compare_types(tokens[0], "NONE", "SCREAM", "FBRAPLUS", NULL);

  switch(result->type){
    case CONGESTION_CONTROLLER_TYPE_SCREAM:
//      _replace_char(",",":",tokens[1]);
      result->rcv_transfer_params = make_rcv_transfer_params(tokens[1]);

      sprintf(result->to_string, "SCReAM congestion controller, Rcv Params: %s",
          result->rcv_transfer_params->to_string);
      break;
    case CONGESTION_CONTROLLER_TYPE_FBRAPLUS:
//      _replace_char(",",":",tokens[1]);
      result->rcv_transfer_params = make_rcv_transfer_params(tokens[1]);

      sprintf(result->to_string, "FBRA+ congestion controller, Rcv Params: %s",
          result->rcv_transfer_params->to_string);
      break;
    default:
      g_print("Unrecognized type (%d) at make_cc_sender_side_params \n", result->type);
      break;
  };

  return result;
}

void free_cc_sender_side_params(CCSenderSideParams *congestion_controller_params)
{
  if(!congestion_controller_params){
    return;
  }
  free_rcv_transfer_params(congestion_controller_params->rcv_transfer_params);
  g_free(congestion_controller_params);
}





RcvTransferParams*  make_rcv_transfer_params(gchar* params_rawstring)
{
  RcvTransferParams *result = g_malloc0(sizeof(RcvTransferParams));
  gchar             **tokens = g_strsplit(params_rawstring, ":", -1);

  result->type = _compare_types(tokens[0], "RTP", "MPRTP", NULL);
  switch(result->type){
     case TRANSFER_TYPE_RTP:

       result->rtp.bound_port = atoi(tokens[1]);

       sprintf(result->to_string, "RTP, Bound Port: %d", result->rtp.bound_port);

       break;
     case TRANSFER_TYPE_MPRTP:
       {
         gint offset = 1;
         gint i;

         sprintf(result->to_string, "MPRTP Subflows (%d); ", result->mprtp.subflows_num);
         result->mprtp.subflows_num = atoi(tokens[1]);

         for(i = 0; i < result->mprtp.subflows_num; ++i){
           gchar subflow_info[255];
           ReceiverSubflow* subflow = g_malloc0(sizeof(ReceiverSubflow));
           subflow->id        = atoi(tokens[++offset]);
           strcpy(subflow->bound_port, tokens[++offset]);

           result->mprtp.subflows = g_slist_append(result->mprtp.subflows, subflow);
           sprintf(subflow_info, "%d Bound port.: %d; ",
               subflow->id,
               subflow->bound_port
           );
           strcat(result->to_string, subflow_info);
         }
       }
       break;
     default:
       g_print("Unrecognized type (%d) at make_rcv_transfer_params \n", result->type);
       break;
   };

  return result;
}

void free_rcv_transfer_params(RcvTransferParams *rcv_transfer_params)
{
  if(rcv_transfer_params->type == TRANSFER_TYPE_MPRTP){
    g_slist_free_full(rcv_transfer_params->mprtp.subflows, g_free);
  }
  g_free(rcv_transfer_params);
}



CCReceiverSideParams*   make_cc_receiver_side_params(gchar* params_rawstring)
{
  CCReceiverSideParams *result = g_malloc0(sizeof(CCReceiverSideParams));
  gchar                     **tokens = g_strsplit(params_rawstring, ":", -1);

  result->type = _compare_types(tokens[0], "NONE", "SCREAM", "FBRAPLUS", NULL);

  switch(result->type){
    case CONGESTION_CONTROLLER_TYPE_SCREAM:
//      _replace_char(",",":",tokens[1]);
      result->snd_transfer_params = make_snd_transfer_params(tokens[1]);

      sprintf(result->to_string, "SCReAM congestion controller, Rcv Params: %s",
          result->snd_transfer_params->to_string);
      break;
    case CONGESTION_CONTROLLER_TYPE_FBRAPLUS:
//      _replace_char(",",":",tokens[1]);
      result->snd_transfer_params = make_snd_transfer_params(tokens[1]);

      sprintf(result->to_string, "FBRA+ congestion controller, Rcv Params: %s",
          result->snd_transfer_params->to_string);
      break;
    default:
      g_print("Unrecognized type (%d) at make_cc_receiver_side_params \n", result->type);
      break;
  };

  return result;
}

void free_cc_receiver_side_params(CCReceiverSideParams *congestion_controller_params)
{
  if(!congestion_controller_params){
    return;
  }
  free_rcv_transfer_params(congestion_controller_params->snd_transfer_params);
  g_free(congestion_controller_params);
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

StatParamsTuple* make_statparams_tuple_by_raw_strings(gchar* statparams_rawstring,
                                       gchar* statlogs_sink_params_rawstring,
                                       gchar* packetlogs_sink_params_rawstring
                                      )
{
  StatParams* stat_params;
  SinkParams* statlogs_sink_params = NULL;
  SinkParams* packetlogs_sink_params = NULL;

  if(statparams_rawstring == NULL){
    return NULL;
  }

  stat_params = make_stat_params(statparams_rawstring);

  if(statlogs_sink_params_rawstring){
      statlogs_sink_params = make_sink_params(statlogs_sink_params_rawstring);
    }

  if(packetlogs_sink_params){
    packetlogs_sink_params = make_sink_params(packetlogs_sink_params_rawstring);
    }

  return make_statparams_tuple(stat_params, statlogs_sink_params, packetlogs_sink_params);
}

StatParamsTuple* make_statparams_tuple(StatParams* stat_params,
                                       SinkParams* statlogs_sink_params,
                                       SinkParams* packetlogs_sink_params
                                      )
{
  StatParamsTuple* result = g_malloc0(sizeof(StatParamsTuple));

  result->stat_params = stat_params;
  result->statlogs_sink_params = statlogs_sink_params;
  result->packetlogs_sink_params = packetlogs_sink_params;

  sprintf(result->to_string, "%s; StatLogs Sink Params: %s; PacketLogs Sink Params: %s",
      result->stat_params->to_string,
      result->statlogs_sink_params->to_string,
      result->packetlogs_sink_params->to_string
      );
  return result;
}

void free_statparams_tuple(StatParamsTuple* statparams_tuple)
{
  if(!statparams_tuple){
    return;
  }

  g_free(statparams_tuple->stat_params);

  if(statparams_tuple->packetlogs_sink_params){
    g_free(statparams_tuple->packetlogs_sink_params);
  }

  if(statparams_tuple->statlogs_sink_params){
    g_free(statparams_tuple->statlogs_sink_params);
  }

  g_free(statparams_tuple);
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
  if(!this->listeners){
    return;
  }
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
  if (message->src != pipe) {
    return;
  }
  g_print ("Pipeline %s changed state from %s to %s\n",
      GST_OBJECT_NAME (message->src),
      gst_element_state_get_name (old), gst_element_state_get_name (new));
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

GstElement* debug_by_src(GstElement*element)
{
  GstBin* debugBin     = gst_bin_new(NULL);
  GstElement *tee      = gst_element_factory_make ("tee", NULL);
  GstElement *q1       = gst_element_factory_make ("queue", NULL);
  GstElement *q2       = gst_element_factory_make ("queue", NULL);
  GstElement *fakesink = gst_element_factory_make ("fakesink", NULL);

  gst_bin_add_many(debugBin, element, tee, q1, q2, fakesink, NULL);

  gst_element_link_pads(element, "src", tee, "sink");
  gst_element_link_pads(tee, "src_1", q1, "sink");
  gst_element_link_pads(tee, "src_2", q2, "sink");
  gst_element_link_pads(q2, "src", fakesink, "sink");

  if(GST_IS_PAD(gst_element_get_static_pad(element, "sink"))){
    setup_ghost_sink(element, debugBin);
  }

  setup_ghost_src(q1, debugBin);

  g_object_set (fakesink, "dump", TRUE, NULL);
  return GST_ELEMENT(debugBin);
}
//
//GstElement* debug_by_sink(GstElement*element)
//{
//  GstBin* debugBin     = gst_bin_new("debugBin");
//  GstElement *tee      = gst_element_factory_make ("tee", NULL);
//  GstElement *q1       = gst_element_factory_make ("queue", NULL);
//  GstElement *q2       = gst_element_factory_make ("queue", NULL);
//  GstElement *fakesink = gst_element_factory_make ("fakesink", NULL);
//
//  gst_bin_add_many(debugBin, element, tee, q1, q2, fakesink, NULL);
//
//  setup_ghost_sink(tee, debugBin);
//
////  gst_element_link_pads(tee, "src_1", q1, "sink");
////  gst_element_link_pads(q1, "src", element, "sink");
////  gst_element_link_pads(tee, "src_2", q2, "sink");
////  gst_element_link_pads(q2, "src", fakesink, "sink");
//
//  gst_element_link_pads(tee, "src_1", element, "sink");
//  gst_element_link_pads(tee, "src_2", fakesink, "sink");
//
//  if(GST_IS_PAD(gst_element_get_static_pad(element, "src"))){
//    setup_ghost_src(element, debugBin);
//  }
////  g_object_set(tee, "allow-not-linked", TRUE, NULL);
//  g_object_set (fakesink, "dump", TRUE, NULL);
//  return GST_ELEMENT(debugBin);
//}

GstElement* debug_by_sink(GstElement*element)
{
  GstBin* debugBin     = gst_bin_new("debugBin");
  GstElement *identity      = gst_element_factory_make ("identity", NULL);

  gst_bin_add_many(debugBin, identity, element, NULL);

  gst_element_link_pads(identity, "src", element, "sink");

  g_object_set (identity, "dump", TRUE, NULL);

  setup_ghost_sink(identity, debugBin);
//  if(GST_IS_PAD(gst_element_get_static_pad(element, "src"))){
//    setup_ghost_src(element, debugBin);
//  }

  return GST_ELEMENT(debugBin);
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

static SenderEventers* sender_eventers = NULL;
SenderEventers* get_sender_eventers(void)
{
  if(sender_eventers){
    return sender_eventers;
  }
  sender_eventers = g_malloc0(sizeof(SenderEventers));
  sender_eventers->on_assembled   = make_notifier("on-assembled");
  sender_eventers->on_playing     = make_notifier("on-playing");
  sender_eventers->on_destroy     = make_notifier("on-destroy");
  sender_eventers->on_target_bitrate_change = make_notifier("on-target-bitrate-change");

  return sender_eventers;
}

void sender_eventers_dtor(void)
{
  if(!sender_eventers){
    return;
  }
  notifier_dtor(sender_eventers->on_assembled);
  notifier_dtor(sender_eventers->on_playing);
  notifier_dtor(sender_eventers->on_destroy);
  notifier_dtor(sender_eventers->on_target_bitrate_change);
  g_free(sender_eventers);
  sender_eventers = NULL;
}
