/*
 * refctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef MPRTP_LOGGER_H_
#define MPRTP_LOGGER_H_

#include <gst/gst.h>
#include "messenger.h"

typedef struct _MPRTPLogger MPRTPLogger;
typedef struct _MPRTPLoggerClass MPRTPLoggerClass;

#define MPRTPLOGGER_TYPE             (mprtp_logger_get_type())
#define MPRTPLOGGER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),MPRTPLOGGER_TYPE,MPRTPLogger))
#define MPRTPLOGGER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),MPRTPLOGGER_TYPE,MPRTPLoggerClass))
#define MPRTPLOGGER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),MPRTPLOGGER_TYPE))
#define MPRTPLOGGER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),MPRTPLOGGER_TYPE))
#define MPRTPLOGGER_CAST(src)        ((MPRTPLogger *)(src))

typedef void (*loggerfnc)(const gchar* format, ...);
typedef void (*loggerfnc_obj)(gpointer obj, const gchar* format, ...);


struct _MPRTPLogger
{
  GObject           object;
  GstClock*         sysclock;
  GstClockTime      made;

  GstTask*          process;
  GRecMutex         process_mutex;

  GHashTable*       memory_consumptions;

  gchar             path[255];
  gboolean          enabled;

  GString*          collector_string;
  gchar             collector_filename[255];

  Messenger*        messenger;
};

struct _MPRTPLoggerClass{
  GObjectClass parent_class;
};



#define mprtp_slice_new0(type) mprtp_slice_alloc(#type, sizeof(type))

//#define mprtp_log_slice_new0(type)  g_slice_new0(type)

#define mprtp_slice_free(type, mem) mprtp_slice_dealloc(#type, sizeof(type), mem)

//#define mprtp_log_slice_free(type, mem)  g_slice_free(type, mem)


#define mprtp_malloc(bytenum) g_malloc0(bytenum)
//gpointer mprtp_malloc(gsize bytenum);

#define mprtp_free(bytenum) g_free(bytenum)
//void mprtp_free(gpointer ptr);

void init_mprtp_logger(void);
gpointer mprtp_slice_alloc(const gchar* type_name, gsize size);
void mprtp_slice_dealloc(const gchar* type_name, gsize size, gpointer memptr);
void mprtp_logger_add_logging_fnc(void(*logging_fnc)(gpointer,gchar*),gpointer data, const gchar* filename);
void mprtp_logger_add_memory_consumption(const gchar *type_name, gsize size);
void mprtp_logger_rem_memory_consumption(const gchar *type_name, gsize size);
void mprtp_logger_print_memory_consumption(loggerfnc fnc);
void mprtp_logger_print_obj_memory_consumption(loggerfnc_obj fnc, gpointer udata);
void mprtp_logger_set_state(gboolean enabled);
void mprtp_logger_set_target_directory(const gchar *path);
void mprtp_logger_set_system_command(const gchar *command);
void mprtp_logger(const gchar *filename, const gchar * format, ...);
void mprtp_log_one(const gchar *filename, const gchar * format, ...);

GType mprtp_logger_get_type (void);
#endif /* MPRTP_LOGGER_H_ */
