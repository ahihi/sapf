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

#include "Object.hpp"
#include "VM.hpp"
#include "doctest.h"
#include "ArrHelpers.hpp"
#include "ZArr.hpp"
#include "Testability.hpp"

// non-vectorized version for comparison
void hann_calc(Z* out, int n) {
	for (int i = 0; i < n; i++) {
		out[i] = 0.5 * (1 - cos(2*M_PI*i/n));
	}
}

TEST_CASE("hanning simd") {
	const int n = 100;
	Z expected[n];
	Thread th;
	th.push(n);

	hann_calc(expected, n);
	hanning_(th, nullptr);
	P<List> outZList = th.popZList("hanning");
	Z* out = outZList->mArray->z();

	CHECK_ARR(expected, out, n);
}

void hamm_calc(Z* out, int n) {
	for (int i = 0; i < n; i++) {
		out[i] = 0.54 - .46 * cos(2*M_PI*i/n);
	}
}

TEST_CASE("hamming simd") {
	const int n = 100;
	Z expected[n];
	Thread th;
	th.push(n);

	hamm_calc(expected, n);
	hamming_(th, nullptr);
	P<List> outZList = th.popZList("hamming");
	Z* out = outZList->mArray->z();

	CHECK_ARR(expected, out, n);
}

void blackman_calc(Z* out, int n) {
	for (int i = 0; i < n; i++) {
		out[i] = 0.42
			- .5 * cos(2*M_PI*i/n)
			+ .08 * cos(4*M_PI*i/n);
	}
}

TEST_CASE("blackman simd") {
	const int n = 100;
	Z expected[n];
	Thread th;
	th.push(n);

	blackman_calc(expected, n);
	blackman_(th, nullptr);
	P<List> outZList = th.popZList("blackman");
	Z* out = outZList->mArray->z();

	CHECK_ARR(expected, out, n);
}

void calc_winseg_apply_window(Z* segbuf, Z* window, int n) {
	LOOP(i,n) { segbuf[i] = segbuf[i] * window[i]; }
}

TEST_CASE("WinSegment apply window simd") {
	const int n = 100;
	Z blackman[n];
	blackman_calc(blackman, n);
	Z segbuf_expected[n];
	Z segbuf_actual[n];
	LOOP(i, n) { segbuf_expected[i] = sin(i / n); }
	LOOP(i, n) { segbuf_actual[i] = sin(i / n); }

	calc_winseg_apply_window(segbuf_expected, blackman, n);
	#ifdef SAPF_ACCELERATE
		wseg_apply_window(segbuf_actual, blackman, n);
	#else
		wseg_apply_window(segbuf_actual, zarr(blackman, 1, n), n);
	#endif

	CHECK_ARR(segbuf_expected, segbuf_actual, n);
}
