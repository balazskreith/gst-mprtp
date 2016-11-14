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

Interface* make_interface(GstElement* element, gchar* padname){
  Interface* result = g_malloc0(sizeof(Interface));
  result->element = element;
  strcpy(result->padname, padname);
  return result;
}
void interface_dtor(Interface* this){
  g_free(this);
}

void connect_interfaces(Interface* source, Interface* sink){
  gst_element_link_pads(source->element, source->padname, sink->element, sink->padname);
}

void _print_info(void)
{
  g_print(
      "Options for using Sender Pipeline\n"
      "\t--info Print this.\n"

      "\nSender pipeline parameters:\n"
      "\t--source=TESTVIDEO|RAWPROXY|LIVEFILE:...\n"
      "\t\t TESTVIDEO\n"
      "\t\t RAWPROXY:NOT IMPLEMENTED YET\n"
      "\t\t FILE:location(string):loop(int):width(int):height(int):GstFormatIdentifier(int):framerate(N/M)\n"
      "\t--encodersink=See the sink param setting\n"
      "\t--scheduler=SCREAM|MPRTP:MPRTPFRACTAL\n"
      "\t\t SCREAM\n"
      "\t\t MPRTP\n"
      "\t\t MPRTPFRACTAL:See the receiver param settings\n"
      "\t--sender=RTP|MPRTP\n"
      "\t\t RTP:dest_ip(string):dest_port(int)\n"
      "\t\t MPRTP:num_of_subflows(int):subflow_id(int):dest_ip(string):dest_port(int):...\n"

      "\nReceiver pipeline parameters:\n"
      "\t--receiver=RTP|MPRTP\n"
      "\t\t RTP:dest_ip(string):dest_port(int)\n"
      "\t\t MPRTP:num_of_subflows(int):subflow_id(int):dest_port(int):...\n"
      "\t--sink=AUTOVIDEO|FILE|RAWPROXY\n"
      "\t\t AUTOVIDEO\n"
      "\t\t FILE:location\n"
      "\t\t RAWPROXY: NOT IMPLEMENTED YET\n"
      "\t--playouter=SCREAM|MPRTP|MPRTPFRACTAL\n"
      "\t\t SCREAM: NOT IMPLEMENTED YET\n"
      "\t\t MPRTP: NOT IMPLEMENTED YET\n"
      "\t\t MPRTPFRACTAL:See the sender param settings\n"


      "\nCommon parameters:\n"
      "\t--codec=VP8|THEORA\n"
      "\t--stat=sampling time (int):accumulation length (int):csv logging (int):touch-sync location (string)\n"
      "\t--packetlogsink=See the --sink param setting\n"
      "\t--statlogsink=See the --sink param setting\n"

      "\nExamples for sender pipeline:\n"
      "./snd_pipeline --source=TESTVIDEO --codec=VP8 --sender=RTP:10.0.0.6:5000\n"

      "\nExamples for receiver pipeline:\n"
      "./rcv_pipeline --receiver=RTP:5000 --codec=VP8 --sink=AUTOVIDEO\n"
      "\n\n\n"
  );
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
  gchar       *rcv_transfer_params = NULL;

  result->type = _compare_types(tokens[0], "TESTVIDEO", "RAWPROXY", "FILE", "V4L2", NULL);

  switch(result->type){
    case SOURCE_TYPE_TESTVIDEO:

      sprintf(result->to_string, "TestVideoSource");
      break;
    case SOURCE_TYPE_RAWPROXY:
      strcpy(result->rawproxy.width,      tokens[1]);
      strcpy(result->rawproxy.height,     tokens[2]);

      result->rawproxy.clock_rate = atoi(tokens[3]);
      setup_framerate(tokens[4], &result->rawproxy.framerate);
      rcv_transfer_params = g_strjoinv(":", tokens + 5);

      result->rawproxy.rcv_transfer_params = make_rcv_transfer_params(rcv_transfer_params);
      g_free(rcv_transfer_params);

      sprintf(result->to_string, "RawProxy, W:%s, H:%s, CR: %d, Fr: %d/%d Rcv: %s",
          result->rawproxy.width,
          result->rawproxy.height,
          result->rawproxy.clock_rate,
          result->rawproxy.framerate.numerator,
          result->rawproxy.framerate.divider,
          result->rawproxy.rcv_transfer_params->to_string
      );

      break;

    case SOURCE_TYPE_FILE:
      strcpy(result->file.location,   tokens[1]);

      result->file.loop = atoi(tokens[2]);

      strcpy(result->file.width,      tokens[3]);
      strcpy(result->file.height,     tokens[4]);

      result->file.format = atoi(tokens[5]);
      setup_framerate(tokens[6], &result->file.framerate);

      sprintf(result->to_string, "File Source: %s, loop: %d, W:%s, H:%s, Framerate: %d/%d, Format: %d",
          result->file.location,
          result->file.loop,
          result->file.width,
          result->file.height,
          result->file.framerate.numerator,
          result->file.framerate.divider,
          result->file.format
      );

      break;
    case SOURCE_TYPE_V4L2:

      sprintf(result->to_string, "V4L2 Source"
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

  result->sampling_time       = atoi(tokens[0]);
  result->accumulation_length = atoi(tokens[1]);
  result->csv_logging         = atoi(tokens[2]);
  strcpy(result->touched_sync,     tokens[3]);


  sprintf(result->to_string,
      "Sampling time: %dms, Acc. length: %dms, CSV logging: %d, Touched sync: %s, ",
      result->sampling_time,
      result->accumulation_length,
      result->csv_logging,
      result->touched_sync

  );

  return result;
}


TransferParams*  make_snd_transfer_params(gchar* params_rawstring)
{
  TransferParams *result = g_malloc0(sizeof(TransferParams));
  gchar          **tokens = g_strsplit(params_rawstring, ":", -1);
  SenderSubflow  *subflow;
  gint i, offset;
  gchar string[256];

  result->type = _compare_types(tokens[0], "RTP", "MPRTP", NULL);
  result->ref = 1;
  switch(result->type){
     case TRANSFER_TYPE_RTP:
       result->length = 1;
       subflow = g_malloc0(sizeof(SenderSubflow));
       strcpy(subflow->dest_ip, tokens[1]);
       subflow->dest_port = atoi(tokens[2]);
       result->subflows = g_slist_append(result->subflows, subflow);
       sprintf(result->to_string, "RTP, Dest.: %s:%d", subflow->dest_ip, subflow->dest_port);
       break;
     case TRANSFER_TYPE_MPRTP:
       result->length = atoi(tokens[1]);
       offset = 1;
       sprintf(result->to_string, "MPRTP, Subflows: ");
       for(i = 0; i < result->length; ++i){
         subflow = g_malloc0(sizeof(SenderSubflow));
         subflow->id = atoi(tokens[++offset]);
         strcpy(subflow->dest_ip, tokens[++offset]);
         subflow->dest_port = atoi(tokens[++offset]);
         result->subflows = g_slist_append(result->subflows, subflow);
         sprintf(string, "ID: %d, Dest.: %s:%d; ", subflow->id, subflow->dest_ip, subflow->dest_port);
         strcat(result->to_string, string);
       }
       break;
     default:
       g_print("Unrecognized type (%d) at make_snd_transfer_params \n", result->type);
       break;
   };

  return result;
}


TransferParams*  make_rcv_transfer_params(gchar* params_rawstring)
{
  TransferParams *result = g_malloc0(sizeof(TransferParams));
  gchar          **tokens = g_strsplit(params_rawstring, ":", -1);
  ReceiverSubflow  *subflow;
  gint i, offset;
  gchar string[256];

  result->type = _compare_types(tokens[0], "RTP", "MPRTP", NULL);
  result->ref = 1;
  switch(result->type){
     case TRANSFER_TYPE_RTP:
       result->length = 1;
       subflow = g_malloc0(sizeof(ReceiverSubflow));
       subflow->bound_port = atoi(tokens[1]);
       result->subflows = g_slist_append(result->subflows, subflow);
       sprintf(result->to_string, "RTP, Port: :%d", subflow->bound_port );
       break;
     case TRANSFER_TYPE_MPRTP:
       result->length = atoi(tokens[1]);
       offset = 1;
       sprintf(result->to_string, "MPRTP, Subflows: ");
       for(i = 0; i < result->length; ++i){
         subflow = g_malloc0(sizeof(ReceiverSubflow));
         subflow->id = atoi(tokens[++offset]);
         subflow->bound_port = atoi(tokens[++offset]);
         result->subflows = g_slist_append(result->subflows, subflow);
         sprintf(string, "ID: %d, Port: %d; ", subflow->id, subflow->bound_port);
         strcat(result->to_string, string);
       }
       break;
     default:
       g_print("Unrecognized type (%d) at make_snd_transfer_params \n", result->type);
       break;
   };

  return result;
}

TransferParams* transfer_params_ref(TransferParams* transfer_params)
{
  if(!transfer_params){
    return NULL;
  }
  ++transfer_params->ref;
  return transfer_params;
}

void transfer_params_unref(TransferParams *transfer_params)
{
  if(0 < --transfer_params->ref){
    return;
  }
  g_slist_free_full(transfer_params->subflows, g_free);
  g_free(transfer_params);
}


SchedulerParams*   make_scheduler_params(gchar* params_rawstring)
{
  SchedulerParams *result = g_malloc0(sizeof(SchedulerParams));
  gchar           **tokens = g_strsplit(params_rawstring, ":", -1);
  gchar           *rcv_transfer_params_string = NULL;

  result->type = _compare_types(tokens[0], "SCREAM", "MPRTP", "MPRTPFRACTAL", NULL);

  switch(result->type){
    case TRANSFER_CONTROLLER_TYPE_SCREAM:

      rcv_transfer_params_string = g_strjoinv(":", tokens + 1);//From the moment of rcv transfer params
      sprintf(result->to_string, "SCReAM ");
      break;
    case TRANSFER_CONTROLLER_TYPE_MPRTPRRACTAL:

      rcv_transfer_params_string = g_strjoinv(":", tokens + 1);//From the moment of rcv transfer params
      sprintf(result->to_string, "MPRTP-FRACTaL ");
      break;
    case TRANSFER_CONTROLLER_TYPE_MPRTP:
      sprintf(result->to_string, "MPRTP ");
      {
        gchar string[256];
        gint i,len,offset;
        gint32 snd_subflow_id;
        len = atoi(tokens[1]);
        offset = 1;
        memset(string, 0, 256);
        for(i = 0; i < len; ++i){
          snd_subflow_id = atoi(tokens[++offset]);
          result->mprtp.subflows[snd_subflow_id].target_bitrate = atoi(tokens[++offset]);
          sprintf(string, "Sub: %d, Target: %d; ", snd_subflow_id, result->mprtp.subflows[snd_subflow_id].target_bitrate);
          strcat(result->to_string, string);
        }
      }
      break;
    default:
      g_print("Unrecognized type (%d) at make_cc_sender_side_params \n", result->type);
      break;
  };
  if(rcv_transfer_params_string){
    result->rcv_transfer_params = make_rcv_transfer_params(rcv_transfer_params_string);
    strcat(result->to_string, result->rcv_transfer_params->to_string);
    g_free(rcv_transfer_params_string);
  }
  return result;
}

void free_scheduler_params(SchedulerParams *snd_packet_scheduler_params)
{
  if(!snd_packet_scheduler_params){
    return;
  }
  transfer_params_unref(snd_packet_scheduler_params->rcv_transfer_params);
  g_free(snd_packet_scheduler_params);
}



PlayouterParams*   make_playouter_params(gchar* params_rawstring)
{
  PlayouterParams *result = g_malloc0(sizeof(PlayouterParams));
  gchar              **tokens = g_strsplit(params_rawstring, ":", -1);
  gchar              *snd_transfer_params_string = NULL;

  result->type = _compare_types(tokens[0], "SCREAM", "MPRTP", "MPRTPFRACTAL", NULL);

  switch(result->type){
    case TRANSFER_CONTROLLER_TYPE_SCREAM:

      snd_transfer_params_string = g_strjoinv(":", tokens + 1);//From the moment of rcv transfer params
      sprintf(result->to_string, "SCReAM ");
      break;
    case TRANSFER_CONTROLLER_TYPE_MPRTP:
      sprintf(result->to_string, "MPRTP ");
      break;
    case TRANSFER_CONTROLLER_TYPE_MPRTPRRACTAL:
      {
        snd_transfer_params_string = g_strjoinv(":", tokens + 1);//From the moment of snd transfer params
        sprintf(result->to_string, "MPRTP-FRACTaL ");
      }
      break;
    default:
      g_print("Unrecognized type (%d) at make_cc_sender_side_params \n", result->type);
      break;
  };
  if(snd_transfer_params_string){
    result->snd_transfer_params = make_snd_transfer_params(snd_transfer_params_string);
    strcat(result->to_string, result->snd_transfer_params->to_string);
    g_free(snd_transfer_params_string);
  }
  return result;
}

void free_playouter_params(PlayouterParams *rcv_playouter_params)
{
  if(!rcv_playouter_params){
    return;
  }
  transfer_params_unref(rcv_playouter_params->snd_transfer_params);
  g_free(rcv_playouter_params);
}


SinkParams*     make_sink_params(gchar* params_rawstring)
{
  SinkParams* result = g_malloc0(sizeof(SinkParams));
  gchar       **tokens = g_strsplit(params_rawstring, ":", -1);

  result->type = _compare_types(tokens[0], "AUTOVIDEO", "RAWPROXY", "FILE", NULL);

  switch(result->type){
    case SINK_TYPE_AUTOVIDEO:
      sprintf(result->to_string, "AUTOVIDEO");
      break;
    case SINK_TYPE_RAWPROXY:
      sprintf(result->to_string, "RawProxy");
      break;
    case SINK_TYPE_FILE:
      strcpy(result->file.location, tokens[1]);
      sprintf(result->to_string, "File location %s", result->file.location);
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

  if(packetlogs_sink_params_rawstring){
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
      result->statlogs_sink_params ? result->statlogs_sink_params->to_string : NULL,
      result->packetlogs_sink_params ? result->packetlogs_sink_params->to_string : NULL
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

void on_fi_called(gpointer object, gpointer user_data){

}

Eventer* eventer_ctor(void)
{
  return g_malloc0(sizeof(Eventer));
}

Eventer* eventer_ref(Eventer* this){
  ++this->ref;
  return this;
}

void eventer_unref(Eventer* this)
{
  GSList* item = this->subscribers;
  if(0 < --this->ref){
    return;
  }
  g_slist_free_full(this->subscribers, g_free);
  g_free(this);
}

Eventer* make_eventer(const gchar* event_name)
{
  Eventer* this = eventer_ctor();
  strcpy(this->name, event_name);
  this->ref = 1;
  return this;
}

void eventer_add_subscriber_full(Eventer* this, subscriber subscriber_func, gpointer user_data)
{
  Subscriber* subscriber = g_malloc0(sizeof(subscriber));
  subscriber->subscriber_func = subscriber_func;
  subscriber->subscriber_obj     = user_data;

  this->subscribers = g_slist_prepend(this->subscribers, subscriber);
}

void eventer_add_subscriber(Eventer* this, Subscriber* subscriber)
{
  this->subscribers = g_slist_prepend(this->subscribers, subscriber);
}

static void _foreach_subscribers(Subscriber* subscriber, gpointer user_data){
  if(!subscriber->subscriber_func){
    g_print("WARN: No subscriber function found to call");
    return;
  }
  subscriber->subscriber_func(subscriber->subscriber_obj, user_data);
}

void eventer_do(Eventer* this, gpointer user_data)
{
  if(!this->subscribers){
    return;
  }
  g_slist_foreach(this->subscribers, (GFunc)_foreach_subscribers, user_data);
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
  GstBin* debugBin     = gst_bin_new("debugBin");
  GstElement *identity      = gst_element_factory_make ("identity", NULL);

  gst_bin_add_many(debugBin, element, identity, NULL);

  gst_element_link_pads( element, "src", identity, "sink");

  g_object_set (identity, "dump", TRUE, NULL);

  setup_ghost_src(identity, debugBin);
  if(GST_IS_PAD(gst_element_get_static_pad(element, "sink"))){
    setup_ghost_sink(element, debugBin);
  }

  return GST_ELEMENT(debugBin);
}

GstElement* debug_by_sink(GstElement*element)
{
  GstBin* debugBin     = gst_bin_new("debugBin");
  GstElement *identity      = gst_element_factory_make ("identity", NULL);

  gst_bin_add_many(debugBin, identity, element, NULL);

  gst_element_link_pads(identity, "src", element, "sink");

  g_object_set (identity, "dump", TRUE, NULL);

  setup_ghost_sink(identity, debugBin);
  if(GST_IS_PAD(gst_element_get_static_pad(element, "src"))){
    setup_ghost_src(element, debugBin);
  }

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


static ObjectsHolderItem* objects_holder_item_ctor(void){
  ObjectsHolderItem* this = g_malloc0(sizeof(ObjectsHolderItem));
  return this;
}

static void object_holder_item_dtor(ObjectsHolderItem* item){
  if(!item){
    return;
  }
  item->dtor(item->object);
  g_free(item);
}

ObjectsHolderItem* make_object_item_holder(gpointer object, GDestroyNotify dtor){
  ObjectsHolderItem* this = objects_holder_item_ctor();
  this->object = object;
  this->dtor = dtor;
  return this;
}

ObjectsHolder* objects_holder_ctor(void){
  ObjectsHolder* this = g_malloc0(sizeof(ObjectsHolder));
  this->holders = NULL;
  return this;
}

void object_holder_dtor(ObjectsHolder* this){
  g_slist_free_full(this->holders, object_holder_item_dtor);
}

void objects_holder_add(ObjectsHolder* this, gpointer object, GDestroyNotify dtor)
{
  this->holders = g_slist_prepend(this->holders, make_object_item_holder(object, dtor) );
}



static Pipeline* pipeline = NULL;
Pipeline* get_pipeline(void)
{
  if(pipeline){
    return pipeline;
  }
  pipeline = g_malloc0(sizeof(Pipeline));
  pipeline->on_assembled   = make_eventer("on-assembled");
  pipeline->on_playing     = make_eventer("on-playing");
  pipeline->on_destroy     = make_eventer("on-destroy");
  pipeline->on_target_bitrate_change = make_eventer("on-target-bitrate-change");

  pipeline->objects_holder = objects_holder_ctor();
  pipeline->events_to_eventers = g_hash_table_new_full(g_str_hash,
      g_str_equal, g_free, (GDestroyNotify)eventer_unref);

  return pipeline;
}

void pipeline_dtor(void)
{
  if(!pipeline){
    return;
  }
  eventer_unref(pipeline->on_assembled);
  eventer_unref(pipeline->on_playing);
  eventer_unref(pipeline->on_destroy);
  eventer_unref(pipeline->on_target_bitrate_change);

  g_slist_free_full(pipeline->objects, object_holder_dtor);
  g_hash_table_destroy(pipeline->events_to_eventers);

  g_free(pipeline);
  pipeline = NULL;
}

void pipeline_add_eventer(const gchar* event_name, Eventer* eventer)
{
  Pipeline* this = get_pipeline();
  gchar* event_copied_name;
  if(g_hash_table_lookup(this->events_to_eventers, event_name) != NULL){
    g_print("Event %s is already registered in the eventer", event_name);
    return;
  }
  event_copied_name = g_malloc(256);
  g_print("Add eventer for event %s\n", event_name);
  strcpy(event_copied_name, event_name);
  g_hash_table_insert(this->events_to_eventers, event_copied_name, eventer_ref(eventer));
}

void pipeline_add_subscriber_full(const gchar* event_name, subscriber subscriber_func, gpointer user_data)
{
  Pipeline* this = get_pipeline();
  Eventer* eventer;

  if((eventer = g_hash_table_lookup(this->events_to_eventers, event_name)) == NULL){
    eventer = make_eventer(event_name);
    pipeline_add_eventer(event_name, eventer);
    eventer_unref(eventer);
    return;
  }

  eventer_add_subscriber_full(eventer, subscriber_func, user_data);

}

void pipeline_add_subscriber(const gchar* event_name, Subscriber* subscriber)
{
  Pipeline* this = get_pipeline();
  Eventer* eventer;

  if((eventer = g_hash_table_lookup(this->events_to_eventers, event_name)) == NULL){
    g_print("Not found any eventer for event %s, we created one\n", event_name);
    eventer = make_eventer(event_name);
    pipeline_add_eventer(event_name, eventer);
    eventer_unref(eventer);
  }

  g_print("subscriber added to pipeline for event %s\n", event_name);
  eventer_add_subscriber(eventer, subscriber);

}

void pipeline_firing_event(const gchar* event_name, gpointer data)
{
  Pipeline* this = get_pipeline();
  Eventer* eventer;
  if((eventer = g_hash_table_lookup(this->events_to_eventers, event_name)) == NULL){
    g_print("Requested event %s not bounded to any Eventer\n", event_name);
    return;
  }
  g_print("Firing event %s\n", event_name);
  eventer_do(eventer, data);
}


void pipeline_add_object(gpointer object, GDestroyNotify dtor)
{
  Pipeline*     this = get_pipeline();
  objects_holder_add(this->objects_holder, object, dtor);
}
