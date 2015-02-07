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

struct _lime_exif_handle {
  const char *path;
  Exiv2::Image::AutoPtr img;
};

lime_exif_handle *lime_exif_handle_new_from_file(const char *path)
{
  lime_exif_handle *h = (lime_exif_handle*)calloc(sizeof(lime_exif_handle), 1);
  
  h->path = path;
  
  return h;
}

float lime_exif_handle_find_float_by_tagname(lime_exif_handle *h, const char *tagname)
{
  if (!h->img.get()) {
    h->img = Exiv2::ImageFactory::open(h->path);
    assert(h->img.get() != 0);
    h->img->readMetadata();
  }
  
  Exiv2::ExifData &exifData = h->img->exifData();
  assert (!exifData.empty());
  
  Exiv2::ExifData::const_iterator end = exifData.end();
  for (Exiv2::ExifData::const_iterator i = exifData.begin(); i != end; ++i)
    if (i->typeId() == Exiv2::unsignedRational && !i->tagName().compare(tagname))
      return i->getValue()->toFloat();

  return NULL;
}


lime_exif_handle *lime_exif_handle_destroy(lime_exif_handle *h)
{
  if (h->img.get())
    h->img.reset();
  
  free(h);
}
