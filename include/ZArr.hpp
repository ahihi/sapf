//    SAPF - Sound As Pure Form
//    Copyright (C) 2019 James McCartney
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <https://www.gnu.org/licenses/>.

#ifndef __ZArr_h__
#define __ZArr_h__
#ifndef SAPF_ACCELERATE
#define _USE_MATH_DEFINES
#include <Eigen/Dense>
#include "Object.hpp"
#include <xsimd/xsimd.hpp>

#if SAMPLE_IS_DOUBLE
	typedef Eigen::Map<Eigen::ArrayXd, 0, Eigen::InnerStride<>> ZArr;
	typedef xsimd::batch<double> ZBatch;
	typedef int64_t Z_INT_EQUIV;
#else
	typedef Eigen::Map<Eigen::ArrayXf, 0, Eigen::InnerStride<>> ZArr;
	typedef xsimd::batch<float> ZBatch;
	typedef int32_t Z_INT_EQUIV;
#endif

constexpr size_t zbatch_size = ZBatch::size;

// create an Eigen Map over an existing array, without copying
ZArr zarr(const Z *vec, int n, int stride);

#endif
#endif