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

#ifndef _Testability_
#define _Testability_

// this file exposes various internal symbols so they can be unit tested, only
// for test builds
#ifdef TEST_BUILD

////////////////////////////////////////////////////////////////////////////////////////////////////////
// OscilUgens
#include "UGen.hpp"
#include "ZArr.hpp"
struct SinOsc : OneInputUGen<SinOsc>
{
    Z phase;
    Z freqmul;

    SinOsc(Thread& th, Arg freq, Z iphase);
    virtual const char* TypeName() const override;
    void calc(int n, Z* out, Z* freq, int freqStride);
};


struct SinOscPM : TwoInputUGen<SinOscPM>
{
    Z phase;
    Z freqmul;

    SinOscPM(Thread& th, Arg freq, Arg phasemod);
    virtual const char* TypeName() const override;
    void calc(int n, Z* out, Z* freq, Z* phasemod, int freqStride, int phasemodStride);
};

void fillWaveTable(int n, Z* amps, int ampStride, Z* phases, int phaseStride, Z smooth, Z* table);

////////////////////////////////////////////////////////////////////////////////////////////////////////
// StreamOps
void hanning_(Thread& th, Prim* prim);
void hamming_(Thread& th, Prim* prim);
void blackman_(Thread& th, Prim* prim);
#ifdef SAPF_ACCELERATE
    inline void wseg_apply_window(Z* segbuf, Z* window, int n);
#else
    inline void wseg_apply_window(Z* segbuf, ZArr window, int n);
#endif

#endif
#endif

