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

GstElement* make_rtpstatmaker_element(StatParams* stat_params)
{
  GstElement* rtpstatmaker = gst_element_factory_make ("rtpstatmaker2", NULL);

  g_object_set(rtpstatmaker,
      "default-logfile-location",     stat_params->logfile_path,
      "touch-sync-location",          stat_params->touched_sync,
      "mprtp-ext-header-id",          stat_params->mprtp_ext_header_id,
      NULL);

  return rtpstatmaker;
}



