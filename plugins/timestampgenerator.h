/*
 * refctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef TIMESTAMPGENERATOR_H_
#define TIMESTAMPGENERATOR_H_

#include <gst/gst.h>

#include "mprtputils.h"
#include "notifier.h"

typedef struct _TimestampGenerator TimestampGenerator;
typedef struct _TimestampGeneratorClass TimestampGeneratorClass;

#define TIMESTAMPGENERATOR_TYPE             (timestamp_generator_get_type())
#define TIMESTAMPGENERATOR(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),TIMESTAMPGENERATOR_TYPE,TimestampGenerator))
#define TIMESTAMPGENERATOR_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),TIMESTAMPGENERATOR_TYPE,TimestampGeneratorClass))
#define TIMESTAMPGENERATOR_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),TIMESTAMPGENERATOR_TYPE))
#define TIMESTAMPGENERATOR_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),TIMESTAMPGENERATOR_TYPE))
#define TIMESTAMPGENERATOR_CAST(src)        ((TimestampGenerator *)(src))


struct _TimestampGenerator
{
  GObject          object;
  GstClock*        sysclock;
  GstClockTime     made;
  guint32          offset;
  guint32          clockrate;
};

struct _TimestampGeneratorClass{
  GObjectClass parent_class;
};


GType timestamp_generator_get_type (void);

TimestampGenerator *make_timestamp_generator(guint32 clockrate);
void timestamp_generator_set_clockrate(TimestampGenerator* this, guint32 clockrate);
guint32 timestamp_generator_get_ts(TimestampGenerator* this);



#endif /* TIMESTAMPGENERATOR_H_ */
