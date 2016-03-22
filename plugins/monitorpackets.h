/*
 * monitorpackets.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef MONITORPACKETS_H_
#define MONITORPACKETS_H_

#ifdef __WIN32__

#define PACKED
#pragma pack(push,1)


#else

#define PACKED __attribute__ ((__packed__))

#endif

//---------------------- STATIC ASSERT ----------------------------------
//Source: http://www.pixelbeat.org/programming/gcc/static_assert.html
#define ASSERT_CONCAT_(a, b) a##b
#define ASSERT_CONCAT(a, b) ASSERT_CONCAT_(a, b)
/* These can't be used after statements in c89. */
#ifdef __COUNTER__
#define STATIC_ASSERT(e,m) \
    ;enum { ASSERT_CONCAT(static_assert_, __COUNTER__) = 1/(!!(e)) }
#else
  /* This can't be used twice on the same line so ensure if using in headers
   * that the headers are not included twice (by wrapping in #ifndef...#endif)
   * Note it doesn't cause an issue when used on same line of separate modules
   * compiled with gcc -combine -fwhole-program.  */
#define STATIC_ASSERT(e,m) \
    ;enum { ASSERT_CONCAT(assert_line_, __LINE__) = 1/(!!(e)) }
#endif
//---------------------- STATIC ASSERT END----------------------------------

#include <gst/gst.h>
#include "bintree.h"

typedef struct _MonitorPackets MonitorPackets;
typedef struct _MonitorPacketsClass MonitorPacketsClass;

#define MONITORPACKETS_TYPE             (monitorpackets_get_type())
#define MONITORPACKETS(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),MONITORPACKETS_TYPE,MonitorPackets))
#define MONITORPACKETS_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),MONITORPACKETS_TYPE,MonitorPacketsClass))
#define MONITORPACKETS_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),MONITORPACKETS_TYPE))
#define MONITORPACKETS_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),MONITORPACKETS_TYPE))
#define MONITORPACKETS_CAST(src)        ((MonitorPackets *)(src))

typedef struct _MonitorPacketsNode MonitorPacketsNode;

#define MONITORPACKETS_MAX_LENGTH 1400

//Note RTP FEC is based on: https://tools.ietf.org/html/draft-ietf-payload-flexible-fec-scheme-01
typedef struct PACKED _GstRTPFECHeader
{
  //first byte
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  unsigned int         CC:4;        /* CC field */
  unsigned int         X:1;         /* X field */
  unsigned int         P:1;         /* padding flag */
  unsigned int         R:1;         /* R field */
  unsigned int         F:1;         /* F field */
#elif G_BYTE_ORDER == G_BIG_ENDIAN
  unsigned int         F:1;         /* F field */
  unsigned int         R:1;         /* F field */
  unsigned int         P:1;         /* padding flag */
  unsigned int         X:1;         /* X field */
  unsigned int         CC:4;        /* CC field*/
#else
#error "G_BYTE_ORDER should be big or little endian."
#endif
  //second byte
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  //second byte
  unsigned int         PT:7;     /* PT field */
  unsigned int         M:1;       /* M field */
#elif G_BYTE_ORDER == G_BIG_ENDIAN
  //second byte
  unsigned int         M:1;         /* M field */
  unsigned int         PT:7;       /* PT field */
#else
#error "G_BYTE_ORDER should be big or little endian."
#endif
  guint16              length_recovery;      /* length of the recovery */
  guint32              TS;                   /* TS to recover */
  guint32              SSRC_Count:8;
  guint32              reserved:24;
  guint32              ssrc;
  guint16              sn_base;
  guint8               M_MASK;
  guint8               N_MASK;
#endif
} GstRTPFECHeader; //20 bytes

STATIC_ASSERT (sizeof (GstRTPFECHeader) == 20, "GstRTPFECHeader size is not ok");

struct _MonitorPackets
{
  GObject                  object;
  GRWLock                  rwmutex;
  GQueue*                  queue;
  guint8                   fec_payload_type;
  guint16                  monitor_seq;
  guint16                  monitor_cycle;
  gint                     counter;
  GstClock*                sysclock;

  guint16                  max_protected_packets_num;
  guint16                  protected_packets_num;
  guint8                   produced_fec_packet[MONITORPACKETS_MAX_LENGTH];
  guint16                  produced_fec_packet_length;
  gint32                   produced_sn_base;
  guint8                   consumed_rtp_packets[MONITORPACKETS_MAX_LENGTH];

};

struct _MonitorPacketsClass{
  GObjectClass parent_class;

};

GType monitorpackets_get_type (void);
MonitorPackets *make_monitorpackets(void);
void monitorpackets_reset(MonitorPackets *this);
void monitorpackets_set_fec_payload_type(MonitorPackets *this, guint8 payload_type);
void monitorpackets_add_outgoing_rtp_packet(MonitorPackets* this, GstBuffer* buf);
void monitorpackets_add_incoming_rtp_packet(MonitorPackets *this, GstBuffer *buf);
GstBuffer *monitorpackets_process_FEC_packet(MonitorPackets *this, GstBuffer *rtpbuf);
GstBuffer * monitorpackets_provide_FEC_packet(MonitorPackets *this, guint8 mprtp_ext_header_id, guint8 subflow_id);


#ifdef __WIN32__

#pragma pack(pop)
#undef PACKED

#else

#undef PACKED

#endif


#endif /* MONITORPACKETS_H_ */
