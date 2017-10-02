#ifndef TESTS_STATSRELAYER_MAPPER_H_
#define TESTS_STATSRELAYER_MAPPER_H_
#include <gst/gst.h>
#include <stdio.h>
#include "common.h"


typedef enum {
  MAPPER_FORMAT_BINARY = 1,
  MAPPER_FORMAT_PACKET_CSV = 2,
}MapperType;


typedef struct _Mapper Mapper;
typedef void (*MapperWriterProcess)(Mapper* this, gpointer item, gint item_length);

struct _Mapper{
  MapperType type;
  gchar* type_in_string;
  PushPort* input;
  PushPort* output;
  WriteItem write_item;
  guint item_size;

  guint transcieved_packets_num;
  guint rcved_bytes;
  guint sent_bytes;
};

Mapper* make_mapper(const gchar* string, guint item_size);
const gchar* mapper_get_type_in_string(Mapper* this);
void mapper_sprintf(Mapper* this, gchar* string);
void mapper_reset_metrics(Mapper* this);
void mapper_dtor(Mapper* this);

#endif /* TESTS_STATSRELAYER_MAPPER_H_ */
