/*
 * GStreamer
 * Copyright (C) 2015 FIXME <fixme@example.com>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
#include <config.h>
#endif

#include <gst/gst.h>
#include "gstmprtpscheduler.h"
#include "gstmprtpsender.h"
#include "gstmprtpplayouter.h"
#include "gstmprtpreceiver.h"
#include "gstrtpstatmaker2.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  gst_element_register (plugin, "mprtpscheduler", GST_RANK_NONE,
      GST_TYPE_MPRTPSCHEDULER);
  gst_element_register (plugin, "mprtpsender", GST_RANK_NONE,
      GST_TYPE_MPRTPSENDER);
  gst_element_register (plugin, "mprtpplayouter", GST_RANK_NONE,
      GST_TYPE_MPRTPPLAYOUTER);
  gst_element_register (plugin, "mprtpreceiver", GST_RANK_NONE,
      GST_TYPE_MPRTPRECEIVER);
  gst_element_register (plugin, "rtpstatmaker2", GST_RANK_NONE,
      GST_TYPE_RTPSTATMAKER2);
  return TRUE;
}


/* FIXME: these are normally defined by the GStreamer build system.
   If you are creating an element to be included in gst-plugins-*,
   remove these, as they're always defined.  Otherwise, edit as
   appropriate for your external plugin package. */
#ifndef VERSION
#define VERSION "0.9.0.1"
#endif
#ifndef PACKAGE
#define PACKAGE "Multipath RTP Protocol Streamer and FBRA+ congestion controller"
#endif
#ifndef PACKAGE_NAME
#define PACKAGE_NAME "mprtp"
#endif
#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "http://https://github.com/multipath-rtp/gst-mprtp/"
#endif

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    mprtp,
    "The plugin is designed to use MPRTP amongst several subflows",
    plugin_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)
