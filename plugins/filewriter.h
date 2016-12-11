/*
 * filewriter.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef FILEWRITER_H_
#define FILEWRITER_H_

#include <gst/gst.h>
#include "rtpfecbuffer.h"
#include "sndsubflows.h"
#include "mediator.h"
#include "recycle.h"
#include "messenger.h"

typedef struct _FileWriter FileWriter;
typedef struct _FileWriterClass FileWriterClass;

#define FILEWRITER_TYPE             (filewriter_get_type())
#define FILEWRITER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),FILEWRITER_TYPE,FileWriter))
#define FILEWRITER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),FILEWRITER_TYPE,FileWriterClass))
#define FILEWRITER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),FILEWRITER_TYPE))
#define FILEWRITER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),FILEWRITER_TYPE))
#define FILEWRITER_CAST(src)        ((FileWriter *)(src))

typedef struct{
  guint              ref;
  GstBuffer*         fecbuffer;
  gint32             payload_size;
  guint8             subflow_id;
}FileWriterResponse;

struct _FileWriter
{
  GObject                    object;
  GstClock*                  sysclock;
  GstClockTime               made;

  gint32                     max_protection_num;
  guint16                    seq_num;
  guint8                     payload_type;
  guint8                     mprtp_ext_header_id;

  GQueue*                    bitstrings;
  GQueue*                    pending_responses;

  GstTask*                   thread;
  GRecMutex                  thread_mutex;
  Messenger*                 messenger;
};



struct _FileWriterClass{
  GObjectClass parent_class;

};


GType filewriter_get_type (void);
FileWriter *make_filewriter(const gchar* path, gboolean append);

void filewriter_add_rtpbuffer(FileWriter *this, const gchar* format, ...);

#endif /* FILEWRITER_H_ */
