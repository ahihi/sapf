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

#ifndef __taggeddoubles__OscilUGens__
#define __taggeddoubles__OscilUGens__

#include "Object.hpp"
#include "UGen.hpp"

void AddOscilUGenOps();

struct SinOsc : OneInputUGen<SinOsc>
{
  Z phase;
  Z freqmul;

  SinOsc(Thread& th, Arg freq, Z iphase);
  const char* TypeName() const override;
  void calc(int n, Z* out, Z* freq, int freqStride);
};

struct SinOscPM : TwoInputUGen<SinOscPM>
{
  Z phase;
  Z freqmul;

  SinOscPM(Thread& th, Arg freq, Arg phasemod);
  const char* TypeName() const override;
  void calc(int n, Z* out, Z* freq, Z* phasemod, int freqStride, int phasemodStride);
};

void fillWaveTable(int n, const Z* amps, int ampStride, const Z* phases, int phaseStride, Z smooth, Z* table);

#endif /* defined(__taggeddoubles__OscilUGens__) */
