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

#include "dsp.hpp"
#include "Object.hpp"
#include "VM.hpp"
#if defined(SAPF_DOCTEST_H)
#include SAPF_DOCTEST_H
#else
#include <doctest.h>
#endif
#include "ArrHelpers.hpp"
#include "OscilUGens.hpp"

// non-vectorized version for comparison
void sinosc_calc(Z phase, Z freqmul, int n, Z* out, Z* freq, int freqStride) {
	for (int i = 0; i < n; ++i) {
		out[i] = phase;
		phase += *freq * freqmul;
		freq += freqStride;
		if (phase >= kTwoPi) phase -= kTwoPi;
		else if (phase < 0.) phase += kTwoPi;
	}
	LOOP(i,n) { out[i] = sin(out[i]); }
}

TEST_CASE("SinOsc SIMD") {
	const int n = 100;
	Z out[n];
	Z freq[n];
	Z expected[n];
	Z startFreq = 400;
	Z freqmul = 1;
	Z iphase;
	int freqStride;
	Thread th;
	th.rate.radiansPerSample = freqmul;
	LOOP(i,n) { freq[i] = sin(i/(double)n)*400 + 200; }

	SUBCASE("") {
		iphase = 0;
		freqStride = 1;
	}

	SUBCASE("") {
		iphase = .3;
		freqStride = 1;
	}

	SUBCASE("") {
		iphase = 0;
		freqStride = 0;
	}

	SUBCASE("") {
		iphase = .3;
		freqStride = 0;
	}
	CAPTURE(iphase);
	CAPTURE(freqStride);

	Z phase = sc_wrap(iphase, 0., 1.) * kTwoPi;

	SinOsc osc(th, startFreq, iphase);
	osc.calc(n, out, freq, freqStride);
	sinosc_calc(phase, freqmul, n, expected, freq, freqStride);
	CHECK_ARR(expected, out, n);
}

// non-vectorized version for comparison
void sinoscpm_calc(Z freqmul, int n, Z* out, Z* freq, Z* phasemod, int freqStride, int phasemodStride) {
	Z phase = 0.;
	for (int i = 0; i < n; ++i) {
		out[i] = phase + *phasemod * kTwoPi;
		phase += *freq * freqmul;
		freq += freqStride;
		phasemod += phasemodStride;
		if (phase >= kTwoPi) phase -= kTwoPi;
		else if (phase < 0.) phase += kTwoPi;
	}
	LOOP(i,n) { out[i] = sin(out[i]); }
}

TEST_CASE("SinOscPM SIMD") {
	const int n = 100;
	Z out[n];
	Z freq[n];
	Z phasemod[n];
	Z expected[n];
	Z startFreq = 400;
	Z freqmul = 1;
	int phasemodStride = 1;
	Z iphase;
	int freqStride;
	Thread th;
	th.rate.radiansPerSample = freqmul;
	LOOP(i,n) { freq[i] = sin(i/(double)n)*400 + 200; }
	LOOP(i,n) { phasemod[i] = cos(i/(double)n); }

	SUBCASE("") {
		freqStride = 1;
	}
	SUBCASE("") {
		freqStride = 0;
	}

	CAPTURE(freqStride);

	SinOscPM osc(th, startFreq, iphase);
	osc.calc(n, out, freq, phasemod, freqStride, phasemodStride);
	sinoscpm_calc(freqmul, n, expected, freq, phasemod, freqStride, phasemodStride);
	CHECK_ARR(expected, out, n);
}

static void zeroTable(size_t n, Z* table)
{
	memset(table, 0, n * sizeof(Z));
}

const int kWaveTableSize = 16384;
const size_t kWaveTableSize2 = kWaveTableSize / 2;
void fillwavetable_calc(int n, Z* amps, int ampStride, Z* phases, int phaseStride, Z smooth, Z* table) {
	const Z two_pi = 2. * M_PI;

	Z real[kWaveTableSize2];
	Z imag[kWaveTableSize2];

	zeroTable(kWaveTableSize2, real);
	zeroTable(kWaveTableSize2, imag);


	Z w = M_PI_2 / n;
	for (int i = 0; i < n; ++i) {
		Z smoothAmp = smooth == 0. ? 1. : pow(cos(w*i), smooth);
		real[i+1] = *amps * smoothAmp;
		imag[i+1] = (*phases - .25) * two_pi;
		amps += ampStride;
		phases += phaseStride;
	}

	for(size_t i = 0; i < kWaveTableSize2; i++) {
		Z radius = real[i];
		Z angle = imag[i];
		real[i] = radius * cos(angle);
		imag[i] = radius * sin(angle);
	}
	rifft(kWaveTableSize, real, imag, table);
}

TEST_CASE("fillWaveTable SIMD") {
	const int n = 100;
	Z amps[n];
	int ampStride = 1;
	Z phases[n];
	int phaseStride = 1;
	Z smooth = 1;
	Z out[kWaveTableSize];
	Z expected[kWaveTableSize];
	LOOP(i,n) { amps[i] = sin(i/(double)n)/2. + .5; }
	LOOP(i,n) { phases[i] = cos(i/(double)n); }

	SUBCASE("") {
		ampStride = 1;
		phaseStride = 1;
	}

	initFFT();
	fillWaveTable(n, amps, ampStride, phases, phaseStride, smooth, out);
	fillwavetable_calc(n , amps, ampStride, phases, phaseStride, smooth, expected);
	CHECK_ARR(expected, out, n);
}
