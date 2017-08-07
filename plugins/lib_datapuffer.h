#ifndef INCGUARD_NTRT_LIBRARY_DATAPUFFER_H_
#define INCGUARD_NTRT_LIBRARY_DATAPUFFER_H_

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>

#include <gst/gst.h>


typedef struct datapuffer_struct_t
{
        gpointer                *items;    ///< A pointer array of data the puffer will uses for storing
        gint32                   length;   ///< The maximal amount of data the puffer can store
        volatile gint32          start;    ///< index for read operations. It points to the next element going to be read
        volatile gint32          end;      ///< index for write operations. It points to the last element, which was written by the puffer
        volatile gint32          count;
        volatile gpointer        read;
} datapuffer_t;


datapuffer_t* datapuffer_ctor(gint32 items_num);
void datapuffer_dtor(datapuffer_t *datapuffer);
gpointer datapuffer_read(datapuffer_t *datapuffer);
gpointer datapuffer_peek_first(datapuffer_t* puffer);
gpointer datapuffer_peek_last(datapuffer_t* puffer);
gpointer datapuffer_peek_custom(datapuffer_t* puffer, gint (*comparator)(gpointer item, gpointer udata), gpointer udata);
void datapuffer_write(datapuffer_t *datapuffer, void *item);
gint32 datapuffer_readcapacity(datapuffer_t *datapuffer);
gint32 datapuffer_writecapacity(datapuffer_t *datapuffer);
gboolean datapuffer_isfull(datapuffer_t *datapuffer);
gboolean datapuffer_isempty(datapuffer_t *datapuffer);
void datapuffer_clear(datapuffer_t *datapuffer, void (*dtor)(gpointer));


#endif /* INCGUARD_NTRT_LIBRARY_DATAPUFFER_H_ */
