/*
 * sink.h
 *
 *  Created on: 13 Sep 2017
 *      Author: balazskreith
 */

#ifndef TESTS_STATSRELAYER_BUFFER_H_
#define TESTS_STATSRELAYER_BUFFER_H_
#include <gst/gst.h>
#include "common.h"

typedef struct {
  GQueue* items;
  GQueue* recycle;
  guint item_size;
  PushPort* input;
  PushPort* output;
  Process* reset_process;
  guint rcved_packets;
  guint sent_packets;
  guint rcved_bytes;
  guint sent_bytes;
}Buffer;

Buffer* make_buffer(guint item_size);
void buffer_dtor(Buffer* this);
void buffer_flush(Buffer* this);
void buffer_sprintf(Buffer* this, gchar* string);
void buffer_reset_metrics(Buffer* this);
void buffer_prepare(Buffer* this, guint num);
//
//typedef struct {
//  PushPort* input;
//  GSList* outputs;
//  guint reserved_slot_num;
//}Tee;
//
//Tee* make_tee(void);
//PushPort* get_new_slot(Tee* this, guint* nth_index);
//PushPort* get_nth_slot(Tee* this, guint nth_index);
//
//typedef struct{
//  PushPort* input_x;
//  GQueue* packets_x;
//  PushPort* input_y;
//  GQueue* packets_y;
//
//  PushPort* output_xy;
//  PushPort* output_x;
//  PushPort* output_y;
//
//  GCompareFunc cmp;
//}Merger;
//
//typedef struct {
//  PushPort* input;
//  PushPort* output;
//}Mapper;
//
//typedef struct {
//
//}Reducer;

#endif /* TESTS_STATSRELAYER_BUFFER_H_ */
