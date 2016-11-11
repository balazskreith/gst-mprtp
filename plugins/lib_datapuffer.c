#include "lib_datapuffer.h"
#include <math.h>



//------------------------------------------------------------------------------

datapuffer_t* datapuffer_ctor(gint32 size)
{
        datapuffer_t* result;
        result = (datapuffer_t*) g_malloc0(sizeof(datapuffer_t));
        result->items = (gpointer*) g_malloc0(sizeof(gpointer) * size);
        result->length = size;
        result->start = 0;
        result->end = 0;
        result->count = 0;
        return result;
}//# datapuffer_ctor end


void datapuffer_dtor(datapuffer_t* puffer)
{
        gint32 index;
        gpointer item;
        index = 0;
        if(puffer == NULL){
                return;
        }
        for(index = 0; index <  puffer->length; index++)
        {
                item = puffer->items[index];
                if(item == NULL)
                {
                  continue;
                }
                g_free(item);
        }
        g_free(puffer->items);
        g_free(puffer);
}//# datapuffer_dtor end

void datapuffer_write(datapuffer_t* puffer, gpointer item)
{
        puffer->items[puffer->end++] = item;
        ++puffer->count;
        if(puffer->length <= puffer->end){
                puffer->end = 0;
        }
}//# datapuffer_write end

gpointer datapuffer_read(datapuffer_t* puffer)
{
        puffer->read = puffer->items[puffer->start];
        puffer->items[puffer->start] = NULL;
        if(puffer->length <= ++puffer->start){
                puffer->start = 0;
        }
        --puffer->count;
        return puffer->read;
}//# datapuffer_read end

gpointer datapuffer_peek_first(datapuffer_t* puffer)
{
        return puffer->items[puffer->start];
}//# datapuffer_read end

gpointer datapuffer_peek_last(datapuffer_t* puffer)
{
  gint32 pos;
  if(puffer->end == 0){
    pos = puffer->length - 1;
  }else{
    pos = puffer->end - 1;
  }
  return puffer->items[pos];
}//# datapuffer_read end

gpointer datapuffer_peek_custom(datapuffer_t* puffer, gint (*comparator)(gpointer item, gpointer udata), gpointer udata)
{
  gint32 pos;
  gint32 npos = puffer->start;
  gpointer item;
  do{
    pos = npos;
    item = puffer->items[pos];
//    g_print("Custom peek: start: %d, end: %d, pos: %d, npos: %d count: %d item: %p\n",
//        puffer->start, puffer->end, pos, npos, puffer->count, item);
    if(comparator(item, udata) == 0){
      return item;
    }
    if(++npos == puffer->length){
      npos = 0;
    }
  }while(npos != puffer->end);
  return NULL;
}

gint32 datapuffer_readcapacity(datapuffer_t *datapuffer)
{
        return datapuffer->count;
}

gint32 datapuffer_writecapacity(datapuffer_t *datapuffer)
{
        return datapuffer->length - datapuffer->count;
}

gboolean datapuffer_isfull(datapuffer_t *datapuffer)
{
        return datapuffer->count == datapuffer->length;
}

gboolean datapuffer_isempty(datapuffer_t *datapuffer)
{
        return datapuffer->count == 0;
}

void datapuffer_clear(datapuffer_t *puffer, void (*dtor)(gpointer))
{
        gint32 i,c;
        void *item;
        for(i = 0, c = datapuffer_readcapacity(puffer); i < c; ++i){
                item = datapuffer_read(puffer);
                if(dtor == NULL){
                        continue;
                }
                dtor(item);
        }
}

