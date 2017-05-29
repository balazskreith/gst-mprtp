/*
 * refctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef TIMESTAMP_H_
#define TIMESTAMP_H_

#include <gst/gst.h>

#include "mprtputils.h"
#include "notifier.h"

typedef struct _Timestamp Timestamp;
typedef struct _TimestampClass TimestampClass;

#define TIMESTAMP_TYPE             (timestamp_get_type())
#define TIMESTAMP(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),TIMESTAMP_TYPE,Timestamp))
#define TIMESTAMP_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),TIMESTAMP_TYPE,TimestampClass))
#define TIMESTAMP_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),TIMESTAMP_TYPE))
#define TIMESTAMP_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),TIMESTAMP_TYPE))
#define TIMESTAMP_CAST(src)        ((Timestamp *)(src))


struct _Timestamp
{
  GObject          object;
  GstClock*        sysclock;
  GstClockTime     made;
  guint32          offset;
  guint32          clockrate;
};

struct _TimestampClass{
  GObjectClass parent_class;
};


GType timestamp_get_type (void);

Timestamp *make_timestamp(guint32 clockrate);
void timestamp_set_clockrate(Timestamp* this, guint32 clockrate);
guint32 timestmap_get(Timestamp* this);



#endif /* TIMESTAMP_H_ */
