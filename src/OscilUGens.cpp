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


#include "OscilUGens.hpp"
#include "UGen.hpp"

#include "VM.hpp"
#include "clz.hpp"
#include "dsp.hpp"
#include <climits>
#include <cmath>
#include <float.h>
#include <vector>
#include <algorithm>
#ifdef SAPF_ACCELERATE
#include <Accelerate/Accelerate.h>
#else
#include "ZArr.hpp"
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const int kNumTables = 30; // third octave tables.
const int kWaveTableSize = 16384;
const int kWaveTableMask = kWaveTableSize - 1;
//const int kWaveTableByteSize = kWaveTableSize * sizeof(Z);
const int kWaveTableTotalSize = kWaveTableSize * kNumTables;
//const Z kPhaseInc = kTwoPi / kWaveTableSize;
const Z kWaveTableSizeF = kWaveTableSize;

// the maximum number of harmonics is actually 1024, but 1290 is needed for extrapolation.
const int kMaxHarmonics = 1290;
const int gNumHarmonicsForTable[kNumTables+1] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 16, 20, 25, 32, 40, 50, 64, 80, 101, 128, 161, 203, 256, 322, 406, 512, 645, 812, 1024, 1290 };

int gWaveTableSize[kNumTables];

const double kMaxHarmonicsF = kMaxHarmonics;


Z gTableForNumHarmonics[kMaxHarmonics+1];
Z gHertzToHarmonics[kMaxHarmonics+1];

static void fillHarmonicsTable()
{	
	double maxval = kNumTables - 1.0000001;
	
	int t = 0;
	for (int i = 1; i < kMaxHarmonics; ++i) {
		if (gNumHarmonicsForTable[t] < i && t < kNumTables) ++t;
		//int t1 = std::clamp(t-1, 0, kNumTables-1);
		
		double frac = (double)(i - gNumHarmonicsForTable[t]) / (double)(gNumHarmonicsForTable[t] - gNumHarmonicsForTable[t-1]);
		double ft = t - 1 + frac;
		gTableForNumHarmonics[i] = ft;
	}
		
	gTableForNumHarmonics[0] = 0.;
	gTableForNumHarmonics[kMaxHarmonics] = maxval;
}

static void zeroTable(size_t n, Z* table)
{
	memset(table, 0, n * sizeof(Z));
}

static void normalize(int n, Z* buf)
{
	Z maxabs = 0.;
	for (int i = 0; i < n; ++i) {
		maxabs = std::max(maxabs, fabs(buf[i]));
	}
	if (maxabs > 0.) {
		Z scale = 1. / maxabs;
		for (int i = 0; i < n; ++i) {
			buf[i] *= scale;
		}
	}
}

void fillWaveTable(const int n, const Z *amps, const int ampStride, const Z *phases, const int phaseStride, const Z smooth, Z *table) {
  
  const size_t kWaveTableSize2 = kWaveTableSize / 2;
  const Z two_pi = 2. * M_PI;

  Z real[kWaveTableSize2];
  Z imag[kWaveTableSize2];
  Z polar[kWaveTableSize];
  Z rect[kWaveTableSize];

  zeroTable(kWaveTableSize2, real);
  zeroTable(kWaveTableSize2, imag);


  Z w = M_PI_2 / n;
  for (int i = 0; i < n; ++i) {
    Z smoothAmp = smooth == 0. ? 1. : pow(cos(w * i), smooth);
    //fillHarmonic(i+1, *amps * smoothAmp, *phases, table);
    real[i + 1] = *amps * smoothAmp;
    imag[i + 1] = (*phases - .25) * two_pi;
    amps += ampStride;
    phases += phaseStride;
  }

  // convert polar to rectangular
#ifdef SAPF_ACCELERATE
	DSPDoubleSplitComplex in;
	in.realp = real;
	in.imagp = imag;

	vDSP_ztocD(&in, 1, (DSPDoubleComplex*)polar, 2, kWaveTableSize2);
	vDSP_rectD(polar, 2, rect, 2, kWaveTableSize2);
	vDSP_ctozD((DSPDoubleComplex*)rect, 2, &in, 1, kWaveTableSize2);
#else
  ZArr real_zarr = zarr(real, kWaveTableSize2, 1);
  ZArr imag_zarr = zarr(imag, kWaveTableSize2, 1);
  Eigen::ArrayXd mag = real_zarr;
  real_zarr = mag * imag_zarr.cos();
  imag_zarr = mag * imag_zarr.sin();
#endif // SAPF_ACCELERATE
  rifft(kWaveTableSize, real, imag, table);
}

static void fill3rdOctaveTables(int n, Z* amps, int ampStride, Z* phases, int phaseStride, Z smooth, Z* tables)
{	
	// tables is assumed to be allocated to kNumTables * kWaveTableSize samples
	for (int i = 0; i < kNumTables; ++i) {
		int numHarmonics = std::min(n, gNumHarmonicsForTable[i]);
		fillWaveTable(numHarmonics, amps, ampStride, phases, phaseStride, smooth, tables + i * kWaveTableSize);
	}
	
	normalize(kWaveTableTotalSize, tables);
}

static P<List> makeWavetable(int n, Z* amps, int ampStride, Z* phases, int phaseStride, Z smooth)
{
	P<List> list = new List(itemTypeZ, kWaveTableTotalSize);
	P<Array> array = list->mArray;
	
	Z* tables = array->z();
		
	fill3rdOctaveTables(n, amps, ampStride, phases, phaseStride, smooth, tables);
	array->setSize(kWaveTableTotalSize);
	
	return list;
}

static void wavefill_(Thread& th, Prim* prim)
{
	Z smooth = th.popFloat("wavefill : smooth");
	V phases = th.popZIn("wavefill : phases");
	V amps = th.popZIn("wavefill : amps");
		
	P<List> ampl;
	P<List> phasel;
	Z *phasez, *ampz;
	int phaseStride, ampStride; 
	int64_t n = kMaxHarmonics;
	if (phases.isZList()) {
		phasel = ((List*)phases.o())->packSome(th, n);
		phasez = phasel->mArray->z();
		phaseStride = 1;
	} else {
		phasez = &phases.f;
		phaseStride = 0;
	}
	
	if (amps.isZList()) {
		ampl = ((List*)amps.o())->packSome(th, n);
		ampz = ampl->mArray->z();
		ampStride = 1;
	} else {
		ampz = &amps.f;
		ampStride = 0;
	}
	
	P<List> list = makeWavetable((int)n, ampz, ampStride, phasez, phaseStride, smooth);
	th.push(list);
}

P<List> gParabolicTable;
P<List> gTriangleTable;
P<List> gSquareTable;
P<List> gSawtoothTable;

static void makeClassicWavetables()
{
	//fprintf(stdout, "computing wave tables\n");
	Z amps[kMaxHarmonics+1];
	Z phases[kMaxHarmonics+1];
	Z smooth = 0.;
	
	for (int i = 1; i <= kMaxHarmonics; ) {
		amps[i] = 1. / (i*i);		++i;
	}
	phases[0] = .25;
	gParabolicTable = makeWavetable(kMaxHarmonics, amps+1, 1, phases, 0, smooth);

	for (int i = 1; i <= kMaxHarmonics; ) {
		amps[i] = 1. / (i*i);		++i; if (i > kMaxHarmonics) break;
		amps[i] = 0.;				++i; if (i > kMaxHarmonics) break;
		amps[i] = -1. / (i*i);		++i; if (i > kMaxHarmonics) break;
		amps[i] = 0.;				++i;
	}

	phases[0] = 0.;
	gTriangleTable = makeWavetable(kMaxHarmonics, amps+1, 1, phases, 0, smooth);
	
	for (int i = 1; i <= kMaxHarmonics; ) {
		amps[i] = 1. / i;		++i;  if (i > kMaxHarmonics) break;
		amps[i] = 0.;			++i;
	}
	phases[0] = 0.;
	gSquareTable = makeWavetable(kMaxHarmonics, amps+1, 1, phases, 0, smooth);
	
	for (int i = 1; i <= kMaxHarmonics; ) {
		amps[i] = 1. / i;		++i;
	}
	for (int i = 1; i <= kMaxHarmonics; ) {
		phases[i] = 0.;		++i;
		phases[i] = .5;		++i;
	}
	gSawtoothTable = makeWavetable(kMaxHarmonics, amps+1, 1, phases+1, 1, smooth);
	
	vm.addBifHelp("\n*** classic wave tables ***");
	vm.def("parTbl", gParabolicTable);		vm.addBifHelp("parTbl - parabolic wave table.");
	vm.def("triTbl", gTriangleTable);		vm.addBifHelp("triTbl - triangle wave table.");
	vm.def("sqrTbl", gSquareTable);			vm.addBifHelp("sqrTbl - square wave table.");
	vm.def("sawTbl", gSawtoothTable);		vm.addBifHelp("sawTbl - sawtooth wave table.");

	//fprintf(stdout, "done computing wave tables\n");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Osc : public ZeroInputUGen<Osc>
{
	P<Array> const array;
	Z phase;
	Z freq;
	Z* table;
	
	Osc(Thread& th, P<Array> const& inArray, Z ifreq, Z iphase) : ZeroInputUGen<Osc>(th, false),
		array(inArray),
		phase(sc_wrap(iphase, 0., 1.) * kWaveTableSizeF), freq(ifreq * kWaveTableSizeF * th.rate.invSampleRate)
	{
		Z numHarmonics = std::clamp(th.rate.freqLimit / fabs(ifreq), 0., kMaxHarmonicsF);
		Z inumHarmonics = floor(numHarmonics);
		int harmIndex = (int)inumHarmonics;
		Z tableF = lut(gTableForNumHarmonics, harmIndex, numHarmonics - inumHarmonics);
		Z tableI = floor(tableF);
		int tableNum = (int)tableI + 1;
		table = array->z() + kWaveTableSize * tableNum;
	}
	
	virtual const char* TypeName() const override { return "Osc"; }
		
	void calc(int n, Z* out) 
	{
		const int mask = kWaveTableMask;

		for (int i = 0; i < n; ++i) {
			
			Z iphase = floor(phase);
			int index = (int)iphase;
			Z fracphase = phase - iphase;

			out[i] = oscilLUT(table, index, mask, fracphase);

			phase += freq;
			if (phase >= kWaveTableSizeF) phase -= kWaveTableSizeF;
			if (phase < 0.) phase += kWaveTableSizeF;
		}
	}
};


struct OscPM : public OneInputUGen<OscPM>
{
	P<Array> const array;
	Z phase;
	Z freq;
	Z* table;
	
	OscPM(Thread& th, P<Array> const& inArray, Z ifreq, Arg phasemod) : OneInputUGen<OscPM>(th, phasemod),
		array(inArray),
		phase(0.), freq(ifreq * kWaveTableSizeF * th.rate.invSampleRate)
	{
		Z numHarmonics = std::clamp(th.rate.freqLimit / fabs(ifreq), 0., kMaxHarmonicsF);
		Z inumHarmonics = floor(numHarmonics);
		int harmIndex = (int)inumHarmonics;
		Z tableF = lut(gTableForNumHarmonics, harmIndex, numHarmonics - inumHarmonics);
		Z tableI = floor(tableF);
		int tableNum = (int)tableI + 1;
		table = array->z() + kWaveTableSize * tableNum;
	}
	
	virtual const char* TypeName() const override { return "Osc"; }
		
	void calc(int n, Z* out, Z* phasemod, int phasemodStride) 
	{
		const int mask = kWaveTableMask;

		for (int i = 0; i < n; ++i) {
			
			Z pphase = phase + *phasemod * kWaveTableSizeF;
			phasemod += phasemodStride;
			Z iphase = floor(pphase);
			int index = (int)iphase;
			Z fracphase = pphase - iphase;

			out[i] = oscilLUT(table, index, mask, fracphase);

			phase += freq;
			if (phase >= kWaveTableSizeF) phase -= kWaveTableSizeF;
			if (phase < 0.) phase += kWaveTableSizeF;
		}
	}
};


struct OscFM : public OneInputUGen<OscFM>
{
	P<Array> const array;
	Z phase;
	Z freqmul;
	Z freqLimit;
	Z* tables;
	
	OscFM(Thread& th, P<Array> const& inArray, Arg freq, Z iphase) : OneInputUGen<OscFM>(th, freq),
		array(inArray),
		phase(sc_wrap(iphase, 0., 1.) * kWaveTableSizeF), freqmul(kWaveTableSizeF * th.rate.invSampleRate),
		tables(array->z()),
		freqLimit(th.rate.freqLimit)
	{
	}
	
	virtual const char* TypeName() const override { return "OscFM"; }
		
	void calc(int n, Z* out, Z* freq, int freqStride) 
	{
		const int mask = kWaveTableMask;
		for (int i = 0; i < n; ++i) {
			Z ffreq = *freq;
			freq += freqStride;
			Z numHarmonics = std::clamp(freqLimit / fabs(ffreq), 0., kMaxHarmonicsF);
			Z inumHarmonics = floor(numHarmonics);
			int harmIndex = (int)inumHarmonics;
			Z tableF = lut(gTableForNumHarmonics, harmIndex, numHarmonics - inumHarmonics);
			Z tableI = floor(tableF);

			int tableNum = (int)tableI;
			Z fractable = tableF - tableI;
			
			Z* tableA = tables + kWaveTableSize * tableNum;
			Z* tableB = tableA + kWaveTableSize;
			
			Z iphase = floor(phase);
			int index = (int)iphase;
			Z fracphase = phase - iphase;

			// optional: round off the attenuation of higher harmonics. this eliminates a broadband tick that happens when a straight line decays to zero.
			fractable = sc_scurve0(fractable);
			
			out[i] = oscilLUT2(tableA, tableB, index, mask, fracphase, fractable);

			phase += ffreq * freqmul;
			if (phase >= kWaveTableSizeF) phase -= kWaveTableSizeF;
			if (phase < 0.) phase += kWaveTableSizeF;
		}
	}
};

struct OscFMPM : public TwoInputUGen<OscFMPM>
{
	P<Array> const array;
	Z phase;
	Z freqmul;
	Z freqLimit;
	Z* tables;
	
	OscFMPM(Thread& th, P<Array> const& inArray, Arg freq, Arg phasemod) : TwoInputUGen<OscFMPM>(th, freq, phasemod),
		array(inArray),
		phase(0.), freqmul(kWaveTableSizeF * th.rate.invSampleRate),
		tables(array->z()),
		freqLimit(th.rate.freqLimit)
	{
	}
	
	virtual const char* TypeName() const override { return "OscFMPM"; }
		
	void calc(int n, Z* out, Z* freq, Z* phasemod, int freqStride, int phasemodStride)
	{
		const int mask = kWaveTableMask;
		for (int i = 0; i < n; ++i) {
			Z ffreq = *freq;
			freq += freqStride;
			//Z numHarmonics = std::min(freqLimit, cutoff) / ffreq;
			Z numHarmonics = std::clamp(freqLimit / fabs(ffreq), 0., kMaxHarmonicsF);
			Z inumHarmonics = floor(numHarmonics);
			int harmIndex = (int)inumHarmonics;
			Z tableF = lut(gTableForNumHarmonics, harmIndex, numHarmonics - inumHarmonics);
			Z tableI = floor(tableF);
			int tableNum = (int)tableI;
			Z fractable = tableF - tableI;
			
			Z* tableA = tables + kWaveTableSize * tableNum;
			Z* tableB = tableA + kWaveTableSize;
			
			Z pphase = phase + *phasemod * kWaveTableSizeF;
			phasemod += phasemodStride;
			Z iphase = floor(pphase);
			int index = (int)iphase;
			Z fracphase = pphase - iphase;

			// optional: round off the attenuation of higher harmonics. this eliminates a broadband tick that happens when a straight line decays to zero.
			fractable = sc_scurve0(fractable);

			out[i] = oscilLUT2(tableA, tableB, index, mask, fracphase, fractable);

			phase += ffreq * freqmul;
			if (phase >= kWaveTableSizeF) phase -= kWaveTableSizeF;
			if (phase < 0.) phase += kWaveTableSizeF;
		}
	}
};


static void newOsc(Thread& th, Arg freq, Arg phase, P<List> const& tables)
{
	if (freq.isZList()) {
		if (phase.isZList()) {
			th.push(new List(new OscFMPM(th, tables->mArray, freq, phase)));
		} else {
			th.push(new List(new OscFM(th, tables->mArray, freq, phase.asFloat())));
		}
	} else {
		if (phase.isZList()) {
			th.push(new List(new OscPM(th, tables->mArray, freq.asFloat(), phase)));
		} else {
			th.push(new List(new Osc(th, tables->mArray, freq.asFloat(), phase.asFloat())));
		}
	}
}


static void osc_(Thread& th, Prim* prim)
{
	P<List> tables = th.popZList("osc : tables");
	V phase = th.popZIn("osc : phase");
	V freq = th.popZIn("osc : freq");

	if (!tables->isPacked() || tables->length(th) != kWaveTableTotalSize) {
		post("osc : tables is not a wave table. must be a signal of %d x %d samples.", kNumTables, kWaveTableSize);
		throw errWrongType;
	}

	newOsc(th, freq, phase, tables);
}

static void par_(Thread& th, Prim* prim)
{
	V phase = th.popZIn("par : phase");
	V freq = th.popZIn("par : freq");

	newOsc(th, freq, phase, gParabolicTable);
}

static void tri_(Thread& th, Prim* prim)
{
	V phase = th.popZIn("tri : phase");
	V freq = th.popZIn("tri : freq");

	newOsc(th, freq, phase, gTriangleTable);
}

static void saw_(Thread& th, Prim* prim)
{
	V phase = th.popZIn("saw : phase");
	V freq = th.popZIn("saw : freq");

	newOsc(th, freq, phase, gSawtoothTable);
}

static void square_(Thread& th, Prim* prim)
{
	V phase = th.popZIn("square : phase");
	V freq = th.popZIn("square : freq");

	newOsc(th, freq, phase, gSquareTable);
}

struct OscPWM : public ThreeInputUGen<OscPWM>
{
	P<Array> const array;
	Z phase;
	Z freqmul;
	Z freqLimit;
	Z* tables;
	
	OscPWM(Thread& th, P<Array> const& inArray, Arg freq, Arg phasemod, Arg duty) : ThreeInputUGen<OscPWM>(th, freq, phasemod, duty),
		array(inArray),
		phase(0.), freqmul(kWaveTableSizeF * th.rate.invSampleRate),
		tables(array->z()),
		freqLimit(th.rate.freqLimit)
	{
	}
	
	virtual const char* TypeName() const override { return "OscPWM"; }
		
	void calc(int n, Z* out, Z* freq, Z* phasemod, Z* duty, int freqStride, int phasemodStride, int dutyStride)
	{
		const int mask = kWaveTableMask;
		for (int i = 0; i < n; ++i) {
			Z ffreq = *freq;
			freq += freqStride;
			//Z numHarmonics = std::min(freqLimit, cutoff) / ffreq;
			Z numHarmonics = std::clamp(freqLimit / fabs(ffreq), 0., kMaxHarmonicsF);
			Z inumHarmonics = floor(numHarmonics);
			int harmIndex = (int)inumHarmonics;
			Z tableF = lut(gTableForNumHarmonics, harmIndex, numHarmonics - inumHarmonics);
			Z tableI = floor(tableF);
			int tableNum = (int)tableI;
			Z fractable = tableF - tableI;
			
			Z* tableA = tables + kWaveTableSize * tableNum;
			Z* tableB = tableA + kWaveTableSize;
			
			Z pphase1 = phase + *phasemod * kWaveTableSizeF;
			Z iphase1 = floor(pphase1);
			int index1 = (int)iphase1;
			Z fracphase1 = pphase1 - iphase1;

			Z pphase2 = pphase1 + *duty * kWaveTableSizeF;
			Z iphase2 = floor(pphase2);
			int index2 = (int)iphase2;
			Z fracphase2 = pphase2 - iphase2;

			phasemod += phasemodStride;
			duty += dutyStride;

			// optional: round off the attenuation of higher harmonics. this eliminates a broadband tick that happens when a straight line decays to zero.
			fractable = sc_scurve0(fractable);

			Z a = oscilLUT2(tableA, tableB, index1, mask, fracphase1, fractable);
			Z b = oscilLUT2(tableA, tableB, index2, mask, fracphase2, fractable);
			out[i] = .5 * (a - b);
			
			phase += ffreq * freqmul;
			if (phase >= kWaveTableSizeF) phase -= kWaveTableSizeF;
			if (phase < 0.) phase += kWaveTableSizeF;
		}
	}
};


struct VarSaw : public ThreeInputUGen<VarSaw>
{
	P<Array> const array;
	Z phase;
	Z freqmul;
	Z freqLimit;
	Z* tables;
	
	VarSaw(Thread& th, P<Array> const& inArray, Arg freq, Arg phasemod, Arg duty) : ThreeInputUGen<VarSaw>(th, freq, phasemod, duty),
		array(inArray),
		phase(0.), freqmul(kWaveTableSizeF * th.rate.invSampleRate),
		tables(array->z()),
		freqLimit(th.rate.freqLimit)
	{
	}
	
	virtual const char* TypeName() const override { return "VarSaw"; }
		
	void calc(int n, Z* out, Z* freq, Z* phasemod, Z* duty, int freqStride, int phasemodStride, int dutyStride)
	{
		const int mask = kWaveTableMask;
		for (int i = 0; i < n; ++i) {
			Z ffreq = *freq;
			freq += freqStride;
			//Z numHarmonics = std::min(freqLimit, cutoff) / ffreq;
			Z numHarmonics = std::clamp(freqLimit / fabs(ffreq), 0., kMaxHarmonicsF);
			Z inumHarmonics = floor(numHarmonics);
			int harmIndex = (int)inumHarmonics;
			Z tableF = lut(gTableForNumHarmonics, harmIndex, numHarmonics - inumHarmonics);
			Z tableI = floor(tableF);
			int tableNum = (int)tableI;
			Z fractable = tableF - tableI;
			
			Z* tableA = tables + kWaveTableSize * tableNum;
			Z* tableB = tableA + kWaveTableSize;
			
			Z pphase1 = phase + *phasemod * kWaveTableSizeF;
			Z iphase1 = floor(pphase1);
			int index1 = (int)iphase1;
			Z fracphase1 = pphase1 - iphase1;
			
			Z zduty = std::clamp(*duty, .01, .99);
			Z pphase2 = pphase1 + zduty * kWaveTableSizeF;
			Z iphase2 = floor(pphase2);
			int index2 = (int)iphase2;
			Z fracphase2 = pphase2 - iphase2;

			phasemod += phasemodStride;
			duty += dutyStride;

			// optional: round off the attenuation of higher harmonics. this eliminates a broadband tick that happens when a straight line decays to zero.
			fractable = sc_scurve0(fractable);

			Z a = oscilLUT2(tableA, tableB, index1, mask, fracphase1, fractable);
			Z b = oscilLUT2(tableA, tableB, index2, mask, fracphase2, fractable);

			Z amp = .25 / (zduty - zduty * zduty);
			out[i] = amp * (a - b);
			
			phase += ffreq * freqmul;
			if (phase >= kWaveTableSizeF) phase -= kWaveTableSizeF;
			if (phase < 0.) phase += kWaveTableSizeF;
		}
	}
};

static void oscp_(Thread& th, Prim* prim)
{
	P<List> tables = th.popZList("oscp : tables");
	V duty = th.popZIn("oscp : phaseOffset");
	V phase = th.popZIn("oscp : phase");
	V freq = th.popZIn("oscp : freq");

	if (!tables->isPacked() || tables->length(th) != kWaveTableTotalSize) {
		post("oscp : tables is not a wave table. must be a signal of %d x %d samples.", kNumTables, kWaveTableSize);
		throw errWrongType;
	}

	th.push(new List(new OscPWM(th, tables->mArray, freq, phase, duty)));
}

static void pulse_(Thread& th, Prim* prim)
{
	V duty = th.popZIn("pulse : phaseOffset");
	V phase = th.popZIn("pulse : phase");
	V freq = th.popZIn("pulse : freq");

	P<List> tables = gSawtoothTable;

	th.push(new List(new OscPWM(th, tables->mArray, freq, phase, duty)));
}

static void vsaw_(Thread& th, Prim* prim)
{
	V duty = th.popZIn("vsaw : phaseOffset");
	V phase = th.popZIn("vsaw : phase");
	V freq = th.popZIn("vsaw : freq");

	P<List> tables = gParabolicTable;

	th.push(new List(new VarSaw(th, tables->mArray, freq, phase, duty)));
}



struct SyncOsc : public TwoInputUGen<SyncOsc>
{
	P<Array> const array;
	Z sinePhaseStart;
	Z sinePhaseReset;
	Z sinePhaseEnd;
    Z wavePhaseResetRatio;
	Z phase1;
	Z phase2a;
	Z phase2b;
	Z freqmul1;
	Z freqmul2;
	Z freqLimit;
	Z* tables;
    bool once = true;
	
	SyncOsc(Thread& th, P<Array> const& inArray, Arg freq1, Arg freq2) : TwoInputUGen<SyncOsc>(th, freq1, freq2),
		array(inArray),
        sinePhaseStart(kSineTableSize/4),
		sinePhaseReset(kSineTableSize/2),
		sinePhaseEnd(sinePhaseStart + sinePhaseReset),
        wavePhaseResetRatio(kWaveTableSizeF / sinePhaseReset),
		phase1(sinePhaseStart), 
        phase2a(0.), 
        phase2b(0.),
		freqmul1(.5 * th.rate.radiansPerSample * gInvSineTableOmega),
		freqmul2(kWaveTableSizeF * th.rate.invSampleRate),
		freqLimit(th.rate.freqLimit),
		tables(array->z())
	{
	}
	
	virtual const char* TypeName() const override { return "SyncOsc"; }
		
	void calc(int n, Z* out, Z* freq1, Z* freq2, int freq1Stride, int freq2Stride)
	{
        if (once) {
            once = false;
            phase2b = kWaveTableSizeF * (fabs(*freq2) / fabs(*freq1));
        }
		const int mask = kWaveTableMask;
		for (int i = 0; i < n; ++i) {
			Z ffreq1 = fabs(*freq1);
			Z ffreq2 = fabs(*freq2);
			freq1 += freq1Stride;
			freq2 += freq2Stride;
									
			Z numHarmonics = std::clamp(freqLimit / fabs(ffreq2), 0., kMaxHarmonicsF);
			Z inumHarmonics = floor(numHarmonics);
			int harmIndex = (int)inumHarmonics;
			Z tableF = lut(gTableForNumHarmonics, harmIndex, numHarmonics - inumHarmonics);
			Z tableI = floor(tableF);
			int tableNum = (int)tableI;
			Z fractable = tableF - tableI;
			
			Z* tableA = tables + kWaveTableSize * tableNum;
			Z* tableB = tableA + kWaveTableSize;
			
			Z iphase2a = floor(phase2a);
			int index2a = (int)iphase2a;
			Z fracphase2a = phase2a - iphase2a;

			Z iphase2b = floor(phase2b);
			int index2b = (int)iphase2b;
			Z fracphase2b = phase2b - iphase2b;

			// optional: round off the attenuation of higher harmonics. this eliminates an (extremely quiet) broadband tick that happens when a straight line decays to zero.
			fractable = sc_scurve0(fractable);

			Z sawA = oscilLUT2(tableA, tableB, index2a, mask, fracphase2a, fractable);
			Z sawB = oscilLUT2(tableA, tableB, index2b, mask, fracphase2b, fractable);
			
			Z window = .5 - .5 * tsinx(phase1);
			out[i] = sawB + window * (sawA - sawB);
            
			Z freq2inc = ffreq2 * freqmul2;

			phase2a += freq2inc;
			if (phase2a >= kWaveTableSizeF) phase2a -= kWaveTableSizeF;

			phase2b += freq2inc;
			if (phase2b >= kWaveTableSizeF) phase2b -= kWaveTableSizeF;

			phase1 += ffreq1 * freqmul1;
			if (phase1 >= sinePhaseEnd) {
				phase1 -= sinePhaseReset;
                
                // reset and swap phases
                phase2b = phase2a;
                phase2a = wavePhaseResetRatio * (phase1 - sinePhaseStart) * (ffreq2 / ffreq1); // reset to proper fractional position.                
			}
		}
	}
};


static void ssaw_(Thread& th, Prim* prim)
{
	V freq2 = th.popZIn("ssaw : freq2");
	V freq1 = th.popZIn("ssaw : freq1");

	P<List> tables = gSawtoothTable;

	th.push(new List(new SyncOsc(th, tables->mArray, freq1, freq2)));
}

static void sosc_(Thread& th, Prim* prim)
{
	P<List> tables = th.popZList("sosc : tables");
	V freq2 = th.popZIn("sosc : freq2");
	V freq1 = th.popZIn("sosc : freq1");

	if (!tables->isPacked() || tables->length(th) != kWaveTableTotalSize) {
		post("sosc : tables is not a wave table. must be a signal of %d x %d samples.", kNumTables, kWaveTableSize);
		throw errWrongType;
	}

	th.push(new List(new SyncOsc(th, tables->mArray, freq1, freq2)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////

struct LFSaw : public OneInputUGen<LFSaw>
{
	Z phase;
	Z freqmul;
	
	LFSaw(Thread& th, Arg freq, Z iphase) : OneInputUGen<LFSaw>(th, freq), phase(sc_wrap(2. * iphase, -1., 1.)), freqmul(th.rate.invNyquistRate)
	{
	}
	
	virtual const char* TypeName() const override { return "LFSaw"; }
	
	void calc(int n, Z* out, Z* freq, int freqStride) 
	{
		for (int i = 0; i < n; ++i) {
			out[i] = phase;
			phase += *freq * freqmul;
			freq += freqStride;
			if (phase >= 1.) phase -= 2.;
			else if (phase < -1.) phase += 2.;
		}
	}
};

struct LFSaw2 : public TwoInputUGen<LFSaw2>
{
	Z phase;
	Z freqmul;
	
	LFSaw2(Thread& th, Arg freq, Arg phasem) : TwoInputUGen<LFSaw2>(th, freq, phasem), phase(0.), freqmul(th.rate.invNyquistRate)
	{
	}
	
	virtual const char* TypeName() const override { return "LFSaw2"; }
	
	void calc(int n, Z* out, Z* freq, Z* phasem, int freqStride, int phasemStride)
	{
		for (int i = 0; i < n; ++i) {
			Z pphase = phase + 2. * *phasem - 1.;
			if (pphase >= 1.) do { pphase -= 2.; } while (pphase >= 1.);
			else if (pphase < -1.) do { pphase += 2.; } while (pphase < -1.);
			out[i] = pphase;
			phase += *freq * freqmul;
			freq += freqStride;
			if (phase >= 1.) phase -= 2.;
			else if (phase < -1.) phase += 2.;
		}
	}
};

struct LFTri : public OneInputUGen<LFTri>
{
	Z phase;
	Z freqmul;
	
	LFTri(Thread& th, Arg freq, Z iphase) : OneInputUGen<LFTri>(th, freq), phase(sc_wrap(2. * iphase, -1., 1.)), freqmul(2. * th.rate.invNyquistRate)
	{
	}
	
	virtual const char* TypeName() const override { return "LFTri"; }
	
	void calc(int n, Z* out, Z* freq, int freqStride) 
	{
		for (int i = 0; i < n; ++i) {
			out[i] = phase <= 1. ? phase : 2. - phase;
			phase += *freq * freqmul;
			freq += freqStride;
			if (phase >= 3.) phase -= 4.;
			else if (phase < -1.) phase += 4.;
		}
	}
};

static void lfsaw_(Thread& th, Prim* prim)
{
	Z phase = th.popFloat("lfsaw : phase");
	V freq = th.popZIn("lfsaw : freq");

	th.push(new List(new LFSaw(th, freq, phase)));
}

static void lftri_(Thread& th, Prim* prim)
{
	Z phase = th.popFloat("lftri : phase");
	V freq = th.popZIn("lftri : freq");

	th.push(new List(new LFTri(th, freq, phase)));
}

struct LFPulse : public TwoInputUGen<LFPulse>
{
	Z phase;
	Z freqmul;
	
	LFPulse(Thread& th, Arg freq, Z iphase, Arg duty) : TwoInputUGen<LFPulse>(th, freq, duty), phase(sc_wrap(iphase, 0., 1.)), freqmul(th.rate.invSampleRate)
	{
	}
	
	virtual const char* TypeName() const override { return "LFPulse"; }
		
	void calc(int n, Z* out, Z* freq, Z* duty, int freqStride, int dutyStride) 
	{
		for (int i = 0; i < n; ++i) {
			if (phase >= 1.) {
				phase -= 1.;
				// output at least one sample from the opposite polarity
				out[i] = *duty < 0.5 ? 1. : 0.;
			} else {
				out[i] = phase < *duty ? 1.f : 0.f;
			}

			phase += *freq * freqmul;
			duty += dutyStride;
			freq += freqStride;
		}
	}
};

struct LFPulseBipolar : public TwoInputUGen<LFPulseBipolar>
{
	Z phase;
	Z freqmul;
	
	LFPulseBipolar(Thread& th, Arg freq, Z iphase, Arg duty) : TwoInputUGen<LFPulseBipolar>(th, freq, duty), phase(sc_wrap(iphase, 0., 1.)), freqmul(th.rate.invSampleRate)
	{
	}
	
	virtual const char* TypeName() const override { return "LFPulseBipolar"; }
		
	void calc(int n, Z* out, Z* freq, Z* duty, int freqStride, int dutyStride) 
	{
		for (int i = 0; i < n; ++i) {
			if (phase >= 1.) {
				phase -= 1.;
				// output at least one sample from the opposite polarity
				out[i] = *duty < 0.5 ? *duty : *duty - 1. ;
			} else {
				out[i] = phase < *duty ?  *duty : *duty - 1.;
			}

			phase += *freq * freqmul;
			duty += dutyStride;
			freq += freqStride;
		}
	}
};

struct LFSquare : public OneInputUGen<LFSquare>
{
	Z phase;
	Z freqmul;
	
	LFSquare(Thread& th, Arg freq, Z iphase) : OneInputUGen<LFSquare>(th, freq), phase(sc_wrap(iphase, 0., 1.)), freqmul(th.rate.invSampleRate)
	{
	}
	
	virtual const char* TypeName() const override { return "LFSquare"; }
		
	void calc(int n, Z* out, Z* freq, int freqStride) 
	{
		for (int i = 0; i < n; ++i) {
			if (phase >= 1.) phase -= 1.;
			out[i] = phase < .5 ? 1. : -1.;
			phase += *freq * freqmul;
			freq += freqStride;
		}
	}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////

struct Vosim : public TwoInputUGen<Vosim>
{
	Z phase;
	Z freqmul;
	Z freqLimit;
	
	Vosim(Thread& th, Arg freq, Z iphase, Arg nth) : TwoInputUGen<Vosim>(th, freq, nth), phase(sc_wrap(2. * iphase, -1., 1.)), freqmul(th.rate.invSampleRate), freqLimit(.5*th.rate.freqLimit)
	{
	}
	
	virtual const char* TypeName() const override { return "SmoothSaw"; }
	
	void calc(int n, Z* out, Z* freq, Z* nth, int freqStride, int nthStride)
	{
		for (int i = 0; i < n; ++i) {
			Z maxnth = (freqLimit / *freq);
			out[i] = sc_squared(std::sin(M_PI*std::min(maxnth, *nth)*phase)) * sc_squared(1.-phase);
			phase += *freq * freqmul;
			freq += freqStride;
			nth += nthStride;
			if (phase >= 1.) phase -= 1.;
			else if (phase < 0.) phase += 1.;
		}
	}
};


static void vosim_(Thread& th, Prim* prim)
{
	V n = th.popZIn("vosim : n");
	Z phase = th.popFloat("vosim : phase");
	V freq = th.popZIn("vosim : freq");

	th.push(new List(new Vosim(th, freq, phase, n)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////

struct SmoothSaw : public TwoInputUGen<SmoothSaw>
{
	Z phase;
	Z freqmul;
	Z freqLimit;
	
	SmoothSaw(Thread& th, Arg freq, Z iphase, Arg nth) : TwoInputUGen<SmoothSaw>(th, freq, nth), phase(sc_wrap(2. * iphase, -1., 1.)), freqmul(th.rate.invNyquistRate), freqLimit(th.rate.freqLimit)
	{
	}
	
	virtual const char* TypeName() const override { return "SmoothSaw"; }
	
	void calc(int n, Z* out, Z* freq, Z* nth, int freqStride, int nthStride)
	{
		for (int i = 0; i < n; ++i) {
			Z maxnth = freqLimit / *freq;
			out[i] = phase-phase*std::pow(std::abs(phase),std::min(maxnth, *nth));
			phase += *freq * freqmul;
			freq += freqStride;
			nth += nthStride;
			if (phase >= 1.) phase -= 2.;
			else if (phase < -1.) phase += 2.;
		}
	}
};

struct SmoothSawPWM : public ThreeInputUGen<SmoothSawPWM>
{
	Z phase;
	Z freqmul;
	Z freqLimit;
	
	SmoothSawPWM(Thread& th, Arg freq, Z iphase, Arg nth, Arg duty) : ThreeInputUGen<SmoothSawPWM>(th, freq, nth, duty), phase(sc_wrap(2. * iphase, -1., 1.)), freqmul(th.rate.invNyquistRate), freqLimit(th.rate.freqLimit)
	{
	}
	
	virtual const char* TypeName() const override { return "SmoothSaw"; }
	
	void calc(int n, Z* out, Z* freq, Z* nth, Z* duty, int freqStride, int nthStride, int dutyStride)
	{
		for (int i = 0; i < n; ++i) {
			Z maxnth = freqLimit / *freq;
			Z w = *duty;
			Z u = .5*phase - .5;
			Z wphase = (w+u)/(w*phase-u);
			out[i] = wphase*(1.-std::pow(std::abs(phase),std::min(maxnth, *nth)));
			phase += *freq * freqmul;
			freq += freqStride;
			nth += nthStride;
			duty += dutyStride;
			if (phase >= 1.) phase -= 2.;
			else if (phase < -1.) phase += 2.;
		}
	}
};


static void smoothsawpwm_(Thread& th, Prim* prim)
{
	V duty = th.popZIn("smoothsawpwm : duty");
	V n = th.popZIn("smoothsawpwm : n");
	Z phase = th.popFloat("smoothsawpwm : phase");
	V freq = th.popZIn("smoothsawpwm : freq");

	th.push(new List(new SmoothSawPWM(th, freq, phase, n, duty)));
}


static void smoothsaw_(Thread& th, Prim* prim)
{
	V n = th.popZIn("smoothsaw : n");
	Z phase = th.popFloat("smoothsaw : phase");
	V freq = th.popZIn("smoothsaw : freq");

	th.push(new List(new SmoothSaw(th, freq, phase, n)));
}

static void lfpulse_(Thread& th, Prim* prim)
{
	V duty = th.popZIn("lfpulse : duty");
	Z phase = th.popFloat("lfpulse : phase");
	V freq = th.popZIn("lfpulse : freq");

	th.push(new List(new LFPulse(th, freq, phase, duty)));
}

static void lfpulseb_(Thread& th, Prim* prim)
{
	V duty = th.popZIn("lfpulseb : duty");
	Z phase = th.popFloat("lfpulseb : phase");
	V freq = th.popZIn("lfpulseb : freq");

	th.push(new List(new LFPulseBipolar(th, freq, phase, duty)));
}

static void lfsquare_(Thread& th, Prim* prim)
{
	Z phase = th.popFloat("lfsquare : phase");
	V freq = th.popZIn("lfsquare : freq");

	th.push(new List(new LFSquare(th, freq, phase)));
}




struct Impulse : public OneInputUGen<Impulse>
{
	Z phase;
	Z freqmul;
	
	Impulse(Thread& th, Arg freq, Z iphase) : OneInputUGen<Impulse>(th, freq), phase(sc_wrap(iphase, 0., 1.)), freqmul(th.rate.invSampleRate)
	{
		if (phase == 0.) phase = 1.; // force an initial impulse.
	}
	
	virtual const char* TypeName() const override { return "Impulse"; }
		
	void calc(int n, Z* out, Z* freq, int freqStride) 
	{
		for (int i = 0; i < n; ++i) {
			if (phase >= 1.) {
				phase -= 1.;
				out[i] = 1.;
			} else {
				out[i] = 0.;
			}
			phase += *freq * freqmul;
			freq += freqStride;
		}
	}
};

static void impulse_(Thread& th, Prim* prim)
{
	Z phase = th.popFloat("impulse : phase");
	V freq = th.popZIn("impulse : freq");

	th.push(new List(new Impulse(th, freq, phase)));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

SinOsc::SinOsc(Thread &th, Arg freq, Z iphase) : OneInputUGen<SinOsc>(th, freq),
		phase(sc_wrap(iphase, 0., 1.) * kTwoPi), freqmul(th.rate.radiansPerSample)
{
}

const char * SinOsc::TypeName() const { return "SinOsc"; }

void SinOsc::calc(int n, Z *out, Z *freq, int freqStride)
{

	for (int i = 0; i < n; ++i) {
		out[i] = phase;
		phase += *freq * freqmul;
		freq += freqStride;
		if (phase >= kTwoPi) phase -= kTwoPi;
		else if (phase < 0.) phase += kTwoPi;
	}
#if SAPF_ACCELERATE
	vvsin(out, out, &n);
#else
	ZArr A = zarr(out, n, 1);
	A = A.sin();
#endif
}

struct SinOsc2 : public OneInputUGen<SinOsc2>
{
	Z phase;
	Z freqmul;
	
	SinOsc2(Thread& th, Arg freq, Z iphase) : OneInputUGen<SinOsc2>(th, freq),
		phase(sc_wrap(iphase, 0., 1.) * kTwoPi), freqmul(th.rate.radiansPerSample)
	{
	}
	
	virtual const char* TypeName() const override { return "SinOsc2"; }
		
	void calc(int n, Z* out, Z* freq, int freqStride) 
	{
		for (int i = 0; i < n; ++i) {
			out[i] = tsin(phase);
			phase += *freq * freqmul;
			freq += freqStride;
			if (phase >= kTwoPi) phase -= kTwoPi;
			else if (phase < 0.) phase += kTwoPi;
		}
	}
};


struct TSinOsc : public OneInputUGen<TSinOsc>
{
	Z phase;
	Z freqmul;
	
	TSinOsc(Thread& th, Arg freq, Z iphase) : OneInputUGen<TSinOsc>(th, freq), phase(sc_wrap(iphase, 0., 1.) * kTwoPi), freqmul(th.rate.radiansPerSample)
	{
	}
	
	virtual const char* TypeName() const override { return "TSinOsc"; }
		
	void calc(int n, Z* out, Z* freq, int freqStride) 
	{
		for (int i = 0; i < n; ++i) {
			out[i] = tsin(phase);
			phase += *freq * freqmul;
			freq += freqStride;
			if (phase >= kTwoPi) phase -= kTwoPi;
			else if (phase < 0.) phase += kTwoPi;
		}
	}
};



struct FSinOsc : public ZeroInputUGen<FSinOsc>
{
	Z freq;
	Z b1, y1, y2;
	
	FSinOsc(Thread& th, Z ifreq, Z iphase) : ZeroInputUGen<FSinOsc>(th, false), 
		freq(ifreq * th.rate.radiansPerSample), 
		b1(2. * cos(freq))
	{
		iphase = sc_wrap(iphase, 0., 1.) * kTwoPi;
		y1 = sin(iphase-freq);
		y2 = sin(iphase-2.*freq);
	}
	
	virtual const char* TypeName() const override { return "FSinOsc"; }
		
	void calc(int n, Z* out) 
	{
		Z zy1 = y1;
		Z zy2 = y2;
		Z zb1 = b1;
		for (int i = 0; i < n; ++i) {
			Z y0 = zb1 * zy1 - zy2;
			out[i] = y0;
			zy2 = zy1;
			zy1 = y0;
		}
		y1 = zy1;
		y2 = zy2;
	}
};




struct SinOscPMFB : public TwoInputUGen<SinOscPMFB>
{
	Z phase;
	Z freqmul;
	Z y1;
	
	SinOscPMFB(Thread& th, Arg freq, Z iphase, Arg phasefb) : TwoInputUGen<SinOscPMFB>(th, freq, phasefb), phase(sc_wrap(iphase, 0., 1.) * kTwoPi), freqmul(th.rate.radiansPerSample)
	{
		freqmul = th.rate.radiansPerSample;
	}
	
	virtual const char* TypeName() const override { return "SinOscPMFB"; }
		
	void calc(int n, Z* out, Z* freq, Z* phasefb, int freqStride, int phasefbStride) 
	{
		for (int i = 0; i < n; ++i) {
			Z y0 = sin(phase + *phasefb * y1);
			out[i] = y0;
			y1 = y0;
			phase += *freq * freqmul;
			freq += freqStride;
			phasefb += phasefbStride;
			if (phase >= kTwoPi) phase -= kTwoPi;
			else if (phase < 0.) phase += kTwoPi;
		}
	}
};

SinOscPM::SinOscPM(Thread& th, Arg freq, Arg phasemod) : TwoInputUGen<SinOscPM>(th, freq, phasemod), phase(0.), freqmul(th.rate.radiansPerSample)
{
	freqmul = th.rate.radiansPerSample;
}

const char * SinOscPM::TypeName() const { return "SinOscPM"; }

void SinOscPM::calc(int n, Z *out, Z *freq, Z *phasemod, int freqStride, int phasemodStride)
{
	for (int i = 0; i < n; ++i) {
		out[i] = phase + *phasemod * kTwoPi;
		phase += *freq * freqmul;
		freq += freqStride;
		phasemod += phasemodStride;
		if (phase >= kTwoPi) phase -= kTwoPi;
		else if (phase < 0.) phase += kTwoPi;
	}
#if SAPF_ACCELERATE
	vvsin(out, out, &n);
#else
	ZArr A = zarr(out, n, 1);
	A = A.sin();
#endif
}

struct SinOscM : public ThreeInputUGen<SinOscM>
{
	Z phase;
	Z freqmul;
	
	SinOscM(Thread& th, Arg freq, Z iphase, Arg mul, Arg add) : ThreeInputUGen<SinOscM>(th, freq, mul, add), phase(sc_wrap(iphase, 0., 1.) * kTwoPi), freqmul(th.rate.radiansPerSample)
	{
	}
	
	virtual const char* TypeName() const override { return "SinOscM"; }
		
	void calc(int n, Z* out, Z* freq, Z* mul, Z* add, int freqStride, int mulStride, int addStride) 
	{
		for (int i = 0; i < n; ++i) {
			out[i] = sin(phase) * *mul + *add;
			phase += *freq * freqmul;
			freq += freqStride;
			mul += mulStride;
			add += addStride;
			if (phase >= kTwoPi) phase -= kTwoPi;
			else if (phase < 0.) phase += kTwoPi;
		}
	}
};


struct SinOscPMM : public FourInputUGen<SinOscPMM>
{
	Z phase;
	Z freqmul;
	
	SinOscPMM(Thread& th, Arg freq, Arg phasemod, Arg mul, Arg add)
		: FourInputUGen<SinOscPMM>(th, freq, phasemod, mul, add), phase(0.), freqmul(th.rate.radiansPerSample)
	{
	}
	
	virtual const char* TypeName() const override { return "SinOscPMM"; }
		
	void calc(int n, Z* out, Z* freq, Z* phasemod, Z* mul, Z* add, int freqStride, int phasemodStride, int mulStride, int addStride) 
	{
		for (int i = 0; i < n; ++i) {
			out[i] = sin(phase + *phasemod) * *mul + *add;
			phase += *freq * freqmul;
			freq += freqStride;
			phasemod += phasemodStride;
			mul += mulStride;
			add += addStride;
			if (phase >= kTwoPi) phase -= kTwoPi;
			else if (phase < 0.) phase += kTwoPi;
		}
	}
};

static void tsinosc_(Thread& th, Prim* prim)
{
	Z phase = th.popFloat("tsinosc : iphase");
	V freq = th.popZIn("tsinosc : freq");

    th.push(new List(new SinOsc2(th, freq, phase)));
}

static void sinosc_(Thread& th, Prim* prim)
{
	V phase = th.popZIn("sinosc : phase");
	V freq = th.popZIn("sinosc : freq");

	if (phase.isZList()) {
		th.push(new List(new SinOscPM(th, freq, phase)));
	} else if (freq.isZList()) {
		th.push(new List(new SinOsc(th, freq, phase.f)));
	} else {
		th.push(new List(new FSinOsc(th, freq.f, phase.f)));
	}
}

static void sinoscm_(Thread& th, Prim* prim)
{
	V add = th.popZIn("sinoscm : mul");
	V mul = th.popZIn("sinoscm : add");
	V phase = th.popZIn("sinoscm : phase");
	V freq = th.popZIn("sinoscm : freq");

	if (phase.isZList()) {
		th.push(new List(new SinOscPMM(th, freq, phase, mul, add)));
	} else {
		th.push(new List(new SinOscM(th, freq, phase.f, mul, add)));
	}
}


static void sinoscfb_(Thread& th, Prim* prim)
{
	V fb = th.popZIn("sinoscfb : fb");
	Z iphase = th.popFloat("sinoscfb : phase");
	V freq = th.popZIn("sinoscfb : freq");

	th.push(new List(new SinOscPMFB(th, freq, iphase, fb)));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

struct Blip : public TwoInputUGen<Blip>
{
	Z phase;
	Z freqmul;
	Z nyq_;
	Blip(Thread& th, Arg freq, Z iphase, Arg numharms)
		: TwoInputUGen<Blip>(th, freq, numharms), phase(sc_wrap(2. * iphase - 1., -1., 1.)), freqmul(th.rate.radiansPerSample),
		nyq_(th.rate.sampleRate * .5)
	{
	}
	
	virtual const char* TypeName() const override { return "Blip"; }
	
	void calc(int n, Z* out, Z* freq, Z* numharms, int freqStride, int numharmsStride) 
	{
		Z nyq = nyq_;
		
		for (int i = 0; i < n; ++i) {

			//f(x)=x-x*sqrt(c^2+1)/sqrt(c^2*x^2+1)
			Z ffreq = *freq * freqmul;

			Z maxN = floor(nyq / ffreq);
			Z N = *numharms;
			
			if (N > maxN) N = maxN;
			else if (N < 1.) N = 1.;
			
			Z Na = floor(N);
			Z Nb = Na + 1.;
			
			Z frac = N - Na;
			Z Na_scale = .5 / Na;
			Z Nb_scale = .5 / Nb;

			Z Na2 = 2. * Na + 1.;
			Z Nb2 = Na2 + 2.;

			Z d = 1. / sin(phase);
			Z a = Na_scale * (sin(Na2 * phase) * d - 1.);
			Z b = Nb_scale * (sin(Nb2 * phase) * d - 1.);

			frac = sc_scurve0(frac); // this eliminates out a broadband tick in the spectrum.

			out[i] = a + frac * (b - a);

			phase += ffreq;
			freq += freqStride;
			numharms += numharmsStride;
			if (phase >= kTwoPi) phase -= kTwoPi;
			else if (phase < -kTwoPi) phase += kTwoPi;
		}
	}
};

static void blip_(Thread& th, Prim* prim)
{
	V numharms = th.popZIn("blip : numharms");
	Z phase = th.popFloat("blip : phase");
	V freq = th.popZIn("blip : freq");

	th.push(new List(new Blip(th, freq, phase, numharms)));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

struct DSF1 : public FourInputUGen<DSF1>
{
	Z phase1;
	Z phase2;
	Z freqmul;
	Z N, N1;
	DSF1(Thread& th, Arg freq, Arg carRatio, Arg modRatio, Arg coef, Z numharms)
		: FourInputUGen<DSF1>(th, freq, carRatio, modRatio, coef), phase1(0.), phase2(0.), freqmul(th.rate.radiansPerSample)
	{
		N = numharms < 1. ? 1. : floor(numharms);
		N1 = N + 1.;
	}
	
	virtual const char* TypeName() const override { return "DSF1"; }
	
	void calc(int n, Z* out, Z* freq, Z* carRatio, Z* modRatio, Z* coef, int freqStride, int carStride, int modStride, int coefStride) 
	{
		Z p1 = phase1;
		Z p2 = phase2;
		for (int i = 0; i < n; ++i) {

			//f(x)=x-x*sqrt(c^2+1)/sqrt(c^2*x^2+1)

			Z a = *coef;
			Z a2 = a*a;
			Z an1 = pow(a, N1);
			Z scale = (a - 1.)/(an1 - 1.);
			out[i] = scale * (sin(p1) - a * sin(p1-p2) - an1 * (sin(p1 + N1*p2) - a * sin(p1 + N*p2)))/(1. + a2 - 2. * a * cos(p2));
printf("%d %f\n", i, out[i]);
			Z ffreq = *freq * freqmul;
			Z f1 = ffreq * *carRatio;
			Z f2 = ffreq * *modRatio;
			p1 += f1;
			p2 += f2;
			freq += freqStride;
			carRatio += carStride;
			modRatio += modStride;
			coef += coefStride;
			if (p1 >= kTwoPi) p1 -= kTwoPi;
			else if (p1 < -kTwoPi) p1 += kTwoPi;
			if (p2 >= kTwoPi) p2 -= kTwoPi;
			else if (p2 < -kTwoPi) p2 += kTwoPi;
		}
		phase1 = p1;
		phase2 = p2;
	}
};

static void dsf1_(Thread& th, Prim* prim)
{
	Z numharms = th.popFloat("dsf1 : numharms");
	V coef = th.popZIn("dsf1 : coef");
	V modRatio = th.popZIn("dsf1 : modRatio");
	V carRatio = th.popZIn("dsf1 : carRatio");
	V freq = th.popZIn("dsf1 : freq");

	th.push(new List(new DSF1(th, freq, carRatio, modRatio, coef, numharms)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////

struct DSF3 : public FourInputUGen<DSF3>
{
	Z phase1;
	Z phase2;
	Z freqmul;
	Z N, N1;
	DSF3(Thread& th, Arg freq, Arg carRatio, Arg modRatio, Arg coef, Z numharms)
		: FourInputUGen<DSF3>(th, freq, carRatio, modRatio, coef), phase1(0.), phase2(0.), freqmul(th.rate.radiansPerSample)
	{
		N = numharms < 1. ? 1. : floor(numharms);
		N1 = N + 1.;
	}
	
	virtual const char* TypeName() const override { return "DSF3"; }
	
	void calc(int n, Z* out, Z* freq, Z* carRatio, Z* modRatio, Z* coef, int freqStride, int carStride, int modStride, int coefStride) 
	{
		Z p1 = phase1;
		Z p2 = phase2;
		for (int i = 0; i < n; ++i) {

			//f(x)=x-x*sqrt(c^2+1)/sqrt(c^2*x^2+1)

			Z a = std::clamp(*coef, -.9999, .9999);
			Z a2 = a*a;
			Z an1 = pow(a, N1);
			Z scalePeak = (a - 1.)/(2.*an1 - a - 1.);
			Z scale = scalePeak;
			Z denom = (1. + a2 - 2. * a * cos(p2));
			out[i] = scale * sin(p1) * (1. - a2 - 2. * an1 * (cos(N1*p2) - a * cos(N*p2)))/denom;

			Z ffreq = *freq * freqmul;
			Z f1 = ffreq * *carRatio;
			Z f2 = ffreq * *modRatio;
			p1 += f1;
			p2 += f2;
			freq += freqStride;
			carRatio += carStride;
			modRatio += modStride;
			coef += coefStride;
			if (p1 >= kTwoPi) p1 -= kTwoPi;
			else if (p1 < -kTwoPi) p1 += kTwoPi;
			if (p2 >= kTwoPi) p2 -= kTwoPi;
			else if (p2 < -kTwoPi) p2 += kTwoPi;
		}
		phase1 = p1;
		phase2 = p2;
	}
};

static void dsf3_(Thread& th, Prim* prim)
{
	Z numharms = th.popFloat("dsf3 : numharms");
	V coef = th.popZIn("dsf3 : coef");
	V modRatio = th.popZIn("dsf3 : modRatio");
	V carRatio = th.popZIn("dsf3 : carRatio");
	V freq = th.popZIn("dsf3 : freq");

	th.push(new List(new DSF3(th, freq, carRatio, modRatio, coef, numharms)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////



////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct KlangOsc
{
	KlangOsc(Arg f, Arg a, Z p) :
		freq(f), amp(a), phase(p) {}
		
	ZIn freq;
	ZIn amp;
	Z phase;
};

struct Klang : public Gen
{
	std::vector<KlangOsc> _oscs;
	Z _freqmul, _K;
	Z _nyq, _cutoff, _slope;
	
	Klang(Thread& th, V freqs, V amps, V phases)
		: Gen(th, itemTypeZ, false),
			_freqmul(th.rate.radiansPerSample),
			_K(log001 / th.rate.sampleRate),
			_nyq(th.rate.sampleRate * .5),
			_cutoff(_nyq * .8),
			_slope(1. / (_nyq - _cutoff))
	{
		int64_t numOscs = LONG_MAX;
		if (freqs.isVList()) { 
			freqs = ((List*)freqs.o())->pack(th); 
			numOscs = std::min(numOscs, freqs.length(th));
		}
		if (amps.isVList()) { 
			amps = ((List*)amps.o())->pack(th); 
			numOscs = std::min(numOscs, amps.length(th));
		}
		if (phases.isList()) {
			phases = ((List*)phases.o())->pack(th);
			numOscs = std::min(numOscs, phases.length(th));
		}
		
		if (numOscs == LONG_MAX) numOscs = 1;
		
		for (int64_t i = 0; i < numOscs; ++i) {
			KlangOsc kf(freqs.at(i), amps.at(i), phases.atz(i));
			_oscs.push_back(kf);
		}
		
	}
		
	virtual const char* TypeName() const override { return "Klang"; }
	
	virtual void pull(Thread& th) override
	{		
		Z* out0 = mOut->fulfillz(mBlockSize);
		memset(out0, 0, mBlockSize * sizeof(Z));
		int maxToFill = 0;
		
		Z freqmul = _freqmul;
		Z nyq = _nyq;
		Z cutoff = _cutoff;
		Z slope = _slope;
		for (size_t osc = 0; osc < _oscs.size(); ++osc) {
			int framesToFill = mBlockSize;
			KlangOsc& ko = _oscs[osc];
			Z phase = ko.phase;

			Z* out = out0;
			while (framesToFill) {
				Z *freq, *amp;
				int n, freqStride, ampStride;
				n = framesToFill;
				if (ko.freq(th, n, freqStride, freq) || ko.amp(th, n, ampStride, amp)) {
					setDone();
					maxToFill = std::max(maxToFill, framesToFill);
					break;
				}
				
				for (int i = 0; i < n; ++i) {
					Z ffreq = *freq;
					if (ffreq > cutoff) {
						if (ffreq < nyq) {
							out[i] +=  (cutoff - ffreq) * slope * *amp * tsin(phase);
						}
					} else {
						out[i] += *amp * tsin(phase);
					}
					phase += ffreq * freqmul;
					if (phase >= kTwoPi) phase -= kTwoPi;
					else if (phase < 0.) phase += kTwoPi;
					freq += freqStride;
					amp += ampStride;
				}
				framesToFill -= n;
				out += n;
				ko.freq.advance(n);
				ko.amp.advance(n);
			}
			ko.phase = phase;
		}
		produce(maxToFill);
	}
};

static void klang_(Thread& th, Prim* prim)
{
	V phases	= th.popZInList("klang : phases");
	V amps		= th.popZInList("klang : amps");
	V freqs     = th.popZInList("klang : freqs");
	
	if (freqs.isVList() && !freqs.isFinite())
		indefiniteOp("klank : freqs", "");

	if (amps.isVList() && !amps.isFinite())
		indefiniteOp("klank : amps", "");

	if (phases.isVList() && !phases.isFinite())
		indefiniteOp("klank : phases", "");

	th.push(new List(new Klang(th, freqs, amps, phases)));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////


#define DEF(NAME, TAKES, LEAVES, HELP) 	vm.def(#NAME, TAKES, LEAVES, NAME##_, HELP);
#define DEFMCX(NAME, N, HELP) 	vm.defmcx(#NAME, N, NAME##_, HELP);
#define DEFAM(NAME, MASK, HELP) 	vm.defautomap(#NAME, #MASK, NAME##_, HELP);

void AddOscilUGenOps()
{
	fillHarmonicsTable();

	vm.addBifHelp("\n*** wavetable generation ***");
	DEFAM(wavefill, aak, "(amps phases smooth -> wavetable) generates a set 1/3 octave wavetables for table lookup oscillators. sin(i*theta + phases[i])*amps[i]*pow(cos(pi*i/n), smooth). smoothing reduces Gibb's phenomenon. zero is no smoothing")
	
	makeClassicWavetables();

	vm.addBifHelp("\n*** oscillator unit generators ***");
	
	DEFMCX(osc, 3, "(freq phase wavetable --> out) band limited wave table oscillator. wavetable is a table created with wavefill.")
	DEFMCX(oscp, 4, "(freq phase phaseOffset wavetable --> out) band limited wave table oscillator pair with phase offset.")
	DEFMCX(sosc, 2, "(freq1 freq2 wavetable --> out) band limited hard sync wave table oscillator. freq1 is the fundamental. freq2 is the slave oscil frequency.")

	DEFMCX(par, 2, "(freq phase --> out) band limited parabolic wave oscillator.")
	DEFMCX(tri, 2, "(freq phase --> out) band limited triangle wave oscillator.")
	DEFMCX(square, 2, "(freq phase --> out) band limited square wave oscillator.")
	DEFMCX(saw, 2, "(freq phase --> out) band limited sawtooth wave oscillator.")
	DEFMCX(pulse, 3, "(freq phase duty --> out) band limited pulse wave oscillator.")
	DEFMCX(vsaw, 3, "(freq phase duty --> out) band limited variable sawtooth oscillator.")
	DEFMCX(ssaw, 2, "(freq1 freq2 --> out) band limited hard sync sawtooth oscillator. freq1 is the fundamental. freq2 is the slave oscil frequency.")

	DEFMCX(blip, 3, "(freq phase numharms --> out) band limited impulse oscillator.")
	DEFMCX(dsf1, 5, "(freq carrierRatio modulatorRatio ampCoef numharms --> out) bandlimited partials with geometric series amplitudes. J.A.Moorer's equation 1")
	DEFMCX(dsf3, 5, "(freq carrierRatio modulatorRatio ampCoef numharms --> out) two sided bandlimited partials with geometric series amplitudes. J.A.Moorer's equation 3")
	
	DEFMCX(lftri, 2, "(freq phase --> out) non band limited triangle wave oscillator.")
	DEFMCX(lfsaw, 2, "(freq phase --> out) non band limited sawtooth wave oscillator.")
	DEFMCX(lfpulse, 3, "(freq phase duty --> out) non band limited unipolar pulse wave oscillator.")
	DEFMCX(lfpulseb, 3, "(freq phase duty --> out) non band limited bipolar pulse wave oscillator.")
	DEFMCX(lfsquare, 2, "(freq phase --> out) non band limited square wave oscillator.")
	DEFMCX(impulse, 2, "(freq phase --> out) non band limited single sample impulse train oscillator.")
	DEFMCX(smoothsaw, 3, "(freq phase nth --> out) smoothed sawtooth.")
	DEFMCX(smoothsawpwm, 4, "(freq phase nth duty --> out) smoothed sawtooth.")
	DEFMCX(vosim, 3, "(freq phase nth --> out) vosim sim.")
	DEFMCX(sinosc, 2, "(freq phase --> out) sine wave oscillator.")
	DEFMCX(tsinosc, 2, "(freq iphase --> out) sine wave oscillator.")
	DEFMCX(sinoscfb, 3, "(freq phase feedback --> out) sine wave oscillator with self feedback phase modulation")
	DEFMCX(sinoscm, 4, "(freq phase mul add --> out) sine wave oscillator with multiply and add.")

	DEF(klang, 3, 1, "(freqs amps iphases --> out) a sine oscillator bank. freqs amps and iphases are arrays.")
}



