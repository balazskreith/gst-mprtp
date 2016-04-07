/* GStreamer
 * Copyright (C) 2015 Balázs Kreith (contact: balazs.kreith@gmail.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include "subratectrler.h"
#include "fbrasubctrler.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

//#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
//#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
//#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
//#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define THIS_READLOCK(this)
#define THIS_READUNLOCK(this)
#define THIS_WRITELOCK(this)
#define THIS_WRITEUNLOCK(this)

GST_DEBUG_CATEGORY_STATIC (subratectrler_debug_category);
#define GST_CAT_DEFAULT subratectrler_debug_category

G_DEFINE_TYPE (SubflowRateController, subratectrler, G_TYPE_OBJECT);


void subratectrler_finalize (GObject * object);
static void _enable(SubflowRateController *this);
static void _disable(SubflowRateController *this);

#define _now(this) (gst_clock_get_time(this->sysclock))

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
subratectrler_class_init (SubflowRateControllerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = subratectrler_finalize;

  GST_DEBUG_CATEGORY_INIT (subratectrler_debug_category, "subratectrler", 0,
      "MpRTP Manual Sending Controller");

}

void
subratectrler_finalize (GObject * object)
{
  SubflowRateController *this;
  this = SUBRATECTRLER(object);
  g_object_unref(this->sysclock);
  g_object_unref(this->path);
}

void
subratectrler_init (SubflowRateController * this)
{
  this->sysclock = gst_system_clock_obtain();
  g_rw_lock_init (&this->rwmutex);
}


SubflowRateController *make_subratectrler(MPRTPSPath *path)
{
  SubflowRateController *result;
  result                      = g_object_new (SUBRATECTRLER_TYPE, NULL);
  result->path                = g_object_ref(path);
  result->id                  = mprtps_path_get_id(result->path);
  return result;
}

void subratectrler_time_update(SubflowRateController *this)
{
  THIS_READLOCK(this);
  if(!this->enabled){
    goto done;
  }

  switch(this->type){
    case SUBRATECTRLER_FBRA_MARC:
      fbrasubctrler_time_update(this->controller);
      break;
    default:
    case SUBRATECTRLER_NO_CTRL:
      goto done;
      break;
  }

done:
  THIS_READUNLOCK(this);
  return;
}

void subratectrler_change(SubflowRateController *this, SubRateControllerType type)
{
  THIS_WRITELOCK(this);
  if(this->type ^ type){
    _disable(this);
  }
  this->type = type;
  _enable(this);
  THIS_WRITEUNLOCK(this);
  return;
}

void subratectrler_report_update(
                         SubflowRateController *this,
                         GstMPRTCPReportSummary *summary)
{
  THIS_READLOCK(this);
  if(!this->enabled){
    goto done;
  }

  switch(this->type){
    case SUBRATECTRLER_FBRA_MARC:
      fbrasubctrler_report_update(this->controller, summary);
      break;
    default:
    case SUBRATECTRLER_NO_CTRL:
      goto done;
      break;
  }

done:
  THIS_READUNLOCK(this);

  return;
}


void _enable(SubflowRateController *this)
{
  switch(this->type){
    case SUBRATECTRLER_FBRA_MARC:
      this->controller = make_fbrasubctrler(this->path);
      fbrasubctrler_enable(this->controller);
      break;
    default:
    case SUBRATECTRLER_NO_CTRL:
      break;
  }

  this->enabled = TRUE;
}

void _disable(SubflowRateController *this)
{
  switch(this->type){
    case SUBRATECTRLER_FBRA_MARC:
      fbrasubctrler_disable(this->controller);
      g_object_unref(this->controller);
      break;
    default:
    case SUBRATECTRLER_NO_CTRL:
      break;
  }
  this->controller = NULL;
  this->enabled = FALSE;
}
