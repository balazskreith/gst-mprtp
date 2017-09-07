#include <gst/gst.h>
#include "owr_arrival_time_meta.h"


GType _owr_arrival_time_meta_api_get_type(void)
{
    static volatile GType type;
    static const gchar *tags[] = {"arrival time", NULL};
    if (g_once_init_enter(&type)) {
        GType _type = gst_meta_api_type_register("OwrArrivalTimeMetaAPI", tags);
        g_once_init_leave(&type, _type);
    }
    return type;
}

const GstMetaInfo *_owr_arrival_time_meta_get_info(void)
{
    static const GstMetaInfo *arrival_time_meta_info = NULL;

    if (g_once_init_enter(&arrival_time_meta_info)) {
        const GstMetaInfo *meta = gst_meta_register(OWR_ARRIVAL_TIME_META_API_TYPE,
            "OwrArrivalTimeMeta", sizeof(OwrArrivalTimeMeta), _owr_arrival_time_meta_init,
            (GstMetaFreeFunction)NULL, _owr_arrival_time_meta_transform);
        g_once_init_leave(&arrival_time_meta_info, meta);
    }
    return arrival_time_meta_info;
}


gboolean _owr_arrival_time_meta_init(GstMeta *meta, gpointer params, GstBuffer *buffer)
{
    OwrArrivalTimeMeta *at_meta = (OwrArrivalTimeMeta *)meta;
    OWR_UNUSED(buffer);

    if (params)
        at_meta->arrival_time = *((guint64 *) params);
    else
        at_meta->arrival_time = GST_CLOCK_TIME_NONE;

    return TRUE;
}


gboolean _owr_arrival_time_meta_transform(GstBuffer *transbuf, GstMeta *meta, GstBuffer *buffer, GQuark type, gpointer data)
{
    OwrArrivalTimeMeta *at_meta = (OwrArrivalTimeMeta *)meta;
    _owr_buffer_add_arrival_time_meta(transbuf, at_meta->arrival_time);
    OWR_UNUSED(buffer);
    OWR_UNUSED(type);
    OWR_UNUSED(data);
    return TRUE;
}


OwrArrivalTimeMeta * _owr_buffer_add_arrival_time_meta(GstBuffer *buffer, guint64 arrival_time)
{
    OwrArrivalTimeMeta *at_meta = NULL;

    g_return_val_if_fail(GST_IS_BUFFER(buffer), NULL);
    at_meta = (OwrArrivalTimeMeta *) gst_buffer_add_meta(buffer, OWR_ARRIVAL_TIME_META_INFO, &arrival_time);

    return at_meta;
}

