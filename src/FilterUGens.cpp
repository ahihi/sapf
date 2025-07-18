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

#include "FilterUGens.hpp"
#include "UGen.hpp"

#include "VM.hpp"
#include "clz.hpp"
#include <climits>
#include <cmath>
#include <float.h>
#include <vector>
#include <algorithm>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// classes for specializing feedback

struct NormalFeedback : public Gen
{
	static inline double feedback(double x) { return x; }
};

struct TanhApproximationFeedback : public Gen
{
	static inline double feedback(double x) { return sc_tanh_approx(x); }
};

struct UnityHardClipFeedback : public Gen
{
	static inline double feedback(double x)
	{
		if (x <= -1.) return -1.;
		if (x >=  1.) return  1.;
		return x;
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct Lag : public Gen
{
	ZIn _in;
	ZIn _lagTime;
	Z _y1;
	Z _lagmul;
	bool once;
	
	Lag(Thread& th, Arg in, Arg lagTime)
		: Gen(th, itemTypeZ, mostFinite(in, lagTime)), _in(in), _lagTime(lagTime), _y1(0.), _lagmul(log001 * th.rate.invSampleRate), once(true)
	{
	}
	
	virtual const char* TypeName() const override { return "Lag"; }
	
	virtual void pull(Thread& th) override
	{
		if (once) {
			once = false;
			_in.peek(th, _y1);
		}
		int framesToFill = mBlockSize;
		
		Z* out = mOut->fulfillz(framesToFill);
		Z y1 = _y1;
		Z lagmul = _lagmul;
		while (framesToFill) {
			Z *in, *lagTime;
			int n, inStride, lagTimeStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _lagTime(th, n, lagTimeStride, lagTime)) {
				setDone();
				break;
			}
			
			if (lagTimeStride == 0) {
				Z b1 = std::max(0., 1. + lagmul / *lagTime); // this is an approximation to exp(log001 * th.rate.invSampleRate / *lagTime)  
				for (int i = 0; i < n; ++i) {
					Z y0 = *in;
					out[i] = y1 = y0 + b1 * (y1 - y0);
					in += inStride;
				}
			} else {			
				for (int i = 0; i < n; ++i) {
					Z b1 = std::max(0., 1. + lagmul / *lagTime); // this is an approximation to exp(log001 * th.rate.invSampleRate / *lagTime)  
					Z y0 = *in;
					out[i] = y1 = y0 + b1 * (y1 - y0);
					in += inStride;
					lagTime += lagTimeStride;
				}
			}
			
			framesToFill -= n;
			out += n;
			_in.advance(n);
			_lagTime.advance(n);
		}
		
		_y1 = y1;
		produce(framesToFill);
	}
	
};

static void lag_(Thread& th, Prim* prim)
{
	V lagTime = th.popZIn("lag : lagTime");
	V in = th.popZIn("lag : in");

	th.push(new List(new Lag(th, in, lagTime)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct Lag2 : public Gen
{
	ZIn _in;
	ZIn _lagTime;
	Z _y1a;
	Z _y1b;
	Z _lagmul;
	bool once;
	
	Lag2(Thread& th, Arg in, Arg lagTime)
		: Gen(th, itemTypeZ, mostFinite(in, lagTime)), _in(in), _lagTime(lagTime), _y1a(0.), _y1b(0.), _lagmul(log001 * th.rate.invSampleRate), once(true)
	{
	}
	
	virtual const char* TypeName() const override { return "Lag2"; }
	
	virtual void pull(Thread& th) override
	{
		if (once) {
			once = false;
			_in.peek(th, _y1a);
			_y1b = _y1a;
		}
		int framesToFill = mBlockSize;
		
		Z* out = mOut->fulfillz(framesToFill);
		Z y1a = _y1a;
		Z y1b = _y1b;
		Z lagmul = _lagmul;
		while (framesToFill) {
			Z *in, *lagTime;
			int n, inStride, lagTimeStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _lagTime(th, n, lagTimeStride, lagTime)) {
				setDone();
				break;
			}
			
			if (lagTimeStride == 0) {
				Z b1 = std::max(0., 1. + lagmul / *lagTime); // this is an approximation to exp(log001 * th.rate.invSampleRate / *lagTime)  
				for (int i = 0; i < n; ++i) {
					Z y0a = *in;
					y1a = y0a + b1 * (y1a - y0a);
					y1b = y1a + b1 * (y1b - y1a);
					out[i] = y1b;
					in += inStride;
				}
			} else {			
				for (int i = 0; i < n; ++i) {
					Z b1 = std::max(0., 1. + lagmul / *lagTime); // this is an approximation to exp(log001 * th.rate.invSampleRate / *lagTime)  
					Z y0a = *in;
					y1a = y0a + b1 * (y1a - y0a);
					y1b = y1a + b1 * (y1b - y1a);
					out[i] = y1b;
					in += inStride;
					lagTime += lagTimeStride;
				}
			}
			
			framesToFill -= n;
			out += n;
			_in.advance(n);
			_lagTime.advance(n);
		}
		
		_y1a = y1a;
		_y1b = y1b;
		produce(framesToFill);
	}
	
};

static void lag2_(Thread& th, Prim* prim)
{
	V lagTime = th.popZIn("lag2 : lagTime");
	V in = th.popZIn("lag2 : in");

	th.push(new List(new Lag2(th, in, lagTime)));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct Lag3 : public Gen
{
	ZIn _in;
	ZIn _lagTime;
	Z _y1a;
	Z _y1b;
	Z _y1c;
	Z _lagmul;
	bool once;
	
	Lag3(Thread& th, Arg in, Arg lagTime)
		: Gen(th, itemTypeZ, mostFinite(in, lagTime)), _in(in), _lagTime(lagTime), _y1a(0.), _y1b(0.), _y1c(0.), _lagmul(log001 * th.rate.invSampleRate), once(true)
	{
	}
	
	virtual const char* TypeName() const override { return "Lag3"; }
	
	virtual void pull(Thread& th) override
	{
		if (once) {
			once = false;
			_in.peek(th, _y1a);
			_y1b = _y1a;
			_y1c = _y1a;
		}
		int framesToFill = mBlockSize;
		
		Z* out = mOut->fulfillz(framesToFill);
		Z y1a = _y1a;
		Z y1b = _y1b;
		Z y1c = _y1c;
		Z lagmul = _lagmul;
		while (framesToFill) {
			Z *in, *lagTime;
			int n, inStride, lagTimeStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _lagTime(th, n, lagTimeStride, lagTime)) {
				setDone();
				break;
			}
			
			if (lagTimeStride == 0) {
				Z b1 = std::max(0., 1. + lagmul / *lagTime); // this is an approximation to exp(log001 * th.rate.invSampleRate / *lagTime)  
				for (int i = 0; i < n; ++i) {
					Z y0a = *in;
					y1a = y0a + b1 * (y1a - y0a);
					y1b = y1a + b1 * (y1b - y1a);
					y1c = y1b + b1 * (y1c - y1b);
					out[i] = y1c;
					in += inStride;
				}
			} else {			
				for (int i = 0; i < n; ++i) {
					Z b1 = std::max(0., 1. + lagmul / *lagTime); // this is an approximation to exp(log001 * th.rate.invSampleRate / *lagTime)  
					Z y0a = *in;
					y1a = y0a + b1 * (y1a - y0a);
					y1b = y1a + b1 * (y1b - y1a);
					y1c = y1b + b1 * (y1c - y1b);
					out[i] = y1c;
					in += inStride;
					lagTime += lagTimeStride;
				}
			}
			
			framesToFill -= n;
			out += n;
			_in.advance(n);
			_lagTime.advance(n);
		}
		
		_y1a = y1a;
		_y1b = y1b;
		_y1c = y1c;
		produce(framesToFill);
	}
	
};

static void lag3_(Thread& th, Prim* prim)
{
	V lagTime = th.popZIn("lag3 : lagTime");
	V in = th.popZIn("lag3 : in");

	th.push(new List(new Lag3(th, in, lagTime)));
}




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct LagUD : public Gen
{
	ZIn _in;
	ZIn _riseTime;
	ZIn _fallTime;
	Z _y1;
	Z _lagmul;
	bool once;
	
	LagUD(Thread& th, Arg in, Arg riseTime, Arg fallTime)
		: Gen(th, itemTypeZ, mostFinite(in, riseTime, fallTime)), _in(in), _riseTime(riseTime), _fallTime(fallTime), _y1(0.), _lagmul(log001 * th.rate.invSampleRate), once(true)
	{
	}
	
	virtual const char* TypeName() const override { return "LagUD"; }
	
	virtual void pull(Thread& th) override
	{
		if (once) {
			once = false;
			_in.peek(th, _y1);
		}
		int framesToFill = mBlockSize;
		
		Z* out = mOut->fulfillz(framesToFill);
		Z y1 = _y1;
		Z lagmul = _lagmul;
		while (framesToFill) {
			Z *in, *riseTime, *fallTime;
			int n, inStride, riseTimeStride, fallTimeStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _riseTime(th, n, riseTimeStride, riseTime) || _fallTime(th, n, fallTimeStride, fallTime)) {
				setDone();
				break;
			}
			
			if (riseTimeStride == 0 && fallTimeStride == 0) {
				Z b1r = std::max(0., 1. + lagmul / *riseTime); // this is an approximation to exp(log001 * th.rate.invSampleRate / *riseTime)  
				Z b1f = std::max(0., 1. + lagmul / *fallTime);   
				for (int i = 0; i < n; ++i) {
					Z y0 = *in;
					Z b1 = y0 > y1 ? b1r : b1f;
					out[i] = y1 = y0 + b1 * (y1 - y0);
					
					in += inStride;
				}
			} else {			
				for (int i = 0; i < n; ++i) {
					Z y0 = *in;
					Z lagTime = y0 > y1 ? *riseTime : *fallTime;
					Z b1 = std::max(0., 1. + lagmul / lagTime); // this is an approximation to exp(log001 * th.rate.invSampleRate / *riseTime)  
					out[i] = y1 = y0 + b1 * (y1 - y0);
					
					in += inStride;
					riseTime += riseTimeStride;
					fallTime += fallTimeStride;
				}
			}
			
			framesToFill -= n;
			out += n;
			_in.advance(n);
			_riseTime.advance(n);
			_fallTime.advance(n);
		}
		
		_y1 = y1;
		produce(framesToFill);
	}
	
};

static void lagud_(Thread& th, Prim* prim)
{
	V fallTime = th.popZIn("lagud : fallTime");
	V riseTime = th.popZIn("lagud : riseTime");
	V in = th.popZIn("lagud : in");

	th.push(new List(new LagUD(th, in, riseTime, fallTime)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct LagUD2 : public Gen
{
	ZIn _in;
	ZIn _riseTime;
	ZIn _fallTime;
	Z _y1a;
	Z _y1b;
	Z _lagmul;
	bool once;
	
	LagUD2(Thread& th, Arg in, Arg riseTime, Arg fallTime)
		: Gen(th, itemTypeZ, mostFinite(in, riseTime, fallTime)), _in(in), _riseTime(riseTime), _fallTime(fallTime), 
			_y1a(0.), _y1b(0.), _lagmul(log001 * th.rate.invSampleRate), once(true)
	{
	}
	
	virtual const char* TypeName() const override { return "LagUD2"; }
	
	virtual void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		if (once) {
			once = false;
			_in.peek(th, _y1a);
			_y1b = _y1a;
		}
		
		Z* out = mOut->fulfillz(framesToFill);
		Z y1a = _y1a;
		Z y1b = _y1b;
		Z lagmul = _lagmul;
		while (framesToFill) {
			Z *in, *riseTime, *fallTime;
			int n, inStride, riseTimeStride, fallTimeStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _riseTime(th, n, riseTimeStride, riseTime) || _fallTime(th, n, fallTimeStride, fallTime)) {
				setDone();
				break;
			}
			
			if (riseTimeStride == 0 && fallTimeStride == 0) {
				Z b1r = std::max(0., 1. + lagmul / *riseTime); // this is an approximation to exp(log001 * th.rate.invSampleRate / *riseTime)  
				Z b1f = std::max(0., 1. + lagmul / *fallTime);   
				for (int i = 0; i < n; ++i) {
					Z y0a = *in;
					y1a = y0a + (y0a > y1a ? b1r : b1f) * (y1a - y0a);
					y1b = y1a + (y1a > y1b ? b1r : b1f) * (y1b - y1a);
					out[i] = y1b;
					
					in += inStride;
				}
			} else {			
				for (int i = 0; i < n; ++i) {
					Z b1r = std::max(0., 1. + lagmul / *riseTime); // this is an approximation to exp(log001 * th.rate.invSampleRate / *riseTime)  
					Z b1f = std::max(0., 1. + lagmul / *fallTime);   

					Z y0a = *in;
					y1a = y0a + (y0a > y1a ? b1r : b1f) * (y1a - y0a);
					y1b = y1a + (y1a > y1b ? b1r : b1f) * (y1b - y1a);
					out[i] = y1b;
					
					in += inStride;
					riseTime += riseTimeStride;
					fallTime += fallTimeStride;
				}
			}
			
			framesToFill -= n;
			out += n;
			_in.advance(n);
			_riseTime.advance(n);
			_fallTime.advance(n);
		}
		
		_y1a = y1a;
		_y1b = y1b;
		produce(framesToFill);
	}
	
};

static void lagud2_(Thread& th, Prim* prim)
{
	V fallTime = th.popZIn("lagud2 : fallTime");
	V riseTime = th.popZIn("lagud2 : riseTime");
	V in = th.popZIn("lagud2 : in");

	th.push(new List(new LagUD2(th, in, riseTime, fallTime)));
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct LagUD3 : public Gen
{
	ZIn _in;
	ZIn _riseTime;
	ZIn _fallTime;
	Z _y1a;
	Z _y1b;
	Z _y1c;
	Z _lagmul;
	bool once;
	
	LagUD3(Thread& th, Arg in, Arg riseTime, Arg fallTime)
		: Gen(th, itemTypeZ, mostFinite(in, riseTime, fallTime)), _in(in), _riseTime(riseTime), _fallTime(fallTime), 
			_y1a(0.), _y1b(0.), _y1c(0.), _lagmul(log001 * th.rate.invSampleRate), once(true)
	{
	}
	
	virtual const char* TypeName() const override { return "LagUD3"; }
	
	virtual void pull(Thread& th) override
	{
		if (once) {
			once = false;
			_in.peek(th, _y1a);
			_y1b = _y1a;
			_y1c = _y1a;
		}
		int framesToFill = mBlockSize;
		
		Z* out = mOut->fulfillz(framesToFill);
		Z y1a = _y1a;
		Z y1b = _y1b;
		Z y1c = _y1c;
		Z lagmul = _lagmul;
		while (framesToFill) {
			Z *in, *riseTime, *fallTime;
			int n, inStride, riseTimeStride, fallTimeStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _riseTime(th, n, riseTimeStride, riseTime) || _fallTime(th, n, fallTimeStride, fallTime)) {
				setDone();
				break;
			}
			
			if (riseTimeStride == 0 && fallTimeStride == 0) {
				Z b1r = std::max(0., 1. + lagmul / *riseTime); // this is an approximation to exp(log001 * th.rate.invSampleRate / *riseTime)  
				Z b1f = std::max(0., 1. + lagmul / *fallTime);   
				for (int i = 0; i < n; ++i) {
					Z y0a = *in;
					y1a = y0a + (y0a > y1a ? b1r : b1f) * (y1a - y0a);
					y1b = y1a + (y1a > y1b ? b1r : b1f) * (y1b - y1a);
					y1c = y1b + (y1b > y1c ? b1r : b1f) * (y1c - y1b);
					out[i] = y1c;
					
					in += inStride;
				}
			} else {			
				for (int i = 0; i < n; ++i) {
					Z b1r = std::max(0., 1. + lagmul / *riseTime); // this is an approximation to exp(log001 * th.rate.invSampleRate / *riseTime)  
					Z b1f = std::max(0., 1. + lagmul / *fallTime);   

					Z y0a = *in;
					y1a = y0a + (y0a > y1a ? b1r : b1f) * (y1a - y0a);
					y1b = y1a + (y1a > y1b ? b1r : b1f) * (y1b - y1a);
					y1c = y1b + (y1b > y1c ? b1r : b1f) * (y1c - y1b);
					out[i] = y1c;
					
					in += inStride;
					riseTime += riseTimeStride;
					fallTime += fallTimeStride;
				}
			}
			
			framesToFill -= n;
			out += n;
			_in.advance(n);
			_riseTime.advance(n);
			_fallTime.advance(n);
		}
		
		_y1a = y1a;
		_y1b = y1b;
		_y1c = y1c;
		produce(framesToFill);
	}
	
};

static void lagud3_(Thread& th, Prim* prim)
{
	V fallTime = th.popZIn("lagud3 : fallTime");
	V riseTime = th.popZIn("lagud3 : riseTime");
	V in = th.popZIn("lagud3 : in");

	th.push(new List(new LagUD3(th, in, riseTime, fallTime)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FirstOrderLPF : public Gen
{
	ZIn _in;
	ZIn _freq;
	Z _x1, _y1;
	Z _freqmul;
	
	FirstOrderLPF(Thread& th, Arg in, Arg freq)
		: Gen(th, itemTypeZ, mostFinite(in, freq)), _in(in), _freq(freq), 
			_x1(0.), _y1(0.), _freqmul(th.rate.invNyquistRate * kFirstOrderCoeffScale)
	{
	}
	
	virtual const char* TypeName() const override { return "FirstOrderLPF"; }
	
	virtual void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		
		Z* out = mOut->fulfillz(framesToFill);
		Z x1 = _x1;
		Z y1 = _y1;
		Z freqmul = _freqmul;
		while (framesToFill) {
			Z *in, *freq;
			int n, inStride, freqStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _freq(th, n, freqStride, freq)) {
				setDone();
				break;
			}
			
			if (freqStride == 0) {
				Z a1 = t_firstOrderCoeff(*freq * freqmul);
				Z scale = .5 * (1. - a1);
				for (int i = 0; i < n; ++i) {
					Z x0 = scale * *in;
					Z y0 = x0 + x1 + a1 * y1;
										
					out[i] = y0;
					y1 = y0;
					x1 = x0;
					
					in += inStride;
				}
				
				_in.advance(n);
			} else {
				for (int i = 0; i < n; ++i) {
					Z a1 = t_firstOrderCoeff(*freq * freqmul);
					Z scale = .5 * (1. - a1);
				
					Z x0 = scale * *in;
					Z y0 = x0 + x1 + a1 * y1;
					
					out[i] = y0;
					y1 = y0;
					x1 = x0;
					
					in += inStride;
					freq += freqStride;
				}
				
				_in.advance(n);
				_freq.advance(n);
			}
			framesToFill -= n;
			out += n;
		}
		
		_x1 = x1;
		_y1 = y1;
		produce(framesToFill);
	}
	
};

struct FirstOrderHPF : public Gen
{
	ZIn _in;
	ZIn _freq;
	Z _x1, _y1;
	Z _freqmul;
	
	FirstOrderHPF(Thread& th, Arg in, Arg freq)
		: Gen(th, itemTypeZ, mostFinite(in, freq)), _in(in), _freq(freq), 
			_x1(0.), _y1(0.), _freqmul(th.rate.invNyquistRate * kFirstOrderCoeffScale)
	{
	}
	
	virtual const char* TypeName() const override { return "FirstOrderHPF"; }
	
	virtual void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		
		Z* out = mOut->fulfillz(framesToFill);
		Z x1 = _x1;
		Z y1 = _y1;
		Z freqmul = _freqmul;
		while (framesToFill) {
			Z *in, *freq;
			int n, inStride, freqStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _freq(th, n, freqStride, freq)) {
				setDone();
				break;
			}
			
			if (freqStride == 0) {
				Z a1 = t_firstOrderCoeff(*freq * freqmul);
				Z scale = .5 * (1. + a1);
				for (int i = 0; i < n; ++i) {
					Z x0 = scale * *in;
					Z y0 = x0 - x1 + a1 * y1;
					
					out[i] = y0;
					y1 = y0;
					x1 = x0;
					
					in += inStride;
				}
				
				_in.advance(n);
			} else {
				for (int i = 0; i < n; ++i) {
					Z a1 = t_firstOrderCoeff(*freq * freqmul);
					Z scale = .5 * (1. + a1);
				
					Z x0 = scale * *in;
					Z y0 = x0 - x1 + a1 * y1;
					
					out[i] = y0;
					y1 = y0;
					x1 = x0;
					
					in += inStride;
					freq += freqStride;
				}
				
				_in.advance(n);
				_freq.advance(n);
			}
			framesToFill -= n;
			out += n;
		}
		
		_x1 = x1;
		_y1 = y1;
		produce(framesToFill);
	}
};


static void lpf1_(Thread& th, Prim* prim)
{
	V freq = th.popZIn("lpf1 : freq");
	V in   = th.popZIn("lpf1 : in");

	th.push(new List(new FirstOrderLPF(th, in, freq)));
}

static void hpf1_(Thread& th, Prim* prim)
{
	V freq = th.popZIn("hpf1 : freq");
	V in   = th.popZIn("hpf1 : in");

	th.push(new List(new FirstOrderHPF(th, in, freq)));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct LPF : public Gen
{
	ZIn _in;
	ZIn _freq;
	Z _x1, _x2, _y1, _y2;
	Z _freqmul;
	Z _alphamul;
	
	LPF(Thread& th, Arg in, Arg freq)
		: Gen(th, itemTypeZ, mostFinite(in, freq)), _in(in), _freq(freq), 
			_x1(0.), _x2(0.), _y1(0.), _y2(0.), _freqmul(th.rate.radiansPerSample * gInvSineTableOmega), _alphamul(.5 * M_SQRT2)
	{
	}
	
	virtual const char* TypeName() const override { return "LPF"; }
	
	virtual void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		
		Z* out = mOut->fulfillz(framesToFill);
		Z x1 = _x1;
		Z x2 = _x2;
		Z y1 = _y1;
		Z y2 = _y2;
		Z freqmul = _freqmul;
		Z alphamul = _alphamul;
		while (framesToFill) {
			Z *in, *freq;
			int n, inStride, freqStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _freq(th, n, freqStride, freq)) {
				setDone();
				break;
			}
			
			if (freqStride == 0) {
				Z w0 = std::max(1e-3, *freq) * freqmul;
				Z sn, cs;
				tsincosx(w0, sn, cs);
				Z alpha = sn * alphamul;
				Z a0 = 1. + alpha;
				Z a0inv = 1. / a0;
				Z a1 = a0inv * (-2. * cs);
				Z a2 = a0inv * (1. - alpha);
				Z b1 = a0inv * (1. - cs);
				Z b0 = a0inv * (.5 * b1);
				Z b2 = a0inv * b0;
				for (int i = 0; i < n; ++i) {
				
					Z x0 = *in;
					Z y0 = b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
					
					out[i] = y0;
					y2 = y1;
					y1 = y0;
					x2 = x1;
					x1 = x0;
					
					in += inStride;
				}
				
				_in.advance(n);
			} else {
				for (int i = 0; i < n; ++i) {				
					Z w0 = std::max(1e-3, *freq) * freqmul;
					Z sn, cs;
					tsincosx(w0, sn, cs);
					Z alpha = sn * alphamul;
					Z a0 = 1. + alpha;
					Z a1 = -2. * cs;
					Z a2 = 1. - alpha;
					Z b1 = 1. - cs;
					Z b0 = .5 * b1;
					Z b2 = b0;
				
					Z x0 = *in;
					Z y0 = (b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2)/a0;
					
					out[i] = y0;
					y2 = y1;
					y1 = y0;
					x2 = x1;
					x1 = x0;
					
					in += inStride;
					freq += freqStride;
				}
				
				_in.advance(n);
				_freq.advance(n);
			}
			framesToFill -= n;
			out += n;
		}
		
		_x1 = x1;
		_x2 = x2;
		_y1 = y1;
		_y2 = y2;
		produce(framesToFill);
	}
	
};

struct LPF2 : public Gen
{
	ZIn _in;
	ZIn _freq;
	Z _x1, _x2, _y1, _y2, _z1, _z2;
	Z _freqmul;
	Z _alphamul;
	
	LPF2(Thread& th, Arg in, Arg freq)
		: Gen(th, itemTypeZ, mostFinite(in, freq)), _in(in), _freq(freq), 
			_x1(0.), _x2(0.), _y1(0.), _y2(0.), _z1(0.), _z2(0.), _freqmul(th.rate.radiansPerSample * gInvSineTableOmega), _alphamul(.5 * M_SQRT2)
	{
	}
	
	virtual const char* TypeName() const override { return "LPF"; }
	
	virtual void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		
		Z* out = mOut->fulfillz(framesToFill);
		Z x1 = _x1;
		Z x2 = _x2;
		Z y1 = _y1;
		Z y2 = _y2;
		Z z1 = _z1;
		Z z2 = _z2;
		Z freqmul = _freqmul;
		Z alphamul = _alphamul;
				
		while (framesToFill) {
			Z *in, *freq;
			int n, inStride, freqStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _freq(th, n, freqStride, freq)) {
				setDone();
				break;
			}
			
			if (freqStride == 0) {
				Z w0 = std::max(1e-3, *freq) * freqmul;
				Z sn, cs;
				tsincosx(w0, sn, cs);
				Z alpha = sn * alphamul;
				Z a0 = 1. + alpha;
				Z a0r = 1./a0;
				Z a1 = -2. * cs;
				Z a2 = 1. - alpha;
				Z b1 = 1. - cs;
				Z b0 = .5 * b1;
				Z b2 = b0;
				for (int i = 0; i < n; ++i) {
				
					Z x0 = *in;
					Z y0 = (b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) * a0r;
					Z z0 = (b0 * y0 + b1 * y1 + b2 * y2 - a1 * z1 - a2 * z2) * a0r;
					
					out[i] = z0;
					z2 = z1;
					z1 = z0;
					y2 = y1;
					y1 = y0;
					x2 = x1;
					x1 = x0;
					
					in += inStride;
					freq += freqStride;
				}
				
				framesToFill -= n;
				out += n;
				_in.advance(n);
			} else {
				for (int i = 0; i < n; ++i) {
					Z w0 = std::max(1e-3, *freq) * freqmul;
					Z sn, cs;
					tsincosx(w0, sn, cs);
					Z alpha = sn * alphamul;
					Z a0 = 1. + alpha;
					Z a0r = 1./a0;
					Z a1 = -2. * cs;
					Z a2 = 1. - alpha;
					Z b1 = 1. - cs;
					Z b0 = .5 * b1;
					Z b2 = b0;
				
					Z x0 = *in;
					Z y0 = (b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) * a0r;
					Z z0 = (b0 * y0 + b1 * y1 + b2 * y2 - a1 * z1 - a2 * z2) * a0r;
					
					out[i] = z0;
					z2 = z1;
					z1 = z0;
					y2 = y1;
					y1 = y0;
					x2 = x1;
					x1 = x0;
					
					in += inStride;
					freq += freqStride;
				}
				
				framesToFill -= n;
				out += n;
				_in.advance(n);
				_freq.advance(n);
			}
		}
		_x1 = x1;
		_x2 = x2;
		_y1 = y1;
		_y2 = y2;
		_z1 = z1;
		_z2 = z2;
		produce(framesToFill);
	}
	
};



struct HPF : public Gen
{
	ZIn _in;
	ZIn _freq;
	Z _x1, _x2, _y1, _y2;
	Z _freqmul;
	Z _alphamul;
	
	HPF(Thread& th, Arg in, Arg freq)
		: Gen(th, itemTypeZ, mostFinite(in, freq)), _in(in), _freq(freq), 
			_x1(0.), _x2(0.), _y1(0.), _y2(0.), _freqmul(th.rate.radiansPerSample * gInvSineTableOmega), _alphamul(.5 * M_SQRT2)
	{
	}
	
	virtual const char* TypeName() const override { return "HPF"; }
	
	virtual void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		
		Z* out = mOut->fulfillz(framesToFill);
		Z x1 = _x1;
		Z x2 = _x2;
		Z y1 = _y1;
		Z y2 = _y2;
		Z freqmul = _freqmul;
		Z alphamul = _alphamul;
		while (framesToFill) {
			Z *in, *freq;
			int n, inStride, freqStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _freq(th, n, freqStride, freq)) {
				setDone();
				break;
			}
			
			if (freqStride == 0) {
				Z w0 = *freq * freqmul;
				Z sn, cs;
				tsincosx(w0, sn, cs);
				Z alpha = sn * alphamul;
				Z a0 = 1. + alpha;
				Z a1 = -2. * cs;
				Z a2 = 1. - alpha;
				Z b1 = -1. - cs;
				Z b0 = -.5 * b1;
				Z b2 = b0;
				for (int i = 0; i < n; ++i) {
				
					Z x0 = *in;
					Z y0 = (b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0;
					
					out[i] = y0;
					y2 = y1;
					y1 = y0;
					x2 = x1;
					x1 = x0;
					
					in += inStride;
					freq += freqStride;
				}
				framesToFill -= n;
				out += n;
				_in.advance(n);
			} else {
				for (int i = 0; i < n; ++i) {				
					Z w0 = *freq * freqmul;
					Z sn, cs;
					tsincosx(w0, sn, cs);
					Z alpha = sn * alphamul;
					Z a0 = 1. + alpha;
					Z a1 = -2. * cs;
					Z a2 = 1. - alpha;
					Z b1 = -1. - cs;
					Z b0 = -.5 * b1;
					Z b2 = b0;
				
					Z x0 = *in;
					Z y0 = (b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0;
					
					out[i] = y0;
					y2 = y1;
					y1 = y0;
					x2 = x1;
					x1 = x0;
					
					in += inStride;
					freq += freqStride;
				}
				framesToFill -= n;
				out += n;
				_in.advance(n);
				_freq.advance(n);
			}
		}
		
		_x1 = x1;
		_x2 = x2;
		_y1 = y1;
		_y2 = y2;
		produce(framesToFill);
	}
	
};

struct HPF2 : public Gen
{
	ZIn _in;
	ZIn _freq;
	Z _x1, _x2, _y1, _y2, _z1, _z2;
	Z _freqmul;
	Z _alphamul;
	
	HPF2(Thread& th, Arg in, Arg freq)
		: Gen(th, itemTypeZ, mostFinite(in, freq)), _in(in), _freq(freq), 
			_x1(0.), _x2(0.), _y1(0.), _y2(0.), _z1(0.), _z2(0.), _freqmul(th.rate.radiansPerSample * gInvSineTableOmega), _alphamul(.5 * M_SQRT2)
	{
	}
	
	virtual const char* TypeName() const override { return "HPF2"; }
	
	virtual void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		
		Z* out = mOut->fulfillz(framesToFill);
		Z x1 = _x1;
		Z x2 = _x2;
		Z y1 = _y1;
		Z y2 = _y2;
		Z z1 = _z1;
		Z z2 = _z2;
		Z freqmul = _freqmul;
		Z alphamul = _alphamul;
		while (framesToFill) {
			Z *in, *freq;
			int n, inStride, freqStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _freq(th, n, freqStride, freq)) {
				setDone();
				break;
			}
			
			if (freqStride == 0) {
				Z w0 = *freq * freqmul;
				Z sn, cs;
				tsincosx(w0, sn, cs);
				Z alpha = sn * alphamul;
				Z a0 = 1. + alpha;
				Z a0r = 1./a0;
				Z a1 = -2. * cs;
				Z a2 = 1. - alpha;
				Z b1 = -1. - cs;
				Z b0 = -.5 * b1;
				Z b2 = b0;
				for (int i = 0; i < n; ++i) {
				
					Z x0 = *in;
					Z y0 = (b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) * a0r;
					Z z0 = (b0 * y0 + b1 * y1 + b2 * y2 - a1 * z1 - a2 * z2) * a0r;
					
					out[i] = z0;
					z2 = z1;
					z1 = z0;
					y2 = y1;
					y1 = y0;
					x2 = x1;
					x1 = x0;
					
					in += inStride;
					freq += freqStride;
				}
				
				framesToFill -= n;
				out += n;
				_in.advance(n);
			} else {
				for (int i = 0; i < n; ++i) {				
					Z w0 = *freq * freqmul;
					Z sn, cs;
					tsincosx(w0, sn, cs);
					Z alpha = sn * alphamul;
					Z a0 = 1. + alpha;
					Z a0r = 1./a0;
					Z a1 = -2. * cs;
					Z a2 = 1. - alpha;
					Z b1 = -1. - cs;
					Z b0 = -.5 * b1;
					Z b2 = b0;
				
					Z x0 = *in;
					Z y0 = (b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) * a0r;
					Z z0 = (b0 * y0 + b1 * y1 + b2 * y2 - a1 * z1 - a2 * z2) * a0r;
					
					out[i] = z0;
					z2 = z1;
					z1 = z0;
					y2 = y1;
					y1 = y0;
					x2 = x1;
					x1 = x0;
					
					in += inStride;
					freq += freqStride;
				}
				
				framesToFill -= n;
				out += n;
				_in.advance(n);
				_freq.advance(n);
			}
		}
		
		_x1 = x1;
		_x2 = x2;
		_y1 = y1;
		_y2 = y2;
		_z1 = z1;
		_z2 = z2;
		produce(framesToFill);
	}
	
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <typename Feedback>
struct RLPF : public Gen
{
	ZIn _in;
	ZIn _freq;
	ZIn _rq;
	Z _x1, _x2, _y1, _y2;
	Z _freqmul;
	
	RLPF(Thread& th, Arg in, Arg freq, Arg rq)
		: Gen(th, itemTypeZ, mostFinite(in, freq, rq)), _in(in), _freq(freq), _rq(rq),
			_x1(0.), _x2(0.), _y1(0.), _y2(0.), _freqmul(th.rate.radiansPerSample * gInvSineTableOmega)
	{
	}
	
	virtual const char* TypeName() const override { return "RLPF"; }
	
	virtual void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		
		Z* out = mOut->fulfillz(framesToFill);
		Z x1 = _x1;
		Z x2 = _x2;
		Z y1 = _y1;
		Z y2 = _y2;
		Z freqmul = _freqmul;
		while (framesToFill) {
			Z *in, *freq, *rq;
			int n, inStride, freqStride, rqStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _freq(th, n, freqStride, freq) || _rq(th, n, rqStride, rq)) {
				setDone();
				break;
			}
			
			for (int i = 0; i < n; ++i) {				
				Z w0 = *freq * freqmul;
				Z sn, cs;
				tsincosx(w0, sn, cs);
				Z alpha = sn * *rq * .5;
				Z a0 = 1. + alpha;
				Z a1 = -2. * cs;
				Z a2 = 1. - alpha;
				Z b1 = 1. - cs;
				Z b0 = .5 * b1;
				Z b2 = b0;
			
				Z x0 = *in;
				Z y0 = (b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2)/a0;
				y0 = Feedback::feedback(y0);
				
				out[i] = y0;
				y2 = y1;
				y1 = y0;
				x2 = x1;
				x1 = x0;
				
				in += inStride;
				freq += freqStride;
				rq += rqStride;
			}
			
			framesToFill -= n;
			out += n;
			_in.advance(n);
			_freq.advance(n);
			_rq.advance(n);
		}
		
		_x1 = x1;
		_x2 = x2;
		_y1 = y1;
		_y2 = y2;
		produce(framesToFill);
	}
	
};


template <typename Feedback>
struct RLPF2 : public Gen
{
	ZIn _in;
	ZIn _freq;
	ZIn _rq;
	Z _x1, _x2, _y1, _y2, _z1, _z2;
	Z _freqmul;
	
	RLPF2(Thread& th, Arg in, Arg freq, Arg rq)
		: Gen(th, itemTypeZ, mostFinite(in, freq, rq)), _in(in), _freq(freq), _rq(rq),
			_x1(0.), _x2(0.), _y1(0.), _y2(0.), _z1(0.), _z2(0.), _freqmul(th.rate.radiansPerSample * gInvSineTableOmega)
	{
	}
	
	virtual const char* TypeName() const override { return "RLPF"; }
	
	virtual void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		
		Z* out = mOut->fulfillz(framesToFill);
		Z x1 = _x1;
		Z x2 = _x2;
		Z y1 = _y1;
		Z y2 = _y2;
		Z z1 = _z1;
		Z z2 = _z2;
		Z freqmul = _freqmul;
		while (framesToFill) {
			Z *in, *freq, *rq;
			int n, inStride, freqStride, rqStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _freq(th, n, freqStride, freq) || _rq(th, n, rqStride, rq)) {
				setDone();
				break;
			}
			
			for (int i = 0; i < n; ++i) {				
				Z w0 = *freq * freqmul;
				Z sn, cs;
				tsincosx(w0, sn, cs);
				Z alpha = sn * *rq * .5;
				Z a0 = 1. + alpha;
				Z a0r = 1./a0;
				Z a1 = -2. * cs;
				Z a2 = 1. - alpha;
				Z b1 = 1. - cs;
				Z b0 = .5 * b1;
				Z b2 = b0;
			
				Z x0 = *in;
				Z y0 = (b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) * a0r;
				y0 = Feedback::feedback(y0);
				Z z0 = (b0 * y0 + b1 * y1 + b2 * y2 - a1 * z1 - a2 * z2) * a0r;
				z0 = Feedback::feedback(z0);
				
				out[i] = z0;
				z2 = z1;
				z1 = z0;
				y2 = y1;
				y1 = y0;
				x2 = x1;
				x1 = x0;
				
				in += inStride;
				freq += freqStride;
				rq += rqStride;
			}
			
			framesToFill -= n;
			out += n;
			_in.advance(n);
			_freq.advance(n);
			_rq.advance(n);
		}
		
		_x1 = x1;
		_x2 = x2;
		_y1 = y1;
		_y2 = y2;
		_z1 = z1;
		_z2 = z2;
		produce(framesToFill);
	}
	
};



template <typename Feedback>
struct RHPF : public Gen
{
	ZIn _in;
	ZIn _freq;
	ZIn _rq;
	Z _x1, _x2, _y1, _y2;
	Z _freqmul;
	
	RHPF(Thread& th, Arg in, Arg freq, Arg rq)
		: Gen(th, itemTypeZ, mostFinite(in, freq, rq)), _in(in), _freq(freq), _rq(rq),
			_x1(0.), _x2(0.), _y1(0.), _y2(0.), _freqmul(th.rate.radiansPerSample * gInvSineTableOmega)
	{
	}
	
	virtual const char* TypeName() const override { return "RHPF"; }
	
	virtual void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		
		Z* out = mOut->fulfillz(framesToFill);
		Z x1 = _x1;
		Z x2 = _x2;
		Z y1 = _y1;
		Z y2 = _y2;
		Z freqmul = _freqmul;
		while (framesToFill) {
			Z *in, *freq, *rq;
			int n, inStride, freqStride, rqStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _freq(th, n, freqStride, freq) || _rq(th, n, rqStride, rq)) {
				setDone();
				break;
			}
			
			for (int i = 0; i < n; ++i) {				
				Z w0 = *freq * freqmul;
				Z sn, cs;
				tsincosx(w0, sn, cs);
				Z alpha = sn * *rq * .5;
				Z a0 = 1. + alpha;
				Z a1 = -2. * cs;
				Z a2 = 1. - alpha;
				Z b1 = -1. - cs;
				Z b0 = -.5 * b1;
				Z b2 = b0;
			
				Z x0 = *in;
				Z y0 = (b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2)/a0;
				y0 = Feedback::feedback(y0);
				
				out[i] = y0;
				y2 = y1;
				y1 = y0;
				x2 = x1;
				x1 = x0;
				
				in += inStride;
				freq += freqStride;
				rq += rqStride;
			}
			
			framesToFill -= n;
			out += n;
			_in.advance(n);
			_freq.advance(n);
			_rq.advance(n);
		}
		
		_x1 = x1;
		_x2 = x2;
		_y1 = y1;
		_y2 = y2;
		produce(framesToFill);
	}
	
};


template <typename Feedback>
struct RHPF2 : public Gen
{
	ZIn _in;
	ZIn _freq;
	ZIn _rq;
	Z _x1, _x2, _y1, _y2, _z1, _z2;
	Z _freqmul;
	
	RHPF2(Thread& th, Arg in, Arg freq, Arg rq)
		: Gen(th, itemTypeZ, mostFinite(in, freq, rq)), _in(in), _freq(freq), _rq(rq),
			_x1(0.), _x2(0.), _y1(0.), _y2(0.), _z1(0.), _z2(0.), _freqmul(th.rate.radiansPerSample * gInvSineTableOmega)
	{
	}
	
	virtual const char* TypeName() const override { return "RHPF2"; }
	
	virtual void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		
		Z* out = mOut->fulfillz(framesToFill);
		Z x1 = _x1;
		Z x2 = _x2;
		Z y1 = _y1;
		Z y2 = _y2;
		Z z1 = _z1;
		Z z2 = _z2;
		Z freqmul = _freqmul;
		while (framesToFill) {
			Z *in, *freq, *rq;
			int n, inStride, freqStride, rqStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _freq(th, n, freqStride, freq) || _rq(th, n, rqStride, rq)) {
				setDone();
				break;
			}
			
			for (int i = 0; i < n; ++i) {				
				Z w0 = *freq * freqmul;
				Z sn, cs;
				tsincosx(w0, sn, cs);
				Z alpha = sn * *rq * .5;
				Z a0 = 1. + alpha;
				Z a0r = 1./a0;
				Z a1 = -2. * cs;
				Z a2 = 1. - alpha;
				Z b1 = -1. - cs;
				Z b0 = -.5 * b1;
				Z b2 = b0;
			
				Z x0 = *in;
				Z y0 = (b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) * a0r;
				y0 = Feedback::feedback(y0);
				Z z0 = (b0 * y0 + b1 * y1 + b2 * y2 - a1 * z1 - a2 * z2) * a0r;
				z0 = Feedback::feedback(z0);
				
				out[i] = z0;
				z2 = z1;
				z1 = z0;
				y2 = y1;
				y1 = y0;
				x2 = x1;
				x1 = x0;
				
				in += inStride;
				freq += freqStride;
				rq += rqStride;
			}
			
			framesToFill -= n;
			out += n;
			_in.advance(n);
			_freq.advance(n);
			_rq.advance(n);
		}
		
		_x1 = x1;
		_x2 = x2;
		_y1 = y1;
		_y2 = y2;
		_z1 = z1;
		_z2 = z2;
		produce(framesToFill);
	}
	
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct BPF : public Gen
{
	ZIn _in;
	ZIn _freq;
	ZIn _bw;
	Z _x1, _x2, _y1, _y2;
	Z _freqmul;
	
	BPF(Thread& th, Arg in, Arg freq, Arg bw)
		: Gen(th, itemTypeZ, mostFinite(in, freq, bw)), _in(in), _freq(freq), _bw(bw),
			_x1(0.), _x2(0.), _y1(0.), _y2(0.), _freqmul(th.rate.radiansPerSample * gInvSineTableOmega)
	{
	}
	
	virtual const char* TypeName() const override { return "BPF"; }
	
	virtual void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		
		Z* out = mOut->fulfillz(framesToFill);
		Z x1 = _x1;
		Z x2 = _x2;
		Z y1 = _y1;
		Z y2 = _y2;
		Z freqmul = _freqmul;
		while (framesToFill) {
			Z *in, *freq, *bw;
			int n, inStride, freqStride, bwStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _freq(th, n, freqStride, freq) || _bw(th, n, bwStride, bw)) {
				setDone();
				break;
			}
			
			for (int i = 0; i < n; ++i) {				
				Z w0 = *freq * freqmul;
				Z sn, cs;
				tsincosx(w0, sn, cs);

				// bw * log(2) is the first term of the taylor series for 2*sinh(log(2)/2 * bw) == 1/Q. 
				// the log(2) is combined with the .5 term in the formula for alpha.
				Z alpha = sn * *bw * log2o2;

				Z a0 = 1. + alpha;
				Z a1 = -2. * cs;
				Z a2 = 1. - alpha;
				Z b0 = alpha;
			
				Z x0 = *in;
				Z y0 = (b0 * (x0 - x2) - a1 * y1 - a2 * y2) / a0;
				
				out[i] = y0;
				y2 = y1;
				y1 = y0;
				x2 = x1;
				x1 = x0;
				
				in += inStride;
				freq += freqStride;
				bw += bwStride;
			}
			
			framesToFill -= n;
			out += n;
			_in.advance(n);
			_freq.advance(n);
			_bw.advance(n);
		}
		
		_x1 = x1;
		_x2 = x2;
		_y1 = y1;
		_y2 = y2;
		produce(framesToFill);
	}
	
};


struct BSF : public Gen
{
	ZIn _in;
	ZIn _freq;
	ZIn _bw;
	Z _x1, _x2, _y1, _y2;
	Z _freqmul;
	
	BSF(Thread& th, Arg in, Arg freq, Arg bw)
		: Gen(th, itemTypeZ, mostFinite(in, freq, bw)), _in(in), _freq(freq), _bw(bw),
			_x1(0.), _x2(0.), _y1(0.), _y2(0.), _freqmul(th.rate.radiansPerSample * gInvSineTableOmega)
	{
	}
	
	virtual const char* TypeName() const override { return "BSF"; }
	
	virtual void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		
		Z* out = mOut->fulfillz(framesToFill);
		Z x1 = _x1;
		Z x2 = _x2;
		Z y1 = _y1;
		Z y2 = _y2;
		Z freqmul = _freqmul;
		while (framesToFill) {
			Z *in, *freq, *bw;
			int n, inStride, freqStride, bwStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _freq(th, n, freqStride, freq) || _bw(th, n, bwStride, bw)) {
				setDone();
				break;
			}
			
			for (int i = 0; i < n; ++i) {				
				Z w0 = *freq * freqmul;
				Z sn, cs;
				tsincosx(w0, sn, cs);

				// bw * log(2) is the first term of the taylor series for 2*sinh(log(2)/2 * bw) == 1/Q. 
				// the log(2) is combined with the .5 term in the formula for alpha.
				Z alpha = sn * *bw * log2o2;

				Z a0 = 1. + alpha;
				Z a0r = 1./a0;
				Z a1 = -2. * cs;
				Z a2 = 1. - alpha;
			
				Z x0 = *in;
				Z y0 = (x0 + x2 + a1 * (x1 - y1) - a2 * y2) * a0r;
				
				out[i] = y0;
				y2 = y1;
				y1 = y0;
				x2 = x1;
				x1 = x0;
				
				in += inStride;
				freq += freqStride;
				bw += bwStride;
			}
			
			framesToFill -= n;
			out += n;
			_in.advance(n);
			_freq.advance(n);
			_bw.advance(n);
		}
		
		_x1 = x1;
		_x2 = x2;
		_y1 = y1;
		_y2 = y2;
		produce(framesToFill);
	}
	
};




struct APF : public Gen
{
	ZIn _in;
	ZIn _freq;
	ZIn _bw;
	Z _x1, _x2, _y1, _y2;
	Z _freqmul;
	
	APF(Thread& th, Arg in, Arg freq, Arg bw)
		: Gen(th, itemTypeZ, mostFinite(in, freq, bw)), _in(in), _freq(freq), _bw(bw),
			_x1(0.), _x2(0.), _y1(0.), _y2(0.), _freqmul(th.rate.radiansPerSample * gInvSineTableOmega)
	{
	}
	
	virtual const char* TypeName() const override { return "APF"; }
	
	virtual void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		
		Z* out = mOut->fulfillz(framesToFill);
		Z x1 = _x1;
		Z x2 = _x2;
		Z y1 = _y1;
		Z y2 = _y2;
		Z freqmul = _freqmul;
		while (framesToFill) {
			Z *in, *freq, *bw;
			int n, inStride, freqStride, bwStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _freq(th, n, freqStride, freq) || _bw(th, n, bwStride, bw)) {
				setDone();
				break;
			}
			
			for (int i = 0; i < n; ++i) {				
				Z w0 = *freq * freqmul;
				Z sn, cs;
				tsincosx(w0, sn, cs);
				
				// bw * log(2) is the first term of the taylor series for 2*sinh(log(2)/2 * bw) == 1/Q. 
				// the log(2) is combined with the .5 term in the formula for alpha.
				Z alpha = sn * *bw * log2o2;  
				
				Z a0 = 1. + alpha;
				Z a0r = 1./a0;
				Z a1 = -2. * cs;
				Z a2 = 1. - alpha;
			
				Z x0 = *in;
				Z y0 = (a2 * (x0 - y2) + a1 * (x1 - y1)) * a0r + x2;
				
				out[i] = y0;
				y2 = y1;
				y1 = y0;
				x2 = x1;
				x1 = x0;
				
				in += inStride;
				freq += freqStride;
				bw += bwStride;
			}
			
			framesToFill -= n;
			out += n;
			_in.advance(n);
			_freq.advance(n);
			_bw.advance(n);
		}
		
		_x1 = x1;
		_x2 = x2;
		_y1 = y1;
		_y2 = y2;
		produce(framesToFill);
	}
	
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct PEQ : public Gen
{
	ZIn _in;
	ZIn _freq;
	ZIn _bw;
	ZIn _gain;
	Z _x1, _x2, _y1, _y2;
	Z _freqmul;
	
	PEQ(Thread& th, Arg in, Arg freq, Arg bw, Arg gain)
		: Gen(th, itemTypeZ, mostFinite(in, freq, bw, gain)), _in(in), _freq(freq), _bw(bw), _gain(gain),
			_x1(0.), _x2(0.), _y1(0.), _y2(0.), _freqmul(th.rate.radiansPerSample * gInvSineTableOmega)
	{
	}
	
	virtual const char* TypeName() const override { return "PEQ"; }
	
	virtual void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		
		Z* out = mOut->fulfillz(framesToFill);
		Z x1 = _x1;
		Z x2 = _x2;
		Z y1 = _y1;
		Z y2 = _y2;
		Z freqmul = _freqmul;
		while (framesToFill) {
			Z *in, *freq, *bw, *gain;
			int n, inStride, freqStride, bwStride, gainStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _freq(th, n, freqStride, freq) || _bw(th, n, bwStride, bw) || _gain(th, n, gainStride, gain)) {
				setDone();
				break;
			}
			
			for (int i = 0; i < n; ++i) {	
				Z A = t_dbamp(.5 * *gain);
				Z w0 = *freq * freqmul;
				Z sn, cs;
				tsincosx(w0, sn, cs);

				// bw * log(2) is the first term of the taylor series for 2*sinh(log(2)/2 * bw) == 1/Q. 
				// the log(2) is combined with the .5 term in the formula for alpha.
				Z alpha = sn * *bw * log2o2;
				Z alphaA = alpha * A;
				Z alphaOverA = alpha / A;
				
				Z a0 = 1. + alphaOverA;
				Z a1 = -2. * cs;
				Z a2 = 1. - alphaOverA;
			
				Z b0 = 1. + alphaA;
				Z b2 = 1. - alphaA;
				
				Z x0 = *in;
				Z y0 = (b0 * x0 + a1 * (x1 - y1) + b2 * x2 - a2 * y2) / a0;
				
				out[i] = y0;
				y2 = y1;
				y1 = y0;
				x2 = x1;
				x1 = x0;
				
				in += inStride;
				freq += freqStride;
				bw += bwStride;
				gain += gainStride;
			}
			
			framesToFill -= n;
			out += n;
			_in.advance(n);
			_freq.advance(n);
			_bw.advance(n);
			_gain.advance(n);
		}
		
		_x1 = x1;
		_x2 = x2;
		_y1 = y1;
		_y2 = y2;
		produce(framesToFill);
	}
	
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct LowShelf : public Gen
{
	ZIn _in;
	ZIn _freq;
	ZIn _gain;
	Z _x1, _x2, _y1, _y2;
	Z _freqmul;
	Z _alphamul;
	
	LowShelf(Thread& th, Arg in, Arg freq, Arg gain)
		: Gen(th, itemTypeZ, mostFinite(in, freq, gain)), _in(in), _freq(freq), _gain(gain),
			_x1(0.), _x2(0.), _y1(0.), _y2(0.), _freqmul(th.rate.radiansPerSample * gInvSineTableOmega), _alphamul(.5 * M_SQRT2)
	{
	}
	
	virtual const char* TypeName() const override { return "LowShelf"; }
	
	virtual void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		
		Z* out = mOut->fulfillz(framesToFill);
		Z x1 = _x1;
		Z x2 = _x2;
		Z y1 = _y1;
		Z y2 = _y2;
		Z freqmul = _freqmul;
		Z alphamul = _alphamul;
		while (framesToFill) {
			Z *in, *freq, *gain;
			int n, inStride, freqStride, gainStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _freq(th, n, freqStride, freq) || _gain(th, n, gainStride, gain)) {
				setDone();
				break;
			}
			
			for (int i = 0; i < n; ++i) {	
				Z A = t_dbamp(.5 * *gain);
				Z Ap1 = A + 1.;
				Z Am1 = A - 1.;
				Z Asqrt = t_dbamp(.25 * *gain);
				Z w0 = *freq * freqmul;
				Z sn, cs;
				tsincosx(w0, sn, cs);
				Z alpha = sn * alphamul;
				Z alpha2Asqrt = 2. * alpha * Asqrt;
				Z Am1cs = Am1*cs;
				Z Ap1cs = Ap1*cs;
				
				Z b0 =       ( Ap1 - Am1cs + alpha2Asqrt );
				Z b1 =    2.*( Am1 - Ap1cs               );
				Z b2 =       ( Ap1 - Am1cs - alpha2Asqrt );
				Z a0 =         Ap1 + Am1cs + alpha2Asqrt;
				Z a1 =   -2.*( Am1 + Ap1cs               );
				Z a2 =         Ap1 + Am1cs - alpha2Asqrt;
			
				Z x0 = *in;
				Z y0 = (A*(b0 * x0 + b1 * x1 + b2 * x2) - a1 * y1 - a2 * y2) / a0;
				
				out[i] = y0;
				y2 = y1;
				y1 = y0;
				x2 = x1;
				x1 = x0;
				
				in += inStride;
				freq += freqStride;
				gain += gainStride;
			}
			
			framesToFill -= n;
			out += n;
			_in.advance(n);
			_freq.advance(n);
			_gain.advance(n);
		}
		
		_x1 = x1;
		_x2 = x2;
		_y1 = y1;
		_y2 = y2;
		produce(framesToFill);
	}
	
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct HighShelf : public Gen
{
	ZIn _in;
	ZIn _freq;
	ZIn _gain;
	Z _x1, _x2, _y1, _y2;
	Z _freqmul;
	Z _alphamul;
	
	HighShelf(Thread& th, Arg in, Arg freq, Arg gain)
		: Gen(th, itemTypeZ, mostFinite(in, freq, gain)), _in(in), _freq(freq), _gain(gain),
			_x1(0.), _x2(0.), _y1(0.), _y2(0.), _freqmul(th.rate.radiansPerSample * gInvSineTableOmega), _alphamul(.5 * M_SQRT2)
	{
	}
	
	virtual const char* TypeName() const override { return "HighShelf"; }
	
	virtual void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		
		Z* out = mOut->fulfillz(framesToFill);
		Z x1 = _x1;
		Z x2 = _x2;
		Z y1 = _y1;
		Z y2 = _y2;
		Z freqmul = _freqmul;
		Z alphamul = _alphamul;
		while (framesToFill) {
			Z *in, *freq, *gain;
			int n, inStride, freqStride, gainStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _freq(th, n, freqStride, freq) || _gain(th, n, gainStride, gain)) {
				setDone();
				break;
			}
			
			for (int i = 0; i < n; ++i) {	
				Z A = t_dbamp(.5 * *gain);
				Z Ap1 = A + 1.;
				Z Am1 = A - 1.;
				Z Asqrt = t_dbamp(.25 * *gain);
				Z w0 = *freq * freqmul;
				Z sn, cs;
				tsincosx(w0, sn, cs);
				Z alpha = sn * alphamul;
				Z alpha2Asqrt = 2. * alpha * Asqrt;
				Z Am1cs = Am1*cs;
				Z Ap1cs = Ap1*cs;
				
				Z b0 =       ( Ap1 + Am1cs + alpha2Asqrt );
				Z b1 =   -2.*( Am1 + Ap1cs               );
				Z b2 =       ( Ap1 + Am1cs - alpha2Asqrt );
				Z a0 =         Ap1 - Am1cs + alpha2Asqrt;
				Z a1 =    2.*( Am1 - Ap1cs               );
				Z a2 =         Ap1 - Am1cs - alpha2Asqrt;
			
				Z x0 = *in;
				Z y0 = (A*(b0 * x0 + b1 * x1 + b2 * x2) - a1 * y1 - a2 * y2) / a0;
				
				out[i] = y0;
				y2 = y1;
				y1 = y0;
				x2 = x1;
				x1 = x0;
				
				in += inStride;
				freq += freqStride;
				gain += gainStride;
			}
			
			framesToFill -= n;
			out += n;
			_in.advance(n);
			_freq.advance(n);
			_gain.advance(n);
		}
		
		_x1 = x1;
		_x2 = x2;
		_y1 = y1;
		_y2 = y2;
		produce(framesToFill);
	}
	
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct LowShelf1 : public Gen
{
	ZIn _in;
	ZIn _freq;
	ZIn _gain;
	Z _x1, _y1;
	Z _freqmul;
	
	LowShelf1(Thread& th, Arg in, Arg freq, Arg gain)
		: Gen(th, itemTypeZ, mostFinite(in, freq, gain)), _in(in), _freq(freq), _gain(gain),
			_x1(0.), _y1(0.), _freqmul(th.rate.invSampleRate)
	{
	}
	
	virtual const char* TypeName() const override { return "LowShelf1"; }
	
	virtual void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		
		Z* out = mOut->fulfillz(framesToFill);
		Z x1 = _x1;
		Z y1 = _y1;
		Z freqmul = _freqmul;
		while (framesToFill) {
			Z *in, *freq, *gain;
			int n, inStride, freqStride, gainStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _freq(th, n, freqStride, freq) || _gain(th, n, gainStride, gain)) {
				setDone();
				break;
			}
			
			for (int i = 0; i < n; ++i) {	
				Z sqrt_g = t_dbamp(.25 * *gain);
				Z d = *freq * freqmul;
				
				Z p = 1. - d*sqrt_g;
				Z q = 1. - d/sqrt_g;

					Z x0 = *in;
					Z y0 = x0 + q * x1 - p * y1;
				
					out[i] = y0;
					y1 = y0;
					x1 = x0;
				
				in += inStride;
				freq += freqStride;
				gain += gainStride;
			}
			
			framesToFill -= n;
			out += n;
			_in.advance(n);
			_freq.advance(n);
			_gain.advance(n);
		}
		
		_x1 = x1;
		_y1 = y1;
		produce(framesToFill);
	}
	
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct RLowShelf : public Gen
{
	ZIn _in;
	ZIn _freq;
	ZIn _bw;
	ZIn _gain;
	Z _x1, _x2, _y1, _y2;
	Z _freqmul;
	
	RLowShelf(Thread& th, Arg in, Arg freq, Arg bw, Arg gain)
		: Gen(th, itemTypeZ, mostFinite(in, freq, gain)), _in(in), _freq(freq), _bw(bw), _gain(gain),
			_x1(0.), _x2(0.), _y1(0.), _y2(0.), _freqmul(th.rate.radiansPerSample * gInvSineTableOmega)
	{
	}
	
	virtual const char* TypeName() const override { return "RLowShelf"; }
	
	virtual void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		
		Z* out = mOut->fulfillz(framesToFill);
		Z x1 = _x1;
		Z x2 = _x2;
		Z y1 = _y1;
		Z y2 = _y2;
		Z freqmul = _freqmul;
		while (framesToFill) {
			Z *in, *freq, *bw, *gain;
			int n, inStride, freqStride, bwStride, gainStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _freq(th, n, freqStride, freq) || _bw(th, n, bwStride, bw) || _gain(th, n, gainStride, gain)) {
				setDone();
				break;
			}
			
			for (int i = 0; i < n; ++i) {	
				Z A = t_dbamp(.5 * *gain);
				Z Ap1 = A + 1.;
				Z Am1 = A - 1.;
				Z Asqrt = t_dbamp(.25 * *gain);
				Z w0 = *freq * freqmul;
				Z sn, cs;
				tsincosx(w0, sn, cs);

				// bw * log(2) is the first term of the taylor series for 2*sinh(log(2)/2 * bw) == 1/Q. 
				// the log(2) is combined with the .5 term in the formula for alpha.
				Z alpha = sn * *bw * log2o2;
				Z alpha2Asqrt = 2. * alpha * Asqrt;
				Z Am1cs = Am1*cs;
				Z Ap1cs = Ap1*cs;
				
				Z b0 =     A*( Ap1 - Am1cs + alpha2Asqrt );
				Z b1 =  2.*A*( Am1 - Ap1cs               );
				Z b2 =     A*( Ap1 - Am1cs - alpha2Asqrt );
				Z a0 =         Ap1 + Am1cs + alpha2Asqrt;
				Z a1 =   -2.*( Am1 + Ap1cs               );
				Z a2 =         Ap1 + Am1cs - alpha2Asqrt;
			
				Z x0 = *in;
				Z y0 = (b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0;
				
				out[i] = y0;
				y2 = y1;
				y1 = y0;
				x2 = x1;
				x1 = x0;
				
				in += inStride;
				freq += freqStride;
				bw += bwStride;
				gain += gainStride;
			}
			
			framesToFill -= n;
			out += n;
			_in.advance(n);
			_freq.advance(n);
			_bw.advance(n);
			_gain.advance(n);
		}
		
		_x1 = x1;
		_x2 = x2;
		_y1 = y1;
		_y2 = y2;
		produce(framesToFill);
	}
	
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



static void lpf_(Thread& th, Prim* prim)
{
	V freq = th.popZIn("lpf : freq");
	V in   = th.popZIn("lpf : in");

	th.push(new List(new LPF(th, in, freq)));
}

static void lpf2_(Thread& th, Prim* prim)
{
	V freq = th.popZIn("lpf2 : freq");
	V in   = th.popZIn("lpf2 : in");

	th.push(new List(new LPF2(th, in, freq)));
}

static void hpf_(Thread& th, Prim* prim)
{
	V freq = th.popZIn("hpf : freq");
	V in   = th.popZIn("hpf : in");

	th.push(new List(new HPF(th, in, freq)));
}

static void hpf2_(Thread& th, Prim* prim)
{
	V freq = th.popZIn("hpf2 : freq");
	V in   = th.popZIn("hpf2 : in");

	th.push(new List(new HPF2(th, in, freq)));
}



static void rlpf_(Thread& th, Prim* prim)
{
	V rq    = th.popZIn("rlpf : rq");
	V freq  = th.popZIn("rlpf : freq");
	V in    = th.popZIn("rlpf : in");

	th.push(new List(new RLPF<NormalFeedback>(th, in, freq, rq)));
}

static void rlpf2_(Thread& th, Prim* prim)
{
	V rq    = th.popZIn("rlpf2 : rq");
	V freq  = th.popZIn("rlpf2 : freq");
	V in    = th.popZIn("rlpf2 : in");

	th.push(new List(new RLPF2<NormalFeedback>(th, in, freq, rq)));
}

static void rhpf_(Thread& th, Prim* prim)
{
	V rq    = th.popZIn("rhpf : rq");
	V freq  = th.popZIn("rhpf : freq");
	V in    = th.popZIn("rhpf : in");

	th.push(new List(new RHPF<NormalFeedback>(th, in, freq, rq)));
}

static void rhpf2_(Thread& th, Prim* prim)
{
	V rq    = th.popZIn("rhpf2 : rq");
	V freq  = th.popZIn("rhpf2 : freq");
	V in    = th.popZIn("rhpf2 : in");

	th.push(new List(new RHPF2<NormalFeedback>(th, in, freq, rq)));
}

static void rlpfc_(Thread& th, Prim* prim)
{
	V rq    = th.popZIn("rlpfc : rq");
	V freq  = th.popZIn("rlpfc : freq");
	V in    = th.popZIn("rlpfc : in");

	th.push(new List(new RLPF<UnityHardClipFeedback>(th, in, freq, rq)));
}

static void rlpf2c_(Thread& th, Prim* prim)
{
	V rq    = th.popZIn("rlpf2c : rq");
	V freq  = th.popZIn("rlpf2c : freq");
	V in    = th.popZIn("rlpf2c : in");

	th.push(new List(new RLPF2<UnityHardClipFeedback>(th, in, freq, rq)));
}

static void rhpfc_(Thread& th, Prim* prim)
{
	V rq    = th.popZIn("rhpfc : rq");
	V freq  = th.popZIn("rhpfc : freq");
	V in    = th.popZIn("rhpfc : in");

	th.push(new List(new RHPF<UnityHardClipFeedback>(th, in, freq, rq)));
}

static void rhpf2c_(Thread& th, Prim* prim)
{
	V rq    = th.popZIn("rhpf2c : rq");
	V freq  = th.popZIn("rhpf2c : freq");
	V in    = th.popZIn("rhpf2c : in");

	th.push(new List(new RHPF2<UnityHardClipFeedback>(th, in, freq, rq)));
}


static void bpf_(Thread& th, Prim* prim)
{
	V bw   = th.popZIn("bpf : bw");
	V freq = th.popZIn("bpf : freq");
	V in   = th.popZIn("bpf : in");

	th.push(new List(new BPF(th, in, freq, bw)));
}

static void bsf_(Thread& th, Prim* prim)
{
	V bw   = th.popZIn("bsf : bw");
	V freq = th.popZIn("bsf : freq");
	V in   = th.popZIn("bsf : in");

	th.push(new List(new BSF(th, in, freq, bw)));
}

static void apf_(Thread& th, Prim* prim)
{
	V bw   = th.popZIn("apf : bw");
	V freq = th.popZIn("apf : freq");
	V in   = th.popZIn("apf : in");

	th.push(new List(new APF(th, in, freq, bw)));
}

static void peq_(Thread& th, Prim* prim)
{
	V gain   = th.popZIn("peq : gain");
	V bw   = th.popZIn("peq : bw");
	V freq = th.popZIn("peq : freq");
	V in   = th.popZIn("peq : in");

	th.push(new List(new PEQ(th, in, freq, bw, gain)));
}

static void lsf_(Thread& th, Prim* prim)
{
	V gain   = th.popZIn("lsf : gain");
	V freq = th.popZIn("lsf : freq");
	V in   = th.popZIn("lsf : in");

	th.push(new List(new LowShelf(th, in, freq, gain)));
}

static void hsf_(Thread& th, Prim* prim)
{
	V gain   = th.popZIn("hsf : gain");
	V freq = th.popZIn("hsf : freq");
	V in   = th.popZIn("hsf : in");

	th.push(new List(new HighShelf(th, in, freq, gain)));
}


static void lsf1_(Thread& th, Prim* prim)
{
	V gain   = th.popZIn("lsf : gain");
	V freq = th.popZIn("lsf : freq");
	V in   = th.popZIn("lsf : in");

	th.push(new List(new LowShelf1(th, in, freq, gain)));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct Resonz : public Gen
{
	ZIn _in;
	ZIn _freq;
	ZIn _rq;
	Z _x1, _x2, _y1, _y2;
	Z _freqmul;
	
	Resonz(Thread& th, Arg in, Arg freq, Arg rq)
		: Gen(th, itemTypeZ, mostFinite(in, freq, rq)), _in(in), _freq(freq), _rq(rq),
			_x1(0.), _x2(0.), _y1(0.), _y2(0.), _freqmul(th.rate.radiansPerSample)
	{
	}
	
	virtual const char* TypeName() const override { return "Resonz"; }
	
	virtual void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		
		Z* out = mOut->fulfillz(framesToFill);
		Z x1 = _x1;
		Z x2 = _x2;
		Z y1 = _y1;
		Z y2 = _y2;
		Z freqmul = _freqmul;
		while (framesToFill) {
			Z *in, *freq, *rq;
			int n, inStride, freqStride, rqStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _freq(th, n, freqStride, freq) || _rq(th, n, rqStride, rq)) {
				setDone();
				break;
			}
			
			for (int i = 0; i < n; ++i) {				
				Z w0 = *freq * freqmul;
				Z R = 1. - .5 * w0 * *rq;
				Z cs = tcos(w0);
				Z a1 = 2. * R * cs;
				Z a2 = -(R * R);
				Z b0 = .5;
			
				Z x0 = *in;
				Z y0 = b0 * (x0 - x2) + a1 * y1 + a2 * y2;
				
				out[i] = y0;
				y2 = y1;
				y1 = y0;
				x2 = x1;
				x1 = x0;
				
				in += inStride;
				freq += freqStride;
				rq += rqStride;
			}
			
			framesToFill -= n;
			out += n;
			_in.advance(n);
			_freq.advance(n);
			_rq.advance(n);
		}
		
		_x1 = x1;
		_x2 = x2;
		_y1 = y1;
		_y2 = y2;
		produce(framesToFill);
	}
};


struct Ringz : public Gen
{
	ZIn _in;
	ZIn _freq;
	ZIn _ringTime;
	Z _x1, _x2, _y1, _y2;
	Z _freqmul, _K;
	
	Ringz(Thread& th, Arg in, Arg freq, Arg ringTime)
		: Gen(th, itemTypeZ, mostFinite(in, freq, ringTime)), _in(in), _freq(freq), _ringTime(ringTime),
			_x1(0.), _x2(0.), _y1(0.), _y2(0.), _freqmul(th.rate.radiansPerSample),
			_K(log001 * th.rate.invSampleRate)
	{
	}
	
	virtual const char* TypeName() const override { return "Ringz"; }
	
	virtual void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		
		Z* out = mOut->fulfillz(framesToFill);
		Z x1 = _x1;
		Z x2 = _x2;
		Z y1 = _y1;
		Z y2 = _y2;
		Z freqmul = _freqmul;
		Z K = _K;
		
		while (framesToFill) {
			Z *in, *freq, *ringTime;
			int n, inStride, freqStride, ringTimeStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _freq(th, n, freqStride, freq) || _ringTime(th, n, ringTimeStride, ringTime)) {
				setDone();
				break;
			}
			
			
			for (int i = 0; i < n; ++i) {				
				Z w0 = *freq * freqmul;
				Z R = 1. + K / *ringTime;
				Z cs = tcos(w0);
				Z a1 = 2. * R * cs;
				Z a2 = -(R * R);
				Z b0 = .5;
				
				Z x0 = *in;
				Z y0 = b0 * (x0 - x2) + a1 * y1 + a2 * y2;
				
				out[i] = y0;
				y2 = y1;
				y1 = y0;
				x2 = x1;
				x1 = x0;
				
				in += inStride;
				freq += freqStride;
				ringTime += ringTimeStride;
			}
			
			framesToFill -= n;
			out += n;
			_in.advance(n);
			_freq.advance(n);
			_ringTime.advance(n);
		}
		
		_x1 = x1;
		_x2 = x2;
		_y1 = y1;
		_y2 = y2;
		produce(framesToFill);
	}
};

struct Formlet : public Gen
{
	ZIn _in;
	ZIn _freq;
	ZIn _atkTime;
	ZIn _dcyTime;
	Z _x1a, _x2a, _y1a, _y2a;
	Z _x1b, _x2b, _y1b, _y2b;
	Z _freqmul, _K;
	
	Formlet(Thread& th, Arg in, Arg freq, Arg atkTime, Arg dcyTime)
		: Gen(th, itemTypeZ, mostFinite(in, freq, atkTime, dcyTime)),
			_in(in), _freq(freq), _atkTime(atkTime), _dcyTime(dcyTime),
			_x1a(0.), _x2a(0.), _y1a(0.), _y2a(0.),
			_x1b(0.), _x2b(0.), _y1b(0.), _y2b(0.),
			_freqmul(th.rate.radiansPerSample),
			_K(log001 * th.rate.invSampleRate)
	{
	}
	
	virtual const char* TypeName() const override { return "Formlet"; }
	
	virtual void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		
		Z* out = mOut->fulfillz(framesToFill);
		Z x1a = _x1a;
		Z x2a = _x2a;
		Z y1a = _y1a;
		Z y2a = _y2a;
		Z x1b = _x1b;
		Z x2b = _x2b;
		Z y1b = _y1b;
		Z y2b = _y2b;
		Z freqmul = _freqmul;
		Z K = _K;
		
		while (framesToFill) {
			Z *in, *freq, *atkTime, *dcyTime;
			int n, inStride, freqStride, atkTimeStride, dcyTimeStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _freq(th, n, freqStride, freq) || _atkTime(th, n, atkTimeStride, atkTime) || _dcyTime(th, n, dcyTimeStride, dcyTime)) {
				setDone();
				break;
			}
			
			for (int i = 0; i < n; ++i) {				
				Z w0 = *freq * freqmul;
				Z Ra = 1. + K / *atkTime;
				Z Rb = 1. + K / *dcyTime;
				Z cs = tcos(w0);
				Z a1a = 2. * Ra * cs;
				Z a2a = -(Ra * Ra);
				Z a1b = 2. * Rb * cs;
				Z a2b = -(Rb * Rb);
				Z b0 = .5;
			
				Z x0a = *in;
				Z y0a = b0 * (x0a - x2a) + a1a * y1a + a2a * y2a;
			
				Z x0b = *in;
				Z y0b = b0 * (x0b - x2b) + a1b * y1b + a2b * y2b;
				
				out[i] = y0b - y0a;
				y2a = y1a;
				y1a = y0a;
				x2a = x1a;
				x1a = x0a;

				y2b = y1b;
				y1b = y0b;
				x2b = x1b;
				x1b = x0b;
				
				in += inStride;
				freq += freqStride;
				atkTime += atkTimeStride;
				dcyTime += dcyTimeStride;
			}
			
			framesToFill -= n;
			out += n;
			_in.advance(n);
			_freq.advance(n);
			_atkTime.advance(n);
			_dcyTime.advance(n);
		}
		
		_x1a = x1a;
		_x2a = x2a;
		_y1a = y1a;
		_y2a = y2a;
		
		_x1b = x1b;
		_x2b = x2b;
		_y1b = y1b;
		_y2b = y2b;
		produce(framesToFill);
	}
};

struct KlankFilter
{
	KlankFilter(V f, V a, V r) : 
		freq(f), amp(a), ringTime(r),
		x1(0.), x2(0.), y1(0.), y2(0.) {}
	
	ZIn freq, amp, ringTime;
	Z x1, x2, y1, y2;
	
};

struct Klank : public Gen
{
	ZIn _in;
	std::vector<KlankFilter> _filters;
	Z _freqmul, _K;
	Z* inputBuffer;
	
	Klank(Thread& th, Arg in, V freqs, V amps, V ringTimes)
		: Gen(th, itemTypeZ, in.isFinite()), _in(in),
			_freqmul(th.rate.radiansPerSample),
			_K(log001 * th.rate.invSampleRate)
	{
		inputBuffer = new Z[mBlockSize];
	
		int64_t numFilters = LONG_MAX;
		if (freqs.isVList()) { 
			freqs = ((List*)freqs.o())->pack(th); 
			numFilters = std::min(numFilters, freqs.length(th)); 
		}
		if (amps.isVList()) { 
			amps = ((List*)amps.o())->pack(th); 
			numFilters = std::min(numFilters, amps.length(th)); 
		}
		if (ringTimes.isVList()) { 
			ringTimes = ((List*)ringTimes.o())->pack(th); 
			numFilters = std::min(numFilters, ringTimes.length(th)); 
		}
		
		if (numFilters == LONG_MAX) numFilters = 1;
		
		for (ssize_t i = 0; i < numFilters; ++i) {
			KlankFilter kf(freqs.at(i), amps.at(i), ringTimes.at(i));
			_filters.push_back(kf);
		}
		
	}
	
	virtual ~Klank()
	{
		delete [] inputBuffer;
	}
	
	virtual const char* TypeName() const override { return "Klank"; }
	
	virtual void pull(Thread& th) override
	{		
		
		// copy input
		int numInputFrames = mBlockSize;
		if (_in.fill(th, numInputFrames, inputBuffer, 1))
		{
			end();
			return;
		}
		int maxToFill = 0;

		Z* out0 = mOut->fulfillz(numInputFrames);
		memset(out0, 0, numInputFrames * sizeof(Z));
		
		Z freqmul = _freqmul;
		Z K = log001 * th.rate.invSampleRate;
		
		for (size_t filter = 0; filter < _filters.size(); ++filter) {
			int framesToFill = numInputFrames;
			KlankFilter& kf = _filters[filter];
				
			Z x1 = kf.x1;
			Z x2 = kf.x2;
			Z y1 = kf.y1;
			Z y2 = kf.y2;

			Z* in = inputBuffer;
			Z* out = out0;
			while (framesToFill) {
				Z *freq, *amp, *ringTime;
				int n, freqStride, ampStride, ringTimeStride;
				n = framesToFill;
				if (kf.freq(th, n, freqStride, freq) || kf.amp(th, n, ampStride, amp) || kf.ringTime(th, n, ringTimeStride, ringTime)) {
					setDone();
					maxToFill = std::max(maxToFill, framesToFill);
					break;
				}
			
				if (freqStride == 0) {
					if (ringTimeStride == 0) {
						Z w0 = *freq * freqmul;
						Z R = 1. + K / *ringTime;
						Z cs = tcos(w0);
						Z a1 = 2. * R * cs;
						Z a2 = -(R * R);
						Z b0 = .5;
						for (int i = 0; i < n; ++i) {
							Z x0 = *in;
							Z y0 = *amp * b0 * (x0 - x2) + a1 * y1 + a2 * y2;
							
							*out += y0;
							y2 = y1;
							y1 = y0;
							x2 = x1;
							x1 = x0;
							
							++in;
							++out;
							amp += ampStride;
						}
					} else {
						Z w0 = *freq * freqmul;
						Z cs = tcos(w0);
						Z b0 = .5;
						for (int i = 0; i < n; ++i) {
							Z R = 1. + K / *ringTime;
							Z a1 = 2. * R * cs;
							Z a2 = -(R * R);
							Z x0 = *in;
							Z y0 = *amp * b0 * (x0 - x2) + a1 * y1 + a2 * y2;
							
							*out += y0;
							y2 = y1;
							y1 = y0;
							x2 = x1;
							x1 = x0;
							
							++in;
							++out;
							amp += ampStride;
						}
					}
				} else {
					Z b0 = .5;
					for (int i = 0; i < n; ++i) {
						Z w0 = *freq * freqmul;
						Z R = 1. + K / *ringTime;
						Z cs = tcos(w0);
						Z a1 = 2. * R * cs;
						Z a2 = -(R * R);
						
						Z x0 = *in;
						Z y0 = *amp * b0 * (x0 - x2) + a1 * y1 + a2 * y2;
						
						*out += y0;
						y2 = y1;
						y1 = y0;
						x2 = x1;
						x1 = x0;
						
						++in;
						++out;
						freq += freqStride;
						amp += ampStride;
						ringTime += ringTimeStride;
					}
				}
				
				framesToFill -= n;
				out += n;
				kf.freq.advance(n);
				kf.amp.advance(n);
				kf.ringTime.advance(n);
			}
			kf.x1 = x1;
			kf.x2 = x2;
			kf.y1 = y1;
			kf.y2 = y2;
		}
		produce(maxToFill);
	}
};


static void resonz_(Thread& th, Prim* prim)
{
	V rq   = th.popZIn("resonz : rq");
	V freq = th.popZIn("resonz : freq");
	V in   = th.popZIn("resonz : in");

	th.push(new List(new Resonz(th, in, freq, rq)));
}

static void ringz_(Thread& th, Prim* prim)
{
	V ringTime = th.popZIn("ringz : ringTime");
	V freq     = th.popZIn("ringz : freq");
	V in       = th.popZIn("ringz : in");

	th.push(new List(new Ringz(th, in, freq, ringTime)));
}

static void formlet_(Thread& th, Prim* prim)
{
	V dcyTime = th.popZIn("formlet : dcyTime");
	V atkTime = th.popZIn("formlet : atkTime");
	V freq    = th.popZIn("formlet : freq");
	V in      = th.popZIn("formlet : in");

	th.push(new List(new Formlet(th, in, freq, atkTime, dcyTime)));
}


static void klank_(Thread& th, Prim* prim)
{
	V ringTimes = th.popZInList("klank : ringTimes");
	V amps		= th.popZInList("klank : amps");
	V freqs     = th.popZInList("klank : freqs");
	V in		= th.popZIn("klank : in");
	
	if (freqs.isVList() && !freqs.isFinite())
		indefiniteOp("klank : freqs", "");

	if (amps.isVList() && !amps.isFinite())
		indefiniteOp("klank : amps", "");

	if (ringTimes.isVList() && !ringTimes.isFinite())
		indefiniteOp("klank : ringTimes", "");

	th.push(new List(new Klank(th, in, freqs, amps, ringTimes)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct LeakDC : public Gen
{
	ZIn _in;
	ZIn _leak;
	Z _x1, _y1;
	
	LeakDC(Thread& th, Arg in, Arg leak)
		: Gen(th, itemTypeZ, mostFinite(in, leak)), _in(in), _leak(leak), 
			_x1(0.), _y1(0.)
	{
	}
	
	virtual const char* TypeName() const override { return "LeakDC"; }
	
	virtual void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		
		Z* out = mOut->fulfillz(framesToFill);
		Z x1 = _x1;
		Z y1 = _y1;
		while (framesToFill) {
			Z *in, *leak;
			int n, inStride, leakStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _leak(th, n, leakStride, leak)) {
				setDone();
				break;
			}
			
			for (int i = 0; i < n; ++i) {
				Z x0 = *in;
				y1 = x0 - x1 + *leak * y1;
				out[i] = y1;
				x1 = x0;
				in += inStride;
				leak += leakStride;
			}
			
			framesToFill -= n;
			out += n;
			_in.advance(n);
			_leak.advance(n);
		}
		
		_x1 = x1;
		_y1 = y1;
		produce(framesToFill);
	}
	
};

static void leakdc_(Thread& th, Prim* prim)
{
	V leak = th.popZIn("leakdc : leak");
	V in   = th.popZIn("leakdc : in");

	th.push(new List(new LeakDC(th, in, leak)));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct LeakyIntegrator : public Gen
{
	ZIn _in;
	ZIn _leak;
	Z _y1;
	
	LeakyIntegrator(Thread& th, Arg in, Arg leak)
		: Gen(th, itemTypeZ, mostFinite(in, leak)), _in(in), _leak(leak), 
			_y1(0.)
	{
	}
	
	virtual const char* TypeName() const override { return "LeakyIntegrator"; }
	
	virtual void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		
		Z* out = mOut->fulfillz(framesToFill);
		Z y1 = _y1;
		while (framesToFill) {
			Z *in, *leak;
			int n, inStride, leakStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _leak(th, n, leakStride, leak)) {
				setDone();
				break;
			}
			
			for (int i = 0; i < n; ++i) {
				Z x0 = *in;
				y1 = x0 + *leak * y1;
				out[i] = y1;
				in += inStride;
				leak += leakStride;
			}
			
			framesToFill -= n;
			out += n;
			_in.advance(n);
			_leak.advance(n);
		}
		
		_y1 = y1;
		produce(framesToFill);
	}
	
};

static void leaky_(Thread& th, Prim* prim)
{
	V leak = th.popZIn("leaky : leak");
	V in   = th.popZIn("leaky : in");

	th.push(new List(new LeakyIntegrator(th, in, leak)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct Decay : public Gen
{
	ZIn _in;
	ZIn _decayTime;
	Z _y1;
	Z _lagmul;
	
	
	Decay(Thread& th, Arg in, Arg decayTime)
		: Gen(th, itemTypeZ, mostFinite(in, decayTime)), _in(in), _decayTime(decayTime), 
			_y1(0.), _lagmul(log001 * th.rate.invSampleRate)
	{
	}
	
	virtual const char* TypeName() const override { return "Decay"; }
	
	virtual void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		
		Z* out = mOut->fulfillz(framesToFill);
		Z y1 = _y1;
		Z lagmul = _lagmul;
		while (framesToFill) {
			Z *in, *decayTime;
			int n, inStride, decayTimeStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _decayTime(th, n, decayTimeStride, decayTime)) {
				setDone();
				break;
			}
			
			if (decayTimeStride == 0) {
				Z b1 = std::max(0., 1. + lagmul / *decayTime); // this is an approximation to exp(log001 * th.rate.invSampleRate / *lagTime)  
				for (int i = 0; i < n; ++i) {
					Z x0 = *in;
					out[i] = y1 = x0 + b1 * y1;
					in += inStride;
				}
			} else {			
				for (int i = 0; i < n; ++i) {
					Z b1 = std::max(0., 1. + lagmul / *decayTime); // this is an approximation to exp(log001 * th.rate.invSampleRate / *lagTime)  
					Z x0 = *in;
					out[i] = y1 = x0 + b1 * y1;
					in += inStride;
					decayTime += decayTimeStride;
				}
			}
			
			framesToFill -= n;
			out += n;
			_in.advance(n);
			_decayTime.advance(n);
		}
		
		_y1 = y1;
		produce(framesToFill);
	}
	
};

static void decay_(Thread& th, Prim* prim)
{
	V decayTime = th.popZIn("decay : decayTime");
	V in   = th.popZIn("decay : in");

	th.push(new List(new Decay(th, in, decayTime)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct Decay2 : public Gen
{
	ZIn _in;
	ZIn _attackTime;
	ZIn _decayTime;
	Z _y1a;
	Z _y1b;
	Z _lagmul;
	
	
	Decay2(Thread& th, Arg in, Arg attackTime, Arg decayTime)
		: Gen(th, itemTypeZ, mostFinite(in, attackTime, decayTime)), _in(in), _attackTime(attackTime), _decayTime(decayTime), 
			_y1a(0.), _y1b(0.), _lagmul(log001 * th.rate.invSampleRate)
	{
	}
	
	virtual const char* TypeName() const override { return "Decay2"; }
	
	virtual void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		
		Z* out = mOut->fulfillz(framesToFill);
		Z y1a = _y1a;
		Z y1b = _y1b;
		Z lagmul = _lagmul;
		while (framesToFill) {
			Z *in, *attackTime, *decayTime;
			int n, inStride, attackTimeStride, decayTimeStride;
			n = framesToFill;
			if (_in(th, n, inStride, in) || _attackTime(th, n, attackTimeStride, attackTime) || _decayTime(th, n, decayTimeStride, decayTime)) {
				setDone();
				break;
			}
			
			if (attackTimeStride == 0 && decayTimeStride == 0) {
				Z b1a = std::max(0., 1. + lagmul / *attackTime); // this is an approximation to exp(log001 * th.rate.invSampleRate / *lagTime)  
				Z b1b = std::max(0., 1. + lagmul / *decayTime); // this is an approximation to exp(log001 * th.rate.invSampleRate / *lagTime)  
				for (int i = 0; i < n; ++i) {
					Z x0 = *in;
					y1a = x0 + b1a * y1a;
					y1b = x0 + b1b * y1b;
					out[i] = y1b - y1a;
					in += inStride;
				}
			} else {			
				for (int i = 0; i < n; ++i) {
					Z b1a = std::max(0., 1. + lagmul / *attackTime); // this is an approximation to exp(log001 * th.rate.invSampleRate / *lagTime)  
					Z b1b = std::max(0., 1. + lagmul / *decayTime); // this is an approximation to exp(log001 * th.rate.invSampleRate / *lagTime)  
					Z x0 = *in;
					y1a = x0 + b1a * y1a;
					y1b = x0 + b1b * y1b;
					out[i] = y1b - y1a;
					in += inStride;
					attackTime += attackTimeStride;
					decayTime += decayTimeStride;
				}
			}
			
			framesToFill -= n;
			out += n;
			_in.advance(n);
			_attackTime.advance(n);
			_decayTime.advance(n);
		}
		
		_y1a = y1a;
		_y1b = y1b;
		produce(framesToFill);
	}
	
};

static void decay2_(Thread& th, Prim* prim)
{
	V decayTime = th.popZIn("decay2 : decayTime");
	V attackTime = th.popZIn("decay2 : attackTime");
	V in   = th.popZIn("decay2 : in");

	th.push(new List(new Decay2(th, in, attackTime, decayTime)));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct Phase90A : public OneInputUGen<Phase90A>
{
	// filters by Olli Niemitalo
	constexpr static Z c1_ = 0.47940086558884; // sq(.6923878);
	constexpr static Z c2_ = 0.87621849353931; // sq(.9360654322959);
	constexpr static Z c3_ = 0.9765975895082;  // sq(.9882295226860);
	constexpr static Z c4_ = 0.99749925593555; // sq(.9987488452737);
	Z v1_ = 0.;
	Z v2_ = 0.;
	Z w1_ = 0.;
	Z w2_ = 0.;
	Z x1_ = 0.;
	Z x2_ = 0.;
	Z y1_ = 0.;
	Z y2_ = 0.;
	Z z1_ = 0.;
	Z z2_ = 0.;
	
	Phase90A(Thread& th, Arg in) : OneInputUGen<Phase90A>(th, in)
	{
	}
	
	virtual const char* TypeName() const override { return "Phase90A"; }
	
	void calc(int n, Z* out, Z* in, int inStride)
	{
		Z c1 = c1_;
		Z c2 = c2_;
		Z c3 = c3_;
		Z c4 = c4_;
		
		Z v1 = v1_;
		Z v2 = v2_;
		Z w1 = w1_;
		Z w2 = w2_;
		Z x1 = x1_;
		Z x2 = x2_;
		Z y1 = y1_;
		Z y2 = y2_;
		Z z1 = z1_;
		Z z2 = z2_;
		for (int i = 0; i < n; ++i) {
			Z v0 = *in; in += inStride;
			Z w0 = c1 * (v0 + w2) - v2;
			Z x0 = c2 * (w0 + x2) - w2;
			Z y0 = c3 * (x0 + y2) - x2;
			Z z0 = c4 * (y0 + z2) - y2;
			out[i] = z1;
			v2 = v1;
			w2 = w1;
			x2 = x1;
			y2 = y1;
			z2 = z1;
			v1 = v0;
			w1 = w0;
			x1 = x0;
			y1 = y0;
			z1 = z0;
		}
		v1_ = v1;
		v2_ = v2;
		w1_ = w1;
		w2_ = w2;
		x1_ = x1;
		x2_ = x2;
		y1_ = y1;
		y2_ = y2;
		z1_ = z1;
		z2_ = z2;
	}
};

struct Phase90B : public OneInputUGen<Phase90B>
{
	// filters by Olli Niemitalo
	constexpr static Z c1_ = 0.1617584983677;  // sc_squared(.4021921162426);
	constexpr static Z c2_ = 0.73302893234149; // sc_squared(.8561710882420);
	constexpr static Z c3_ = 0.94534970032911; // sc_squared(.9722909545651);
	constexpr static Z c4_ = 0.99059915668453; // sc_squared(.9952884791278);
	Z v1_ = 0.;
	Z v2_ = 0.;
	Z w1_ = 0.;
	Z w2_ = 0.;
	Z x1_ = 0.;
	Z x2_ = 0.;
	Z y1_ = 0.;
	Z y2_ = 0.;
	Z z1_ = 0.;
	Z z2_ = 0.;
	
	Phase90B(Thread& th, Arg in) : OneInputUGen<Phase90B>(th, in)
	{
	}
	
	virtual const char* TypeName() const override { return "Phase90B"; }
	
	void calc(int n, Z* out, Z* in, int inStride)
	{
		Z c1 = c1_;
		Z c2 = c2_;
		Z c3 = c3_;
		Z c4 = c4_;
		
		Z v1 = v1_;
		Z v2 = v2_;
		Z w1 = w1_;
		Z w2 = w2_;
		Z x1 = x1_;
		Z x2 = x2_;
		Z y1 = y1_;
		Z y2 = y2_;
		Z z1 = z1_;
		Z z2 = z2_;
		for (int i = 0; i < n; ++i) {
			Z v0 = *in; in += inStride;
			Z w0 = c1 * (v0 + w2) - v2;
			Z x0 = c2 * (w0 + x2) - w2;
			Z y0 = c3 * (x0 + y2) - x2;
			Z z0 = c4 * (y0 + z2) - y2;
			out[i] = z0;
			v2 = v1;
			w2 = w1;
			x2 = x1;
			y2 = y1;
			z2 = z1;
			v1 = v0;
			w1 = w0;
			x1 = x0;
			y1 = y0;
			z1 = z0;
		}
		v1_ = v1;
		v2_ = v2;
		w1_ = w1;
		w2_ = w2;
		x1_ = x1;
		x2_ = x2;
		y1_ = y1;
		y2_ = y2;
		z1_ = z1;
		z2_ = z2;
	}
};

struct AmpFollow : public OneInputUGen<AmpFollow>
{
	Z _y1a;
	Z _y1b;
	Z _lagmul;
	bool once;

	// filters by Olli Niemitalo
	constexpr static Z c1a_ = 0.47940086558884; // sq(.6923878);
	constexpr static Z c2a_ = 0.87621849353931; // sq(.9360654322959);
	constexpr static Z c3a_ = 0.9765975895082;  // sq(.9882295226860);
	constexpr static Z c4a_ = 0.99749925593555; // sq(.9987488452737);
	Z v1a_ = 0.;
	Z v2a_ = 0.;
	Z w1a_ = 0.;
	Z w2a_ = 0.;
	Z x1a_ = 0.;
	Z x2a_ = 0.;
	Z y1a_ = 0.;
	Z y2a_ = 0.;
	Z z1a_ = 0.;
	Z z2a_ = 0.;

	constexpr static Z c1b_ = 0.1617584983677;  // sc_squared(.4021921162426);
	constexpr static Z c2b_ = 0.73302893234149; // sc_squared(.8561710882420);
	constexpr static Z c3b_ = 0.94534970032911; // sc_squared(.9722909545651);
	constexpr static Z c4b_ = 0.99059915668453; // sc_squared(.9952884791278);
	Z v1b_ = 0.;
	Z v2b_ = 0.;
	Z w1b_ = 0.;
	Z w2b_ = 0.;
	Z x1b_ = 0.;
	Z x2b_ = 0.;
	Z y1b_ = 0.;
	Z y2b_ = 0.;
	Z z1b_ = 0.;
	Z z2b_ = 0.;

	Z b1r_;
	Z b1f_;
	
	Z l1a_ = 0.;
	Z l1b_ = 0.;

	AmpFollow(Thread& th, Arg in, Z atk, Z dcy)
		: OneInputUGen<AmpFollow>(th, in),
		_y1a(0.), _y1b(0.), _lagmul(log001 * th.rate.invSampleRate), once(true)
	{
		b1r_ = atk == 0. ? 0. : std::max(0., 1. + _lagmul / atk); // this is an approximation to exp(log001 * th.rate.invSampleRate / *riseTime)
		b1f_ = dcy == 0. ? 0. : std::max(0., 1. + _lagmul / dcy);
	}
	
	virtual const char* TypeName() const override { return "AmpFollow"; }
	
	void calc(int n, Z* out, Z* in, int inStride)
	{
		Z c1a = c1a_;
		Z c2a = c2a_;
		Z c3a = c3a_;
		Z c4a = c4a_;
		
		Z v1a = v1a_;
		Z v2a = v2a_;
		Z w1a = w1a_;
		Z w2a = w2a_;
		Z x1a = x1a_;
		Z x2a = x2a_;
		Z y1a = y1a_;
		Z y2a = y2a_;
		Z z1a = z1a_;
		Z z2a = z2a_;

		Z c1b = c1b_;
		Z c2b = c2b_;
		Z c3b = c3b_;
		Z c4b = c4b_;
		
		Z v1b = v1b_;
		Z v2b = v2b_;
		Z w1b = w1b_;
		Z w2b = w2b_;
		Z x1b = x1b_;
		Z x2b = x2b_;
		Z y1b = y1b_;
		Z y2b = y2b_;
		Z z1b = z1b_;
		Z z2b = z2b_;
		
		Z l1a = l1a_;
		Z l1b = l1b_;

		for (int i = 0; i < n; ++i) {
			Z v0 = *in; in += inStride;
			{
				Z w0 = c1a * (v0 + w2a) - v2a;
				Z x0 = c2a * (w0 + x2a) - w2a;
				Z y0 = c3a * (x0 + y2a) - x2a;
				Z z0 = c4a * (y0 + z2a) - y2a;
				v2a = v1a;
				w2a = w1a;
				x2a = x1a;
				y2a = y1a;
				z2a = z1a;
				v1a = v0;
				w1a = w0;
				x1a = x0;
				y1a = y0;
				z1a = z0;
			}

			{
				Z w0 = c1b * (v0 + w2b) - v2b;
				Z x0 = c2b * (w0 + x2b) - w2b;
				Z y0 = c3b * (x0 + y2b) - x2b;
				Z z0 = c4b * (y0 + z2b) - y2b;
				v2b = v1b;
				w2b = w1b;
				x2b = x1b;
				y2b = y1b;
				z2b = z1b;
				v1b = v0;
				w1b = w0;
				x1b = x0;
				y1b = y0;
				z1b = z0;
			}
			
			Z l0a = hypot(z1a, z1b); // vectorize this

			l1a = l0a + (l0a > l1a ? b1r_ : b1f_) * (l1a - l0a);
			l1b = l1a + (l1a > l1b ? b1r_ : b1f_) * (l1b - l1a);
			out[i] = l1b;
		}
		v1a_ = v1a;
		v2a_ = v2a;
		w1a_ = w1a;
		w2a_ = w2a;
		x1a_ = x1a;
		x2a_ = x2a;
		y1a_ = y1a;
		y2a_ = y2a;
		z1a_ = z1a;
		z2a_ = z2a;

		v1b_ = v1b;
		v2b_ = v2b;
		w1b_ = w1b;
		w2b_ = w2b;
		x1b_ = x1b;
		x2b_ = x2b;
		y1b_ = y1b;
		y2b_ = y2b;
		z1b_ = z1b;
		z2b_ = z2b;
		
		l1a_ = l1a;
		l1b_ = l1b;
	}
};

static void hilbert_(Thread& th, Prim* prim)
{
	V in   = th.popZIn("hilbert : in");
	
	th.push(new List(new Phase90A(th, in)));
	th.push(new List(new Phase90B(th, in)));
}

static void ampf_(Thread& th, Prim* prim)
{
	Z dcy  = th.popFloat("ampf : dcyTime");
	Z atk  = th.popFloat("ampf : atkTime");
	V in   = th.popZIn("ampf : in");
	
	th.push(new List(new AmpFollow(th, in, atk, dcy)));
}

#define DEF(NAME, N, HELP) 	vm.def(#NAME, N, NAME##_, HELP);
#define DEFMCX(NAME, N, HELP) 	vm.defmcx(#NAME, N, NAME##_, HELP);
#define DEFAM(NAME, MASK, HELP) 	vm.defautomap(#NAME, #MASK, NAME##_, HELP);

void AddFilterUGenOps()
{
	vm.addBifHelp("\n*** filter unit generators ***");
	DEFMCX(lag, 2, "(in decayTime --> out) one pole lag filter. decayTime determines rate of convergence.")
	DEFMCX(lag2, 2, "(in decayTime --> out) cascade of two one pole lag filters. decayTime determines rate of convergence.")
	DEFMCX(lag3, 2, "(in decayTime --> out) cascade of three one pole lag filters. decayTime determines rate of convergence.")

	DEFMCX(lagud, 3, "(in upDecayTime, downDecayTime --> out) one pole lag filter. up/down DecayTimes determines rate of convergence up/down.")
	DEFMCX(lagud2, 3, "(in upDecayTime, downDecayTime --> out) cascade of two one pole lag filters. up/down DecayTimes determines rate of convergence up/down.")
	DEFMCX(lagud3, 3, "(in upDecayTime, downDecayTime --> out) cascade of three one pole lag filters. up/down DecayTimes determines rate of convergence up/down.")
	
	DEFMCX(lpf1, 2, "(in freq --> out) low pass filter. 6 dB/oct.")
	DEFMCX(hpf1, 2, "(in freq --> out) high pass filter. 6 dB/oct.")
	DEFMCX(lpf, 2, "(in freq --> out) low pass filter. 12 dB/oct.")
	DEFMCX(hpf, 2, "(in freq --> out) high pass filter. 12 dB/oct.")
	DEFMCX(lpf2, 2, "(in freq --> out) low pass filter. 24 dB/oct.")
	DEFMCX(hpf2, 2, "(in freq --> out) high pass filter. 24 dB/oct.")
	
	DEFMCX(rlpf, 3, "(in freq rq --> out) resonant low pass filter. 12 dB/oct slope. rq is 1/Q.")
	DEFMCX(rhpf, 3, "(in freq rq --> out) resonant high pass filter. 12 dB/oct slope. rq is 1/Q.")
	DEFMCX(rlpf2, 3, "(in freq rq --> out) resonant low pass filter. 24 dB/oct slope. rq is 1/Q.")
	DEFMCX(rhpf2, 3, "(in freq rq --> out) resonant high pass filter. 24 dB/oct slope. rq is 1/Q.")
	
	DEFMCX(rlpfc, 3, "(in freq rq --> out) resonant low pass filter with saturation. 12 dB/oct slope. rq is 1/Q.")
	DEFMCX(rhpfc, 3, "(in freq rq --> out) resonant high pass filter with saturation. 12 dB/oct slope. rq is 1/Q.")
	DEFMCX(rlpf2c, 3, "(in freq rq --> out) resonant low pass filter with saturation. 24 dB/oct slope. rq is 1/Q.")
	DEFMCX(rhpf2c, 3, "(in freq rq --> out) resonant high pass filter with saturation. 24 dB/oct slope. rq is 1/Q.")

	DEFMCX(bpf, 3, "(in freq bw --> out) band pass filter. bw is bandwidth in octaves.")
	DEFMCX(bsf, 3, "(in freq bw --> out) band stop filter. bw is bandwidth in octaves.")
	DEFMCX(apf, 3, "(in freq bw --> out) all pass filter. bw is bandwidth in octaves.")
	
	DEFMCX(peq, 4, "(in freq bw gain --> out) parametric equalization filter. bw is bandwidth in octaves.")
	DEFMCX(lsf, 3, "(in freq gain --> out) low shelf filter.")
	DEFMCX(hsf, 3, "(in freq gain --> out) high shelf filter.")
	DEFMCX(lsf1, 3, "(in freq gain --> out) low shelf filter.")

	DEFMCX(resonz, 3, "(in freq rq --> out) resonant filter.")
	DEFMCX(ringz, 3, "(in freq ringTime --> out) resonant filter specified by a ring time in seconds.")
	DEFMCX(formlet, 4, "(in freq atkTime dcyTime --> out) a formant filter whose impulse response is a sine grain.")
	DEFAM(klank, zaaa, "(in freqs amps ringTimes --> out) a bank of ringz filters. freqs amps and ringTimes are arrays.")

	DEFMCX(leakdc, 2, "(in coef --> out) leaks away energy at 0 Hz.")
	DEFMCX(leaky, 2, "(in coef --> out) leaky integrator.")
	DEFMCX(decay, 2, "(in decayTime --> out) outputs an exponential decay for impulses at the input.")
	DEFMCX(decay2, 3, "(in atkTime dcyTime --> out) outputs an exponential attack and decay for impulses at the input.")

	DEFMCX(hilbert, 1, "(in --> outA outB) returns two signals that are 90 degrees phase shifted from each other.")
	DEFMCX(ampf, 3, "(in atkTime dcyTime --> out) amplitude follower.")
}



