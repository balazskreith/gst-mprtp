/*
 * refctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef RTPFECBUFFER_H_
#define RTPFECBUFFER_H_

#include <gst/gst.h>


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
} GstRTPFECHeader; //20 bytes

STATIC_ASSERT (sizeof (GstRTPFECHeader) == 20, "GstRTPFECHeader size is not ok");

void rtpfecbuffer_get_inistring(GstBuffer *buf, guint8* result);
guint16 rtpfecbuffer_get_sn_base(GstBuffer *buf);
#endif /* RTPFECBUFFER_H_ */
