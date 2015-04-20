/*
 * Copyright (C) 2014 Hendrik Siedelmann <hendrik.siedelmann@googlemail.com>
 *
 * This file is part of lime.
 * 
 * Lime is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * Lime is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with Lime.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <exiv2/exiv2.hpp>
#include <assert.h>

#include "exif_helpers.h"

struct _lime_exif {
  const char *path;
  Exiv2::Image::AutoPtr img;
  pthread_mutex_t lock;
};

lime_exif *lime_exif_handle_new_from_file(const char *path)
{
  lime_exif *h = (lime_exif*)calloc(sizeof(lime_exif), 1);
  
  h->path = path;
  pthread_mutex_init(&h->lock, NULL);
  
  return h;
}

float lime_exif_handle_find_float_by_tagname(lime_exif *h, const char *tagname)
{
  pthread_mutex_lock(&h->lock);
  if (!h->img.get()) {
    try {
      h->img = Exiv2::ImageFactory::open(h->path);
    }
    catch (...) {
      pthread_mutex_unlock(&h->lock);
      return -1.0;
    }
    assert(h->img.get() != 0);
    h->img->readMetadata();
  }
  
  Exiv2::ExifData &exifData = h->img->exifData();
  assert (!exifData.empty());
  
  Exiv2::ExifData::const_iterator end = exifData.end();
  for (Exiv2::ExifData::const_iterator i = exifData.begin(); i != end; ++i)
    if (i->typeId() == Exiv2::unsignedRational && !i->tagName().compare(tagname)) {
      pthread_mutex_unlock(&h->lock);
      return i->getValue()->toFloat();
    }

  pthread_mutex_unlock(&h->lock);
  return -1.0;
}

char *lime_exif_handle_find_str_by_tagname(lime_exif *h, const char *tagname)
{
  pthread_mutex_lock(&h->lock);
  std::string str;
  char *c_str;
  if (!h->img.get()) {
    try {
      h->img = Exiv2::ImageFactory::open(h->path);
    }
    catch (...) {
      pthread_mutex_unlock(&h->lock);
      return NULL;
    }
    assert(h->img.get() != 0);
    h->img->readMetadata();
  }
  
  Exiv2::ExifData &exifData = h->img->exifData();
  if (exifData.empty())
    return NULL;
  
  Exiv2::ExifData::const_iterator end = exifData.end();
  for (Exiv2::ExifData::const_iterator i = exifData.begin(); i != end; ++i)
    if ((i->typeId() == Exiv2::unsignedByte || i->typeId() == Exiv2::asciiString) && !i->tagName().compare(tagname)) {
      str = i->print(&exifData);
      c_str = strdup(str.c_str());
      pthread_mutex_unlock(&h->lock);
      return c_str;
    }

  pthread_mutex_unlock(&h->lock);
  return NULL;
}

void lime_exif_handle_destroy(lime_exif *h)
{
  if (h->img.get())
    h->img.reset();
  
  free(h);
}
  
  
static inline int strlen_null(const char *str)
{
  if (!str)
    return 0;
  return strlen(str);
}
  
char *lime_exif_model_make_string(lime_exif *h)
{
  char *make, *model;
  char *buf;
  if (!h) return NULL;
  
  make = lime_exif_handle_find_str_by_tagname(h, "Make");
  model = lime_exif_handle_find_str_by_tagname(h, "Model");
  
  if (!make && !model)
    return NULL;
  
  buf = (char*)malloc(strlen_null(make)+strlen_null(model)+1);
  buf[0] = '\0';
  
  if (make) {
    strcat(buf, make);
    free(make); 
  }
  if (model) {
    strcat(buf, model);
    free(model);
  }
  
  return buf;
}

char *lime_exif_lens_string(lime_exif *h)
{
  const char *lens;
  if (!h) return NULL;
  
  lens = lime_exif_handle_find_str_by_tagname(h, "LensType");
  if (lens)
    return strdup(lens);
  lens = lime_exif_handle_find_str_by_tagname(h, "LensModel");
  if (lens)
    return strdup(lens); 
  return NULL;
}
