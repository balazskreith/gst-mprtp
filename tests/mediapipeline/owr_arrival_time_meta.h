#ifndef __OWR_ARRIVAL_TIME_META_H__
#define __OWR_ARRIVAL_TIME_META_H__

#include <gst/gst.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

G_BEGIN_DECLS

#define OWR_UNUSED(x) (void)x
#define OWR_ARRIVAL_TIME_META_API_TYPE  (_owr_arrival_time_meta_api_get_type())
#define OWR_ARRIVAL_TIME_META_INFO  (_owr_arrival_time_meta_get_info())
#define _owr_buffer_get_arrival_time_meta(b) ((OwrArrivalTimeMeta *)gst_buffer_get_meta((b), OWR_ARRIVAL_TIME_META_API_TYPE))
#define GST_RTCP_RTPFB_TYPE_SCREAM 18

typedef struct _OwrArrivalTimeMeta OwrArrivalTimeMeta;

struct _OwrArrivalTimeMeta {
    GstMeta meta;

    guint64 arrival_time;
};

GType _owr_arrival_time_meta_api_get_type(void);
const GstMetaInfo * _owr_arrival_time_meta_get_info(void);
OwrArrivalTimeMeta * _owr_buffer_add_arrival_time_meta(GstBuffer *buffer, guint64 arrival_time);

G_END_DECLS

gboolean _owr_arrival_time_meta_init(GstMeta *meta, gpointer params, GstBuffer *buffer);
gboolean _owr_arrival_time_meta_transform(GstBuffer *transbuf, GstMeta *meta, GstBuffer *buffer, GQuark type, gpointer data);


#endif /*__OWR_ARRIVAL_TIME_META_H__*/
