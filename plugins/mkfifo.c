/* GStreamer Scheduling tree
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

#include "mkfifo.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <gst/gst.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>


GST_DEBUG_CATEGORY_STATIC (mkfifo_debug_category);
#define GST_CAT_DEFAULT mkfifo_debug_category

#define _now(this) gst_clock_get_time (this->sysclock)

G_DEFINE_TYPE (MkFifo, mkfifo, G_TYPE_OBJECT);

static void
mkfifo_finalize (
    GObject * object);


void
mkfifo_class_init (MkFifoClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = mkfifo_finalize;

  GST_DEBUG_CATEGORY_INIT (mkfifo_debug_category, "mkfifo", 0,
      "MkFIFO Representation");
}

void
mkfifo_finalize (GObject * object)
{
  MkFifo *this = MKFIFO (object);
  close(this->fd);
}


void
mkfifo_init (MkFifo * this)
{

}

MkFifo*
make_mkfifo(const gchar* path, MkFifoMode mode, guint message_size)
{
  MkFifo *this;
  this = (MkFifo *) g_object_new (MKFIFO_TYPE, NULL);
  strcpy(this->path, path);
  this->mode = mode;
  this->message_size = message_size;
  if (!g_file_test(this->path, G_FILE_TEST_EXISTS)) {
    mkfifo(this->path, 0666);
  }
  if (this->mode == MKFIFO_MODE_READER) {
    this->fd = open(this->path, O_RDONLY);
  } else {
    this->fd = open(this->path, O_WRONLY);
  }
  return this;
}

void
mkfifo_push(MkFifo* this, gpointer message) {
  ssize_t result = write(this->fd, message, this->message_size);
  if (result < 0) {

  }
}

gpointer
mkfifo_pop(MkFifo* this) {
  gpointer message = NULL;
  ssize_t result = read(this->fd, message, this->message_size);
  if (result < 0) {

  }
  return message;
}
