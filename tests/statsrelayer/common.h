#ifndef TESTS_STATSRELAYER_COMMON_H_
#define TESTS_STATSRELAYER_COMMON_H_
#include <gst/gst.h>
#include <glib.h>

#ifdef __WIN32__

#define PACKED
#pragma pack(push,1)


#else

#define PACKED __attribute__ ((__packed__))

#endif

typedef struct PACKED _RTPStatPacket
{
  guint64              tracked_ntp;
  guint16              seq_num;
  guint32              ssrc;
  guint8               subflow_id;
  guint16              subflow_seq;

  guint8               marker : 1;
  guint8               payload_type : 7;
  guint32              timestamp;

  guint                header_size;
  guint                payload_size;

  guint16              protect_begin;
  guint16              protect_end;
}RTPStatPacket;

typedef struct {
  gpointer subject;
  guint length;
}WriteItem;

typedef void (*PushCb)(gpointer udata, gpointer item);
typedef gpointer (*PullCb)(gpointer this);

typedef struct {
  PushCb push_cb;
  gpointer udata;
}PushPort;

PushPort* make_pushport(PushCb push_cb, gpointer udata);
void pushport_send(PushPort* this, gpointer item);
#define pushport_dtor(target) g_free(target)

typedef void (*ProcessCb)(gpointer udata);
typedef struct{
  ProcessCb process;
  gpointer udata;
}Process;

Process* make_process(ProcessCb process, gpointer udata);
void process_call(Process* this);
#define process_dtor(target) g_free(target)

gint32 common_assign_string_to_int(gchar* type, ...);

#ifdef __WIN32__

#pragma pack(pop)
#undef PACKED

#else

#undef PACKED

#endif

#endif /* TESTS_STATSRELAYER_COMMON_H_ */
