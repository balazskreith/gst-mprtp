#include "rtpstatmaker.h"
#include "sink.h"

static int _instance_counter = 0;

RTPStatMaker* rtpstatmaker_ctor(void){
  RTPStatMaker* this = g_malloc0(sizeof(RTPStatMaker));
  this->holder = objects_holder_ctor();
  sprintf(this->bin_name, "RTPStatMakerBin_%d", _instance_counter++);
  return this;
}

void rtpstatmaker_dtor(RTPStatMaker* this){
  if(!this){
    return;
  }
  object_holder_dtor(this->holder);
  g_free(this);
}

RTPStatMaker* make_rtpstatmaker(StatParamsTuple *params_tuple)
{
  RTPStatMaker* this = rtpstatmaker_ctor();

  GstElement*   statBin = gst_bin_new(this->bin_name);

  StatParams*   stat_params            = params_tuple->stat_params;
  SinkParams*   statlogs_sink_params   = params_tuple->statlogs_sink_params;
  SinkParams*   packetlogs_sink_params = params_tuple->packetlogs_sink_params;

  GstElement*   rtpstatmaker = make_rtpstatmaker_element(stat_params);
  GstElement*   statsink;
  GstElement*   packetlogsink;

  gst_bin_add(statBin, rtpstatmaker);

  if(statlogs_sink_params){
    if(statlogs_sink_params->type == SINK_TYPE_FILE){
      g_object_set(rtpstatmaker, "statslog-location", statlogs_sink_params->file.location, NULL);
    }else if(statlogs_sink_params->type == SINK_TYPE_MULTIFILE){
      GSList* it;
      SinkFileItem* sink_file_item;
      g_object_set(rtpstatmaker, "mprtp-ext-header-id", 3, NULL);
      for(it = statlogs_sink_params->multifile.items; it; it = it->next){
        sink_file_item = it->data;
        g_object_set(rtpstatmaker, "next-id", sink_file_item->id, NULL);
        g_object_set(rtpstatmaker, "statslog-location", sink_file_item->path, NULL);
      }
    }else{
      g_print("Testing pipeline at the moment not support other than file writing for Statistical collection");
    }
  }

  if(packetlogs_sink_params){
    if(packetlogs_sink_params->type == SINK_TYPE_FILE){
      g_object_set(rtpstatmaker, "packetslog-location", packetlogs_sink_params->file.location, NULL);
    }else if(packetlogs_sink_params->type == SINK_TYPE_MULTIFILE){
      GSList* it;
      SinkFileItem* sink_file_item;
      for(it = packetlogs_sink_params->multifile.items; it; it = it->next){
        sink_file_item = it->data;
        g_object_set(rtpstatmaker, "next-id", sink_file_item->id, NULL);
        g_object_set(rtpstatmaker, "packetslog-location", sink_file_item->path, NULL);
      }
    }else{
      g_print("Testing pipeline at the moment not support other than file writing for Statistical collection");
    }
  }

  setup_ghost_src(rtpstatmaker, statBin);
  setup_ghost_sink(rtpstatmaker, statBin);

  setup_ghost_src_by_padnames(rtpstatmaker,  "packet_src",  statBin, "packet_src");
  setup_ghost_sink_by_padnames(rtpstatmaker, "packet_sink", statBin, "packet_sink");
  this->element = GST_ELEMENT(statBin);

  return this;
}

GstElement* make_rtpstatmaker_element(StatParams* stat_params)
{
  GstElement* rtpstatmaker = gst_element_factory_make ("rtpstatmaker2", NULL);

  g_object_set(rtpstatmaker,
      "sampling-time",       stat_params->sampling_time,
      "accumulation-length", stat_params->accumulation_length,
      "fec-payload-type",    126,
      "csv-logging",         stat_params->csv_logging,
      "touch-sync-location", stat_params->touched_sync,
      NULL);

  //  if(mprtp){
  //    g_object_set(rtpstatmaker,
  //        "mprtp-ext-header-id", 3,
  //        NULL);
  //  }

  return rtpstatmaker;
}



