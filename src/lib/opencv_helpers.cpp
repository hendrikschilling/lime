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

#include <cv.hpp>

#include "opencv_helpers.h"

using namespace cv;

void cv_gauss(int w, int h, int type, void *in, void *out, float r)
{
  Mat in_m = Mat(h, w, type, in);
  Mat out_m = Mat(h, w, type, out);
  
  GaussianBlur(in_m, out_m, Size((int)(r+1)*2+5, (int)(r+1)*2+5), r);
}
