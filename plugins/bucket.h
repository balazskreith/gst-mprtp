/*
 * smanctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef BUCKET_H_
#define BUCKET_H_

#include <gst/gst.h>
#include "lib_swplugins.h"
#include "rcvtracker.h"
#include "mediator.h"

typedef struct _Bucket Bucket;
typedef struct _BucketClass BucketClass;


#define BUCKET_TYPE             (bucket_get_type())
#define BUCKET(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),BUCKET_TYPE,Bucket))
#define BUCKET_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),BUCKET_TYPE,BucketClass))
#define BUCKET_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),BUCKET_TYPE))
#define BUCKET_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),BUCKET_TYPE))
#define BUCKET_CAST(src)        ((Bucket *)(src))

struct _Bucket
{
  GObject object;
  GstClock* sysclock;
  gdouble* bucket_sizes;
  guint* positive_reference;
  guint* negative_reference;
  guint* transform_reference;
  guint* actual;
  gint32 total_number;
  guint length;
  GstClockTime window_size;
  GQueue* recycle;
  GQueue* pushed_items;
};

struct _BucketClass{
  GObjectClass parent_class;
};

Bucket*
make_bucket(const guint length, gdouble first_bucket_size);
void bucket_set_positive_reference(Bucket* this, guint* vector);
void bucket_set_negative_reference(Bucket* this, guint* vector);
void bucket_set_transform_reference(Bucket* this, guint* vector);
void bucket_set_bucket_sizes(Bucket* this, gdouble* vector);
void bucket_set_window_size(Bucket* this, GstClockTime size);
void bucket_print(Bucket* this, const gchar* name);
void bucket_set_bucket_chain(Bucket* this, gdouble first_bucket_size, gdouble growth_factor);
void bucket_add_value(Bucket* this, gdouble value);
void bucket_add_value_at(Bucket* this, guint index);
void bucket_clear(Bucket* this);
gdouble bucket_get_positive_cosine_similarity(Bucket* this);
gdouble bucket_get_negative_cosine_similarity(Bucket* this);
gdouble bucket_get_stability(Bucket* this);
guint bucket_get_counter_at(Bucket* this, gint index);
gint32 bucket_get_total_number(Bucket* this);
GType
bucket_get_type (void);

#endif /* BUCKET_H_ */
