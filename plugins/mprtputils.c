#include "mprtputils.h"
#include <string.h>

static void _test_do_bitxor(void);

void do_bitxor(guint8* to, guint8* from, gint32 length) {
  gint i;
  for(i=8; i <= length; i+=8){
    *(guint64*)(to+i-8) ^= *(guint64*)(from+i-8);
  }

  if (i == length) {
    return;
  }
  i-=8;

  if (i+4 <= length) {
    *(guint32*)(to+i) ^= *(guint32*)(from+i);
    i+=4;
  }

  if (i+2 <= length) {
    *(guint16*)(to+i) ^= *(guint16*)(from+i);
    i+=2;
  }
  if (i+1 <= length) {
    *(to + i) ^= *(from + i);
  }
}

void gst_rtp_buffer_set_mprtp_extension(GstRTPBuffer* rtp, guint8 ext_header_id, guint8 subflow_id, guint16 subflow_seq)
{
  MPRTPSubflowHeaderExtension mprtp_ext;
  mprtp_ext.id = subflow_id;
  mprtp_ext.seq = subflow_seq;
  DISABLE_LINE _test_do_bitxor();
  gst_rtp_buffer_add_extension_onebyte_header (rtp, ext_header_id, (gpointer) &mprtp_ext, sizeof (mprtp_ext));
}

void gst_rtp_buffer_get_mprtp_extension(GstRTPBuffer* rtp, guint8 ext_header_id, guint8 *subflow_id, guint16 *subflow_seq)
{
  gpointer pointer = NULL;
  guint size;
  MPRTPSubflowHeaderExtension *subflow_infos;

  gst_rtp_buffer_get_extension_onebyte_header(rtp, ext_header_id, 0, &pointer, &size);
  subflow_infos = (MPRTPSubflowHeaderExtension *) pointer;
  if(subflow_id){
    *subflow_id = subflow_infos->id;
  }

  if(subflow_seq){
    *subflow_seq = subflow_infos->seq;
  }
}

guint16 subflowseqtracker_increase(SubflowSeqTrack *subseqtracker)
{
  if(++subseqtracker->seqence_num == 0){
    ++subseqtracker->cycle_num;
  }
  return subseqtracker->seqence_num;
}

void gst_rtp_buffer_set_abs_time_extension(GstRTPBuffer* rtp, guint8 abs_time_ext_header_id)
{
    RTPAbsTimeExtension data;
    guint32 time;

    //Absolute sending time +0x83AA7E80
    //https://tools.ietf.org/html/draft-alvestrand-rmcat-remb-03
    time = (NTP_NOW >> 14) & 0x00ffffff;
    memcpy (&data, &time, 3);
    gst_rtp_buffer_add_extension_onebyte_header (rtp, abs_time_ext_header_id, (gpointer) &data, sizeof (data));
}

guint64 gst_rtp_buffer_get_abs_time_extension(GstRTPBuffer* rtp, guint8 abs_time_ext_header_id)
{
  gpointer pointer = NULL;
  guint    size;
  guint32  rcv_chunk = (NTP_NOW >> 14) & 0x00ffffff;
  guint64  ntp_base = NTP_NOW;
  guint64  snd_time = 0;

  gst_rtp_buffer_get_extension_onebyte_header(rtp, abs_time_ext_header_id, 0, &pointer, &size);
  memcpy (&snd_time, pointer, 3);
  if(rcv_chunk < snd_time){
      ntp_base-=0x0000004000000000UL;
  }
  snd_time <<= 14;
  snd_time |=  (ntp_base & 0xFFFFFFC000000000UL);

  return snd_time;
}

guint64 gst_rtp_buffer_get_abs_time_extension_new(GstRTPBuffer* rtp, guint8 abs_time_ext_header_id)
{
  gpointer pointer = NULL;
  guint    size;
  guint64  result = 0;

  gst_rtp_buffer_get_extension_onebyte_header(rtp, abs_time_ext_header_id, 0, &pointer, &size);
  memcpy (&result, pointer, 3);
  return result << 14;
}

gboolean gst_buffer_is_mprtp(GstBuffer* buffer, guint8 mprtp_ext_header_id)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  gboolean result;
  gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp);
  result = gst_rtp_buffer_is_mprtp(&rtp, mprtp_ext_header_id);
  gst_rtp_buffer_unmap(&rtp);
  return result;
}

gboolean gst_rtp_buffer_is_mprtp(GstRTPBuffer* rtp, guint8 mprtp_ext_header_id)
{
  gpointer pointer = NULL;
  guint size;
  return gst_rtp_buffer_get_extension_onebyte_header(rtp, mprtp_ext_header_id, 0, &pointer, &size);
}

gboolean gst_rtp_buffer_is_fectype(GstRTPBuffer* rtp, guint8 fec_payload_type)
{
  return gst_rtp_buffer_get_payload_type(rtp) == fec_payload_type;
}

static void _test_do_bitxor(void) {
  {
    guint8 a[] = {0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10};
    guint8 b[] = {0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01};
    do_bitxor(a, b, 8);
    g_print("test bitxor 8*8bits expected: 1111... value: %X%X%X%X%X%X%X%X\n",
        a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
  }

  {
    guint8 a[] = {0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01};
    guint8 b[] = {0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10};
    do_bitxor(a, b, 9);
    g_print("test bitxor 8*9bits expected: 1111... value: %X%X%X%X%X%X%X%X%X\n",
        a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8]);
  }

  {
    guint8 a[] = {0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10};
    guint8 b[] = {0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01};
    do_bitxor(a, b, 10);
    g_print("test bitxor 8*10bits expected: 1111... value: %X%X%X%X%X%X%X%X%X%X\n",
        a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9]);
  }

  {
    guint8 a[] = {0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01};
    guint8 b[] = {0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10};
    do_bitxor(a, b, 11);
    g_print("test bitxor 8*11bits expected: 1111... value: %X%X%X%X%X%X%X%X%X%X%X\n",
        a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10]);
  }

  {
    guint8 a[] = {0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10};
    guint8 b[] = {0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01};
    do_bitxor(a, b, 12);
    g_print("test bitxor 8*12bits expected: 1111... value: %X%X%X%X%X%X%X%X%X%X%X%X\n",
        a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11]);
  }

  {
    guint8 a[] = {0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01};
    guint8 b[] = {0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10};
    do_bitxor(a, b, 13);
    g_print("test bitxor 8*13bits expected: 1111... value: %X%X%X%X%X%X%X%X%X%X%X%X%X\n",
        a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12]);
  }

  {
    guint8 a[] = {0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10};
    guint8 b[] = {0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01};
    do_bitxor(a, b, 14);
    g_print("test bitxor 8*13bits expected: 1111... value: %X%X%X%X%X%X%X%X%X%X%X%X%X%X\n",
        a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12], a[13]);
  }

  {
    guint8 a[] = {0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01};
    guint8 b[] = {0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10};
    do_bitxor(a, b, 15);
    g_print("test bitxor 8*13bits expected: 1111... value: %X%X%X%X%X%X%X%X%X%X%X%X%X%X%X\n",
        a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12], a[13], a[14]);
  }
}




// Code migrated from https://www.geeksforgeeks.org/iterative-quick-sort/
// Function to swap numbers
static void _swap(gpointer *a, gpointer *b){
    gpointer temp = *a;
    *a = *b;
    *b = temp;
}

/* This function takes last element as pivot,
   places the pivot element at its correct
   position in sorted  array, and places
   all smaller (smaller than pivot) to left
   of pivot and all greater elements to
   right of pivot */
static int _partition (gpointer arr[], gint l, gint h, GCompareDataFunc cmp, gpointer udata)
{
    gpointer x = arr[h];
    int i = (l - 1);
    int j;

    for (j = l; j <= h- 1; j++)
    {
      if (cmp(arr[j], x, udata) <= 0)
        {
            i++;
            _swap (&arr[i], &arr[j]);
        }
    }
    _swap (&arr[i + 1], &arr[h]);
    return (i + 1);
}

/* A[] --> Array to be sorted,
l --> Starting index,
h --> Ending index */
static void _quickSort(gpointer A[], gint l, gint h, GCompareDataFunc cmp, gpointer udata)
{
    if (l < h)
    {
        /* Partitioning index */
        int p = _partition(A, l, h, cmp, udata);
        _quickSort(A, l, p - 1, cmp, udata);
        _quickSort(A, p + 1, h, cmp, udata);
    }
}

void qsort_pointers_with_udata(gpointer to[], size_t items_number, GCompareDataFunc cmp, gpointer udata) {
  _quickSort(to, 0, items_number - 1, cmp, udata);
}


