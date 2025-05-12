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

#include "VM.hpp"
#include "clz.hpp"
#include "elapsedTime.hpp"
#include <climits>
#include <cmath>
#include <memory>
#include <float.h>
#include <vector>
#include <algorithm>

#include "ZArr.hpp"
#include "MultichannelExpansion.hpp"
#include "UGen.hpp"
#include "dsp.hpp"
#include "SoundFiles.hpp"

const Z kOneThird = 1. / 3.;

// list ops
#pragma mark LIST OPS

static void finite_(Thread& th, Prim* prim)
{
	V v = th.pop();
	th.pushBool(v.isFinite());
}

static void size_(Thread& th, Prim* prim)
{
	V v = th.pop();
	if (v.isList() && !v.isFinite()) 
		th.push(INFINITY);
	else 
		th.push(v.length(th));
}


static void rank_(Thread& th, Prim* prim)
{
	V a = th.pop();
	
	int rank = 0;
	while (a.isVList()) {
		++rank;
		VIn in(a);
		if (in.one(th, a)) break;
	}
	
	th.push(rank);
}

static void shape_(Thread& th, Prim* prim)
{
	P<Array> shape = new Array(itemTypeZ, 4);
	
	V a = th.pop();
	
	while (a.isVList()) {
		Z len;
		if (a.isFinite()) {
			len = a.length(th);
		} else {
			len = INFINITY;
		}
		shape->addz(len);
		VIn in(a);
		if (in.one(th, a)) break;
	}
	th.push(new List(shape));
}

static void bub_(Thread& th, Prim* prim)
{
	V a = th.pop();
	P<List> seq = new List(itemTypeV, 1);
	seq->add(a);
	th.push(seq);
}

static void nbub_(Thread& th, Prim* prim)
{
	int64_t n = th.popInt("nbub : n");
	V a = th.pop();
	
	for (int64_t i = 0; i < n; ++i) {
		P<List> seq = new List(itemTypeV, 1);
		seq->add(a);
		a = seq;
	}
	th.push(a);
}

template <int N>
static void tupleN_(Thread& th, Prim* prim)
{
	P<List> seq = new List(itemTypeV, N);
	P<Array> arr = seq->mArray;
	arr->setSize(N);
	for (int i = 0; i < N; ++i) {
		V v = th.pop();
		arr->put(N-1-i, v);
	}
	th.push(seq);
}

template <int N>
static void untupleN_(Thread& th, Prim* prim)
{
	P<List> s = th.popList("unN : s");
	
	BothIn in(s);
	for (int i = 0; i < N; ++i) {
		V v;
		if (in.one(th, v)) {
			post("too few items in list for un%d", N);
			throw errFailed;
		}
		th.push(v);
	}
}

static void reverse_(Thread& th, Prim* prim)
{
	P<List> s = th.popList("reverse : s");
	
	if (!s->isFinite())
		indefiniteOp("reverse", "");
		
	s = s->pack(th);
	if (s->isEnd()) {
		th.push(s);
		return;
	}

	P<Array> const& a = s->mArray;
	P<List> s2 = new List(a->elemType, a->size());
	P<Array> const& a2 = s2->mArray;
	int64_t n = a->size();
	int64_t n1 = n-1;
	a2->setSize(n);
	if (a->isV()) {
		V* p = a2->v();
		V* q = a->v();
		for (int i = 0; i < n; ++i) {
			p[i] = q[n1-i];
		}
	} else {
		Z* p = a2->z();
		Z* q = a->z();
		for (int i = 0; i < n; ++i) {
			p[i] = q[n1-i];
		}
	}
	th.push(s2);
}

template <typename T>
void copy(T* dst, T* src, int64_t n)
{
	for (int64_t i = 0; i <  n; ++i) dst[i] = src[i];
}

template <typename T>
void reverse_copy(T* dst, T* src, int64_t n)
{
	for (int64_t i = 0; i <  n; ++i) dst[i] = src[-i];
}

static List* makeMirror(P<Array> const& a, int64_t n, int64_t nr, int64_t roff)
{
	int type = a->elemType;
	int64_t size = n + nr;
	List* s = new List(type, size);
	Array* b = s->mArray();
	
	b->setSize(size);
	if (type == itemTypeV) {
		V* p = b->v();
		V* q = a->v();
		copy(p, q, n);
		reverse_copy(p+n, q+roff, nr);
	} else {
		Z* p = b->z();
		Z* q = a->z();
		copy(p, q, n);
		reverse_copy(p+n, q+roff, nr);
	}
	return s;
}

static void mirror(Thread& th, int w, P<List> s)
{	
	if (!s->isFinite())
		indefiniteOp("mirror", "");

	s = s->pack(th);

	P<Array> const& a = s->mArray;
	int64_t n = a->size();
	int64_t n1 = n-1;
	int64_t n2 = n-2;
	
	switch (w) {
		case 0 : {
			if (n < 3) {
				th.push(s);
				return;
			}
			th.push(makeMirror(a, n, n2, n2));
		} break;
		case 1 : {
			if (n < 2) {
				th.push(s);
				return;
			}
			th.push(makeMirror(a, n, n1, n2));
		} break;
		case 2 : {
			if (n == 0) {
				th.push(s);
				return;
			}
			th.push(makeMirror(a, n, n, n1));
		} break;
	}
}

static void mirror0_(Thread& th, Prim* prim)
{
	P<List> s = th.popList("mirror0 : s");
	mirror(th, 0, s);
}

static void mirror1_(Thread& th, Prim* prim)
{
	P<List> s = th.popList("mirror1 : s");
	mirror(th, 1, s);
}

static void mirror2_(Thread& th, Prim* prim)
{
	P<List> s = th.popList("mirror2 : s");
	mirror(th, 2, s);
}

static void rot_(Thread& th, Prim* prim)
{
	int64_t r = th.popInt("rot : r");
	P<List> s = th.popList("rot : s");
	
	if (r == 0) {
		th.push(s);
		return;
	}

	if (!s->isFinite())
		indefiniteOp("rot", "");

	s = s->pack(th);
	if (s->isEnd()) {
		th.push(s);
		return;
	}

	P<Array> const& a = s->mArray;
	int64_t n = a->size();
	int type = a->elemType;
	
	P<List> s2 = new List(type, n);
	P<Array> const& b = s2->mArray;
	if (type == itemTypeV) {
		for (int i = 0; i < n; ++i) b->add(a->wrapAt(i-r));
	} else {
		for (int i = 0; i < n; ++i) b->addz(a->wrapAtz(i-r));
	}
	th.push(s2);
}

static void shift_(Thread& th, Prim* prim)
{
	int64_t r = th.popInt("shift : r");
	P<List> s = th.popList("shift : s");

	if (r == 0) {
		th.push(s);
		return;
	}

	if (!s->isFinite())
		indefiniteOp("shift", "");

	s = s->pack(th);
	if (s->isEnd()) {
		th.push(s);
		return;
	}
    
	P<Array> const& a = s->mArray;
	int64_t n = a->size();
	int type = a->elemType;
	
	P<List> s2 = new List(type, n);
	P<Array> const& b = s2->mArray;
	if (type == itemTypeV) {
		for (int i = 0; i < n; ++i) b->add(a->at(i-r));
	} else {
		for (int i = 0; i < n; ++i) b->addz(a->atz(i-r));
	}
	th.push(s2);
}

static void clipShift_(Thread& th, Prim* prim)
{
	int64_t r = th.popInt("clipShift : r");
	P<List> s = th.popList("clipShift : s");

	if (r == 0) {
		th.push(s);
		return;
	}
	
	if (!s->isFinite())
		indefiniteOp("clipShift", "");

	s = s->pack(th);
	if (s->isEnd()) {
		th.push(s);
		return;
	}
    
	P<Array> const& a = s->mArray;
	int64_t n = a->size();
	int type = a->elemType;
	
	P<List> s2 = new List(type, n);
	P<Array> const& b = s2->mArray;
	if (type == itemTypeV) {
		for (int i = 0; i < n; ++i) b->add(a->clipAt(i-r));
	} else {
		for (int i = 0; i < n; ++i) b->addz(a->clipAtz(i-r));
	}
	th.push(s2);
}

static void foldShift_(Thread& th, Prim* prim)
{
	int64_t r = th.popInt("foldShift : r");
	P<List> s = th.popList("foldShift : s");

	if (r == 0) {
		th.push(s);
		return;
	}
	
	if (!s->isFinite())
		indefiniteOp("foldShift", "");

	s = s->pack(th);
	if (s->isEnd()) {
		th.push(s);
		return;
	}
    
	P<Array> const& a = s->mArray;
	int64_t n = a->size();
	int type = a->elemType;
	
	P<List> s2 = new List(type, n);
	P<Array> const& b = s2->mArray;
	if (type == itemTypeV) {
		for (int i = 0; i < n; ++i) b->add(a->foldAt(i-r));
	} else {
		for (int i = 0; i < n; ++i) b->addz(a->foldAtz(i-r));
	}
	th.push(s2);
}

static void muss_(Thread& th, Prim* prim)
{
	P<List> s = th.popList("muss : s");
	
	if (!s->isFinite())
		indefiniteOp("muss", "");
    
	s = s->pack(th);
	if (s->isEnd()) {
		th.push(s);
		return;
	}
    
	P<Array> const& a = s->mArray;
	P<List> s2 = new List(a->elemType, a->size());
	P<Array> const& a2 = s2->mArray;
	int64_t n = a->size();
	int64_t n1 = n-1;
	a2->setSize(n);
	if (a->isV()) {
		V* p = a2->v();
		V* q = a->v();
		for (int i = 0; i < n; ++i) {
			p[i] = q[i];
		}
		for (int64_t i = 0; i < n1; ++i) {
            int64_t j = th.rgen.irand(i, n1);
            if (j != i) 
                std::swap(p[i], p[j]);
		}
	} else {
		Z* p = a2->z();
		Z* q = a->z();
		for (int64_t i = 0; i < n; ++i) {
			p[i] = q[i];
		}
		for (int64_t i = 0; i < n1; ++i) {
            int64_t j = th.rgen.irand(i, n1);
            if (j != i) 
                std::swap(p[i], p[j]);
		}
	}
	th.push(s2);
}





V do_at(Thread& th, P<Array> const& a, Arg i);
V do_wrapAt(Thread& th, P<Array> const& a, Arg i);
V do_foldAt(Thread& th, P<Array> const& a, Arg i);
V do_clipAt(Thread& th, P<Array> const& a, Arg i);
V do_degkey(Thread& th, P<Array> const& a, Arg b, Z cycleWidth, int degreesPerCycle);
V do_keydeg(Thread& th, P<Array> const& a, Arg b, Z cycleWidth, int degreesPerCycle);

class AtGenVV : public Gen
{
	P<Array> _a;
	VIn _b;
public:
	AtGenVV(Thread& th, P<Array> const& a, Arg b) : Gen(th, itemTypeV, b.isFinite()), _a(a), _b(b) {}

	virtual const char* TypeName() const override { return "AtGenVV"; }

	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int bstride;
			V *b;
			if (_b(th, n,bstride, b)) {
				setDone();
				break;
			} else {
				for (int i = 0; i < n; ++i) {
					out[i] = do_at(th, _a, *b);
					b += bstride;
				}
				_b.advance(n);
				framesToFill -= n;
				out += n;
			}
		}
		produce(framesToFill);
	}
	
};

class AtGenVZ : public Gen
{
	P<Array> _a;
	ZIn _b;
public:
	AtGenVZ(Thread& th, P<Array> const& a, Arg b) : Gen(th, itemTypeV, b.isFinite()), _a(a), _b(b) {}
	
	virtual const char* TypeName() const override { return "AtGenVZ"; }
	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int bstride;
			Z *b;
			if (_b(th, n,bstride, b)) {
				setDone();
				break;
			} else {
				for (int i = 0; i < n; ++i) {
					out[i] = _a->at(*b);
					b += bstride;
				}
				_b.advance(n);
				framesToFill -= n;
				out += n;
			}
		}
		produce(framesToFill);
	}
	
};

class AtGenZZ : public Gen
{
	P<Array> _a;
	ZIn _b;
public:
	AtGenZZ(Thread& th, P<Array> const& a, Arg b) : Gen(th, itemTypeZ, b.isFinite()), _a(a), _b(b) {}
	
	virtual const char* TypeName() const override { return "AtGenZZ"; }
	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		Z* out = mOut->fulfillz(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int bstride;
			Z *b;
			if (_b(th, n,bstride, b)) {
				setDone();
				break;
			} else {
				for (int i = 0; i < n; ++i) {
					out[i] = _a->atz(*b);
					b += bstride;
				}
				_b.advance(n);
				framesToFill -= n;
				out += n;
			}
		}
		produce(framesToFill);
	}
	
};

class WrapAtGenVV : public Gen
{
	P<Array> _a;
	VIn _b;
public:
	WrapAtGenVV(Thread& th, P<Array> const& a, Arg b) : Gen(th, itemTypeV, b.isFinite()), _a(a), _b(b) {}
	
	virtual const char* TypeName() const override { return "WrapAtGenVV"; }
	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int bstride;
			V *b;
			if (_b(th, n,bstride, b)) {
				setDone();
				break;
			} else {
				for (int i = 0; i < n; ++i) {
					out[i] = do_wrapAt(th, _a, *b);
					b += bstride;
				}
				_b.advance(n);
				framesToFill -= n;
				out += n;
			}
		}
		produce(framesToFill);
	}
	
};

class WrapAtGenVZ : public Gen
{
	P<Array> _a;
	ZIn _b;
public:
	WrapAtGenVZ(Thread& th, P<Array> const& a, Arg b) : Gen(th, itemTypeV, b.isFinite()), _a(a), _b(b) {}

	virtual const char* TypeName() const override { return "WrapAtGenVZ"; }
	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int bstride;
			Z *b;
			if (_b(th, n,bstride, b)) {
				setDone();
				break;
			} else {
				for (int i = 0; i < n; ++i) {
					out[i] = _a->wrapAt(*b);
					b += bstride;
				}
				_b.advance(n);
				framesToFill -= n;
				out += n;
			}
		}
		produce(framesToFill);
	}
	
};

class WrapAtGenZZ : public Gen
{
	P<Array> _a;
	ZIn _b;
public:
	WrapAtGenZZ(Thread& th, P<Array> const& a, Arg b) : Gen(th, itemTypeZ, b.isFinite()), _a(a), _b(b) {}

	virtual const char* TypeName() const override { return "WrapAtGenZZ"; }
	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		Z* out = mOut->fulfillz(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int bstride;
			Z *b;
			if (_b(th, n,bstride, b)) {
				setDone();
				break;
			} else {
				for (int i = 0; i < n; ++i) {
					out[i] = _a->wrapAtz(*b);
					b += bstride;
				}
				_b.advance(n);
				framesToFill -= n;
				out += n;
			}
		}
		produce(framesToFill);
	}
	
};


class FoldAtGenVV : public Gen
{
	P<Array> _a;
	VIn _b;
public:
	FoldAtGenVV(Thread& th, P<Array> const& a, Arg b) : Gen(th, itemTypeV, b.isFinite()), _a(a), _b(b) {}

	virtual const char* TypeName() const override { return "FoldAtGenVV"; }
	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int bstride;
			V *b;
			if (_b(th, n,bstride, b)) {
				setDone();
				break;
			} else {
				for (int i = 0; i < n; ++i) {
					out[i] = do_foldAt(th, _a, *b);
					b += bstride;
				}
				_b.advance(n);
				framesToFill -= n;
				out += n;
			}
		}
		produce(framesToFill);
	}
	
};

class FoldAtGenVZ : public Gen
{
	P<Array> _a;
	ZIn _b;
public:
	FoldAtGenVZ(Thread& th, P<Array> const& a, Arg b) : Gen(th, itemTypeV, b.isFinite()), _a(a), _b(b) {}

	virtual const char* TypeName() const override { return "FoldAtGenVZ"; }
	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int bstride;
			Z *b;
			if (_b(th, n,bstride, b)) {
				setDone();
				break;
			} else {
				for (int i = 0; i < n; ++i) {
					out[i] = _a->foldAt(*b);
					b += bstride;
				}
				_b.advance(n);
				framesToFill -= n;
				out += n;
			}
		}
		produce(framesToFill);
	}
	
};

class FoldAtGenZZ : public Gen
{
	P<Array> _a;
	ZIn _b;
public:
	FoldAtGenZZ(Thread& th, P<Array> const& a, Arg b) : Gen(th, itemTypeZ, b.isFinite()), _a(a), _b(b) {}

	virtual const char* TypeName() const override { return "FoldAtGenZZ"; }
	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		Z* out = mOut->fulfillz(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int bstride;
			Z *b;
			if (_b(th, n,bstride, b)) {
				setDone();
				break;
			} else {
				for (int i = 0; i < n; ++i) {
					out[i] = _a->foldAtz(*b);
					b += bstride;
				}
				_b.advance(n);
				framesToFill -= n;
				out += n;
			}
		}
		produce(framesToFill);
	}
	
};


class ClipAtGenVV : public Gen
{
	P<Array> _a;
	VIn _b;
public:
	ClipAtGenVV(Thread& th, P<Array> const& a, Arg b) : Gen(th, itemTypeV, b.isFinite()), _a(a), _b(b) {}

	virtual const char* TypeName() const override { return "ClipAtGenVV"; }
	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int bstride;
			V *b;
			if (_b(th, n,bstride, b)) {
				setDone();
				break;
			} else {
				for (int i = 0; i < n; ++i) {
					out[i] = do_clipAt(th, _a, *b);
					b += bstride;
				}
				_b.advance(n);
				framesToFill -= n;
				out += n;
			}
		}
		produce(framesToFill);
	}
	
};

class ClipAtGenVZ : public Gen
{
	P<Array> _a;
	ZIn _b;
public:
	ClipAtGenVZ(Thread& th, P<Array> const& a, Arg b) : Gen(th, itemTypeV, b.isFinite()), _a(a), _b(b) {}

	virtual const char* TypeName() const override { return "ClipAtGenVZ"; }
	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int bstride;
			Z *b;
			if (_b(th, n,bstride, b)) {
				setDone();
				break;
			} else {
				for (int i = 0; i < n; ++i) {
					out[i] = _a->clipAt(*b);
					b += bstride;
				}
				_b.advance(n);
				framesToFill -= n;
				out += n;
			}
		}
		produce(framesToFill);
	}
	
};

class ClipAtGenZZ : public Gen
{
	P<Array> _a;
	ZIn _b;
public:
	ClipAtGenZZ(Thread& th, P<Array> const& a, Arg b) : Gen(th, itemTypeZ, b.isFinite()), _a(a), _b(b) {}

	virtual const char* TypeName() const override { return "ClipAtGenZZ"; }
	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		Z* out = mOut->fulfillz(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int bstride;
			Z *b;
			if (_b(th, n,bstride, b)) {
				setDone();
				break;
			} else {
				for (int i = 0; i < n; ++i) {
					out[i] = _a->clipAtz(*b);
					b += bstride;
				}
				_b.advance(n);
				framesToFill -= n;
				out += n;
			}
		}
		produce(framesToFill);
	}
	
};

static Z degkey(Z degree, P<Array> const& scale, Z cycleWidth, int degreesPerCycle)
{
	Z fidegree = floor(degree + .5);
	//Z frac = degree - fidegree;
	int idegree = (int)fidegree;
	int modDegree = (int)sc_imod(idegree, degreesPerCycle);
	//return frac + scale->atz(modDegree) + cycleWidth * sc_div(idegree, degreesPerCycle);
	return scale->atz(modDegree) + cycleWidth * sc_div(idegree, degreesPerCycle);
}


static Z keydeg(Z key, P<Array> const& scale, Z cycleWidth, int degreesPerCycle)
{
	Z cycles, cyckey;
	sc_fdivmod(key, cycleWidth, cycles, cyckey);
	
	Z frac = scale->atz(0) + cycleWidth - cyckey;
	Z mindiff = std::abs(frac);
	int idegree = 0;
	for (int i = 0; i < degreesPerCycle; ++i) {
		frac = std::abs(cyckey - scale->atz(i));
		if (frac < mindiff) {
			mindiff = frac;
			idegree = i;
		}
	}
	
	return idegree + cycles * degreesPerCycle;
}

class DegKeyVV : public Gen
{
	P<Array> _scale;
	Z _cycleWidth;
	int _degreesPerCycle;
	VIn _degree;
public:

	DegKeyVV(Thread& th, P<Array> const& inScale, Arg inDegree, Z inCycleWidth, int inDegreesPerCycle)
		: Gen(th, itemTypeV, inDegree.isFinite()), 
		_scale(inScale), 
		_degree(inDegree), 
		_cycleWidth(inCycleWidth),
		_degreesPerCycle(inDegreesPerCycle) 
		{}
	
	virtual const char* TypeName() const override { return "DegKeyVV"; }
	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int bstride;
			V *b;
			if (_degree(th, n,bstride, b)) {
				setDone();
				break;
			} else {
				for (int i = 0; i < n; ++i) {
					out[i] = do_degkey(th, _scale, *b, _cycleWidth, _degreesPerCycle);
					b += bstride;
				}
				_degree.advance(n);
				framesToFill -= n;
				out += n;
			}
		}
		produce(framesToFill);
	}
	
};

class DegKeyVZ : public Gen
{
	P<Array> _scale;
	Z _cycleWidth;
	int _degreesPerCycle;
	ZIn _degree;
public:

	DegKeyVZ(Thread& th, P<Array> const& inScale, Arg inDegree, Z inCycleWidth, int inDegreesPerCycle)
		: Gen(th, itemTypeV, inDegree.isFinite()), 
		_scale(inScale), 
		_degree(inDegree), 
		_cycleWidth(inCycleWidth),
		_degreesPerCycle(inDegreesPerCycle)
		{}
	
	virtual const char* TypeName() const override { return "DegKeyVZ"; }
	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int bstride;
			Z *b;
			if (_degree(th, n,bstride, b)) {
				setDone();
				break;
			} else {
				for (int i = 0; i < n; ++i) {
					out[i] = degkey(*b, _scale, _cycleWidth, _degreesPerCycle);
					b += bstride;
				}
				_degree.advance(n);
				framesToFill -= n;
				out += n;
			}
		}
		produce(framesToFill);
	}
	
};

class DegKeyZZ : public Gen
{
	P<Array> _scale;
	Z _cycleWidth;
	int _degreesPerCycle;
	ZIn _degree;
public:

	DegKeyZZ(Thread& th, P<Array> const& inScale, Arg inDegree, Z inCycleWidth, int inDegreesPerCycle)
		: Gen(th, itemTypeZ, inDegree.isFinite()), 
		_scale(inScale), 
		_degree(inDegree), 
		_cycleWidth(inCycleWidth),
		_degreesPerCycle(inDegreesPerCycle)
		{}
	
	virtual const char* TypeName() const override { return "DegKeyZZ"; }
	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		Z* out = mOut->fulfillz(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int bstride;
			Z *b;
			if (_degree(th, n,bstride, b)) {
				setDone();
				break;
			} else {
				for (int i = 0; i < n; ++i) {
					out[i] = degkey(*b, _scale, _cycleWidth, _degreesPerCycle);
					b += bstride;
				}
				_degree.advance(n);
				framesToFill -= n;
				out += n;
			}
		}
		produce(framesToFill);
	}
	
};

class KeyDegVV : public Gen
{
	P<Array> _scale;
	Z _cycleWidth;
	int _degreesPerCycle;
	VIn _key;
public:

	KeyDegVV(Thread& th, P<Array> const& inScale, Arg inDegree, Z inCycleWidth, int inDegreesPerCycle)
		: Gen(th, itemTypeV, inDegree.isFinite()), 
		_scale(inScale), 
		_key(inDegree), 
		_cycleWidth(inCycleWidth),
		_degreesPerCycle(inDegreesPerCycle) 
		{}
	
	virtual const char* TypeName() const override { return "KeyDegVV"; }
	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int bstride;
			V *b;
			if (_key(th, n,bstride, b)) {
				setDone();
				break;
			} else {
				for (int i = 0; i < n; ++i) {
					out[i] = do_keydeg(th, _scale, *b, _cycleWidth, _degreesPerCycle);
					b += bstride;
				}
				_key.advance(n);
				framesToFill -= n;
				out += n;
			}
		}
		produce(framesToFill);
	}
	
};

class KeyDegVZ : public Gen
{
	P<Array> _scale;
	Z _cycleWidth;
	int _degreesPerCycle;
	ZIn _key;
public:

	KeyDegVZ(Thread& th, P<Array> const& inScale, Arg inDegree, Z inCycleWidth, int inDegreesPerCycle)
		: Gen(th, itemTypeV, inDegree.isFinite()), 
		_scale(inScale), 
		_key(inDegree), 
		_cycleWidth(inCycleWidth),
		_degreesPerCycle(inDegreesPerCycle)
		{}
	
	virtual const char* TypeName() const override { return "KeyDegVZ"; }
	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int bstride;
			Z *b;
			if (_key(th, n,bstride, b)) {
				setDone();
				break;
			} else {
				for (int i = 0; i < n; ++i) {
					out[i] = keydeg(*b, _scale, _cycleWidth, _degreesPerCycle);
					b += bstride;
				}
				_key.advance(n);
				framesToFill -= n;
				out += n;
			}
		}
		produce(framesToFill);
	}
	
};

class KeyDegZZ : public Gen
{
	P<Array> _scale;
	Z _cycleWidth;
	int _degreesPerCycle;
	ZIn _key;
public:

	KeyDegZZ(Thread& th, P<Array> const& inScale, Arg inDegree, Z inCycleWidth, int inDegreesPerCycle)
		: Gen(th, itemTypeZ, inDegree.isFinite()), 
		_scale(inScale), 
		_key(inDegree), 
		_cycleWidth(inCycleWidth),
		_degreesPerCycle(inDegreesPerCycle)
		{}
	
	virtual const char* TypeName() const override { return "KeyDegZZ"; }
	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		Z* out = mOut->fulfillz(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int bstride;
			Z *b;
			if (_key(th, n,bstride, b)) {
				setDone();
				break;
			} else {
				for (int i = 0; i < n; ++i) {
					out[i] = keydeg(*b, _scale, _cycleWidth, _degreesPerCycle);
					b += bstride;
				}
				_key.advance(n);
				framesToFill -= n;
				out += n;
			}
		}
		produce(framesToFill);
	}
	
};

static Gen* newAtGen(Thread& th, P<Array> const& a, Arg b)
{
	if (b.isVList()) {
		return new AtGenVV(th, a, b);
	} else {
		if (a->isV()) {
			return new AtGenVZ(th, a, b);
		} else {
			return new AtGenZZ(th, a, b);
		}
	}
}

static Gen* newWrapAtGen(Thread& th, P<Array> const& a, Arg b)
{
	if (b.isVList()) {
		return new WrapAtGenVV(th, a, b);
	} else {
		if (a->isV()) {
			return new WrapAtGenVZ(th, a, b);
		} else {
			return new WrapAtGenZZ(th, a, b);
		}
	}
}

static Gen* newDegKeyGen(Thread& th, P<Array> const& a, Arg b, Z cycleWidth, int degreesPerCycle)
{
	if (b.isVList()) {
		return new DegKeyVV(th, a, b, cycleWidth, degreesPerCycle);
	} else {
		if (a->isV()) {
			return new DegKeyVZ(th, a, b, cycleWidth, degreesPerCycle);
		} else {
			return new DegKeyZZ(th, a, b, cycleWidth, degreesPerCycle);
		}
	}
}

static Gen* newKeyDegGen(Thread& th, P<Array> const& a, Arg b, Z cycleWidth, int degreesPerCycle)
{
	if (b.isVList()) {
		return new KeyDegVV(th, a, b, cycleWidth, degreesPerCycle);
	} else {
		if (a->isV()) {
			return new KeyDegVZ(th, a, b, cycleWidth, degreesPerCycle);
		} else {
			return new KeyDegZZ(th, a, b, cycleWidth, degreesPerCycle);
		}
	}
}

static Gen* newFoldAtGen(Thread& th, P<Array> const& a, Arg b)
{
	if (b.isVList()) {
		return new FoldAtGenVV(th, a, b);
	} else {
		if (a->isV()) {
			return new FoldAtGenVZ(th, a, b);
		} else {
			return new FoldAtGenZZ(th, a, b);
		}
	}
}

static Gen* newClipAtGen(Thread& th, P<Array> const& a, Arg b)
{
	if (b.isVList()) {
		return new ClipAtGenVV(th, a, b);
	} else {
		if (a->isV()) {
			return new ClipAtGenVZ(th, a, b);
		} else {
			return new ClipAtGenZZ(th, a, b);
		}
	}
}

V do_at(Thread& th, P<Array> const& a, Arg b)
{
	if (b.isReal()) {
		return a->at(b.asInt());
	} else if (b.isList()) {
		return new List(newAtGen(th, a, b));
	} else wrongType("at : b", "Real or List", b);

	return 0.;
}

V do_wrapAt(Thread& th, P<Array> const& a, Arg b)
{
	if (b.isReal()) {
		return a->wrapAt(b.asInt());
	} else if (b.isList()) {
		return new List(newWrapAtGen(th, a, b));
	} else wrongType("wrapAt : b", "Real or List", b);

	return 0.;
}

V do_degkey(Thread& th, P<Array> const& a, Arg b, Z cycleWidth, int degreesPerCycle)
{
	if (b.isReal()) {
		return degkey(b.asFloat(), a, cycleWidth, degreesPerCycle);
	} else if (b.isList()) {
		return new List(newDegKeyGen(th, a, b, cycleWidth, degreesPerCycle));
	} else wrongType("degkey : b", "Real or List", b);

	return 0.;
}

V do_keydeg(Thread& th, P<Array> const& a, Arg b, Z cycleWidth, int degreesPerCycle)
{
	if (b.isReal()) {
		return keydeg(b.asFloat(), a, cycleWidth, degreesPerCycle);
	} else if (b.isList()) {
		return new List(newKeyDegGen(th, a, b, cycleWidth, degreesPerCycle));
	} else wrongType("keydeg : b", "Real or List", b);

	return 0.;
}

V do_foldAt(Thread& th, P<Array> const& a, Arg b)
{
	if (b.isReal()) {
		return a->foldAt(b.asInt());
	} else if (b.isList()) {
		return new List(newFoldAtGen(th, a, b));
	} else wrongType("foldAt : b", "Real or List", b);

	return 0.;
}

V do_clipAt(Thread& th, P<Array> const& a, Arg b)
{
	if (b.isReal()) {
		return a->clipAt(b.asInt());
	} else if (b.isList()) {
		return new List(newClipAtGen(th, a, b));
	} else wrongType("clipAt : b", "Real or List", b);

	return 0.;
}

static void at_(Thread& th, Prim* prim)
{
	V i = th.pop();
	P<List> s = th.popList("at : s");

	if (!s->isFinite())
		indefiniteOp("at", "");

	s = s->pack(th);
	P<Array> const& a = s->mArray;
	
	V v = do_at(th, a, i);
	th.push(v);
}

static void wrapAt_(Thread& th, Prim* prim)
{
	V i = th.pop();
	P<List> s = th.popList("wrapAt : s");

	if (!s->isFinite())
		indefiniteOp("wrapAt", "");

	s = s->pack(th);
	P<Array> const& a = s->mArray;
	
	V v = do_wrapAt(th, a, i);
	th.push(v);
}

static void degkey_(Thread& th, Prim* prim)
{
	P<List> s = th.popList("degkey : s");

	V i = th.pop();

	if (!s->isFinite())
		indefiniteOp("degkey", "");

	s = s->pack(th);
	P<Array> const& a = s->mArray;
	
	int degreesPerCycle = (int)a->size()-1;
	if (degreesPerCycle <= 0) {
		post("degkey : scale has no degrees");
		throw errFailed;
	}
	Z cycleWidth = a->atz(degreesPerCycle);

	V v = do_degkey(th, a, i, cycleWidth, degreesPerCycle);
	th.push(v);
}

static void keydeg_(Thread& th, Prim* prim)
{
	P<List> s = th.popList("keydeg : s");

	V i = th.pop();

	if (!s->isFinite())
		indefiniteOp("keydeg", "");

	s = s->pack(th);
	P<Array> const& a = s->mArray;
	
	int degreesPerCycle = (int)a->size()-1;
	if (degreesPerCycle <= 0) {
		post("keydeg : scale has no degrees");
		throw errFailed;
	}
	Z cycleWidth = a->atz(degreesPerCycle);

	V v = do_keydeg(th, a, i, cycleWidth, degreesPerCycle);
	th.push(v);
}

static void foldAt_(Thread& th, Prim* prim)
{
	V i = th.pop();
	P<List> s = th.popList("foldAt : s");

	if (!s->isFinite())
		indefiniteOp("foldAt", "");

	s = s->pack(th);
	P<Array> const& a = s->mArray;
	
	V v = do_foldAt(th, a, i);
	th.push(v);
}

static void clipAt_(Thread& th, Prim* prim)
{
	V i = th.pop();
	P<List> s = th.popList("clipAt : s");

	if (!s->isFinite())
		indefiniteOp("clipAt", "");

	s = s->pack(th);
	P<Array> const& a = s->mArray;
	
	V v = do_clipAt(th, a, i);
	th.push(v);
}


#pragma mark CONVERSION


struct VGen : public Gen
{
	ZIn _a;
	
	VGen(Thread& th, Arg a) : Gen(th, itemTypeV, true), _a(a) {}
	virtual const char* TypeName() const override { return "VGen"; }
    	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int astride;
			Z *a;
			if (_a(th, n,astride, a)) {
				setDone();
				break;
			} else {
				for (int i = 0; i < n; ++i) {
					out[i] = *a;
					a += astride;
				}
				_a.advance(n);
				framesToFill -= n;
				out += n;
			}
		}
		produce(framesToFill);
	}
};

struct ZGen : public Gen
{
	VIn _a;
	
	ZGen(Thread& th, Arg a) : Gen(th, itemTypeZ, true), _a(a) {}
	virtual const char* TypeName() const override { return "VGen"; }
    	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		Z* out = mOut->fulfillz(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int astride;
			V *a;
			if (_a(th, n,astride, a)) {
				setDone();
				break;
			} else {
				for (int i = 0; i < n; ++i) {
					out[i] = a->asFloat();
					a += astride;
				}
				_a.advance(n);
				framesToFill -= n;
				out += n;
			}
		}
		produce(framesToFill);
	}
};


static P<List> stringToZList(P<String> const& string)
{
	const char* s = string->s;
	size_t n = strlen(s);
	P<List> list = new List(itemTypeZ, n);
	
	Array* a = list->mArray();
	a->setSize(n);
	Z* z = a->z();
	
	for (size_t i = 0; i < n; ++i) {
		z[i] = s[i];
	}
	
	return list;
}

static P<List> stringToVList(P<String> const& string)
{
	const char* s = string->s;
	size_t n = strlen(s);
	P<List> list = new List(itemTypeV, n);
	
	Array* a = list->mArray();
	a->setSize(n);
	V* v = a->v();
	
	for (size_t i = 0; i < n; ++i) {
		v[i] = s[i];
	}
	
	return list;
}

static P<String> vlistToString(Thread& th, P<List> const& list)
{
	if (!list->isFinite())
		indefiniteOp("stream to string", "");
		
	P<List> packedList = list->pack(th);
	size_t n = packedList->length(th);
	char* s = (char*)malloc(n+1);
	
	P<String> string = new String(s, "dummy");
	
	Array* a = packedList->mArray();
	V* v = a->v();
	
	for (size_t i = 0; i < n; ++i) {
		s[i] = toascii((int)v[i].asFloat());
	}
	s[n] = 0;
	
	return string;
}

static P<String> zlistToString(Thread& th, P<List> const& list)
{
	if (!list->isFinite())
		indefiniteOp("signal to string", "");
		
	P<List> packedList = list->pack(th);
	size_t n = packedList->length(th);
	char* s = (char*)malloc(n+1);
	
	P<String> string = new String(s, "dummy");
	
	Array* a = packedList->mArray();
	Z* z = a->z();
	
	for (size_t i = 0; i < n; ++i) {
		s[i] = toascii((int)z[i]);
	}
	s[n] = 0;
	
	return string;
}

static void V_(Thread& th, Prim* prim)
{
	V a = th.pop();
	
	if (a.isZList()) {
		Gen* g = new VGen(th, a);
		th.push(new List(g));
	} else if (a.isString()) {
		P<String> s = (String*)a.o();
		th.push(stringToVList(s));
	} else {
		th.push(a);
	}
}

static void Z_(Thread& th, Prim* prim)
{
	V a = th.pop();
	
	if (a.isVList()) {
		Gen* g = new ZGen(th, a);
		th.push(new List(g));
	} else if (a.isString()) {
		P<String> s = (String*)a.o();
		th.push(stringToZList(s));
	} else {
		th.push(a);
	}
}

static void unspell_(Thread& th, Prim* prim)
{
	V a = th.pop();
	
	if (a.isVList()) {
		P<List> list = (List*)a.o();
		th.push(vlistToString(th, list));
	} else if (a.isZList()) {
		P<List> list = (List*)a.o();
		th.push(zlistToString(th, list));
	} else if (a.isString()) {
		th.push(a);
	} else {
		wrongType("unspell : list", "List or String", a);
	}
}



#pragma mark NUMERIC SERIES


struct Ever : public Gen
{
	V _val;

	Ever(Thread& th, Arg val) : Gen(th, itemTypeV, false), _val(val) {}

	virtual const char* TypeName() const override { return "Ever"; }

	virtual void pull(Thread& th) override
	{
		int n = mBlockSize;
		V* out = mOut->fulfill(n);
		V v = _val;
		for (int i = 0; i < n; ++i) {
			out[i] = v;
		}
		mOut = mOut->nextp();
	}
};

struct Everz : public Gen
{
	Z _val;

	Everz(Thread& th, Z val) : Gen(th, itemTypeZ, false), _val(val) {}

	virtual const char* TypeName() const override { return "Everz"; }

	virtual void pull(Thread& th) override
	{
		int n = mBlockSize;
		Z* out = mOut->fulfillz(n);
		Z z = _val;
		for (int i = 0; i < n; ++i) {
			out[i] = z;
		}
		mOut = mOut->nextp();
	}
};

extern UnaryOp* gUnaryOpPtr_recip;
extern UnaryOp* gUnaryOpPtr_cb;
extern BinaryOp* gBinaryOpPtr_plus;
extern BinaryOp* gBinaryOpPtr_mul;

struct By : public Gen
{
	V _start;
	V _step;

	By(Thread& th, Arg start, Arg step) : Gen(th, itemTypeV, false), _start(start), _step(step) {}

	virtual const char* TypeName() const override { return "By"; }

	virtual void pull(Thread& th) override
	{
		int n = mBlockSize;
		V* out = mOut->fulfill(n);
		for (int i = 0; i < n; ++i) {
			out[i] = _start;
			_start = _start.binaryOp(th, gBinaryOpPtr_plus, _step);
		}
		mOut = mOut->nextp();
	}
};

struct Byz : public Gen
{
	Z _start;
	Z _step;

	Byz(Thread& th, Z start, Z step) : Gen(th, itemTypeZ, false), _start(start), _step(step) {}

	virtual const char* TypeName() const override { return "Byz"; }

	virtual void pull(Thread& th) override
	{
		int n = mBlockSize;
		Z* out = mOut->fulfillz(n);
		Z start = _start;
		Z step = _step;
		for (int i = 0; i < n; ++i) {
			out[i] = start;
			start += step;	
		}
		_start = start;
		mOut = mOut->nextp();
	}
};

struct Grow : public Gen
{
	V _start;
	V _step;

	Grow(Thread& th, Arg start, Arg step) : Gen(th, itemTypeV, false), _start(start), _step(step) {}

	virtual const char* TypeName() const override { return "Grow"; }

	virtual void pull(Thread& th) override
	{
		int n = mBlockSize;
		V* out = mOut->fulfill(n);
		for (int i = 0; i < n; ++i) {
			out[i] = _start;
			_start = _start.binaryOp(th, gBinaryOpPtr_mul, _step);	
		}
		mOut = mOut->nextp();
	}
};

struct Growz : public Gen
{
	Z _start;
	Z _step;

	Growz(Thread& th, Z start, Z step) : Gen(th, itemTypeZ, false), _start(start), _step(step) {}

	virtual const char* TypeName() const override { return "Growz"; }

	virtual void pull(Thread& th) override
	{
		int n = mBlockSize;
		Z* out = mOut->fulfillz(n);
		Z start = _start;
		Z step = _step;
		for (int i = 0; i < n; ++i) {
			out[i] = start;
			start *= step;	
		}
		_start = start;
		mOut = mOut->nextp();
	}
};

struct CubicLine : public Gen
{
	V _start;
	V _step;

	CubicLine(Thread& th, Arg start, Arg step) : Gen(th, itemTypeV, false), _start(start), _step(step) {}

	virtual const char* TypeName() const override { return "CubicLine"; }

	virtual void pull(Thread& th) override
	{
		int n = mBlockSize;
		V* out = mOut->fulfill(n);
		for (int i = 0; i < n; ++i) {
			V cubed = _start.unaryOp(th, gUnaryOpPtr_cb);	
			out[i] = cubed;
			_start = _start.binaryOp(th, gBinaryOpPtr_plus, _step);	
		}
		mOut = mOut->nextp();
	}
};

struct CubicLinez : public Gen
{
	Z _start;
	Z _step;

	CubicLinez(Thread& th, Z start, Z step) : Gen(th, itemTypeZ, false), _start(start), _step(step) {}

	virtual const char* TypeName() const override { return "CubicLinez"; }

	virtual void pull(Thread& th) override
	{
		int n = mBlockSize;
		Z* out = mOut->fulfillz(n);
		Z start = _start;
		Z step = _step;
		for (int i = 0; i < n; ++i) {
			out[i] = start*start*start;
			start += step;	
		}
		_start = start;
		mOut = mOut->nextp();
	}
};

struct Inv : public Gen
{
	V _start;

	Inv(Thread& th) : Gen(th, itemTypeV, false), _start(1.) {}

	virtual const char* TypeName() const override { return "Inv"; }

	virtual void pull(Thread& th) override
	{
		int n = mBlockSize;
		V* out = mOut->fulfill(n);
		V vone = 1.;
		for (int i = 0; i < n; ++i) {
			V vout = _start.unaryOp(th, gUnaryOpPtr_recip);
			out[i] = vout;
			_start = _start.binaryOp(th, gBinaryOpPtr_plus, vone);
		}
		mOut = mOut->nextp();
	}
};

struct Invz : public Gen
{
	Z _start;

	Invz(Thread& th) : Gen(th, itemTypeZ, false), _start(1.) {}

	virtual const char* TypeName() const override { return "Invz"; }

	virtual void pull(Thread& th) override
	{
		int n = mBlockSize;
		Z* out = mOut->fulfillz(n);
		Z start = _start;
		for (int i = 0; i < n; ++i) {
			out[i] = 1. / start;
			start += 1.;
		}
		_start = start;
		mOut = mOut->nextp();
	}
};


struct NInv : public Gen
{
	V _start;
	int64_t _n;

	NInv(Thread& th, int64_t n) : Gen(th, itemTypeV, true), _start(1.), _n(n) {}

	virtual const char* TypeName() const override { return "NInv"; }

	virtual void pull(Thread& th) override
	{
		if (_n <= 0) {
			end();
		} else {
			int n = (int)std::min(_n, (int64_t)mBlockSize);
			V* out = mOut->fulfill(n);
			V vone = 1.;
			for (int i = 0; i < n; ++i) {
				V vout = _start.unaryOp(th, gUnaryOpPtr_recip);
				out[i] = vout;
				_start = _start.binaryOp(th, gBinaryOpPtr_plus, vone);
			}
			_n -= n;
			mOut = mOut->nextp();
		}
	}
};

struct NInvz : public Gen
{
	Z _start;
	int64_t _n;

	NInvz(Thread& th, int64_t n) : Gen(th, itemTypeZ, true), _start(1.), _n(n) {}

	virtual const char* TypeName() const override { return "NInvz"; }

	virtual void pull(Thread& th) override
	{
		if (_n <= 0) {
			end();
		} else {
			int n = (int)std::min(_n, (int64_t)mBlockSize);
			Z* out = mOut->fulfillz(n);
			Z start = _start;
			for (int i = 0; i < n; ++i) {
				out[i] = 1. / start;
				start += 1.;
			}
			_start = start;
			_n -= n;
			mOut = mOut->nextp();
		}
	}
};

struct NBy : public Gen
{
	V _start;
	V _step;
	int64_t _n;

	NBy(Thread& th, Arg start, Arg step, int64_t n) : Gen(th, itemTypeV, true), _start(start), _step(step), _n(n) {}

	virtual const char* TypeName() const override { return "NBy"; }

	virtual void pull(Thread& th) override
	{	
		if (_n <= 0) {
			end();
		} else {
			int n = (int)std::min(_n, (int64_t)mBlockSize);
			V* out = mOut->fulfill(n);
			for (int i = 0; i < n; ++i) {
				out[i] = _start;
				_start = _start.binaryOp(th, gBinaryOpPtr_plus, _step);
			}
			_n -= n;
			mOut = mOut->nextp();
		}
	}
};

struct NByz : public Gen
{
	Z _start;
	Z _step;
	int64_t _n;
	
	NByz(Thread& th, Z start, Z step, int64_t n) : Gen(th, itemTypeZ, true), _start(start), _step(step), _n(n) {}

	virtual const char* TypeName() const override { return "NByz"; }

	virtual void pull(Thread& th) override
	{	
		if (_n <= 0) {
			end();
		} else {
			int n = (int)std::min(_n, (int64_t)mBlockSize);
			Z* out = mOut->fulfillz(n);
			Z start = _start;
			Z step = _step;
			for (int i = 0; i < n; ++i) {
				out[i] = start;
				start += step;	
			}
			_start = start;
			_n -= n;
			mOut = mOut->nextp();
		}
	}
};


struct NGrow : public Gen
{
	V _start;
	V _step;
	int64_t _n;

	NGrow(Thread& th, Arg start, Arg step, int64_t n) : Gen(th, itemTypeV, true), _start(start), _step(step), _n(n) {}

	virtual const char* TypeName() const override { return "NGrow"; }

	virtual void pull(Thread& th) override
	{	
		if (_n <= 0) {
			end();
		} else {
			int n = (int)std::min(_n, (int64_t)mBlockSize);
			V* out = mOut->fulfill(n);
			for (int i = 0; i < n; ++i) {
				out[i] = _start;
				_start = _start.binaryOp(th, gBinaryOpPtr_mul, _step);
			}
			_n -= n;
			mOut = mOut->nextp();
		}
	}
};

struct NGrowz : public Gen
{
	Z _start;
	Z _step;
	int64_t _n;

	NGrowz(Thread& th, Z start, Z step, int64_t n) : Gen(th, itemTypeZ, true), _start(start), _step(step), _n(n) {}

	virtual const char* TypeName() const override { return "NGrowz"; }

	virtual void pull(Thread& th) override
	{	
		if (_n <= 0) {
			end();
		} else {
			int n = (int)std::min(_n, (int64_t)mBlockSize);
			Z* out = mOut->fulfillz(n);
			Z start = _start;
			Z step = _step;
			for (int i = 0; i < n; ++i) {
				out[i] = start;
				start *= step;	
			}
			_start = start;
			_n -= n;
			mOut = mOut->nextp();
		}
	}
};

struct NCubicLinez : public Gen
{
	Z _start;
	Z _step;
	int64_t _n;

	NCubicLinez(Thread& th, Z start, Z step, int64_t n) : Gen(th, itemTypeZ, true), _start(start), _step(step), _n(n) {}

	virtual const char* TypeName() const override { return "NCubicLinez"; }

	virtual void pull(Thread& th) override
	{	
		if (_n <= 0) {
			end();
		} else {
			int n = (int)std::min(_n, (int64_t)mBlockSize);
			Z* out = mOut->fulfillz(n);
			Z start = _start;
			Z step = _step;
			for (int i = 0; i < n; ++i) {
				out[i] = start*start*start;
				start += step;	
			}
			_start = start;
			_n -= n;
			mOut = mOut->nextp();
		}
	}
};

struct Fib : public Gen
{
	V _a;
	V _b;
    
	Fib(Thread& th, Arg a, Arg b) : Gen(th, itemTypeV, false), _a(a), _b(b) {}
    
	virtual const char* TypeName() const override { return "Fib"; }
        
	virtual void pull(Thread& th) override
	{
		int n = mBlockSize;
		V* out = mOut->fulfill(n);
		for (int i = 0; i < n; ++i) {
			V a = _a;
            out[i] = a;
            _a = _b;
            _b = a.binaryOp(th, gBinaryOpPtr_plus, _b);
		}
		mOut = mOut->nextp();
	}
};

struct Fibz : public Gen
{
	Z _a;
	Z _b;
    
	Fibz(Thread& th, Z a, Z b) : Gen(th, itemTypeZ, false), _a(a), _b(b) {}
    
	virtual const char* TypeName() const override { return "Fibz"; }
        
	virtual void pull(Thread& th) override
	{
        int n = mBlockSize;
		Z* out = mOut->fulfillz(n);
		Z a = _a;
		Z b = _b;
		for (int i = 0; i < n; ++i) {
			out[i] = a;
            Z aa = a;
            a = b;
            b += aa;
		}
		_a = a;
		_b = b;
		mOut = mOut->nextp();
    }
};

static void L_(Thread& th, Prim* prim)
{
	if (!th.top().isVList()) {
		th.push(new List(new Ever(th, th.pop())));
	}
}

static void L1_(Thread& th, Prim* prim)
{
	if (!th.top().isVList()) {
		P<List> list = new List(itemTypeV, 1);
		list->add(th.pop());
		th.push(list);
	}
}

static void ever_(Thread& th, Prim* prim)
{
	V value  = th.pop();
	
	Gen* g = new Ever(th, value);
	th.push(new List(g));
}

static void everz_(Thread& th, Prim* prim)
{
	Z value  = th.popFloat("everz : value");
	
	Gen* g = new Everz(th, value);
	th.push(new List(g));
}

static void by_(Thread& th, Prim* prim)
{
	V step = th.pop();
	V start = th.pop();
	
	Gen* g = new By(th, start, step);
	th.push(new List(g));
}

static void nby_(Thread& th, Prim* prim)
{
	V step = th.pop();
	V start = th.pop();
	int64_t n = th.popInt("nby : n");
	
	Gen* g = new NBy(th, start, step, n);
	th.push(new List(g));
}

static void to_(Thread& th, Prim* prim)
{
	Z end = th.popFloat("to : end");
	Z start = th.popFloat("to : start");
	Z step = start < end ? 1. : -1.;
	int64_t n = (int64_t)((end - start) * step) + 1;
	
	Gen* g = new NBy(th, start, step, n);
	th.push(new List(g));
}

static void toz_(Thread& th, Prim* prim)
{
	Z end = th.popFloat("toz : end");
	Z start = th.popFloat("toz : start");
	Z step = start < end ? 1. : -1.;
	int64_t n = (int64_t)((end - start) * step) + 1;
	
	Gen* g = new NByz(th, start, step, n);
	th.push(new List(g));
}

static void lindiv_(Thread& th, Prim* prim)
{
	Z end = th.popFloat("lindiv : end");
	Z start = th.popFloat("lindiv : start");
	int64_t n = th.popInt("lindiv : n");
	Z step = (end - start) / (n - 1);
	
	Gen* g = new NBy(th, start, step, n);
	th.push(new List(g));
}

static void lindivz_(Thread& th, Prim* prim)
{
	Z end = th.popFloat("lindivz : end");
	Z start = th.popFloat("lindivz : start");
	int64_t n = th.popInt("lindivz : n");
	Z step = (end - start) / (n - 1);
	
	Gen* g = new NByz(th, start, step, n);
	th.push(new List(g));
}

static void expdiv_(Thread& th, Prim* prim)
{
	Z end = th.popFloat("expdiv : end");
	Z start = th.popFloat("expdiv : start");
	int64_t n = th.popInt("expdiv : n");
	Z step = pow(end/start, 1. / (n - 1));
	
	Gen* g = new NGrow(th, start, step, n);
	th.push(new List(g));
}

static void expdivz_(Thread& th, Prim* prim)
{
	Z end = th.popFloat("expdivz : end");
	Z start = th.popFloat("expdivz : start");
	int64_t n = th.popInt("expdivz : n");
	Z step = pow(end/start, 1. / (n - 1));
	
	Gen* g = new NGrowz(th, start, step, n);
	th.push(new List(g));
}

static void lindiv1_(Thread& th, Prim* prim)
{
	Z end = th.popFloat("lindiv1 : end");
	Z start = th.popFloat("lindiv1 : start");
	int64_t n = th.popInt("lindiv1 : n");
	Z step = (end - start) / n;
	
	Gen* g = new NBy(th, start, step, n);
	th.push(new List(g));
}

static void lindiv1z_(Thread& th, Prim* prim)
{
	Z end = th.popFloat("lindiv1z : end");
	Z start = th.popFloat("lindiv1z : start");
	int64_t n = th.popInt("lindiv1z : n");
	Z step = (end - start) / n;
	
	Gen* g = new NByz(th, start, step, n);
	th.push(new List(g));
}

static void expdiv1_(Thread& th, Prim* prim)
{
	Z end = th.popFloat("expdiv1 : end");
	Z start = th.popFloat("expdiv1 : start");
	int64_t n = th.popInt("expdiv1 : n");
	Z step = pow(end/start, 1. / n);
	
	Gen* g = new NGrow(th, start, step, n);
	th.push(new List(g));
}

static void expdiv1z_(Thread& th, Prim* prim)
{
	Z end = th.popFloat("expdiv1z : end");
	Z start = th.popFloat("expdiv1z : start");
	int64_t n = th.popInt("expdiv1z : n");
	Z step = pow(end/start, 1. / n);
	
	Gen* g = new NGrowz(th, start, step, n);
	th.push(new List(g));
}

static void line_(Thread& th, Prim* prim)
{
	Z end = th.popFloat("line : end");
	Z start = th.popFloat("line : start");
	Z dur = th.popFloat("line : dur");
	double n = std::max(1., floor(dur * th.rate.sampleRate + .5));
	Z step = (end - start) / n;
	
	Gen* g = new NByz(th, start, step, (int64_t)n);
	th.push(new List(g));
}

static void xline_(Thread& th, Prim* prim)
{
	Z end = th.popFloat("xline : end");
	Z start = th.popFloat("xline : start");
	Z dur = th.popFloat("xline : dur");
	double n = std::max(1., floor(dur * th.rate.sampleRate + .5));
	
	Gen* g;
	if (sc_sgn(start) != sc_sgn(end) || start == 0. || end == 0.) {
		start = sc_sgn(start) * pow(fabs(start), kOneThird);
		end = sc_sgn(end) * pow(fabs(end), kOneThird);
		Z step = (end - start) / n;		
		g = new NCubicLinez(th, start, step, (int64_t)n);
	} else {
		Z step = pow(end/start, 1. / n);
		g = new NGrowz(th, start, step, (int64_t)n);
	}
	th.push(new List(g));
}


static void grow_(Thread& th, Prim* prim)
{
	V step = th.pop();
	V start = th.pop();
	
	Gen* g = new Grow(th, start, step);
	th.push(new List(g));
}

static void ngrow_(Thread& th, Prim* prim)
{
	V step = th.pop();
	V start = th.pop();
	int64_t n = th.popInt("ngrow : n");
	
	Gen* g = new NGrow(th, start, step, n);
	th.push(new List(g));
}

static void byz_(Thread& th, Prim* prim)
{
	Z step  = th.popFloat("byz : step");
	Z start = th.popFloat("byz : start");
	
	Gen* g = new Byz(th, start, step);
	th.push(new List(g));
}

static void nbyz_(Thread& th, Prim* prim)
{
	Z step  = th.popFloat("nbyz : step");
	Z start = th.popFloat("nbyz : start");
	int64_t n = th.popInt("nbyz : n");
	
	Gen* g = new NByz(th, start, step, n);
	th.push(new List(g));
}

static void growz_(Thread& th, Prim* prim)
{
	Z step  = th.popFloat("growz : step");
	Z start = th.popFloat("growz : start");
	
	Gen* g = new Growz(th, start, step);
	th.push(new List(g));
}

static void ngrowz_(Thread& th, Prim* prim)
{
	Z step  = th.popFloat("ngrowz : step");
	Z start = th.popFloat("ngrowz : start");
	int64_t n = th.popInt("ngrowz : n");
	
	Gen* g = new NGrowz(th, start, step, n);
	th.push(new List(g));
}

static void ord_(Thread& th, Prim* prim)
{
	Gen* g = new By(th, 1., 1.);
	th.push(new List(g));
}

static void negs_(Thread& th, Prim* prim)
{
	Gen* g = new By(th, -1., -1.);
	th.push(new List(g));
}

static void nat_(Thread& th, Prim* prim)
{
	Gen* g = new By(th, 0., 1.);
	th.push(new List(g));
}

static void evens_(Thread& th, Prim* prim)
{
	Gen* g = new By(th, 0., 2.);
	th.push(new List(g));
}

static void odds_(Thread& th, Prim* prim)
{
	Gen* g = new By(th, 1., 2.);
	th.push(new List(g));
}


static void invs_(Thread& th, Prim* prim)
{
	Gen* g = new Inv(th);
	th.push(new List(g));
}

static void invz_(Thread& th, Prim* prim)
{
	Gen* g = new Invz(th);
	th.push(new List(g));
}

static void ninvs_(Thread& th, Prim* prim)
{
	int64_t n = th.popInt("ninvs : n");
	Gen* g = new NInv(th, n);
	th.push(new List(g));
}

static void ninvz_(Thread& th, Prim* prim)
{
	int64_t n = th.popInt("ninvz : n");
	Gen* g = new NInvz(th, n);
	th.push(new List(g));
}


static void ordz_(Thread& th, Prim* prim)
{
	Gen* g = new Byz(th, 1., 1.);
	th.push(new List(g));
}

static void negz_(Thread& th, Prim* prim)
{
	Gen* g = new Byz(th, -1., -1.);
	th.push(new List(g));
}

static void natz_(Thread& th, Prim* prim)
{
	Gen* g = new Byz(th, 0., 1.);
	th.push(new List(g));
}

static void evenz_(Thread& th, Prim* prim)
{
	Gen* g = new Byz(th, 0., 2.);
	th.push(new List(g));
}

static void oddz_(Thread& th, Prim* prim)
{
	Gen* g = new Byz(th, 1., 2.);
	th.push(new List(g));
}

static void fib_(Thread& th, Prim* prim)
{
	V b = th.pop();
	V a  = th.pop();
	
	Gen* g = new Fib(th, a, b);
	th.push(new List(g));
}

static void fibz_(Thread& th, Prim* prim)
{
	Z b = th.popFloat("fibz : b");
	Z a  = th.popFloat("fibz : a");
	
	Gen* g = new Fibz(th, a, b);
	th.push(new List(g));
}

struct Ints : public Gen
{
	Z _a;
    
	Ints(Thread& th) : Gen(th, itemTypeV, false), _a(0.) {}
    
	virtual const char* TypeName() const override { return "Ints"; }
    
	virtual void pull(Thread& th) override
	{
		int n = mBlockSize;
		V* out = mOut->fulfill(n);
        Z a = _a;
		for (int i = 0; i < n; ++i) {
            out[i] = a;
            if (a <= 0.) a = 1. - a;
            else a = -a;
		}
        _a = a;
		mOut = mOut->nextp();
	}
};

struct Intz : public Gen
{
	Z _a;
    
	Intz(Thread& th) : Gen(th, itemTypeZ, false), _a(0.) {}
    
	virtual const char* TypeName() const override { return "Intz"; }
    
	virtual void pull(Thread& th) override
	{
		int n = mBlockSize;
		Z* out = mOut->fulfillz(n);
        Z a = _a;
		for (int i = 0; i < n; ++i) {
            out[i] = a;
            if (a <= 0.) a = 1. - a;
            else a = -a;
		}
        _a = a;
		mOut = mOut->nextp();
	}
};


static void ints_(Thread& th, Prim* prim)
{
	Gen* g = new Ints(th);
	th.push(new List(g));
}

static void intz_(Thread& th, Prim* prim)
{
	Gen* g = new Intz(th);
	th.push(new List(g));
}

#include "primes.hpp"

struct Primes : Gen
{
	int byte;
	int bit;
	
	Primes(Thread& th) : Gen(th, itemTypeV, false), byte(-1), bit(0) {}
    
	virtual const char* TypeName() const override { return "Primes"; }
        
	virtual void pull(Thread& th) override
	{
		int n = mBlockSize;
		V* out = mOut->fulfill(n);
		for (int i = 0; i < n; ++i) {
            if (byte < 0) {
                out[i] = gLowPrimes[bit];
                if (++bit >= 10) {
                    byte = 0;
                    bit = 0;
                }
            } else {
                while (1) {
					if (byte >= kPrimesMaskSize) {
						setDone();
						produce(n - i);
						return;
					} else if (gPrimesMask[byte] & (1 << bit)) {
                        out[i] = 30 * (1 + byte) + gPrimeOffsets[bit];
						if (++bit >= 8) {
                            ++byte;
                            bit = 0;
                        }
                        break;
                    } else {
						if (++bit >= 8) {
                            ++byte;
                            bit = 0;
                        }
                    }
                }
            }
        }
		mOut = mOut->nextp();
    }
};

struct Primez : Gen
{
	int byte;
	int bit;
	
	Primez(Thread& th) : Gen(th, itemTypeZ, false), byte(-1), bit(0) {}
    
	virtual const char* TypeName() const override { return "Primez"; }
        
	virtual void pull(Thread& th) override
	{
		int n = mBlockSize;
		Z* out = mOut->fulfillz(n);
		for (int i = 0; i < n; ++i) {
            if (byte < 0) {
                out[i] = gLowPrimes[bit];
                if (++bit >= 10) {
                    byte = 0;
                    bit = 0;
                }
            } else {
                while (1) {
					if (byte >= kPrimesMaskSize) {
						setDone();
						produce(n - i);
						return;
                    } else if (gPrimesMask[byte] & (1 << bit)) {
                        out[i] = 30 * (1 + byte) + gPrimeOffsets[bit];
						if (++bit >= 8) {
                            ++byte;
                            bit = 0;
                        }
                        break;
                    } else {
                        if (++bit >= 8) {
                            ++byte;
                            bit = 0;
                        }
                    }
                }
            }
        }
		mOut = mOut->nextp();
    }
};


static void primes_(Thread& th, Prim* prim)
{
	Gen* g = new Primes(th);
	th.push(new List(g));
}

static void primez_(Thread& th, Prim* prim)
{
	Gen* g = new Primez(th);
	th.push(new List(g));
}


#pragma mark ORDERING


class Perms : public Gen
{
	std::vector<int> mOrder;
	std::vector<V> mItems;
	int64_t m;
public:

	Perms(Thread& th, P<Array> const& inItems)
		: Gen(th, itemTypeV, true)
	{
		mOrder.reserve(inItems->size());
		mItems.reserve(inItems->size());
		for (int i = 0; i < inItems->size(); ++i) {
			mItems.push_back(inItems->_at(i));
			mOrder.push_back(i);
		}
		numPerms();
	}
	
	void numPerms()
	{
		m = 1;
		for (int64_t i = 2; i <= (int64_t)mItems.size(); ++i) {
			m *= i;
		}
	}
	
	virtual const char* TypeName() const override { return "Perms"; }
	
	void pull(Thread& th) override
	{
		if (m <= 0) {
			end();
		} else {
			int n = (int)std::min(m, (int64_t)mBlockSize);
			V* out = mOut->fulfill(n);
			for (int i = 0; i < n; ++i) {
				const int len = (int)mItems.size();
				P<List> list = new List(itemTypeV, len);
				P<Array> arr = list->mArray;
				V* outItems = arr->v();
				
				for (int j = 0; j < len; ++j) {
					outItems[j] = mItems[mOrder[j]];
				}
				arr->setSize(len);
				out[i] = list;
				getNext();
			}
			m -= n;
			mOut = mOut->nextp();
		}
	}
	
	void getNext()
	{
		int N = (int)mOrder.size();
		int i = N - 1;
		while (mOrder[i-1] >= mOrder[i]) 
			i = i-1;

		if (i <= 0) return;

		int j = N;
		while (mOrder[j-1] <= mOrder[i-1]) 
			j = j-1;

		std::swap(mOrder[i-1], mOrder[j-1]);

		i++; j = N;
		while (i < j) {
			std::swap(mOrder[i-1], mOrder[j-1]);    
			i++;
			j--;
		}
	}
};

class Permz : public Gen
{
	std::vector<int> mOrder;
	std::vector<Z> mItems;
	int64_t m;
public:

	Permz(Thread& th, P<Array> const& inItems)
		: Gen(th, itemTypeV, true)
	{
		mOrder.reserve(inItems->size());
		mItems.reserve(inItems->size());
		for (int i = 0; i < inItems->size(); ++i) {
			mItems.push_back(inItems->_atz(i));
			mOrder.push_back(i);
		}
		numPerms();
	}
	
	void numPerms()
	{
		m = 1;
		for (int64_t i = 2; i <= (int64_t)mItems.size(); ++i) {
			m *= i;
		}
	}
	
	virtual const char* TypeName() const override { return "Permz"; }
	
	void pull(Thread& th) override
	{
		if (m <= 0) {
			end();
		} else {
			int n = (int)std::min(m, (int64_t)mBlockSize);
			V* out = mOut->fulfill(n);
			for (int i = 0; i < n; ++i) {
				const int len = (int)mItems.size();
				P<List> list = new List(itemTypeZ, len);
				P<Array> arr = list->mArray;
				Z* outItems = arr->z();
				
				for (int j = 0; j < len; ++j) {
					outItems[j] = mItems[mOrder[j]];
				}
				arr->setSize(len);
				out[i] = list;
				getNext();
			}
			m -= n;
			mOut = mOut->nextp();
		}
	}
	
	void getNext()
	{
		int N = (int)mOrder.size();
		int i = N - 1;
		while (mOrder[i-1] >= mOrder[i]) 
			i = i-1;

		if (i <= 0) return;

		int j = N;
		while (mOrder[j-1] <= mOrder[i-1]) 
			j = j-1;

		std::swap(mOrder[i-1], mOrder[j-1]);

		i++; j = N;
		while (i < j) {
			std::swap(mOrder[i-1], mOrder[j-1]);    
			i++;
			j--;
		}
	}
};

static void perms_(Thread& th, Prim* prim)
{
	P<List> a = th.popVList("perms : list");
	if (!a->isFinite())
		indefiniteOp("perms : list", "");
	
	a = a->pack(th);
    P<Array> arr = a->mArray;
	Gen* g = new Perms(th, arr);
	th.push(new List(g));
}

static void permz_(Thread& th, Prim* prim)
{
	P<List> a = th.popZList("permz : list");
	if (!a->isFinite())
		indefiniteOp("permz : list", "");
	
	a = a->pack(th);
    P<Array> arr = a->mArray;
	Gen* g = new Permz(th, arr);
	th.push(new List(g));
}



class PermsWithRepeatedItems : public Gen
{
	std::vector<V> mOrig;
	std::vector<V> mItems;
	bool mThereafter = false;
public:

	PermsWithRepeatedItems(Thread& th, P<Array> const& inItems)
		: Gen(th, itemTypeV, true)
	{
		mOrig.reserve(inItems->size());
		for (int i = 0; i < inItems->size(); ++i) {
			mOrig.push_back(inItems->_at(i));
		}
		mItems = mOrig;
	}
	
	virtual const char* TypeName() const override { return "PermsWithRepeatedItems"; }
	
	void pull(Thread& th) override
	{
		int n = mBlockSize;
		int framesRemaining = n;
		V* out = mOut->fulfill(n);
		for (int i = 0; i < n; ++i) {
			if (mThereafter) {
				if (getNext(th)) {
					setDone();
					break;
				}
			} else {
				mThereafter = true;
			}
			const int len = (int)mItems.size();
			P<List> list = new List(itemTypeV, len);
			P<Array> arr = list->mArray;
			V* outItems = arr->v();
			
			for (int j = 0; j < len; ++j) {
				outItems[j] = mItems[j];
			}
			arr->setSize(len);
			out[i] = list;
			--framesRemaining;
		}
		produce(framesRemaining);
	}
	
	bool getNext(Thread& th)
	{
		auto vless = [&](Arg a, Arg b){ return ::Compare(th, a, b) < 0; };
		next_permutation(mItems.begin(), mItems.end(), vless);
		
		for (size_t i = 0; i < mItems.size(); ++i) {
			if (!::Equals(th, mItems[i], mOrig[i])) return false;
		}
		return true;
	}
};


class PermsWithRepeatedItemsZ : public Gen
{
	std::vector<Z> mOrig;
	std::vector<Z> mItems;
	bool mThereafter = false;
public:

	PermsWithRepeatedItemsZ(Thread& th, P<Array> const& inItems)
		: Gen(th, itemTypeV, true)
	{
		mOrig.reserve(inItems->size());
		for (int i = 0; i < inItems->size(); ++i) {
			mOrig.push_back(inItems->_atz(i));
		}
		mItems = mOrig;
	}
	
	virtual const char* TypeName() const override { return "PermsWithRepeatedItemsZ"; }
	
	void pull(Thread& th) override
	{
		int n = mBlockSize;
		int framesRemaining = n;
		Z* out = mOut->fulfillz(n);
		for (int i = 0; i < n; ++i) {
			if (mThereafter) {
				if (getNext(th)) {
					setDone();
					break;
				}
			} else {
				mThereafter = true;
			}
			const int len = (int)mItems.size();
			P<List> list = new List(itemTypeV, len);
			P<Array> arr = list->mArray;
			V* outItems = arr->v();
			
			for (int j = 0; j < len; ++j) {
				outItems[j] = mItems[j];
			}
			arr->setSize(len);
			out[i] = list;
			--framesRemaining;
		}
		produce(framesRemaining);
	}
	
	bool getNext(Thread& th)
	{
		auto vless = [&](Arg a, Arg b){ return ::Compare(th, a, b) < 0; };
		next_permutation(mItems.begin(), mItems.end(), vless);
		
		for (size_t i = 0; i < mItems.size(); ++i) {
			if (!::Equals(th, mItems[i], mOrig[i])) return false;
		}
		return true;
	}
};

static void permswr_(Thread& th, Prim* prim)
{
	P<List> a = th.popVList("permswr : list");
	if (!a->isFinite())
		indefiniteOp("permswr : list", "");
	
	a = a->pack(th);
    P<Array> arr = a->mArray;
	Gen* g = new PermsWithRepeatedItems(th, arr);
	th.push(new List(g));
}

static void permzwr_(Thread& th, Prim* prim)
{
	P<List> a = th.popZList("permzwr : list");
	if (!a->isFinite())
		indefiniteOp("permzwr : list", "");
	
	a = a->pack(th);
    P<Array> arr = a->mArray;
	Gen* g = new PermsWithRepeatedItemsZ(th, arr);
	th.push(new List(g));
}


struct Repeat : Gen
{
    V _a;
	int64_t _m;
    
	Repeat(Thread& th, Arg a, int64_t m) : Gen(th, itemTypeV, m < LLONG_MAX), _a(a), _m(m) {}
	    
	virtual const char* TypeName() const override { return "Repeat"; }
	
	virtual void pull(Thread& th) override {
		if (_m <= 0) {
			end();
		} else {
            int n = (int)std::min(_m, (int64_t)mBlockSize);
            V* out = mOut->fulfill(n);
            V a = _a;
            for (int i = 0; i < n; ++i) {
                out[i] = a;
            }
            _m -= n;
			mOut = mOut->nextp();
        }
	}
    
};

struct RepeatFun : Gen
{
    V _a;
	Z _b;
	int64_t _m;
    
	RepeatFun(Thread& th, Arg a, int64_t m) : Gen(th, itemTypeV, m < LLONG_MAX), _a(a), _b(0.), _m(m) {}
	    
	virtual const char* TypeName() const override { return "Repeat"; }
	
	virtual void pull(Thread& th) override {
		if (_m <= 0) {
			end();
		} else {
            int n = (int)std::min(_m, (int64_t)mBlockSize);
            V* out = mOut->fulfill(n);
            for (int i = 0; i < n; ++i) {
				SaveStack ss(th);
				th.push(_b);
				_b += 1.;
				_a.apply(th);
                out[i] = th.pop();
            }
            _m -= n;
			mOut = mOut->nextp();
        }
	}
    
};

struct InfRepeatFun : Gen
{
    V _a;
	Z _b;
    
	InfRepeatFun(Thread& th, Arg a) : Gen(th, itemTypeV, false), _a(a), _b(0.) {}
	    
	virtual const char* TypeName() const override { return "InfRepeatFun"; }
	
	virtual void pull(Thread& th) override {
		int n = mBlockSize;
		V* out = mOut->fulfill(n);
		for (int i = 0; i < n; ++i) {
			SaveStack ss(th);
			th.push(_b);
			_b += 1.;
			_a.apply(th);
			out[i] = th.pop();
		}
		mOut = mOut->nextp();
	}
    
};

struct Repeatz : Gen
{
    Z _a;
	int64_t _m;
    
	Repeatz(Thread& th, Z a, int64_t m) : Gen(th, itemTypeZ, true), _a(a), _m(m) {}
    
	virtual const char* TypeName() const override { return "Repeatz"; }
    
	virtual void pull(Thread& th) override {
		if (_m <= 0) {
			end();
		} else {
            int n = (int)std::min(_m, (int64_t)mBlockSize);
            Z* out = mOut->fulfillz(n);
            Z a = _a;
            for (int i = 0; i < n; ++i) {
                out[i] = a;
            }
            _m -= n;
			mOut = mOut->nextp();
        }
    }
};

struct RepeatFunz : Gen
{
    V _a;
	Z _b;
	int64_t _m;
    
	RepeatFunz(Thread& th, Arg a, int64_t m) : Gen(th, itemTypeZ, true), _a(a), _b(0.), _m(m) {}
    
	virtual const char* TypeName() const override { return "RepeatFunz"; }

	virtual void pull(Thread& th) override {
		if (_m <= 0) {
			end();
		} else {
            int n = (int)std::min(_m, (int64_t)mBlockSize);
            Z* out = mOut->fulfillz(n);
            for (int i = 0; i < n; ++i) {
				SaveStack ss(th);
				th.push(_b);
				_b += 1.;
				_a.apply(th);
                out[i] = th.pop().asFloat();
            }
            _m -= n;
			mOut = mOut->nextp();
        }
    }
};

struct InfRepeatFunz : Gen
{
    V _a;
	Z _b;
    
	InfRepeatFunz(Thread& th, Arg a) : Gen(th, itemTypeZ, false), _a(a), _b(0.) {}
    
	virtual const char* TypeName() const override { return "InfRepeatFunz"; }

	virtual void pull(Thread& th) override {
		int n = mBlockSize;
		Z* out = mOut->fulfillz(n);
		for (int i = 0; i < n; ++i) {
			SaveStack ss(th);
			th.push(_b);
			_b += 1.;
			_a.apply(th);
			out[i] = th.pop().asFloat();
		}
		mOut = mOut->nextp();
    }
};

struct RCyc : public Gen
{
	V _ref;
	P<List> _a0;
	P<List> _a;

	RCyc(Thread& th, Arg ref, P<List> const& a) : Gen(th, a->elemType, false), _ref(ref), _a0(a), _a(a) {}

	virtual const char* TypeName() const override { return "RCyc"; }

	virtual void pull(Thread& th) override
	{
		if (!_a) {
			V v = _ref.deref();
			if (v.isList()) {
				_a0 = (List*)v.o();
			}
			_a = _a0;
		}
		_a->force(th);
		mOut->fulfill(_a->mArray);
		_a = _a->next();
		mOut = mOut->nextp();
	}
};

struct Cyc : public Gen
{
	P<List> _a0;
	P<List> _a;

	Cyc(Thread& th, P<List> const& a) : Gen(th, a->elemType, false), _a0(a), _a(a) {}

	virtual const char* TypeName() const override { return "Cyc"; }

	virtual void pull(Thread& th) override
	{
		if (!_a) _a = _a0;
		_a->force(th);
		mOut->fulfill(_a->mArray);
		_a = _a->next();
		mOut = mOut->nextp();
	}
};


struct NCyc : public Gen
{
	P<List> _a0;
	P<List> _a;
	int64_t _n;
	
	NCyc(Thread& th, int64_t n, P<List> const& a) : Gen(th, a->elemType, true), _a0(a), _a(a), _n(n) {}

	virtual const char* TypeName() const override { return "Cyc"; }

	virtual void pull(Thread& th) override
	{
		if (!_a) {
			if (_n <= 1) {
				end();
				return;
			} else {
				_a = _a0;
				--_n;
			}
		}

		_a->force(th);
		mOut->fulfill(_a->mArray);
		_a = _a->next();
		mOut = mOut->nextp();
	}
};

static void repeat_(Thread& th, Prim* prim)
{
	Z x = th.popFloat("X : n");
	V a = th.pop();
    
	if (x <= 0.) {
		th.push(vm._nilv);
	} else {
		Gen* g;
		if (x >= (Z)LONG_MAX) {
			if (a.isFunOrPrim()) {
				g = new InfRepeatFun(th, a);
			} else {
				g = new Ever(th, a);
			}
		} else {
			int64_t n = (int64_t)floor(x + .5);
			if (a.isFunOrPrim()) {
				g = new RepeatFun(th, a, n);
			} else {
				g = new Repeat(th, a, n);
			}
		}
		th.push(new List(g));
	}
}

static void repeatz_(Thread& th, Prim* prim)
{
	Z x = th.popFloat("XZ : n");
	V a = th.pop();
    
	if (x <= 0.) {
		th.push(vm._nilv);
	} else {
		Gen* g;
		if (x >= (Z)LONG_MAX) {
			if (a.isFunOrPrim()) {
				g = new InfRepeatFunz(th, a);
			} else {
				g = new Everz(th, a.asFloat());
			}
		} else {
			int64_t n = (int64_t)floor(x + .5);
			if (a.isFunOrPrim()) {
				g = new RepeatFunz(th, a, n);
			} else {
				g = new Repeatz(th, a.asFloat(), n);
			}
		}
		th.push(new List(g));
	}
}

struct Silence : Gen
{
	int64_t _m;
    
	Silence(Thread& th, int64_t m) : Gen(th, itemTypeZ, true), _m(m) {}
    
	virtual const char* TypeName() const override { return "Silence"; }
    
	virtual void pull(Thread& th) override {
		if (_m <= 0) {
			end();
		} else {
            int n = (int)std::min(_m, (int64_t)mBlockSize);
            Z* out = mOut->fulfillz(n);
			memset(out, 0, n * sizeof(Z));
            _m -= n;
			mOut = mOut->nextp();
        }
    }
};

static void mum_(Thread& th, Prim* prim)
{
	Z t = th.popFloat("mum : duration");
	
	int64_t n = (int64_t)floor(.5 + th.rate.sampleRate * t);
	if (std::isinf(t) || (n <= 0 && t > 0.)) {
		th.push(new List(new Everz(th, 0.)));
	} else {
		Gen* g = new Silence(th, n);
		th.push(new List(g));
	}
}


static void cyc_(Thread& th, Prim* prim)
{
	V v = th.pop();
	
	if (!v.isList()) {
		th.push(v);
		return;
	}
	
	P<List> s = (List*)v.o();
	

    s->force(th);
    if (s->isEnd()) {
        th.push(s);
        return;
    }
    
	Gen* g = new Cyc(th, s);
	th.push(new List(g));
}


static void rcyc_(Thread& th, Prim* prim)
{
	V ref = th.pop();
	V v = ref.deref();
	if (!v.isList()) {
		wrongType("rcyc : ref get", "List", v);
	}
	
	P<List> list = (List*)v.o();
	
	Gen* g = new RCyc(th, ref, list);
	th.push(new List(g));
}


static void ncyc_(Thread& th, Prim* prim)
{
	int64_t n = th.popInt("ncyc : n");
	P<List> s = th.popList("ncyc : seq");
    
    s->force(th);
    if (s->isEnd()) {
        th.push(s);
        return;
    }
	
	if (n <= 0) {
		th.push(vm.getNil(s->elemType));		
	} else {
		Gen* g = new NCyc(th, n, s);
		th.push(new List(g));
	}
}


struct Append : Gen
{
	P<List> _a;
	V _b;

	Append(Thread& th, P<List> const& a, Arg b, bool inFinite) : Gen(th, a->elemType, inFinite), _a(a), _b(b) {}
	virtual const char* TypeName() const override { return "Append"; }

	virtual void pull(Thread& th) override
	{
		if (_a) {
			_a->force(th);
			mOut->fulfill(_a->mArray);
			_a = _a->next();
			mOut = mOut->nextp();
		} else {
			if (_b.isFunOrPrim()) {
				SaveStack ss(th);
				_b.apply(th);
				_b = th.pop();
			}
			if (!_b.isList()) {
				post("$ : b is not a sequence  '%s'\n", _b.TypeName());
				end();
				return;
			}
			
			List* b = (List*)_b.o();
			if (elemType != b->elemType) {
				post("$ : b item type doesn't match\n");
				end();
				return;
			}
			setDone();
			mOut->link(th, b);
		}
	}
};


struct Cat : Gen
{
	VIn _a;
	VIn _b;

	Cat(Thread& th, Arg a, P<List> const& b) : Gen(th, itemTypeV, b->isFinite()), _a(a), _b(b)
	{
		V v;
		_b.one(th, v); // skip over a.
	}
	virtual const char* TypeName() const override { return "Cat"; }

	virtual void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int astride;
			V *a;
			if (_a(th, n,astride, a)) {
				V b;
				if (_b.one(th, b)) {
					setDone();
					break;
				} else {
					if (b.isFunOrPrim()) {
						SaveStack ss(th);
						try {
							b.apply(th);
						} catch (...) {
							setDone();
							produce(framesToFill);
							throw;
						}
						b = th.pop();
					}
					if (!b.isVList()) { 
						setDone(); 
						break; 
					}
					_a.set(b);
					continue;
				}
			}
			for (int i = 0; i < n; ++i) {
				out[i] = a[i];
			}
			_a.advance(n);
			framesToFill -= n;
			out += n;
		}
		produce(framesToFill);
	}
};


struct CatZ : Gen
{
	ZIn _a;
	VIn _b;

	// this makes the assumption that all of the sublists of b are finite! if they are not then this will not prevent infinite loops.
	CatZ(Thread& th, Arg a, P<List> const& b) : Gen(th, itemTypeZ, b->isFinite()), _a(a), _b(b)
	{
		V v;
		_b.one(th, v); // skip over a.
	}
	virtual const char* TypeName() const override { return "CatZ"; }

	virtual void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		Z* out = mOut->fulfillz(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int astride;
			Z *a;
			if (_a(th, n,astride, a)) {
				V b;
				if (_b.one(th, b)) {
					setDone();
					break;
				} else {
					if (b.isFunOrPrim()) {
						SaveStack ss(th);
						try {
							b.apply(th);
						} catch (...) {
							setDone();
							produce(framesToFill);
							throw;
						}
						b = th.pop();
					}
					if (!b.isZList()) { 
						setDone(); 
						break; 
					}
					_a.set(b);
					continue;
				}
			}
			for (int i = 0; i < n; ++i) {
				out[i] = a[i];
			}
			_a.advance(n);
			framesToFill -= n;
			out += n;
		}
		produce(framesToFill);
	}				
};

#include <stack>

struct Flat : Gen
{
	std::stack<VIn> in; // stack of list continuations

	Flat(Thread& th, Arg inA) : Gen(th, itemTypeV, inA.isFinite())
	{
		VIn vin(inA);
		in.push(vin);
	}
	virtual const char* TypeName() const override { return "Flat"; }

	virtual void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		VIn* vin = &in.top();
		for (int i = 0; framesToFill; ) {
			V a;
			if (vin->one(th, a)) {
				if (in.size() == 1) {
					setDone();
					break;
				} else {
					in.pop();
					vin = &in.top();
				}
			} else if (a.isVList()) {
				VIn vin2(a);
				in.push(vin2);
				vin = &in.top();
			} else {
				out[i++] = a;
				--framesToFill;
			}
		}
		produce(framesToFill);
	}
};

struct Flatten : Gen
{
	std::stack<VIn> in; // stack of list continuations
	size_t depth;
	
	Flatten(Thread& th, Arg inA, size_t inDepth) : Gen(th, itemTypeV, inA.isFinite()), depth(inDepth)
	{
		VIn vin(inA);
		in.push(vin);
	}
	virtual const char* TypeName() const override { return "Flat"; }

	virtual void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		VIn* vin = &in.top();
		for (int i = 0; framesToFill; ) {
			V a;
			if (vin->one(th, a)) {
				if (in.size() == 1) {
					setDone();
					break;
				} else {
					in.pop();
					vin = &in.top();
				}
			} else if (a.isVList() && in.size() <= depth) {
				VIn vin2(a);
				in.push(vin2);
				vin = &in.top();
			} else {
				out[i++] = a;
				--framesToFill;
			}
		}
		produce(framesToFill);
	}
};

struct Keep : Gen
{
	VIn _a;
	int64_t _n;
	
	Keep(Thread& th, int64_t n, Arg a) : Gen(th, itemTypeV, true), _a(a), _n(n) {}
	virtual const char* TypeName() const override { return "Keep"; }
    	
	virtual void pull(Thread& th) override {
		if (_n <= 0) {
			end();
		} else {
            int framesToFill = (int)std::min(_n, (int64_t)mBlockSize);
            V* out = mOut->fulfill(framesToFill);
            _n -= framesToFill;
           while (framesToFill) {
                int n = framesToFill;
                int astride;
                V *a;
                if (_a(th, n,astride, a)) {
                    setDone();
                    break;
                } else {
                    for (int i = 0; i < n; ++i) {
                        out[i] = *a;
						a += astride;
                    }
                    _a.advance(n);
                    framesToFill -= n;
                    out += n;
                }
            }
            produce(framesToFill);
        }
	}
};

struct Take : Gen
{
	VIn _a;
	int64_t _n;
	
	Take(Thread& th, int64_t n, Arg a) : Gen(th, itemTypeV, true), _a(a), _n(n) {}
	virtual const char* TypeName() const override { return "Take"; }
    	
	virtual void pull(Thread& th) override {
		if (_n <= 0) {
			end();
		} else {
            int framesToFill = (int)std::min(_n, (int64_t)mBlockSize);
            V* out = mOut->fulfill(framesToFill);
           while (framesToFill) {
                int n = framesToFill;
                int astride;
                V *a;
                if (_a(th, n,astride, a)) {
					produce(framesToFill);
					P<List> g = new List(new Repeat(th, 0., _n));
					setDone();
					mOut->link(th, g());
                    return;
                } else {
					_n -= framesToFill;
					for (int i = 0; i < n; ++i) {
                        out[i] = *a;
						a += astride;
                    }
                    _a.advance(n);
                    framesToFill -= n;
                    out += n;
               }
           }
           produce(framesToFill);
        }
	}
};

struct Keepz : Gen
{
	ZIn _a;
	int64_t _n;
	
	Keepz(Thread& th, int64_t n, Arg a) : Gen(th, itemTypeZ, true), _a(a), _n(n) {}
	virtual const char* TypeName() const override { return "Keepz"; }
    	
	virtual void pull(Thread& th) override {
		if (_n <= 0) {
			end();
		} else {
            int framesToFill = (int)std::min(_n, (int64_t)mBlockSize);
            Z* out = mOut->fulfillz(framesToFill);
            _n -= framesToFill;
            while (framesToFill) {
                int n = framesToFill;
                int astride;
                Z *a;
                if (_a(th, n,astride, a)) {
                  setDone();
                    break;
                } else {
                    for (int i = 0; i < n; ++i) {
                        out[i] = *a;
						a += astride;
                    }
                    _a.advance(n);
                    framesToFill -= n;
                    out += n;
                }
            }
           produce(framesToFill);
        }
	}
};

struct Takez : Gen
{
	ZIn _a;
	int64_t _n;
	
	Takez(Thread& th, int64_t n, Arg a) : Gen(th, itemTypeZ, true), _a(a), _n(n) {}
	virtual const char* TypeName() const override { return "Takez"; }
    	
	virtual void pull(Thread& th) override {
		if (_n <= 0) {
			end();
		} else {
            int framesToFill = (int)std::min(_n, (int64_t)mBlockSize);
            Z* out = mOut->fulfillz(framesToFill);
            while (framesToFill) {
                int n = framesToFill;
                int astride;
                Z *a;
                if (_a(th, n,astride, a)) {
					produce(framesToFill);
					P<List> g = new List(new Repeatz(th, 0., _n));
                    setDone();
					mOut->link(th, g());
                    return;
                } else {
					_n -= framesToFill;
					for (int i = 0; i < n; ++i) {
                        out[i] = *a;
						a += astride;
                    }
                    _a.advance(n);
                    framesToFill -= n;
                    out += n;
                }
            }
            produce(framesToFill);
        }
	}
};

static void append_(Thread& th, Prim* prim)
{
	V b = th.pop();
	V a = th.pop();
	
	if (a.isString() && b.isString()) {
		std::string s;
		a.print(th, s);
		b.print(th, s);
		th.push(new String(s.c_str()));
	} else if (a.isList()) {
		P<List> list = (List*)a.o();
		th.push(new List(new Append(th, list, b, leastFinite(a,b))));
	} else {
		wrongType("$ : a", "List or String", a);
	}
}

struct AppendSubs : Gen
{
	VIn _a;
	VIn _b;
	
	AppendSubs(Thread& th, Arg a, Arg b) : Gen(th, itemTypeV, mostFinite(a,b)), _a(a), _b(b) {}
	
	virtual const char* TypeName() const override { return "AppendSubs"; }
 
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int astride, bstride;
			V *a, *b;
			if (_a(th, n,astride, a) || _b(th, n,bstride, b)) {
				setDone();
				break;
			} else {
				for (int i = 0; i < n; ++i) {
					SaveStack ss(th);
					if (!a->isList())
						wrongType("$$ : *a", "List", *a);
					
					List* aa = (List*)a->o();
						
					out[i] = new List(new Append(th, aa, *b, mostFinite(*a,*b)));
					a += astride;
					b += bstride;
				}
				_a.advance(n);
				_b.advance(n);
				framesToFill -= n;
				out += n;
			}
		}
		produce(framesToFill);
	}
};

static void appendSubs_(Thread& th, Prim* prim)
{
	V b = th.popVList("$$ : b");
	V a = th.popVList("$$ : a");
	
    th.push(new List(new AppendSubs(th, a, b)));
}

static void cat_(Thread& th, Prim* prim)
{
	V v = th.pop();
	if (!v.isList()) {
		th.push(v);
		return;
	}
	
	P<List> b = (List*)v.o();

	b->force(th);
	if (b->isEnd()) {
		th.push(vm.getNil(b->elemType));
		return;
	}
	
	VIn a_(b);
	V a;
	if (a_.one(th, a)) {
		th.push(vm.getNil(b->elemType));
		return;
	}
	
	
	
	//V a = b->mArray->v()[0];
	
	if (a.isString()) {
		if (!b->isFinite())
			indefiniteOp("$/ : list of strings", "");
		
		std::string s;

		VIn in_(b);
		while (true) {
			V in;
			if (in_.one(th, in)) {
				th.push(new String(s.c_str()));
				return;
			}
			in.print(th, s);
		}
		
	} else if (!a.isList()) {
		wrongType("$/ : b", "List", a);
	}
		
	Gen* g;
	if (a.isVList()) g = new Cat(th, a, b);
	else g = new CatZ(th, a, b);
	th.push(new List(g));

}

static void flat_(Thread& th, Prim* prim)
{
	V a = th.pop();

	if (a.isVList()) th.push(new List(new Flat(th, a)));
	else th.push(a);
}

static void flatten_(Thread& th, Prim* prim)
{
	int64_t n = th.popInt("flatten : n");
	V a = th.pop();
	
	if (a.isVList()) th.push(new List(new Flatten(th, a, n)));
	else th.push(a);
}

static void N_(Thread& th, Prim* prim)
{
	int64_t n = th.popInt("N : n");
	V v = th.pop();
	
    if (v.isVList()) {
		if (n <= 0) v = vm._nilv;
        else v = new List(new Keep(th, n, v));
    } else if (v.isZList()) {
		if (n <= 0) v = vm._nilz;
        else v = new List(new Keepz(th, n, v));
	}
    
    th.push(v);
}

static void NZ_(Thread& th, Prim* prim)
{
	int64_t n = th.popInt("NZ : n");
	V v = th.pop();
	
    if (v.isZList()) {
		if (n <= 0) v = vm._nilz;
        else v = new List(new Keepz(th, n, v));
	}
    
    th.push(v);
}

static void T_(Thread& th, Prim* prim)
{
	Z t = th.popFloat("T : t");
	V v = th.pop();
	
	int64_t n = (int64_t)floor(.5 + th.rate.sampleRate * t);

    if (v.isVList()) {
		if (n <= 0) v = vm._nilv;
        else v = new List(new Keep(th, n, v));
    } else if (v.isZList()) {
		if (n <= 0) v = vm._nilz;
        else v = new List(new Keepz(th, n, v));
	}
    
    th.push(v);
}

static void take_(Thread& th, Prim* prim)
{
	int64_t n = th.popInt("take : n");
	P<List> s = th.popList("take : s");

    Gen* g;
	if (n > 0) {
		if (s->isVList()) 
			g = new Take(th, n, s);
		else
			g = new Takez(th, n, s);
		th.push(new List(g));
    } else if (n < 0) {
		if (!s->isFinite())
			indefiniteOp("take", "");
			
		s = s->pack(th);
		int64_t size = s->length(th);
		n = -n;
		
		List* s2 = new List(s->elemType, n);
		th.push(s2);
		s2->mArray->setSize(n);
		if (s->isVList()) {
			V* p = s2->mArray->v();
			V* q = s->mArray->v();
			if (size < n) {
				int64_t offset = n - size;
				for (int64_t i = 0; i < offset; ++i) p[i] = 0.;
				for (int64_t i = 0, j = offset; i < size; ++i, ++j) p[j] = q[i];
			} else {
				for (int64_t i = 0, j = size - n; i < n; ++i, ++j) p[i] = q[j];
			}
		} else {
			Z* p = s2->mArray->z();
			Z* q = s->mArray->z();
			size_t elemSize = s2->mArray->elemSize();
			if (size < n) {
				int64_t offset = n - size;
				memset(p, 0, offset * elemSize);
				memcpy(p + offset, q, size * elemSize);
			} else {
				memcpy(p, q + size - n, n * elemSize);
			}
		}
		return;
		
	} else {
		if (s->isVList())
			th.push(vm._nilv);
		else 
			th.push(vm._nilz);
		return;
		
	}
}




static void skip_positive_(Thread& th, P<List>& list, int64_t n)
{
	if (n <= 0) return;

	int itemType = list->elemType;
	
	while (list && n > 0) {
		list->force(th);

		Array* a = list->mArray();
		int64_t asize = a->size();
		if (asize > n) {
			int64_t remain = asize - n;
			Array* a2 = new Array(list->elemType, remain);
			a2->setSize(remain);
			if (list->isVList()) {
				for (int64_t i = 0, j = n; i < remain; ++i, ++j) {
					a2->v()[i] = a->v()[j];
				}
			} else {
				memcpy(a2->z(), a->z() + n, remain * a->elemSize());
			}
			list = new List(a2, list->next());
			return;
		}
		n -= asize;
		list = list->next();
	}
	
	if (!list) {
		list = vm.getNil(itemType);
	}
}

static void skip_(Thread& th, Prim* prim)
{
	int64_t n = th.popInt("skip : n");
	P<List> s = th.popList("skip : s");

	if (n <= 0) {
		th.push(s);
		return;
	}
	
	skip_positive_(th, s, n);
	th.push(s);
}


static void skipT_(Thread& th, Prim* prim)
{
	Z t = th.popFloat(">T : t");
	P<List> s = th.popList(">T : s");
	
	int64_t n = (int64_t)floor(.5 + th.rate.sampleRate * t);

	skip_positive_(th, s, n);
	th.push(s);
}

struct Hops : Gen
{
	P<List> _a;
	BothIn _hop;
	bool _once = true;
	
	Hops(Thread& th, Arg hop, P<List> const& a) : Gen(th, itemTypeV, true), _a(a), _hop(hop) {}
	virtual const char* TypeName() const override { return "Hops"; }
    	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;		
		V* out = mOut->fulfill(framesToFill);
		for (int i = 0; i < mBlockSize; ++i) {
			if (_once) {
				_once = false;
			} else {
				int64_t hop;
				if (_hop.onei(th, hop)) {
					setDone();
					break;
				} else {
					skip_positive_(th, _a, hop);
				}
			}
			out[i] = _a;
			framesToFill -= 1;
		}
		produce(framesToFill);
	}
};

struct HopTs : Gen
{
	P<List> _a;
	BothIn _hop;
	bool _once = true;
	
	HopTs(Thread& th, Arg hop, P<List> const& a) : Gen(th, itemTypeV, true), _a(a), _hop(hop) {}
	virtual const char* TypeName() const override { return "HopTs"; }
    	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		for (int i = 0; i < framesToFill; ++i) {
			if (_once) {
				_once = false;
			} else {
				Z hop;
				if (_hop.onez(th, hop)) {
					setDone();
					break;
				} else {
					int64_t n = (int64_t)floor(.5 + th.rate.sampleRate * hop);
					skip_positive_(th, _a, n);
				}
			}
			out[i] = _a;
			framesToFill -= 1;
		}
		produce(framesToFill);
	}
};


static void hops_(Thread& th, Prim* prim)
{
	V n = th.pop();
	P<List> s = th.popList("N>> : list");

	th.push(new List(new Hops(th, n, s)));
}

static void hopTs_(Thread& th, Prim* prim)
{
	V n = th.pop();
	P<List> s = th.popList("T>> : list");

	th.push(new List(new HopTs(th, n, s)));
}




static void drop_(Thread& th, Prim* prim)
{
	int64_t n = th.popInt("drop : n");
	P<List> s = th.popList("drop : s");

	if (n == 0) {
		th.push(s);
	} else if (n > 0) {
		skip_positive_(th, s, n);
		th.push(s);
	} else {
		if (!s->isFinite())
			indefiniteOp("drop", "");
			
		s = s->pack(th);
		int64_t size = s->length(th);
		n = -n;
		
		int64_t remain = std::max(INT64_C(0), size - n);
		if (remain <= 0) {
			th.push(vm.getNil(s->elemType));
			return;
		}
		
		P<List> s2 = new List(s->elemType, remain);
		th.push(s2);
		s2->mArray->setSize(remain);
		size_t elemSize = s2->mArray->elemSize();
		if (s->isVList()) {
			V* y =  s->mArray->v();
			V* x = s2->mArray->v();
			for (int64_t i = 0; i < remain; ++i) {
				x[i] = y[i];
			}
		} else {
			memcpy(s2->mArray->z(), s->mArray->z(), remain * elemSize);
		}
	}
}

static void choff_(Thread& th, Prim* prim)
{
	int64_t n = th.popInt("choff : n");
	int64_t c = th.popInt("choff : c");
	V a = th.pop();
	
	P<List> s2 = new List(itemTypeV, n);
	s2->mArray->setSize(n);
	
	if (a.isVList()) {
		if (!a.isFinite())
			indefiniteOp("choff : a", "");
			
		P<List> aa = ((List*)a.o())->pack(th);
		int64_t m = aa->length(th);
		int64_t mn = std::min(m,n);
		for (int64_t i = 0; i < mn; ++i) {
			int64_t j = sc_imod(i+c, n);
			s2->mArray->put(j, aa->at(i));
		}
	} else {
		c = sc_imod(c, n);
		s2->mArray->put(c, a);
	}
		
	th.push(s2);
}

static int64_t countWhileTrue(Thread& th, List* list)
{
    int64_t n = 0;
    while (list) {
        list->force(th);

        Array* a = list->mArray();
        int64_t asize = a->size();
        
        for (int i = 0; i < asize; ++i) {
            if (a->at(i).isTrue()) ++n;
            else return n;
        }
        list = list->nextp();
    }
	return n;
}

static void skipWhile_(Thread& th, Prim* prim)
{
	V f = th.pop();
	P<List> s = th.popList("skipWhile : s");

    if (f.isList()) {
        int64_t n = countWhileTrue(th, (List*)f.o());
        skip_positive_(th, s, n);
		th.push(s);
    } else {
    
        List* list = s();

        while (1) {
            list->force(th);
            if (list->isEnd()) {
                th.push(vm.getNil(s->elemType));
                return;
            }
            
            Array* a = list->mArray();
            int64_t asize = a->size();
            
            for (int i = 0; i < asize; ++i) {
                V v;
                {
                    SaveStack ss(th);
                    th.push(a->at(i));
					f.apply(th);
                    v = th.pop();
                }
                if (v.isFalse()) {
                    if (i == 0) {
                        th.push(list);
                    } else {
                        int64_t remain = asize - i;
                        Array* a2 = new Array(s->elemType, remain);
                        th.push(new List(a2, list->next()));
                        a2->setSize(remain);
						if (a->isV()) {
							for (int64_t j = 0; j < remain; ++j) a2->v()[j] = a->v()[j+i];
						} else {
							memcpy(a2->v(), a->v() + i, remain * a->elemSize());
						}
                    }
                    return;
                }
            }
            list = list->nextp();
        }
        th.push(list);
    }
}


struct KeepWhile : Gen
{
	VIn _a;
	VIn _b;
	
	KeepWhile(Thread& th, Arg a, Arg b) : Gen(th, itemTypeV, true), _a(a), _b(b) {}
	virtual const char* TypeName() const override { return "KeepWhile"; }
 
	virtual void pull(Thread& th) override {
        int framesToFill = mBlockSize;
        V* out = mOut->fulfill(framesToFill);
        while (framesToFill && !mDone) {
            int n = framesToFill;
            int astride, bstride;
            V *a, *b;
            if (_a(th, n,astride, a) || _b(th, n,bstride, b)) {
                setDone();
                break;
            } else {
                int k = 0;
                for (int i = 0; i < n; ++i) {
					if (b->isFunOrPrim()) {
						SaveStack ss(th);
						th.push(*a);
						b->apply(th);
						V v = th.pop();
						if (v.isFalse()) {
							setDone();
							break;
						} else {
							out[k++] = *a;
						}
					} else {
						if (b->isFalse()) {
							setDone();
							break;
						} else {
							out[k++] = *a;
						}
					}
                    a += astride;
                    b += bstride;
                }
                _a.advance(n);
                _b.advance(n);
                framesToFill -= k;
                out += k;
            }
        }
        produce(framesToFill);
	}
};

struct KeepWhileZ : Gen
{
	ZIn _a;
	ZIn _b;
	
	KeepWhileZ(Thread& th, Arg a, Arg b) : Gen(th, itemTypeZ, true), _a(a), _b(b) {}
	virtual const char* TypeName() const override { return "KeepWhileZ"; }
 
	virtual void pull(Thread& th) override {
        int framesToFill = mBlockSize;
        Z* out = mOut->fulfillz(framesToFill);
        while (framesToFill && !mDone) {
            int n = framesToFill;
            int astride, bstride;
            Z *a, *b;
            if (_a(th, n,astride, a) || _b(th, n,bstride, b)) {
                setDone();
                break;
            } else {
                int k = 0;
                for (int i = 0; i < n; ++i) {
                    if (*b == 0.) {
                        setDone();
                        break;
                    } else {
                        out[k++] = *a;
                    }
                    a += astride;
                    b += bstride;
                }
                _a.advance(n);
                _b.advance(n);
                framesToFill -= k;
                out += k;
            }
        }
        produce(framesToFill);
	}
};

struct KeepWhileVZ : Gen
{
	VIn _a;
	ZIn _b;
	
	KeepWhileVZ(Thread& th, Arg a, Arg b) : Gen(th, itemTypeV, true), _a(a), _b(b) {}
	virtual const char* TypeName() const override { return "KeepWhileVZ"; }
 
	virtual void pull(Thread& th) override {
        int framesToFill = mBlockSize;
        V* out = mOut->fulfill(framesToFill);
        while (framesToFill && !mDone) {
            int n = framesToFill;
            int astride, bstride;
            V *a;
			Z *b;
            if (_a(th, n,astride, a) || _b(th, n,bstride, b)) {
                setDone();
                break;
            } else {
                int k = 0;
                for (int i = 0; i < n; ++i) {
                    if (*b == 0.) {
                        setDone();
                        break;
                    } else {
                        out[k++] = *a;
                    }
                    a += astride;
                    b += bstride;
                }
                _a.advance(n);
                _b.advance(n);
                framesToFill -= k;
                out += k;
            }
        }
        produce(framesToFill);
	}
};

struct KeepWhileZV : Gen
{
	ZIn _a;
	VIn _b;
	
	KeepWhileZV(Thread& th, Arg a, Arg b) : Gen(th, itemTypeZ, true), _a(a), _b(b) {}
	virtual const char* TypeName() const override { return "KeepWhileZV"; }
 
	virtual void pull(Thread& th) override {
        int framesToFill = mBlockSize;
        Z* out = mOut->fulfillz(framesToFill);
        while (framesToFill && !mDone) {
            int n = framesToFill;
            int astride, bstride;
            Z *a;
			V *b;
            if (_a(th, n,astride, a) || _b(th, n,bstride, b)) {
                setDone();
                break;
            } else {
                int k = 0;
                for (int i = 0; i < n; ++i) {
					if (b->isFunOrPrim()) {
						SaveStack ss(th);
						th.push(*a);
						b->apply(th);
						V v = th.pop();
						if (v.isFalse()) {
							setDone();
							break;
						} else {
							out[k++] = *a;
						}
					} else {
						if (b->isFalse()) {
							setDone();
							break;
						} else {
							out[k++] = *a;
						}
					}
                    a += astride;
                    b += bstride;
                }
                _a.advance(n);
                _b.advance(n);
                framesToFill -= k;
                out += k;
            }
        }
        produce(framesToFill);
	}
};

static void keepWhile_(Thread& th, Prim* prim)
{
	V f = th.pop();
	P<List> s = th.popList("keepWhile : s");
	
	if (s->isZ()) {
		if (f.isZList()) {
			th.push(new List(new KeepWhileZ(th, s, f)));
		} else {
			th.push(new List(new KeepWhileZV(th, s, f)));
		}
	} else {
		if (f.isZList()) {
			th.push(new List(new KeepWhileVZ(th, s, f)));
		} else {
			th.push(new List(new KeepWhile(th, s, f)));
		}
	}    
}


struct Tog : Gen
{
	VIn _in[2];
	int tog;
	
	Tog(Thread& th, Arg a, Arg b) : Gen(th, itemTypeV, mostFinite(a,b)), tog(0) { _in[0] = a; _in[1] = b; }
	virtual const char* TypeName() const override { return "Tog"; }
    	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		while (framesToFill) {
			V v;
			if (_in[tog].one(th, v)) {
				produce(framesToFill);
				setDone();
				return;
			}
			*out++ = v;
			--framesToFill;
			tog = 1 - tog;
		}
		produce(framesToFill);
	}
};

struct Togz : Gen
{
	ZIn _a;
	ZIn _b;
	
	Togz(Thread& th, Arg a, Arg b) : Gen(th, itemTypeZ, mostFinite(a,b)), _a(a), _b(b) {}
	virtual const char* TypeName() const override { return "Togz"; }
 			   	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		Z* out = mOut->fulfillz(framesToFill);
		while (framesToFill) {
			int n = framesToFill / 2;
			int astride, bstride;
			Z *a, *b;
			if (_a(th, n,astride, a) || _b(th, n,bstride, b)) {
				setDone();
				break;
			} else {
				for (int i = 0; i < n; ++i) {
					*out++ = *a;
					*out++ = *b;
					a += astride;
					b += bstride;
				}
				_a.advance(n);
				_b.advance(n);
				framesToFill -= 2*n;
			}
		}
		produce(framesToFill);
	}
};


static void tog_(Thread& th, Prim* prim)
{
	V b = th.pop();
	V a = th.pop();

	th.push(new List(new Tog(th, a, b)));
}

static void togz_(Thread& th, Prim* prim)
{
	V b = th.pop();
	V a = th.pop();

	th.push(new List(new Togz(th, a, b)));
}



struct Tog1 : Gen
{
	VIn _in[2];
	int tog;
	
	Tog1(Thread& th, Arg a, Arg b) : Gen(th, itemTypeV, mostFinite(a,b)), tog(0) { _in[0] = a; _in[1] = b; }
	virtual const char* TypeName() const override { return "Tog1"; }
    	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		while (framesToFill) {
			V v;
			if (_in[tog].one(th, v)) {
				produce(framesToFill);
				setDone();
				return;
			}
			*out++ = v;
			--framesToFill;
			tog = 1 - tog;
		}
		produce(framesToFill);
	}
};




struct Hang : Gen
{
	P<List> _a;
	V _b;

	Hang(Thread& th, P<List> const& a) : Gen(th, itemTypeV, false), _a(a) {}
	virtual const char* TypeName() const override { return "Hang"; }

	virtual void pull(Thread& th) override
	{
		if (_a) {
			_a->force(th);
			if (_a->isEnd())
				goto ended;
			mOut->fulfill(_a->mArray);
			if (_a->mArray->size()) {
				_b = _a->mArray->v()[_a->mArray->size() - 1];
			}
			_a = _a->next();
		} else {
ended:
			_a = nullptr;
			V* out = mOut->fulfill(mBlockSize);
			for (int i = 0; i < mBlockSize; ++i) {
				out[i] = _b;
			}
		}
		mOut = mOut->nextp();
	}
};

struct Hangz : Gen
{
	P<List> _a;
	Z _b;

	Hangz(Thread& th, P<List> const& a) : Gen(th, itemTypeZ, false), _a(a) {}
	virtual const char* TypeName() const override { return "Hangz"; }

	virtual void pull(Thread& th) override
	{
		if (_a) {
			_a->force(th);
			if (_a->isEnd())
				goto ended;
			mOut->fulfillz(_a->mArray);
			if (_a->mArray->size()) {
				_b = _a->mArray->z()[_a->mArray->size() - 1];
			}
			_a = _a->next();
		} else {
ended:
			_a = nullptr;
			Z* out = mOut->fulfillz(mBlockSize);
			for (int i = 0; i < mBlockSize; ++i) {
				out[i] = _b;
			}
		}
		mOut = mOut->nextp();
	}
};

static void hang_(Thread& th, Prim* prim)
{
	P<List> a = th.popList("hang : a");
	
	if (a->isV())
		th.push(new List(new Hang(th, a)));
	else
		th.push(new List(new Hangz(th, a)));
}

static void hangz_(Thread& th, Prim* prim)
{
	P<List> a = th.popZList("hangz : a");
	
	th.push(new List(new Hangz(th, a)));
}


static void histo_(Thread& th, Prim* prim)
{
	int64_t n = th.popInt("histo : n");
	P<List> a = th.popList("histo : list");

	if (!a->isFinite()) {
		indefiniteOp("histo : list", "");
	}
	
	a = a->pack(th);
	
	int64_t size = a->mArray->size();
	
	P<List>outList = new List(itemTypeZ, n);
	outList->mArray->setSize(n);
	Z* out = outList->mArray->z();
	memset(out, 0, sizeof(Z) * n);
	
	Z n1 = n - 1;
	if (a->isZ()) {
		Z* in = a->mArray->z();
		
		for (int64_t i = 0; i < size; ++i) {
			int64_t j = (int64_t)std::clamp(in[i], 0., n1);
			out[j] += 1.;
		}
	} else {
		V* in = a->mArray->v();
		for (int64_t i = 0; i < size; ++i) {
			int64_t j = (int64_t)std::clamp(in[i].asFloat(), 0., n1);
			out[j] += 1.;
		}
	}
	
	th.push(outList);
	
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma mark MAP FILTER REDUCE


struct Stutter : Gen
{
	VIn _a;
	BothIn _b;
	int n_;
	V aa_;
	
	Stutter(Thread& th, Arg a, Arg b) : Gen(th, itemTypeV, mostFinite(a,b)), _a(a), _b(b), n_(0) {}

	virtual const char* TypeName() const override { return "Stutter"; }
    	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		while (framesToFill) {			
			if (n_) {
				int n = std::min(n_, framesToFill);
				for (int i = 0; i < n; ++i) {
					out[i] = aa_;
				}
				framesToFill -= n;
				n_ -= n;
				out += n;
				
				if (framesToFill == 0) break;
			}
			V b;
			if (_a.one(th, aa_) || _b.one(th, b)) {
				setDone();
				break;
			} else {
				if (b.isFunOrPrim()) {
					SaveStack ss(th);
					th.push(aa_);
					try {
						b.apply(th);
					} catch (...) {
						setDone();
						produce(framesToFill);
						throw;
					}
					b = th.pop();
				} 				
				n_ = b.asFloat();
			}
		}
		produce(framesToFill);
	}
};

struct Stutterz : Gen
{
	ZIn _a;
	BothIn _b;
	int n_;
	Z aa_;
	
	Stutterz(Thread& th, Arg a, Arg b) : Gen(th, itemTypeZ, mostFinite(a,b)), _a(a), _b(b), n_(0) {}

	virtual const char* TypeName() const override { return "Stutterz"; }
    	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		Z* out = mOut->fulfillz(framesToFill);
		Z a = aa_;
		while (framesToFill) {		
			if (n_) {
				int n = std::min(n_, framesToFill);
				for (int i = 0; i < n; ++i) {
					out[i] = a;
				}
				framesToFill -= n;
				n_ -= n;
				out += n;
				
				if (framesToFill == 0) break;
			}
			V b;
			if (_a.onez(th, a) || _b.one(th, b)) {
				setDone();
				break;
			} else {
				if (b.isFunOrPrim()) {
					SaveStack ss(th);
					th.push(a);
					try {
						b.apply(th);
					} catch (...) {
						setDone();
						produce(framesToFill);
						throw;
					}
					b = th.pop();
				} 				
				n_ = b.asFloat();
			}
		}
		aa_ = a;
		produce(framesToFill);
	}
};


static void filter_(Thread& th, Prim* prim)
{
	V b = th.pop();
	V a = th.popList("? : a");

	if (a.isVList()) {
		th.push(new List(new Stutter(th, a, b)));
	} else {
		th.push(new List(new Stutterz(th, a, b)));
	}
}


struct Change : Gen
{
	VIn _a;
	V _prev;
	
	Change(Thread& th, Arg a) : Gen(th, itemTypeV, a.isFinite()), _a(a), _prev(12347918239.19798729839470170) {}
    
	virtual const char* TypeName() const override { return "Change"; }
    
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
        V a;
        for (int i = 0; framesToFill; ) {
            if (_a.one(th, a)) {
                setDone();
                break;
            }
            if (!a.Equals(th, _prev)) {
                out[i++] = a;
                --framesToFill;
                _prev = a;
            }
		}
		produce(framesToFill);
	}
};

struct Changez : Gen
{
	ZIn _a;
	Z _prev;
	
	Changez(Thread& th, Arg a) : Gen(th, itemTypeZ, a.isFinite()), _a(a), _prev(12347918239.19798729839470170) {}
    
	virtual const char* TypeName() const override { return "Changez"; }
    
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		Z* out = mOut->fulfillz(framesToFill);
        Z a;
        for (int i = 0; framesToFill; ) {
            if (_a.onez(th, a)) {
                setDone();
                break;
            }
            if (a != _prev) {
                out[i++] = a;
                --framesToFill;
                _prev = a;
            }
		}
		produce(framesToFill);
	}
};

static void change_(Thread& th, Prim* prim)
{
	V a = th.popList("change : a");
    
	if (a.isVList()) {
		th.push(new List(new Change(th, a)));
	} else {
		th.push(new List(new Changez(th, a)));
	}
}

static void changez_(Thread& th, Prim* prim)
{
	V a = th.popZList("change : a");
    
    th.push(new List(new Changez(th, a)));
}

struct Spread : Gen
{
	VIn _a;
	BothIn _b;
	int n_;
	V aa_;
	
	Spread(Thread& th, Arg a, Arg b) : Gen(th, itemTypeV, mostFinite(a,b)), _a(a), _b(b), n_(0) {}
    
	virtual const char* TypeName() const override { return "Spread"; }
    
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		V a = aa_;
		while (framesToFill) {			
			if (n_) {
				int n = std::min(n_, framesToFill);
				for (int i = 0; i < n; ++i) {
					out[i] = 0.;
				}
				framesToFill -= n;
				n_ -= n;
				out += n;
				
				if (framesToFill == 0) break;
			}
			V b;
			if (_a.one(th, a) || _b.one(th, b)) {
				setDone();
				break;
			} else {
				if (b.isFunOrPrim()) {
					SaveStack ss(th);
					th.push(a);
					try {
						b.apply(th);
					} catch (...) {
						setDone();
						produce(framesToFill);
						throw;
					}
					b = th.pop();
				} 				
				n_ = b.asFloat();
                *out++ = a;
                --framesToFill;
			}
		}
		produce(framesToFill);
	}
};

struct Spreadz : Gen
{
	ZIn _a;
	BothIn _b;
	int n_;
	
	Spreadz(Thread& th, Arg a, Arg b) : Gen(th, itemTypeZ, mostFinite(a,b)), _a(a), _b(b), n_(0) {}
    
	virtual const char* TypeName() const override { return "Spreadz"; }
    
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		Z* out = mOut->fulfillz(framesToFill);
		Z a;
		while (framesToFill) {			
			if (n_) {
				int n = std::min(n_, framesToFill);
				for (int i = 0; i < n; ++i) {
					out[i] = 0.;
				}
				framesToFill -= n;
				n_ -= n;
				out += n;
				
				if (framesToFill == 0) break;
			}
			V b;
			if (_a.onez(th, a) || _b.one(th, b)) {
				setDone();
				break;
			} else {
				if (b.isFunOrPrim()) {
					SaveStack ss(th);
					th.push(a);
					try {
						b.apply(th);
					} catch (...) {
						setDone();
						produce(framesToFill);
						throw;
					}
					b = th.pop();
				} 				
				n_ = b.asFloat();
                
                *out++ = a;
                --framesToFill;
			}
		}
		produce(framesToFill);
	}
};

static void spread_(Thread& th, Prim* prim)
{
	V b = th.pop();
	V a = th.pop();
    
	if (a.isVList()) {
		th.push(new List(new Spread(th, a, b)));
	} else {
		th.push(new List(new Spreadz(th, a, b)));
	}
}

static void spreadz_(Thread& th, Prim* prim)
{
	V b = th.popZIn("spreadz : b");
	V a = th.popZIn("spreadz : a");
    
    th.push(new List(new Spreadz(th, a, b)));
}

struct Expand : Gen
{
	VIn _a;
	BothIn _b;
	
	Expand(Thread& th, Arg a, Arg b) : Gen(th, itemTypeV, a.isFinite()), _a(a), _b(b) {}
    
	virtual const char* TypeName() const override { return "Expand"; }
    
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
        for (int i = 0; framesToFill; ) {
			V b;
			if (_b.one(th, b)) {
				setDone();
				break;
			} else {
				if (b.isTrue()) {
					V a;
					if (_a.one(th, a)) {
						setDone();
						break;
					}
					out[i++] = a;
					--framesToFill;
				} else {
					out[i++] = 0.;
					--framesToFill;
				}
			}
		}
		produce(framesToFill);
	}
};

struct Expandz : Gen
{
	ZIn _a;
	BothIn _b;
	
	Expandz(Thread& th, Arg a, Arg b) : Gen(th, itemTypeZ, a.isFinite()), _a(a), _b(b) {}
    
	virtual const char* TypeName() const override { return "Expandz"; }
    
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		Z* out = mOut->fulfillz(framesToFill);
        for (int i = 0; framesToFill; ) {
			V b;
			if (_b.one(th, b)) {
				setDone();
				break;
			} else {
				if (b.isTrue()) {
					Z a;
					if (_a.onez(th, a)) {
						setDone();
						break;
					}
					out[i++] = a;
					--framesToFill;
				} else {
					out[i++] = 0.;
					--framesToFill;
				}
			}
		}
		produce(framesToFill);
	}
};

static void expand_(Thread& th, Prim* prim)
{
	V b = th.pop();
	V a = th.pop();
    
	if (a.isVList()) {
		th.push(new List(new Expand(th, a, b)));
	} else {
		th.push(new List(new Expandz(th, a, b)));
	}
}

static void expandz_(Thread& th, Prim* prim)
{
	V b = th.popZIn("expandz : b");
	V a = th.popZIn("expandz : a");
    
    th.push(new List(new Expandz(th, a, b)));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Clump : Gen
{
	VIn _a;
	BothIn _b;
	
	Clump(Thread& th, Arg a, Arg b) : Gen(th, itemTypeV, a.isFinite()), _a(a), _b(b) {}
    
	virtual const char* TypeName() const override { return "Clump"; }
    
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		while (framesToFill) {			
			V b;
			if (_b.one(th, b)) {
				setDone();
				break;
			}
			int64_t n = b.asFloat();

			P<List> list = new List(itemTypeV, 1);
			for (int i = 0; i < n; ++i) {
				V a;
				if (_a.one(th, a)) {
					setDone();
					goto leave;
				}
				list->add(a);
			}
			
			*out++ = list;
			--framesToFill;
		}
leave:
		produce(framesToFill);
	}
};

struct Clumpz : Gen
{
	ZIn _a;
	BothIn _b;
	
	Clumpz(Thread& th, Arg a, Arg b) : Gen(th, itemTypeV, a.isFinite()), _a(a), _b(b) {}
    
	virtual const char* TypeName() const override { return "Clumpz"; }
    
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		while (framesToFill) {			
			V b;
			if (_b.one(th, b)) {
				setDone();
				break;
			}
			int64_t n = b.asFloat();

			P<List> list = new List(itemTypeZ, 1);
			for (int i = 0; i < n; ++i) {
				Z a;
				if (_a.onez(th, a)) {
					setDone();
					goto leave;
				}
				list->addz(a);
			}
			
			*out++ = list;
			--framesToFill;
		}
leave:
		produce(framesToFill);
	}
};

static void clump_(Thread& th, Prim* prim)
{
	V b = th.pop();
	V a = th.pop();
    
	if (a.isVList()) {
		th.push(new List(new Clump(th, a, b)));
	} else {
		th.push(new List(new Clumpz(th, a, b)));
	}
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShortAs : public Gen
{
	VIn a_;
	VIn b_;
public:
	
	ShortAs(Thread& th, Arg a, Arg b) : Gen(th, itemTypeV, mostFinite(a, b)), a_(a), b_(b) {}

	virtual const char* TypeName() const override { return "ShortAs"; }
	
	void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int astride, bstride;
			V *a, *b;
			if (a_(th, n, astride, a) || b_(th, n, bstride, b)) {
				setDone();
				break;
			}
			
			for (int i = 0; i < n; ++i) {
				out[i] = *a;
				a += astride;
			}
			out += n;
			framesToFill -= n;
			a_.advance(n);
			b_.advance(n);
		}
		produce(framesToFill);
	}
	
};

class ShortAsZ : public Gen
{
	ZIn a_;
	ZIn b_;
public:
	
	ShortAsZ(Thread& th, Arg a, Arg b) : Gen(th, itemTypeZ, mostFinite(a, b)), a_(a), b_(b) {}

	virtual const char* TypeName() const override { return "ShortAsZ"; }
	
	void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		Z* out = mOut->fulfillz(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int astride, bstride;
			Z *a, *b;
			if (a_(th, n, astride, a) || b_(th, n, bstride, b)) {
				setDone();
				break;
			}
			
			for (int i = 0; i < n; ++i) {
				out[i] = *a;
				a += astride;
			}
			out += n;
			framesToFill -= n;
			a_.advance(n);
			b_.advance(n);
		}
		produce(framesToFill);
	}
	
};

static void shortas_(Thread& th, Prim* prim)
{
	V b = th.pop();
	V a = th.pop();
    
	if (a.isVList() && b.isVList()) {
		th.push(new List(new ShortAs(th, a, b)));
	} else if (a.isZList() && b.isZList()) {
		th.push(new List(new ShortAsZ(th, a, b)));
	} else {
		wrongType("shortas : a, b must be same type", "two streams or two signals", a);
	}
}

class LongAs : public Gen
{
	VIn a_;
	VIn b_;
	V last;
public:
	
	LongAs(Thread& th, Arg a, Arg b) : Gen(th, itemTypeV, b.isFinite()), a_(a), b_(b) {}

	virtual const char* TypeName() const override { return "LongAs"; }
	
	void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int astride, bstride;
			V *a, *b;
			
			if (b_(th, n, bstride, b)) {
				setDone();
				break;
			}
			
			int n0 = n;
			if (a_(th, n, astride, a)) {
				n = n0;
				for (int i = 0; i < n; ++i) {
					out[i] = last;
				}
			} else {
				last = *(a + (n-1)*astride);
				for (int i = 0; i < n; ++i) {
					out[i] = *a;
					a += astride;
				}
				a_.advance(n);
			}
			out += n;
			framesToFill -= n;
			b_.advance(n);
		}
		produce(framesToFill);
	}
	
};

class LongAsZ : public Gen
{
	ZIn a_;
	ZIn b_;
	Z last;
public:
	
	LongAsZ(Thread& th, Arg a, Arg b) : Gen(th, itemTypeZ, b.isFinite()), a_(a), b_(b) {}

	virtual const char* TypeName() const override { return "LongAsZ"; }
	
	void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		Z* out = mOut->fulfillz(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int astride, bstride;
			Z *a, *b;
						
			if (b_(th, n, bstride, b)) {
				setDone();
				break;
			}
			
			int n0 = n; // should ZIn::operator() return n = 0 if it is at end of stream??
			if (a_(th, n, astride, a)) {
				n = n0;
				for (int i = 0; i < n; ++i) {
					out[i] = last;
				}
			} else {
				last = *(a + (n-1)*astride);
				for (int i = 0; i < n; ++i) {
					out[i] = *a;
					a += astride;
				}
				a_.advance(n);
			}
			out += n;
			framesToFill -= n;
			b_.advance(n);
		}
		produce(framesToFill);
	}
	
};


static void longas_(Thread& th, Prim* prim)
{
	V b = th.pop();
	V a = th.pop();
    
	if (a.isVList() && b.isVList()) {
		th.push(new List(new LongAs(th, a, b)));
	} else if (a.isZList() && b.isZList()) {
		th.push(new List(new LongAsZ(th, a, b)));
	} else {
		wrongType("longas : a, b must be same type", "two streams or two signals", a);
	}
}

class LongAs0 : public Gen
{
	VIn a_;
	VIn b_;
public:
	
	LongAs0(Thread& th, Arg a, Arg b) : Gen(th, itemTypeV, b.isFinite()), a_(a), b_(b) {}

	virtual const char* TypeName() const override { return "LongAs0"; }
	
	void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int astride, bstride;
			V *a, *b;
			
			if (b_(th, n, bstride, b)) {
				setDone();
				break;
			}
			
			int n0 = n;
			if (a_(th, n, astride, a)) {
				n = n0;
				V zero(0.);
				for (int i = 0; i < n; ++i) {
					out[i] = zero;
				}
			} else {
				for (int i = 0; i < n; ++i) {
					out[i] = *a;
					a += astride;
				}
				a_.advance(n);
			}
			out += n;
			framesToFill -= n;
			b_.advance(n);
		}
		produce(framesToFill);
	}
	
};

class LongAs0Z : public Gen
{
	ZIn a_;
	ZIn b_;
public:
	
	LongAs0Z(Thread& th, Arg a, Arg b) : Gen(th, itemTypeZ, b.isFinite()), a_(a), b_(b) {}

	virtual const char* TypeName() const override { return "LongAs0Z"; }
	
	void pull(Thread& th) override
	{
		int framesToFill = mBlockSize;
		Z* out = mOut->fulfillz(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int astride, bstride;
			Z *a, *b;
						
			if (b_(th, n, bstride, b)) {
				setDone();
				break;
			}
			
			int n0 = n; // should ZIn::operator() return n = 0 if it is at end of stream??
			if (a_(th, n, astride, a)) {
				n = n0;
				for (int i = 0; i < n; ++i) {
					out[i] = 0.;
				}
			} else {
				for (int i = 0; i < n; ++i) {
					out[i] = *a;
					a += astride;
				}
				a_.advance(n);
			}
			out += n;
			framesToFill -= n;
			b_.advance(n);
		}
		produce(framesToFill);
	}
	
};


static void longas0_(Thread& th, Prim* prim)
{
	V b = th.pop();
	V a = th.pop();
    
	if (a.isVList() && b.isVList()) {
		th.push(new List(new LongAs0(th, a, b)));
	} else if (a.isZList() && b.isZList()) {
		th.push(new List(new LongAs0Z(th, a, b)));
	} else {
		wrongType("longas0 : a, b must be same type", "two streams or two signals", a);
	}
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Play.hpp"

static void play_(Thread& th, Prim* prim)
{
	V v = th.popList("play : list");
	playWithPlayer(th, v);
}

static void record_(Thread& th, Prim* prim)
{
	V filename = th.pop();
	V v = th.popList("record : list");
	recordWithPlayer(th, v, filename);
}

static void stop_(Thread& th, Prim* prim)
{
	stopPlaying();
}

static void stopDone_(Thread& th, Prim* prim)
{
	stopPlayingIfDone();
}

static void interleave(int stride, int numFrames, double* in, float* out)
{
	for (int f = 0, k = 0; f < numFrames; ++f, k += stride)
		out[k] = in[f];
}

static void deinterleave(int numChans, int numFrames, float* in, double** out)
{
	switch (numChans) {
		case 1 : 
			for (int f = 0, k = 0; f < numFrames; ++f, ++k) {
				out[0][f] = in[k];
			}
			break;
		case 2 :
			for (int f = 0, k = 0; f < numFrames; ++f, k+=2) {
				out[0][f] = in[k  ];
				out[1][f] = in[k+1];
			}
			break;
		case 3 : // e.g. W X Y 
			for (int f = 0, k = 0; f < numFrames; ++f, k+=3) {
				out[0][f] = in[k  ];
				out[1][f] = in[k+1];
				out[2][f] = in[k+2];
			}
			break;
		case 4 :
			for (int f = 0, k = 0; f < numFrames; ++f, k+=4) {
				out[0][f] = in[k  ];
				out[1][f] = in[k+1];
				out[2][f] = in[k+2];
				out[3][f] = in[k+3];
			}
			break;
		case 5 :
			for (int f = 0, k = 0; f < numFrames; ++f, k+=5) {
				out[0][f] = in[k  ];
				out[1][f] = in[k+1];
				out[2][f] = in[k+2];
				out[3][f] = in[k+3];
				out[4][f] = in[k+4];
			}
			break;
		case 6 :
			for (int f = 0, k = 0; f < numFrames; ++f, k+=6) {
				out[0][f] = in[k  ];
				out[1][f] = in[k+1];
				out[2][f] = in[k+2];
				out[3][f] = in[k+3];
				out[4][f] = in[k+4];
				out[5][f] = in[k+5];
			}
			break;
		default :
			for (int f = 0, k = 0; f < numFrames; ++f) {
				for (int c = 0; c < numChans; ++c, ++k) {
					out[c][f] = in[k];
				}
			}
			break;
	}
}

static const size_t gSessionTimeMaxLen = 256;
char gSessionTime[gSessionTimeMaxLen];
#include <time.h>

static void setSessionTime()
{
	time_t t;
	tm tt;
	time(&t);
	localtime_r(&t, &tt);
	snprintf(gSessionTime, gSessionTimeMaxLen, "%04d-%02d%02d-%02d%02d%02d",
		tt.tm_year+1900, tt.tm_mon+1, tt.tm_mday, tt.tm_hour, tt.tm_min, tt.tm_sec);
}


static void sfwrite_(Thread& th, Prim* prim)
{
	
	V filename = th.pop();
	
	V v = th.popList(">sf : channels");
	
	sfwrite(th, v, filename, false);
}

static void sfwriteopen_(Thread& th, Prim* prim)
{
	
	V filename = th.pop();
	
	V v = th.pop();

	sfwrite(th, v, filename, true);
}


static void sfread_(Thread& th, Prim* prim)
{
	
	V filename = th.popString("sf> : filename");
		
	sfread(th, filename, 0, -1);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void bench_(Thread& th, Prim* prim)
{
	ZIn in[kMaxSFChannels];
		
	int numChannels = 0;
	
	V v = th.popList("bench : channels");
		
	if (v.isZList()) {
		if (!v.isFinite()) indefiniteOp(">sf : s - indefinite number of frames", "");
		numChannels = 1;
		in[0].set(v);
	} else {
		if (!v.isFinite()) indefiniteOp(">sf : s - indefinite number of channels", "");
		P<List> s = (List*)v.o();
		s = s->pack(th);
		Array* a = s->mArray();
		numChannels = (int)a->size();
		if (numChannels > kMaxSFChannels)
			throw errOutOfRange;
		
		bool allIndefinite = true;
		for (int i = 0; i < numChannels; ++i) {
			V va = a->at(i);
			if (va.isFinite()) allIndefinite = false;
			in[i].set(va);
			va.o = nullptr;
		}

		s = nullptr;
		a = nullptr;
		
		if (allIndefinite) indefiniteOp(">sf : s - all channels have indefinite number of frames", "");
	}
	v.o = nullptr;

	double t0 = elapsedTime();
	bool done = false;
	int64_t framesFilled = 0;
	while (!done) {
		for (int i = 0; i < numChannels; ++i) {
			int n = kBufSize;
			bool imdone = in[i].bench(th, n);
			if (imdone) done = true;
			framesFilled += n;
		}
	}
	double t1 = elapsedTime();
	
	double secondsOfCPU = t1-t0;
	double secondsOfAudio = (double)framesFilled * th.rate.invSampleRate;
	double percentOfRealtime = 100. * secondsOfCPU / secondsOfAudio;
	
	post("bench:\n");
	post("  %f seconds of audio.\n", secondsOfAudio);
	post("  %f seconds of CPU.\n", secondsOfCPU);
	post("  %f %% of real time.\n", percentOfRealtime);
	
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef SAPF_AUDIOTOOLBOX
#include "Spectrogram.hpp"

std::atomic<int32_t> gSpectrogramFileCount = 0;

static void sgram_(Thread& th, Prim* prim)
{
	V filename = th.pop();
	Z dBfloor = fabs(th.popFloat("sgram : dBfloor"));
	P<List> list = th.popZList("sgram : signal");
		
	if (!list->isFinite()) {
		indefiniteOp("sgram : signal - indefinite number of frames", "");
	}

	char path[1024];
	if (filename.isString()) {
		const char* sgramDir = getenv("SAPF_SPECTROGRAMS");
		if (!sgramDir || strlen(sgramDir)==0) sgramDir = "/tmp";
		snprintf(path, 1024, "%s/%s-%d.jpg", sgramDir, ((String*)filename.o())->s, (int)floor(dBfloor + .5));
	} else {
		int32_t count = ++gSpectrogramFileCount;
		snprintf(path, 1024, "/tmp/sapf-%s-%04d.jpg", gSessionTime, count);
	}


	list = list->pack(th);
	P<Array> array = list->mArray;
	int64_t n = array->size();
	double* z = array->z();
	spectrogram((int)n, z, 3200, 11, path, -dBfloor);
	
	{
		char cmd[1100];
		snprintf(cmd, 1100, "open \"%s\"", path);
		system(cmd);
	}
	
}
#else
// TODO
#endif // SAPF_AUDIOTOOLBOX

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static double bessi0(double x)
{
	//returns the modified Bessel function I_0(x) for any real x
	//from numerical recipes
	
	double ax, ans;
	double y;
	
	if((ax=fabs(x))<3.75){
		y=x/3.75;
		y *= y;
		ans =1.0+y*(3.5156229+y*(3.0899424+y*(1.2067492
			+y*(0.2659732+y*(0.360768e-1+y*0.45813e-2)))));
	}
	else{
		y=3.75/ax;
		ans = (exp(ax)/sqrt(ax))*(0.39894228+y*(0.1328592e-1
			+y*(0.225319e-2+y*(-0.157565e-2+y*(0.916281e-2
			+y*(-0.2057706e-1+y*(0.2635537e-1+y*(-0.1647633e-1
			+y*0.392377e-2))))))));
	}

	return ans;
}

static double kaiser_alpha(double atten)
{
	double alpha = 0.;
	if (atten > 50.) 
		alpha = .1102 * (atten - 8.7);
	else if (atten >= 21.)
		alpha = .5842 * pow(atten - 21., .4) + .07886 * (atten - 21.);
	return alpha;
}

static void kaiser(size_t m, double *s, double alpha)
{
	if (m == 0) return;
	if (m == 1) {
		s[0] = 1.;
		return;
	}
	size_t n = m-1;
	double p = n / 2.;
	double rp = 1. / p;
	double rb = 1. / bessi0(alpha);
	
	for (size_t i = 0; i < m; ++i) {
		double x = (i-p) * rp;
		s[i] = rb * bessi0(alpha * sqrt(1. - x*x));
	}
}

static void kaiser_(Thread& th, Prim* prim)
{
	Z atten = fabs(th.popFloat("kaiser : stopband attenuation"));
	int64_t n = th.popInt("kaiser : n");
	
	P<List> out = new List(itemTypeZ, n);
	out->mArray->setSize(n);
	
	Z alpha = kaiser_alpha(atten);
	kaiser(n, out->mArray->z(), alpha);
	
	th.push(out);
}

#include <iostream>
#ifdef TEST_BUILD
void hanning_(Thread& th, Prim* prim)
#else
static void hanning_(Thread& th, Prim* prim)
#endif
{
	int64_t n = th.popInt("hanning : n");
	
	P<List> out = new List(itemTypeZ, n);
	out->mArray->setSize(n);

#ifdef SAPF_ACCELERATE
	vDSP_hann_windowD(out->mArray->z(), n, 0);
#else
	Eigen::ArrayXd arr = Eigen::ArrayXd::LinSpaced(n, 0, n - 1);
	ZArr outzarr = zarr(out->mArray->z(), n, 1);
	outzarr = 0.5 * (1.0 - (2.0 * M_PI * arr / n).cos());
#endif // SAPF_ACCELERATE
	
	th.push(out);
}

#ifdef TEST_BUILD
void hamming_(Thread& th, Prim* prim)
#else
static void hamming_(Thread& th, Prim* prim)
#endif
{
	int64_t n = th.popInt("hanning : n");
	
	P<List> out = new List(itemTypeZ, n);
	out->mArray->setSize(n);

#ifdef SAPF_ACCELERATE
	vDSP_hamm_windowD(out->mArray->z(), n, 0);
#else
	Eigen::ArrayXd arr = Eigen::ArrayXd::LinSpaced(n, 0, n - 1);
	ZArr outzarr = zarr(out->mArray->z(), n, 1);
	outzarr = 0.54 - .46 * (2.0 * M_PI * arr / n).cos();
#endif // SAPF_ACCELERATE
	
	th.push(out);
}

#ifdef TEST_BUILD
void blackman_(Thread& th, Prim* prim)
#else
static void blackman_(Thread& th, Prim* prim)
#endif
{
	int64_t n = th.popInt("hanning : n");
	
	P<List> out = new List(itemTypeZ, n);
	out->mArray->setSize(n);

#ifdef SAPF_ACCELERATE
	vDSP_blkman_windowD(out->mArray->z(), n, 0);
#else
	Eigen::ArrayXd arr = Eigen::ArrayXd::LinSpaced(n, 0, n - 1);
	ZArr outzarr = zarr(out->mArray->z(), n, 1);
	outzarr = 0.42
		- .5 * (2.0 * M_PI * arr / n).cos()
		+ .08 * (4.0 * M_PI * arr / n).cos();
#endif // SAPF_ACCELERATE
	
	th.push(out);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////

struct Segment : public Gen
{
	ZIn in_;
	BothIn hop_;
	BothIn length_;
	int offset;
    Z fracsamp_;
    Z sr_;
	
	Segment(Thread& th, Arg in, Arg hop, Arg length)
        : Gen(th, itemTypeV, mostFinite(in, hop, length)), in_(in), hop_(hop), length_(length),
        fracsamp_(0.), sr_(th.rate.sampleRate)
	{
	}
	
	virtual const char* TypeName() const override { return "Segment"; }
    
	virtual void pull(Thread& th) override 
	{		
		int framesToFill = mBlockSize;
		int framesFilled = 0;
		V* out = mOut->fulfill(framesToFill);
		for (int i = 0; i < framesToFill; ++i) {
			Z zlength, zhop;
			if (length_.onez(th, zlength)) {
				setDone();
				goto leave;
			}
			
			int length = (int)floor(sr_ * zlength + .5);
			P<List> segment = new List(itemTypeZ, length);
			segment->mArray->setSize(length);
			bool nomore = in_.fillSegment(th, length, segment->mArray->z());
			out[i] = segment;
			++framesFilled;
			if (nomore) {
				setDone();
				goto leave;
			}
			
			if (hop_.onez(th, zhop)) {
				setDone();
				goto leave;
			}
            
            Z fhop = sr_ * zhop + fracsamp_;
            Z ihop = floor(fhop);
            fracsamp_ = fhop - ihop;
            
			in_.hop(th, (int)ihop);
		}
	leave:
		produce(framesToFill - framesFilled);
	}
	
};

static void seg_(Thread& th, Prim* prim)
{
	V length = th.pop();
	V hop = th.pop();
	V in = th.popZIn("segment : in");
    
	th.push(new List(new Segment(th, in, hop, length)));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

// separated out for better testability of simd implementation
#ifdef SAPF_ACCELERATE
	#ifdef TEST_BUILD
	    void wseg_apply_window(Z* segbuf, Z* window, int n) {
	#else
	    inline void wseg_apply_window(Z* segbuf, Z* window, int n) {
	#endif
        vDSP_vmulD(segbuf, 1, window, 1, segbuf, 1, n);
    }
#else
	#ifdef TEST_BUILD
	    void wseg_apply_window(Z* segbuf, ZArr window, int n) {
	#else
	    inline void wseg_apply_window(Z* segbuf, ZArr window, int n) {
	#endif
        ZArr segbufarr = zarr(segbuf, 1, n);
		segbufarr = segbufarr * window;
    }
#endif

struct WinSegment : public Gen
{
	ZIn in_;
	BothIn hop_;
	P<Array> window_;
#ifndef SAPF_ACCELERATE
	ZArr windowzarr_;
#endif
    int length_;
	int offset;
    Z fracsamp_;
    Z sr_;
	
	WinSegment(Thread& th, Arg in, Arg hop, P<Array> const& window)
        : Gen(th, itemTypeV, mostFinite(in, hop)), in_(in), hop_(hop), window_(window),
#ifndef SAPF_ACCELERATE
			windowzarr_(zarr(window_->z(), 1, (int)window_->size())),
#endif
            length_((int)window_->size()),
        fracsamp_(0.), sr_(th.rate.sampleRate)
	{
	}
	
	virtual const char* TypeName() const override { return "WinSegment"; }
    
	virtual void pull(Thread& th) override 
	{		
		int framesToFill = mBlockSize;
		int framesFilled = 0;
		V* out = mOut->fulfill(framesToFill);
		for (int i = 0; i < framesToFill; ++i) {
			Z zhop;
			
			P<List> segment = new List(itemTypeZ, length_);
			segment->mArray->setSize(length_);
            Z* segbuf = segment->mArray->z();
			bool nomore = in_.fillSegment(th, (int)length_, segbuf);
#ifdef SAPF_ACCELERATE
            wseg_apply_window(segbuf, window_->z(), length_);
#else
            wseg_apply_window(segbuf, windowzarr_, length_);
#endif // SAPF_ACCELERATE
			out[i] = segment;
			++framesFilled;
			if (nomore) {
				setDone();
				goto leave;
			}
			
			if (hop_.onez(th, zhop)) {
				setDone();
				goto leave;
			}

            Z fhop = sr_ * zhop + fracsamp_;
            Z ihop = floor(fhop);
            fracsamp_ = fhop - ihop;
            
			in_.hop(th, (int)ihop);
		}
	leave:
		produce(framesToFill - framesFilled);
	}
	
};

static void wseg_(Thread& th, Prim* prim)
{
	P<List> window = th.popZList("wseg : window");
	V hop = th.pop();
	V in = th.popZIn("segment : in");
    
    window = window->pack(th);
    
	th.push(new List(new WinSegment(th, in, hop, window->mArray)));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



static void fft_(Thread& th, Prim* prim)
{
	P<List> inImag = th.popZList("fft : imag");
	P<List> inReal = th.popZList("fft : real");
	
	if (!inReal->isFinite())
		indefiniteOp("fft : real", "");
		
	if (!inImag->isFinite())
		indefiniteOp("fft : imag", "");

	int n = (int)inReal->length(th);
	int m = (int)inImag->length(th);
	if (n != m) {
		post("fft : real and imag parts are different lengths.\n");
		throw errFailed;
	}
	if (!ISPOWEROFTWO64(n)) {
		post("fft : size is not a power of two.\n");
		throw errFailed;
	}
	
	inReal = inReal->pack(th);
	inImag = inImag->pack(th);
	
	P<List> outReal = new List(itemTypeZ, n);
	P<List> outImag = new List(itemTypeZ, n);
	outReal->mArray->setSize(n);
	outImag->mArray->setSize(n);


	fft(n, inReal->mArray->z(), inImag->mArray->z(), outReal->mArray->z(), outImag->mArray->z());
	
	th.push(outReal);
	th.push(outImag);
}


static void ifft_(Thread& th, Prim* prim)
{
	P<List> inImag = th.popZList("ifft : imag");
	P<List> inReal = th.popZList("ifft : real");
	
	if (!inReal->isFinite())
		indefiniteOp("ifft : real", "");
		
	if (!inImag->isFinite())
		indefiniteOp("ifft : imag", "");

	int n = (int)inReal->length(th);
	int m = (int)inImag->length(th);
	if (n != m) {
		post("ifft : real and imag parts are different lengths.\n");
		throw errFailed;
	}
	if (!ISPOWEROFTWO64(n)) {
		post("ifft : size is not a power of two.\n");
		throw errFailed;
	}
	
	inReal = inReal->pack(th);
	inImag = inImag->pack(th);
	
	P<List> outReal = new List(itemTypeZ, n);
	P<List> outImag = new List(itemTypeZ, n);
	outReal->mArray->setSize(n);
	outImag->mArray->setSize(n);

	ifft(n, inReal->mArray->z(), inImag->mArray->z(), outReal->mArray->z(), outImag->mArray->z());
	
	th.push(outReal);
	th.push(outImag);
}

struct Add : Gen
{
	P<List> _a;
	V _b;

	Add(Thread& th, P<List> const& a, Arg b) : Gen(th, itemTypeV, a->isFinite()), _a(a), _b(b) {}
	virtual const char* TypeName() const override { return "Add"; }

	virtual void pull(Thread& th) override
	{
		if (_a) {
			_a->force(th);
			if (_a->isEnd())
				goto ended;
			mOut->fulfill(_a->mArray);
			_a = _a->next();
		} else {
ended:
			V* out = mOut->fulfill(1);
			out[0] = _b;
			setDone();
		}
		mOut = mOut->nextp();
	}
};

struct Addz : Gen
{
	P<List> _a;
	Z _b;

	Addz(Thread& th, P<List> const& a, Z b) : Gen(th, itemTypeZ, a->isFinite()), _a(a), _b(b) {}
	virtual const char* TypeName() const override { return "Addz"; }

	virtual void pull(Thread& th) override
	{
		if (_a) {
			_a->force(th);
			if (_a->isEnd())
				goto ended;
			mOut->fulfillz(_a->mArray);
			_a = _a->next();
		} else {
ended:
			Z* out = mOut->fulfillz(1);
			out[0] = _b;
			setDone();
		}
		mOut = mOut->nextp();
	}
};

static void add_(Thread& th, Prim* prim)
{
	V item = th.pop();
	P<List> list = th.popList("add : list");
	if (list->isZ()) {
		th.push(new List(new Addz(th, list, item.asFloat())));
	} else {
		th.push(new List(new Add(th, list, item)));
	}
}

static void empty_(Thread& th, Prim* prim)
{
	P<List> list = th.popList("head : list");
	list->force(th);
	
	P<Array> array = list->mArray;
	int64_t size = array->size();
	th.pushBool(size==0);
}

static void nonempty_(Thread& th, Prim* prim)
{
	P<List> list = th.popList("head : list");
	list->force(th);
	
	P<Array> array = list->mArray;
	int64_t size = array->size();
	th.pushBool(size!=0);
}

static void head_(Thread& th, Prim* prim)
{
	P<List> list = th.popList("head : list");
	list->force(th);
	
	BothIn in(list);
	V v;
	if (in.one(th, v)) {
		throw errOutOfRange;
	}
	
	th.push(v);
}

static void tail_(Thread& th, Prim* prim)
{
	P<List> list = th.popList("tail : list");
	skip_positive_(th, list, 1);
	th.push(list);
}

static void uncons_(Thread& th, Prim* prim)
{
	P<List> list = th.popList("tail : list");
	list->force(th);

	BothIn in(list);
	V head;
	if (in.one(th, head)) {
		throw errOutOfRange;
	}
	
	skip_positive_(th, list, 1);
	
	th.push(list);
	th.push(head);
}

class Cons : public Gen
{
	V fun;
public:
	Cons(Thread& th, Arg inFun) : Gen(th, itemTypeV, false), fun(inFun) {}
	
	virtual const char* TypeName() const override { return "Cons"; }
	
	virtual void pull(Thread& th) override {
		SaveStack ss(th);
		fun.apply(th);
		V v = th.pop();
		if (v.isList()) {
			setDone();
			mOut->link(th, (List*)v.o());
		} else {
			end();
		}
	}
};

static V cons(Thread& th, Arg head, Arg tail)
{
	if (tail.isFunOrPrim()) {
		P<Array> array = new Array(itemTypeV, 1);
		array->add(head);
		return new List(array, new List(new Cons(th, tail)));
	} else if (tail.isList()) {
	
		P<List> list = (List*)tail.o();
		
		list->force(th);
			
		int64_t size = list->mArray->size();

		P<Array> array = list->mArray;
		P<Array> newArray = new Array(list->ItemType(), size+1);

		P<List> newList = new List(newArray, list->next());

		newArray->add(head);

		if (list->isZ()) {
			for (int i = 0; i < size; ++i) {
				Z z = array->atz(i);
				newArray->add(z);
			}
		} else {
			for (int i = 0; i < size; ++i) {
				V v = array->at(i);
				newArray->add(v);
			}
		}

		return newList;
	} else {
		wrongType("cons : list", "List or Fun", tail);
	}
}

static void cons_(Thread& th, Prim* prim)
{
	V head = th.pop();
	V tail = th.pop();
	
	th.push(cons(th, head, tail));
}

static void pack_(Thread& th, Prim* prim)
{
	P<List> list = th.popList("pack : list");
	th.push(list->pack(th));
}

static void packed_(Thread& th, Prim* prim)
{
	P<List> list = th.popList("packed : list");
	th.pushBool(list->isPacked());
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct Scan : Gen
{
	VIn list_;
    V fun_;
    V val_;
    
	Scan(Thread& th, Arg list, Arg fun, Arg val) : Gen(th, itemTypeV, list.isFinite()), list_(list), fun_(fun), val_(val) {}
	    
	virtual const char* TypeName() const override { return "Scan"; }
	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		while (framesToFill) {
			V* in;
			int instride;
			int n = framesToFill;
			if (list_(th, n, instride, in)) {
				setDone();
				break;
			}
			for (int i = 0; i < n; ++i) {
				SaveStack ss(th);
				th.push(*in);
				th.push(val_);
				fun_.apply(th);
				val_ = th.pop();
				out[i] = val_;
				in += instride;
			}
			framesToFill -= n;
			out += n;
			list_.advance(n);
		}
		produce(framesToFill);
	}
};


static void scan_(Thread& th, Prim* prim)
{
	V fun = th.pop();
	V value = th.pop();
	V list = th.pop();
	th.push(new List(new Scan(th, list, fun, value)));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct Scan1 : Gen
{
	VIn list_;
    V fun_;
    V val_;
	bool once_;
    
	Scan1(Thread& th, Arg list, Arg fun) : Gen(th, itemTypeV, list.isFinite()), list_(list), fun_(fun), once_(true) {}
	    
	virtual const char* TypeName() const override { return "Scan"; }
	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		if (once_) {
			once_ = false;
			if (list_.one(th, val_)) {
				setDone();
				produce(framesToFill);
				return;
			}
			*out++ = val_;
			--framesToFill;
		}
		while (framesToFill) {
			V* in;
			int instride;
			int n = framesToFill;
			if (list_(th, n, instride, in)) {
				setDone();
				break;
			}
			for (int i = 0; i < n; ++i) {
				SaveStack ss(th);
				th.push(*in);
				th.push(val_);
				fun_.apply(th);
				val_ = th.pop();
				out[i] = val_;
				in += instride;
			}
			framesToFill -= n;
			out += n;
			list_.advance(n);
		}
		produce(framesToFill);
	}
};


static void scan1_(Thread& th, Prim* prim)
{
	V fun = th.pop();
	V list = th.pop();
	th.push(new List(new Scan1(th, list, fun)));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct Iter : Gen
{
    V fun_;
    V val_;
    Z index = 0.;
	
	Iter(Thread& th, Arg fun, Arg val) : Gen(th, itemTypeV, true), fun_(fun), val_(val) {}
	    
	virtual const char* TypeName() const override { return "Iter"; }
	
	virtual void pull(Thread& th) override {
		int n = mBlockSize;
		V* out = mOut->fulfill(n);
		for (int i = 0; i < n; ++i) {
			out[i] = val_;
			SaveStack ss(th);
			th.push(val_);
			if (fun_.takes() == 2) {
				th.push(index);
				index += 1.;
			}
			fun_.apply(th);
			val_ = th.pop();
		}
		mOut = mOut->nextp();
	}
};

struct NIter : Gen
{
    V fun_;
    V val_;
	int64_t n_;
    Z index = 0.;
    
	NIter(Thread& th, Arg fun, Arg val, int64_t n) : Gen(th, itemTypeV, false), fun_(fun), val_(val), n_(n) {}
	    
	virtual const char* TypeName() const override { return "NIter"; }
	
	virtual void pull(Thread& th) override {
		if (n_ <= 0) {
			end();
			return;
		}
		
		int n = (int)std::min(n_, (int64_t)mBlockSize);
		V* out = mOut->fulfill(n);
		for (int i = 0; i < n; ++i) {
			out[i] = val_;
			SaveStack ss(th);
			th.push(val_);
			if (fun_.takes() == 2) {
				th.push(index);
				index += 1.;
			}
			fun_.apply(th);
			val_ = th.pop();
		}
		n_ -= n;
		mOut = mOut->nextp();
	}
};

static void iter_(Thread& th, Prim* prim)
{
	V fun = th.pop();
	V value = th.pop();
	
	th.push(new List(new Iter(th, fun, value)));
}

static void itern_(Thread& th, Prim* prim)
{
	int64_t n = th.popInt("itern : n");
	V fun = th.pop();
	V value = th.pop();
	
	th.push(new List(new NIter(th, fun, value, n)));
}



static void reduce_(Thread& th, Prim* prim)
{
	V fun = th.pop();
	V value = th.pop();
	P<List> list = th.popList("reduce : list");
	if (!list->isFinite()) {
		indefiniteOp("reduce : list", "");
	}

	BothIn in_(list);
	while (true) {
		V in;
		if (in_.one(th, in)) {
			th.push(value);
			return;
		}
		
		SaveStack ss(th);
		th.push(in);
		th.push(value);
		fun.apply(th);
		value = th.pop();
	}
}


static void reduce1_(Thread& th, Prim* prim)
{
	V fun = th.pop();

	P<List> list = th.popList("reduce : list");
	if (!list->isFinite()) {
		indefiniteOp("reduce : list", "");
	}

	BothIn in_(list);
	V value;
	if (in_.one(th, value)) {
		th.push(value);
		return;
	}
	
	while (true) {
		V in;
		if (in_.one(th, in)) {
			th.push(value);
			return;
		}
		
		SaveStack ss(th);
		th.push(in);
		th.push(value);
		fun.apply(th);
		value = th.pop();
	}
}

static void chain_(Thread& th, Prim* prim)
{
	int64_t n = th.popInt("chain : n");
	V fun = th.pop();
	V value = th.pop();

	bool pushIndex = fun.takes() == 2;
	
	for (int64_t i = 0; i < n; ++i) {		
		SaveStack ss(th);
		th.push(value);
		if (pushIndex) th.push(i);
		fun.apply(th);
		value = th.pop();
	}
	th.push(value);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Merge : Gen
{
	VIn a_;
	VIn b_;
    V fun_;
	V aa, bb;
	bool flag = true;
	bool once = true;
    
	Merge(Thread& th, Arg a, Arg b, Arg fun) : Gen(th, itemTypeV, leastFinite(a, b)), a_(a), b_(b), fun_(fun) {}
	    
	virtual const char* TypeName() const override { return "Merge"; }
	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		for (int i = 0; i < framesToFill; ++i) {
			SaveStack ss(th);
			if (once) {
				once = false;
				if (a_.one(th, aa)) {
					produce(framesToFill);
					b_.link(th, mOut);
					setDone();
					return;
				}
				if (b_.one(th, bb)) {
					out[i] = aa;
					produce(framesToFill - i - 1);
					a_.link(th, mOut);
					setDone();
					return;
				}
			} else if (flag) {
				// last produced was a.
				if (a_.one(th, aa)) {
					out[i] = bb;
					produce(framesToFill - i - 1);
					b_.link(th, mOut);
					setDone();
					return;
				}
			} else {
				// last produced was b.
				if (b_.one(th, bb)) {
					out[i] = aa;
					produce(framesToFill - i - 1);
					a_.link(th, mOut);
					setDone();
					return;
				}
			}
			th.push(aa);
			th.push(bb);
			fun_.apply(th);
			flag = th.pop().isTrue();
			if (flag) {
				out[i] = aa;
				aa = 0.;
			} else {
				out[i] = bb;
				bb = 0.;
			}
		}
		produce(0);
	}
};
	    
struct MergeZ : Gen
{
	ZIn a_;
	ZIn b_;
    V fun_;
	Z aa, bb;
	bool flag = true;
	bool once = true;
    
	MergeZ(Thread& th, Arg a, Arg b, Arg fun) : Gen(th, itemTypeZ, leastFinite(a, b)), a_(a), b_(b), fun_(fun) {}
	    
	virtual const char* TypeName() const override { return "MergeZ"; }
	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		Z* out = mOut->fulfillz(framesToFill);
		for (int i = 0; i < framesToFill; ++i) {
			SaveStack ss(th);
			if (once) {
				once = false;
				if (a_.onez(th, aa)) {
					produce(framesToFill);
					b_.link(th, mOut);
					setDone();
					return;
				}
				if (b_.onez(th, bb)) {
					out[i] = aa;
					produce(framesToFill - i - 1);
					a_.link(th, mOut);
					setDone();
					return;
				}
			} else if (flag) {
				// last produced was a.
				if (a_.onez(th, aa)) {
					out[i] = bb;
					produce(framesToFill - i - 1);
					b_.link(th, mOut);
					setDone();
					return;
				}
			} else {
				// last produced was b.
				if (b_.onez(th, bb)) {
					out[i] = aa;
					produce(framesToFill - i - 1);
					a_.link(th, mOut);
					setDone();
					return;
				}
			}
			th.push(aa);
			th.push(bb);
			fun_.apply(th);
			flag = th.pop().isTrue();
			if (flag) {
				out[i] = aa;
				aa = 0.;
			} else {
				out[i] = bb;
				bb = 0.;
			}
		}
		produce(0);
	}
};

struct MergeByKey : Gen
{
	VIn a_;
	VIn b_;
    V key_;
	V aa, bb;
	bool flag = true;
	bool once = true;
    
	MergeByKey(Thread& th, Arg a, Arg b, Arg key) : Gen(th, itemTypeV, leastFinite(a, b)), a_(a), b_(b), key_(key) {}
	    
	virtual const char* TypeName() const override { return "MergeByKey"; }
	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		for (int i = 0; i < framesToFill; ++i) {
			SaveStack ss(th);
			if (once) {
				once = false;
				if (a_.one(th, aa)) {
					produce(framesToFill);
					b_.link(th, mOut);
					setDone();
					return;
				}
				if (b_.one(th, bb)) {
					out[i] = aa;
					produce(framesToFill - i - 1);
					a_.link(th, mOut);
					setDone();
					return;
				}
			} else if (flag) {
				// last produced was a.
				if (a_.one(th, aa)) {
					out[i] = bb;
					produce(framesToFill - i - 1);
					b_.link(th, mOut);
					setDone();
					return;
				}
			} else {
				// last produced was b.
				if (b_.one(th, bb)) {
					out[i] = aa;
					produce(framesToFill - i - 1);
					a_.link(th, mOut);
					setDone();
					return;
				}
			}
			{
				V a, b;
				bool aok = aa.dot(th, key_, a);
				bool bok = bb.dot(th, key_, b);
				if (!aok || !bok) {
					setDone();
					return;
				}
				flag = ::Compare(th, a, b) < 0;
				if (flag) {
					out[i] = aa;
					aa = 0.;
				} else {
					out[i] = bb;
					bb = 0.;
				}
			}
		}
		produce(0);
	}
};

struct MergeCmp : Gen
{
	VIn a_;
	VIn b_;
    V fun_;
	V aa, bb;
	enum { left, right, both };
	int which = both;
    
	MergeCmp(Thread& th, Arg a, Arg b, Arg fun) : Gen(th, itemTypeV, leastFinite(a, b)), a_(a), b_(b), fun_(fun) {}
	    
	virtual const char* TypeName() const override { return "MergeCmp"; }
	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		for (int i = 0; i < framesToFill; ++i) {
			SaveStack ss(th);
			if (which == both) {
				if (a_.one(th, aa)) {
					produce(framesToFill);
					b_.link(th, mOut);
					setDone();
					return;
				}
				if (b_.one(th, bb)) {
					out[i] = aa;
					produce(framesToFill - i - 1);
					a_.link(th, mOut);
					setDone();
					return;
				}
			} else if (which == left) {
				if (a_.one(th, aa)) {
					out[i] = bb;
					produce(framesToFill - i - 1);
					b_.link(th, mOut);
					setDone();
					return;
				}
			} else {
				if (b_.one(th, bb)) {
					out[i] = aa;
					produce(framesToFill - i - 1);
					a_.link(th, mOut);
					setDone();
					return;
				}
			}
			th.push(aa);
			th.push(bb);
			fun_.apply(th);
			Z compare = th.popFloat("mergec : compareValue");
			if (compare < 0.) {
				out[i] = aa;
				aa = 0.;
				which = left;
			} else if (compare == 0.) {
				out[i] = aa;
				aa = 0.;
				bb = 0.;
				which = both;
			} else {
				out[i] = bb;
				bb = 0.;
				which = right;
			}
		}
		produce(0);
	}
};
	    

struct MergeCmpZ : Gen
{
	ZIn a_;
	ZIn b_;
    V fun_;
	Z aa, bb;
	enum { left, right, both };
	int which = both;
    
	MergeCmpZ(Thread& th, Arg a, Arg b, Arg fun) : Gen(th, itemTypeZ, leastFinite(a, b)), a_(a), b_(b), fun_(fun) {}
	    
	virtual const char* TypeName() const override { return "MergeCmpZ"; }
	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		Z* out = mOut->fulfillz(framesToFill);
		for (int i = 0; i < framesToFill; ++i) {
			SaveStack ss(th);
			if (which == both) {
				if (a_.onez(th, aa)) {
					produce(framesToFill);
					b_.link(th, mOut);
					setDone();
					return;
				}
				if (b_.onez(th, bb)) {
					out[i] = aa;
					produce(framesToFill - i - 1);
					a_.link(th, mOut);
					setDone();
					return;
				}
			} else if (which == left) {
				if (a_.onez(th, aa)) {
					out[i] = bb;
					produce(framesToFill - i - 1);
					b_.link(th, mOut);
					setDone();
					return;
				}
			} else {
				if (b_.onez(th, bb)) {
					out[i] = aa;
					produce(framesToFill - i - 1);
					a_.link(th, mOut);
					setDone();
					return;
				}
			}
			th.push(aa);
			th.push(bb);
			fun_.apply(th);
			Z compare = th.popFloat("mergec : compareValue");
			if (compare < 0.) {
				out[i] = aa;
				which = right;
			} else if (compare == 0.) {
				out[i] = aa;
				which = both;
			} else {
				out[i] = bb;
				which = right;
			}
		}
		produce(0);
	}
};
	    


static void merge_(Thread& th, Prim* prim)
{
	V fun = th.pop();
	V b = th.popList("merge : b");
	V a = th.popList("merge : a");
	
	if (a.isZList() && b.isZList()) {
		th.push(new List(new MergeZ(th, a, b, fun)));
	} else if (a.isVList() && b.isVList()) {
		if (fun.isString()) {
			th.push(new List(new MergeByKey(th, a, b, fun)));
		} else {
			th.push(new List(new Merge(th, a, b, fun)));
		}
	} else {
		post("merge : lists not same type\n");
		throw errFailed;
	}
}

static void mergec_(Thread& th, Prim* prim)
{
	V fun = th.pop();
	V b = th.popList("mergec : b");
	V a = th.popList("mergec : a");
	
	if (a.isZList() && b.isZList()) {
		th.push(new List(new MergeCmpZ(th, a, b, fun)));
	} else if (a.isVList() && b.isVList()) {
		th.push(new List(new MergeCmp(th, a, b, fun)));
	} else {
		post("mergec : lists not same type\n");
		throw errFailed;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//evmerge

extern P<String> s_dt;
extern P<String> s_out;
P<String> s_dur;

P<TableMap> dtTableMap;
P<TableMap> restTableMap;

P<Form> extendFormByOne(Thread& th, P<Form> const& parent, P<TableMap> const& tmap, Arg value);

static P<Form> makeRestEvent(Z dt)
{
	P<Table> table = new Table(restTableMap);
	table->put(0, V(0.));
	table->put(1, V(dt));
	table->put(2, V(dt));
	return new Form(table);
}

struct MergeEvents : Gen
{
	VIn a_;
	VIn b_;
	Z nextATime = 0.;
	Z nextBTime;
	
	MergeEvents(Thread& th, Arg a, Arg b, Z t) : Gen(th, itemTypeV, leastFinite(a, b)), a_(a), b_(b), nextBTime(t)
	{
	}
	    
	virtual const char* TypeName() const override { return "MergeEvents"; }
	
	virtual void pull(Thread& th) override {
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		for (int i = 0; i < framesToFill; ++i) {
			SaveStack ss(th);
			{
				if (nextATime <= nextBTime) {
					V aa;
					if (a_.one(th, aa)) {
						if (nextATime < nextBTime) {
							out[i] = makeRestEvent(nextBTime - nextATime);
							produce(framesToFill - i - 1);
						}
						b_.link(th, mOut);
						setDone();
						return;
					}

					V a;
					bool aok = aa.dot(th, s_dt, a);
					if (!aok) {
						setDone();
						return;
					}
					Z dta = a.asFloat();
					Z dt = std::min(dta, nextBTime - nextATime);
					out[i] = extendFormByOne(th, asParent(th, aa), dtTableMap, dt);
					nextATime += dta;
					aa = 0.;
				} else {
					V bb;
					if (b_.one(th, bb)) {
						if (nextBTime < nextATime) {
							out[i] = makeRestEvent(nextATime - nextBTime);
							produce(framesToFill - i - 1);
						}
						a_.link(th, mOut);
						setDone();
						return;
					}
					V b;
					bool bok = bb.dot(th, s_dt, b);
					if (!bok) {
						setDone();
						return;
					}
					Z dtb = b.asFloat();
					Z dt = std::min(dtb, nextATime - nextBTime);
					out[i] = extendFormByOne(th, asParent(th, bb), dtTableMap, dt);
					nextBTime += dtb;
					bb = 0.;
				}
			}
		}
		produce(0);
	}
};

static void evmerge_(Thread& th, Prim* prim)
{
	Z t = th.popFloat("evmerge : t");
	V b = th.popVList("evmerge : b");
	V a = th.popVList("evmerge : a");
	th.push(new List(new MergeEvents(th, a, b, t)));
}

static void evrest_(Thread& th, Prim* prim)
{
	Z t = th.popFloat("evrest : t");
	th.push(makeRestEvent(t));
}

static void evdelay_(Thread& th, Prim* prim)
{
	Z t = th.popFloat("evdelay : t");
	V a = th.popVList("evdelay : a");
	th.push(cons(th, makeRestEvent(t), a));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class CompareFun
{
public:
	CompareFun() {}
	virtual ~CompareFun() {}
	virtual bool operator()(Thread& th, Arg a, Arg b) = 0;
};

class VLess : public CompareFun
{
public:
	VLess() {}
	~VLess() {} 
	virtual bool operator()(Thread& th, Arg a, Arg b) { return Compare(th, a, b) < 0; }
};

class VGreater : public CompareFun
{
public:
	VGreater() {}
	~VGreater() {} 
	virtual bool operator()(Thread& th, Arg a, Arg b) { return Compare(th, a, b) > 0; }
};

class VCompareF : public CompareFun
{
	V fun;
public:
	VCompareF(V inFun) : fun(inFun) {}
	~VCompareF() {}
	virtual bool operator()(Thread& th, Arg a, Arg b) {
		SaveStack ss(th);
		th.push(a);
		th.push(b);
		fun.apply(th);
		return th.pop().isTrue();
	}
};



class ZCompareFun
{
public:
	ZCompareFun() {}
	virtual ~ZCompareFun() {}
	virtual bool operator()(Thread& th, Z a, Z b) = 0;
};

class ZLess : public ZCompareFun
{
public:
	ZLess() {}
	~ZLess() {} 
	virtual bool operator()(Thread& th, Z a, Z b) { return a < b; }
};

class ZCompareF : public ZCompareFun
{
	V fun;
public:
	ZCompareF(V inFun) : fun(inFun) {}
	~ZCompareF() {}
	virtual bool operator()(Thread& th, Z a, Z b) {
		SaveStack ss(th);
		th.push(a);
		th.push(b);
		fun.apply(th);
		return th.pop().isTrue();
	}
};

class ZGreater : public ZCompareFun
{
public:
	ZGreater() {}
	~ZGreater() {} 
	virtual bool operator()(Thread& th, Z a, Z b) { return a > b; }
};


static void merge(Thread& th, int64_t an, V* a, int64_t bn, V* b, V* c, CompareFun* compare)
{
	// merge a and b using scratch space c.
	// copy result back to a.
	// a and b are assumed to be contiguous.
	int64_t ai = 0;
	int64_t bi = 0;
	int64_t ci = 0;
	while (ai < an && bi < bn) {
		if ((*compare)(th, a[ai], b[bi])) {
			c[ci++] = a[ai++];
		} else {
			c[ci++] = b[bi++];
		}
	}
	while (ai < an) {
		c[ci++] = a[ai++];
	}
	while (bi < bn) {
		c[ci++] = b[bi++];
	}
	for (int64_t i = 0; i < ci; ++i) {
		a[i] = c[i];
	}
}

static void merge(Thread& th, int64_t an, Z* a, int64_t bn, Z* b, Z* c, ZCompareFun* compare)
{
	// merge a and b using scratch space c.
	// copy result back to a.
	// a and b are assumed to be contiguous.
	int64_t ai = 0;
	int64_t bi = 0;
	int64_t ci = 0;
	while (ai < an && bi < bn) {
		if ((*compare)(th, a[ai], b[bi])) {
			c[ci++] = a[ai++];
		} else {
			c[ci++] = b[bi++];
		}
	}
	while (ai < an) {
		c[ci++] = a[ai++];
	}
	while (bi < bn) {
		c[ci++] = b[bi++];
	}
	for (int64_t i = 0; i < ci; ++i) {
		a[i] = c[i];
	}
}

static void mergesort(Thread& th, int64_t n, V* a, V* tmp, CompareFun* compare)
{
	if (n == 1) return;
	int64_t an = n / 2;
	int64_t bn = n - an;
	V* b = a + an;
	mergesort(th, an, a, tmp, compare);
	mergesort(th, bn, b, tmp, compare);
	merge(th, an, a, bn, b, tmp, compare);
}

static void mergesort(Thread& th, int64_t n, Z* a, Z* tmp, ZCompareFun* compare)
{
	if (n == 1) return;
	int64_t an = n / 2;
	int64_t bn = n - an;
	Z* b = a + an;
	mergesort(th, an, a, tmp, compare);
	mergesort(th, bn, b, tmp, compare);
	merge(th, an, a, bn, b, tmp, compare);
}

static void sort(Thread& th, int64_t n, const V* in, V* out, CompareFun* compare)
{
	V* tmp = new V[n];
	ArrayDeleter<V> d(tmp);
	
	for (int64_t i = 0; i < n; ++i) out[i] = in[i];
	mergesort(th, n, out, tmp, compare);
}

static void sort(Thread& th, int64_t n, const Z* in, Z* out, ZCompareFun* compare)
{
	Z* tmp = new Z[n];
	ArrayDeleter<Z> d(tmp);
	
	for (int64_t i = 0; i < n; ++i) out[i] = in[i];
	mergesort(th, n, out, tmp, compare);
}

static void merge(Thread& th, int64_t an, V* a, Z* az, int64_t bn, V* b, Z* bz, V* c, Z* cz, CompareFun* compare)
{
	// merge a and b using scratch space c.
	// copy result back to a.
	// a and b are assumed to be contiguous.
	int64_t ai = 0;
	int64_t bi = 0;
	int64_t ci = 0;
	while (ai < an && bi < bn) {
		if ((*compare)(th, a[ai], b[bi])) {
			c[ci] = a[ai];
			cz[ci++] = az[ai++];
		} else {
			c[ci] = b[bi];
			cz[ci++] = bz[bi++];
		}
	}
	while (ai < an) {
		c[ci] = a[ai];
		cz[ci++] = az[ai++];
	}
	while (bi < bn) {
		c[ci] = b[bi];
		cz[ci++] = bz[bi++];
	}
	for (int64_t i = 0; i < ci; ++i) {
		a[i] = c[i];
		az[i] = cz[i];
	}
}

static void merge(Thread& th, int64_t an, Z* a, Z* az, int64_t bn, Z* b, Z* bz, Z* c, Z* cz, ZCompareFun* compare)
{
	// merge a and b using scratch space c.
	// copy result back to a.
	// a and b are assumed to be contiguous.
	int64_t ai = 0;
	int64_t bi = 0;
	int64_t ci = 0;
	while (ai < an && bi < bn) {
		if ((*compare)(th, a[ai], b[bi])) {
			c[ci] = a[ai];
			cz[ci++] = az[ai++];
		} else {
			c[ci] = b[bi];
			cz[ci++] = bz[bi++];
		}
	}
	while (ai < an) {
		c[ci] = a[ai];
		cz[ci++] = az[ai++];
	}
	while (bi < bn) {
		c[ci] = b[bi];
		cz[ci++] = bz[bi++];
	}
	for (int64_t i = 0; i < ci; ++i) {
		a[i] = c[i];
		az[i] = cz[i];
	}
}

static void mergesort(Thread& th, int64_t n, V* a, Z* az, V* c, Z* cz, CompareFun* compare)
{
	if (n == 1) return;
	int64_t an = n / 2;
	int64_t bn = n - an;
	V* b = a + an;
	Z* bz = az + an;
	mergesort(th, an, a, az, c, cz, compare);
	mergesort(th, bn, b, bz, c, cz, compare);
	merge(th, an, a, az, bn, b, bz, c, cz, compare);
}

static void mergesort(Thread& th, int64_t n, Z* a, Z* az, Z* c, Z* cz, ZCompareFun* compare)
{
	if (n == 1) return;
	int64_t an = n / 2;
	int64_t bn = n - an;
	Z* b = a + an;
	Z* bz = az + an;
	mergesort(th, an, a, az, c, cz, compare);
	mergesort(th, bn, b, bz, c, cz, compare);
	merge(th, an, a, az, bn, b, bz, c, cz, compare);
}


static void grade(Thread& th, int64_t n, const V* in, Z* zout, CompareFun* compare)
{
	V* out = new V[n];
	V* tmp = new V[n];
	Z* ztmp = new Z[n];
	ArrayDeleter<V> d1(out);
	ArrayDeleter<V> d2(tmp);
	ArrayDeleter<Z> d3(ztmp);
	
	for (int64_t i = 0; i < n; ++i) out[i] = in[i];
	double z = 0.;
	for (int64_t i = 0; i < n; ++i, z+=1.) zout[i] = z;
	mergesort(th, n, out, zout, tmp, ztmp, compare);
}

static void grade(Thread& th, int64_t n, const Z* in, Z* zout, ZCompareFun* compare)
{
	Z* out = new Z[n];
	Z* tmp = new Z[n];
	Z* ztmp = new Z[n];
	ArrayDeleter<Z> d1(out);
	ArrayDeleter<Z> d2(tmp);
	ArrayDeleter<Z> d3(ztmp);
	
	for (int64_t i = 0; i < n; ++i) out[i] = in[i];
	double z = 0.;
	for (int64_t i = 0; i < n; ++i, z+=1.) zout[i] = z;
	mergesort(th, n, out, zout, tmp, ztmp, compare);
}

static void sort_(Thread& th, Prim* prim)
{
	V a = th.popList("sort : a");
	
	if (!a.isFinite()) 
		indefiniteOp("sort : a", "");
	
	P<List> list = ((List*)a.o())->pack(th);
		
	P<Array> array = list->mArray; 
	int64_t n = array->size();
	
	if (list->isVList()) {
		V* v = array->v();
		VLess cmp;
		
		P<List> out = new List(itemTypeV, n);
		out->mArray->setSize(n);
		V* vout = out->mArray->v();
		
		sort(th, n, v, vout, &cmp);
		th.push(out);
	} else {
		Z* z = array->z();
		ZLess cmp;
		P<List> out = new List(itemTypeZ, n);
		out->mArray->setSize(n);
		Z* zout = out->mArray->z();

		sort(th, n, z, zout, &cmp);
		th.push(out);
	}
}


static void sortf_(Thread& th, Prim* prim)
{
	V fun = th.popList("sort : fun");
	V a = th.popList("sort : a");
	
	if (!a.isFinite()) 
		indefiniteOp("sort : a", "");
	
	P<List> list = ((List*)a.o())->pack(th);
		
	P<Array> array = list->mArray; 
	int64_t n = array->size();
	
	if (list->isVList()) {
		V* v = array->v();
		VCompareF cmp(fun);
		
		P<List> out = new List(list->ItemType(), n);
		out->mArray->setSize(n);
		V* vout = out->mArray->v();
		
		sort(th, n, v, vout, &cmp);
		th.push(out);
	} else {
		Z* z = array->z();
		ZCompareF cmp(fun);
		P<List> out = new List(itemTypeZ, n);
		out->mArray->setSize(n);
		Z* zout = out->mArray->z();

		sort(th, n, z, zout, &cmp);
		th.push(out);
	}
}

static void sort_gt_(Thread& th, Prim* prim)
{
	V a = th.popList("sort> : a");
	
	if (!a.isFinite()) 
		indefiniteOp("sort> : a", "");
	
	P<List> list = ((List*)a.o())->pack(th);
		
	P<Array> array = list->mArray; 
	int64_t n = array->size();
	
	if (list->isVList()) {
		V* v = array->v();
		VGreater cmp;
		
		P<List> out = new List(itemTypeV, n);
		out->mArray->setSize(n);
		V* vout = out->mArray->v();
		
		sort(th, n, v, vout, &cmp);
		th.push(out);
	} else {
		Z* z = array->z();
		ZGreater cmp;
		P<List> out = new List(itemTypeZ, n);
		out->mArray->setSize(n);
		Z* zout = out->mArray->z();

		sort(th, n, z, zout, &cmp);
		th.push(out);
	}
}

static void grade_(Thread& th, Prim* prim)
{
	V a = th.popList("grade : a");
	
	if (!a.isFinite()) 
		indefiniteOp("grade : a", "");
	
	P<List> list = ((List*)a.o())->pack(th);
		
	P<Array> array = list->mArray; 
	int64_t n = array->size();
	
	if (list->isVList()) {
		V* v = array->v();
		VLess cmp;
		
		P<List> out = new List(itemTypeZ, n);
		out->mArray->setSize(n);
		Z* zout = out->mArray->z();
		
		grade(th, n, v, zout, &cmp);
		th.push(out);
	} else {
		Z* z = array->z();
		ZLess cmp;
		P<List> out = new List(itemTypeZ, n);
		out->mArray->setSize(n);
		Z* zout = out->mArray->z();

		grade(th, n, z, zout, &cmp);
		th.push(out);
	}
}


static void gradef_(Thread& th, Prim* prim)
{
	V fun = th.popList("grade : fun");
	V a = th.popList("grade : a");
	
	if (!a.isFinite()) 
		indefiniteOp("grade : a", "");
	
	P<List> list = ((List*)a.o())->pack(th);
		
	P<Array> array = list->mArray; 
	int64_t n = array->size();
	
	if (list->isVList()) {
		V* v = array->v();
		VCompareF cmp(fun);
		
		P<List> out = new List(itemTypeZ, n);
		out->mArray->setSize(n);
		Z* zout = out->mArray->z();
		
		grade(th, n, v, zout, &cmp);
		th.push(out);
	} else {
		Z* z = array->z();
		ZCompareF cmp(fun);
		P<List> out = new List(itemTypeZ, n);
		out->mArray->setSize(n);
		Z* zout = out->mArray->z();

		grade(th, n, z, zout, &cmp);
		th.push(out);
	}
}

static void grade_gt_(Thread& th, Prim* prim)
{
	V a = th.popList("grade> : a");
	
	if (!a.isFinite()) 
		indefiniteOp("grade> : a", "");
	
	P<List> list = ((List*)a.o())->pack(th);
		
	P<Array> array = list->mArray; 
	int64_t n = array->size();
	
	if (list->isVList()) {
		V* v = array->v();
		VGreater cmp;
		
		P<List> out = new List(itemTypeZ, n);
		out->mArray->setSize(n);
		Z* zout = out->mArray->z();
		
		grade(th, n, v, zout, &cmp);
		th.push(out);
	} else {
		Z* z = array->z();
		ZGreater cmp;
		P<List> out = new List(itemTypeZ, n);
		out->mArray->setSize(n);
		Z* zout = out->mArray->z();

		grade(th, n, z, zout, &cmp);
		th.push(out);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma mark ADD STREAM OPS

#define DEF(NAME, TAKES, LEAVES, HELP) 	vm.def(#NAME, TAKES, LEAVES, NAME##_, HELP);
#define DEFMCX(NAME, N, HELP) 	vm.defmcx(#NAME, N, NAME##_, HELP);
#define DEFAM(NAME, MASK, HELP) 	vm.defautomap(#NAME, #MASK, NAME##_, HELP);
#define DEFnoeach(NAME, TAKES, LEAVES, HELP) 	vm.def(#NAME, TAKES, LEAVES, NAME##_, HELP, V(0.), true);
#define DEFN(NAME, TAKES, LEAVES, FUN, HELP) 	vm.def(NAME, TAKES, LEAVES, FUN, HELP);
#define DEFNnoeach(NAME, TAKES, LEAVES, FUN, HELP) 	vm.def(NAME, TAKES, LEAVES, FUN, HELP, V(0.), true);

void AddStreamOps();
void AddStreamOps()
{
	initFFT();
	s_dt  = getsym("dt");
	s_out = getsym("out");
	s_dur = getsym("dur");
	dtTableMap = new TableMap(V(s_dt));
	restTableMap = new TableMap(3);
	restTableMap->put(0, s_out, s_out->Hash());
	restTableMap->put(1, s_dur, s_dur->Hash());
	restTableMap->put(2, s_dt,  s_dt->Hash());
	
	vm.addBifHelp("\n*** list conversion ***");
	DEF(V, 1, 1, "(signal --> stream) converts a signal or string to a stream.")
	DEF(Z, 1, 1, "(series --> signal) converts a stream or string to a signal.")	
	DEF(L, 1, 1, "(anything --> stream) streams are returned as is. anything else is made into an infinite stream of itself.")
	DEF(L1, 1, 1, "(anything --> stream) streams are returned as is. anything else is wrapped in a one item list.")
	DEF(unspell, 1, 1, "(sequence --> string) converts a stream of numbers or a signal to a string.")

	vm.addBifHelp("\n*** basic list operations ***");

	DEF(size, 1, 1, "(seq --> num) Return the length of a sequence if it is finite. Returns inf if the sequence is of indefinite length (It may not actually be infinitely long).")
	DEF(rank, 1, 1, "(a --> n) Return the rank of an object. Makes the assumption that lists at all depths are homogenous.")
	DEF(shape, 1, 1, "(a --> [n..]) Return the shape of an object. Axes of indefinite length are represented by inf. Makes the assumption that lists at all depths are homogenous.")
	DEF(finite, 1, 1, "(seq --> bool) Returns 1 if the sequence is finite, 0 if indefinite.")

	DEF(empty, 1, 1, "(list --> bool) returns whether the list is empty.")
	DEF(nonempty, 1, 1, "(list --> bool) returns whether the list is nonempty.")
	DEF(head, 1, 1, "(list --> item) returns first item of list. fails if list is empty.")
	DEF(tail, 1, 1, "(list --> list) returns the rest of the list after the first item. fails if list is empty.")
	DEF(add, 2, 1, "(list item --> list) returns a new list with the item added to the end.")	
	DEF(cons, 2, 1, "(list item --> list) returns a new list with the item added to the front.")	
	DEF(uncons, 1, 2, "(list --> tail head) returns the tail and head of a list. fails if list is empty.")
	DEF(pack, 1, 1, "(list --> list) returns a packed version of the list.");
	DEF(packed, 1, 1, "(list --> bool) returns whether the list is packed.");

	vm.addBifHelp("\n*** list generation ***");

	DEFnoeach(ord,    0, 1, "(--> series) return an infinite series of integers ascending from 1.")
	DEFnoeach(nat,    0, 1, "(--> series) return an infinite series of integers ascending from 0.")
	DEFnoeach(invs,   0, 1, "(--> series) return an infinite series of reciprocals. equivalent to ord 1/")
	DEFnoeach(negs,   0, 1, "(--> series) return an infinite series of integers descending from -1.")
	DEFnoeach(evens,  0, 1, "(--> series) return an infinite series of ascending non-negative even integers.")
	DEFnoeach(odds,   0, 1, "(--> series) return an infinite series of ascending non-negative odd integers.")
	DEFnoeach(ints,   0, 1, "(--> series) return the infinite series [0 1 -1 2 -2 3 -3...]")
	DEFnoeach(primes, 0, 1, "(--> series) returns a finite series of prime numbers up to 1000039.")
	DEFAM(fib, kk, "(a b --> series) returns a fibonacci series starting with the two numbers given.") 

	DEFnoeach(ordz,   0, 1, "(--> signal) return an infinite signal of integers ascending from 1.")
	DEFnoeach(natz,   0, 1, "(--> signal) return an infinite signal of integers ascending from 0.")
	DEFnoeach(invz,   0, 1, "(--> signal) return an infinite signal of reciprocals. equivalent to ordz 1/")
	DEFnoeach(negz,   0, 1, "(--> signal) return an infinite signal of integers descending from -1.")
	DEFnoeach(evenz,  0, 1, "(--> signal) return an infinite signal of ascending non-negative even integers.")
	DEFnoeach(oddz,   0, 1, "(--> signal) return an infinite signal of ascending non-negative odd integers.")
	DEFnoeach(intz,   0, 1, "(--> signal) return the infinite signal [0 1 -1 2 -2 3 -3...]")
	DEFnoeach(primez, 0, 1, "(--> signal) returns a finite signal of prime numbers up to 1000039.")	
	DEFMCX(fibz, 2, "(a b --> signal) returns a fibonacci signal starting with the two numbers given.")

	DEFAM(ninvs, k, "(n --> stream) return a finite stream of n reciprocals. equivalent to n 1 1 nby 1/")
	DEFMCX(ninvz, 1, "(n --> signal) return a finite signal of n reciprocals. equivalent to n 1 1 nbyz 1/")
	
	DEF(ever, 1, 1, "(value --> series) return an infinite stream of value.")
	DEFAM(by, kk, "(start step --> series) return an infinite arithmetic series.") 
	DEFAM(nby, kkk, "(n start step --> series) return a finite arithmetic series.") 
	DEFAM(grow, kk, "(start step --> series) return an infinite geometric series.") 
	DEFAM(ngrow, kkk, "(n start step --> series) return a finite geometric series.") 
	DEFAM(to, kk, "(a b --> series) return a finite series from a to b stepping by +1 if a < b, or -1 if a < b.") 

	DEFMCX(everz, 1, "(value --> signal) return an infinite signal of value.")
	DEFMCX(byz, 2, "(start step --> series) return an infinite arithmetic series as a signal.") 
	DEFMCX(nbyz, 3, "(n start step --> series) return a finite arithmetic series as a signal.") 
	DEFMCX(growz, 2, "(start step --> series) return an infinite geometric series as a signal.") 
	DEFMCX(ngrowz, 3, "(n start step --> series) return a finite geometric series as a signal.") 
	DEFMCX(toz, 2, "(a b --> series) return a finite signal from a to b stepping by +1 if a < b, or -1 if a < b.") 

	DEFAM(lindiv, kkk, "(n start end --> series) returns a series of n equal steps from start to end.") 
	DEFAM(expdiv, kkk, "(n start end --> series) returns a series of n exponentially spaced steps from start to end.") 
	DEFMCX(lindivz, 3, "(n start end --> series) returns a signal of n equal steps from start to end.") 
	DEFMCX(expdivz, 3, "(n start end --> series)  returns a signal of n exponentially spaced steps from start to end.") 

	DEFAM(lindiv1, kkk, "(n start end --> series) returns a series of n equal steps from start up to but not including end.") 
	DEFAM(expdiv1, kkk, "(n start end --> series) returns a series of n exponentially spaced steps from start up to but not including end.") 
	DEFMCX(lindiv1z, 3, "(n start end --> series) returns a signal of n equal steps from start up to but not including end.") 
	DEFMCX(expdiv1z, 3, "(n start end --> series)  returns a signal of n exponentially spaced steps from start up to but not including end.") 

	DEFMCX(line, 3, "(dur start end --> z) return a signal ramping linearly from start to end in dur seconds.") // mcx
	DEFMCX(xline, 3, "(dur start end --> z) return a signal ramping exponentially from start to end in dur seconds.") // mcx

	vm.addBifHelp("\n*** list reduction operations ***");
	DEFAM(reduce, aak, "(list value fun --> value) applies fun to each item in list and the current value to get a new value. returns the ending value.")
	DEFAM(reduce1, ak, "(list fun --> value) like reduce except that the initial value is the first item in the list.")

	DEFAM(scan, aak, "(list value fun --> list) applies fun to each item in list and the current value to get a new value, which is added to the output list.")
	DEFAM(scan1, ak, "(list fun --> list) like scan except that the initial value is the first item in the list.")
	DEFAM(iter, ak, "(value fun --> list) returns an infinite list of repeated applications of fun to value.")
	DEFAM(itern, akk, "(value fun n --> list) returns a list of n repeated applications of fun to value.")
 	
	DEFAM(chain, akk, "(value fun n --> list) returns the result of n repeated applications of fun to value.")
   
	vm.addBifHelp("\n*** list ordering operations ***");
	DEF(cyc, 1, 1, "(list --> list) makes a finite list become cyclic.")
	DEFAM(ncyc, ak, "(n list --> list) concatenates n copies of a finite list.")
	DEF(rcyc, 1, 1, "(ref --> list) gets a new list from ref each time list is exhausted.")

	vm.defautomap("X", "ak", repeat_, "(value n --> stream) makes a list containing n copies of value. If value is a function, then the results of applying the function with an integer count argument is used as the contents of the output list.");
	vm.defmcx("XZ", 2, repeatz_, "(value n --> signal) returns a signal with value repeated n times.");
	vm.defmcx("mum", 1, mum_, "(t --> signal) returns a signal of t seconds of silence.");

	vm.def("$", 2, 1, append_, "(listA listB --> out) returns the concatenation of listA and listB.");
	vm.defmcx("$z", 2, append_, "(signalA signalB --> signal) returns the concatenation of signalA and signalB.");
	
	vm.def("$$", 2, 1, appendSubs_, "(listA listB --> out) return the concatenation of the sublists of listA and listB. equivalent to (listA @ listB @ $)");
	vm.def("$/", 1, 1, cat_, "(list --> out) returns the concatenation of the sub-lists of the input list.");
	DEF(flat, 1, 1, "(list --> list) flattens a list.")
	DEFAM(flatten, ak, "(list n --> list) makes a list n levels flatter.")
	vm.defautomap("keep", "ak", N_, "(list n --> list) returns a list of the first n items of the input list.");
	
	DEFAM(T, zk, "(signal t --> signal) returns a signal of the first t seconds of the input signal.");
	vm.defautomap("T>", "zk", skipT_, "(signal t --> signal) skips the first t seconds of the input signal.");
	vm.defautomap("N>", "ak", skip_, "(list n --> list) skips the first n items of the input list.");
	vm.def("N>>", 2, 1, hops_, "(list hops --> listOfLists) returns a list of tails of the input list. equivalent to (list (hops 0 | L 0 cons +\\) N>).");
	vm.defautomap("T>>", "za", hopTs_, "(signal hops --> listOfSignals) returns a list of tails of the input list. equivalent to (signal (hops 0 | L 0 cons +\\) T>).");
	DEFAM(N, ak, "(list n --> list) returns a list of the first n items of the input list.") 
	DEFAM(NZ, zk, "(signal n --> signal) returns a signal of the first n items of the input signal. automaps over streams.") 
	
	DEFAM(skip, ak, "(list n --> list) skips the first n items of the input list.") 

	DEFAM(take, ak, "(list n --> list) returns a list of the first n items of the input list, or the last n items if n is negative and the list is finite.") 
	DEFAM(drop, ak, "(list n --> list) skips the first n items of the input list, or the last n items if n is negative and the list is finite.") 
	
	DEFAM(choff, akk, "(channel(s) c n --> out) takes a finite list of channels or a single signal and places it into an array of n channels beginning at offset c. Other channels are set to zero.");

	DEF(tog, 2, 1, "(a b --> series) return a series alternating between a and b.")
	DEFMCX(togz, 2, "(a b --> signal) return a signal alternating between a and b.")
	DEF(sel, 2, 1, "(a j --> out) select. a is a list of lists. out[i] is a[j][i]")
	DEF(sell, 2, 1, "(a j --> out) lazy select. a is a list of lists. out[i] is the next value from a[j].")

	vm.def("?", 2, 1, filter_, "(a b --> out) the output list contains a[i] repeated b[i] times. If b is a list of booleans (1 or 0) then this functions as a filter.");
	DEF(spread, 2, 1, "(a n --> out) inserts n[i] zeroes after a[i].")	
	DEFMCX(spreadz, 2, "(a n --> signal) inserts n[i] zeroes after a[i]. automaps over stream inputs.")	

	DEF(change, 1, 1, "(a --> b) eliminates sequential duplicates in a signal or stream.")	
	DEFMCX(changez, 1, "(a --> b) eliminates sequential duplicates in a signal. automaps over streams.")	
	DEF(expand, 2, 1, "(a b --> out) when b is true, a value from a is written to out, when b is false, zero is written to out.")	
	DEFMCX(expandz, 2, "(a b --> out) when b is true, a value from a is written to out, when b is false, zero is written to out. automaps over stream inputs.")	

	DEF(clump, 2, 1, "(a n --> out) groups elements from list a into sub-lists of size n.")	
	DEF(hang, 1, 1, "(a --> out) repeats the last value of a finite list indefinitely.")	
	DEFMCX(hangz, 1, "(a --> out) repeats the last value of a finite signal indefinitely. automaps over streams.")	
	DEFAM(histo, ak, "(a n --> out) makes a histogram of the finite stream a.")	
	vm.defautomap("histoz", "zk", histo_, "(a n --> out) makes a histogram of the finite signal a. automaps over streams.");

	DEF(keepWhile, 2, 1, "(a b --> out) return items from a while items from b are true.")
	DEF(skipWhile, 2, 1, "(a b --> out) skip items from a while items from b are true.")
	
	DEF(flop, 1, 1, "(a --> b) returns the transpose of the list of lists a. At least one of the dimensions must be finite.")	
	DEF(flops, 1, 1, "(a --> b) like flop, but signals are treated as scalars and not flopped.")
	DEF(flop1, 1, 1, "(a --> b) like flop, but if list a is not a list of lists then it is wrapped in a list. compare: [[1 2 3][[4 5] 6 7]] @ flop $/ with: [[1 2 3][[4 5] 6 7]] @ flop1 $/")	
	DEF(lace, 1, 1, "(a --> b) returns the concatenation of the transpose of the list of lists a.")	
	DEFAM(merge, aak, "(a b fun --> c) merges two lists according to the function given. The function should work like <.")
	DEFAM(mergec, aak, "(a b fun --> c) merges two lists without duplicates according to the function given. The function should work like cmp.")
	
	DEF(perms, 1, 1, "(a --> b) returns a list of all permutations of the input list.")
	DEFMCX(permz, 1, "(a --> b) returns a list of all permutations of the input signal. automaps over streams.")
	
	DEF(permswr, 1, 1, "(a --> b) returns a list of all unique permutations of an input stream with repeated elements.")
	DEFMCX(permzwr, 1, "(a --> b) returns a returns a list of all unique permutations of an input signal with repeated elements. automaps over streams.")
	
	DEF(shortas, 2, 1, "(a b --> a') makes list a as short as list b.")
	DEF(longas, 2, 1, "(a b --> a') makes list a as long as list b by repeating the last item.")
	DEF(longas0, 2, 1, "(a b --> a') makes list a as long as list b by appending zeroes.")

	// array ops

	vm.addBifHelp("\n*** list ops ***");

	DEF(bub, 1, 1, "(a --> [a]) makes the top item on the stack into a one item list. i.e. puts a bubble around it.")
	DEF(nbub, 2, 1, "(a n --> [[..[a]..]]) embeds the top item in N one item lists.")

	vm.def("2ple", 2, 1, tupleN_<2>, "(a b --> [a b]) make a pair from the top two stack items.");
	vm.def("3ple", 3, 1, tupleN_<3>, "(a b c --> [a b c]) make a triple from the top three stack items.");
	vm.def("4ple", 4, 1, tupleN_<4>, "(a b c d --> [a b c d]) make a quadriple from the top four stack items.");
	vm.def("5ple", 5, 1, tupleN_<5>, "(a b c d e --> [a b c d e]) make a quintuple from the top five stack items.");
	vm.def("6ple", 6, 1, tupleN_<6>, "(a b c d e f --> [a b c d e f]) make a sextuple from the top six stack items.");
	vm.def("7ple", 7, 1, tupleN_<7>, "(a b c d e f g --> [a b c d e f g]) make a septuple from the top seven stack items.");
	vm.def("8ple", 8, 1, tupleN_<8>, "(a b c d e f g h --> [a b c d e f g h]) make an octuple from the top eight stack items.");
	
	vm.defautomap("2ples", "kk", tupleN_<2>, "(a b --> [[a0 b0][a1 b1]..[aN bN]]) make a sequence of pairs from the sequences a and b.");
	vm.defautomap("3ples", "kkk", tupleN_<3>, "(a b c --> [[a0 b0 c0][a1 b1 c1]..[aN bN cN]]) make a sequence of triples from the sequences a, b and c.");
	vm.defautomap("4ples", "kkkk", tupleN_<4>, "(a b c d --> seq) make a sequence of quadruples from the sequences a, b, c and d.");
	vm.defautomap("5ples", "kkkkk", tupleN_<5>, "(a b c d e --> seq) make a sequence of quintuples from the sequences a through e.");
	vm.defautomap("6ples", "kkkkkk", tupleN_<6>, "(a b c d e f--> seq) make a sequence of sextuples from the sequences a through f.");
	vm.defautomap("7ples", "kkkkkkk", tupleN_<7>, "(a b c d e f g--> seq) make a sequence of septuples from the sequences a through g.");
	vm.defautomap("8ples", "kkkkkkkk", tupleN_<8>, "(a b c d e f g h --> seq) make a sequence of octuples from the sequences a through h.");

	DEFNnoeach("un2", 1, 2, untupleN_<2>, "([a0 a1 .. aN-1] --> a0 a1) Push two items from a sequence onto the stack.")
	DEFNnoeach("un3", 1, 3, untupleN_<3>, "([a0 a1 .. aN-1] --> a0 a1 a2) Push three items from a sequence onto the stack.")
	DEFNnoeach("un4", 1, 4, untupleN_<4>, "([a0 a1 .. aN-1] --> a0 a1 a2 a3) Push four items from a sequence onto the stack.")
	DEFNnoeach("un5", 1, 5, untupleN_<5>, "([a0 a1 .. aN-1] --> a0 a1 a2 a3 a4) Push five items from a sequence onto the stack.")
	DEFNnoeach("un6", 1, 6, untupleN_<6>, "([a0 a1 .. aN-1] --> a0 a1 a2 .. a5) Push six items from a sequence onto the stack.")
	DEFNnoeach("un7", 1, 7, untupleN_<7>, "([a0 a1 .. aN-1] --> a0 a1 a2 .. a6) Push seven items from a sequence onto the stack.")
	DEFNnoeach("un8", 1, 8, untupleN_<8>, "([a0 a1 .. aN-1] --> a0 a1 a2 .. a7) Push eight items from a sequence onto the stack.")

	DEF(reverse, 1, 1, "(a --> b) reverses a finite sequence.")
	DEF(mirror0, 1, 1, "(a --> b) cyclic mirror of a sequence. [1 2 3 4] --> [1 2 3 4 3 2]")
	DEF(mirror1, 1, 1, "(a --> b) odd mirror of a sequence. [1 2 3 4] --> [1 2 3 4 3 2 1]")
	DEF(mirror2, 1, 1, "(a --> b) even mirror of a sequence. [1 2 3 4] --> [1 2 3 4 4 3 2 1]")
	DEFAM(rot, ak, "(seq M --> seq') rotation of a sequence by M places. M > 0 moves right.") 
	DEFAM(shift, ak, "(seq M --> seq') shift of a sequence by M places. zeroes are shifted in to fill vacated positions.") 
	DEFAM(clipShift, ak, "(seq M --> seq') shift of a sequence by M places. the end value is copied in to fill vacated positions.") 
	DEFAM(foldShift, ak, "(seq M --> seq') shift of a sequence by M places. values from the cyclic mirrored sequence are copied in to fill vacated positions.") 
	DEF(muss, 1, 1, "(a --> b) puts a finite sequence into a random order.")

	DEF(at, 2, 1, "(seq index(es) --> value(s)) looks up item(s) in sequence at index(es). out of range indexes return zero.") 
	DEF(wrapAt, 2, 1, "(seq index(es) --> value(s)) looks up item(s) in sequence at index(es). out of range indexes return the value at the end point.") 
	DEF(foldAt, 2, 1, "(seq index(es) --> value(s)) looks up item(s) in sequence at index(es). out of range indexes return the items from the cyclic sequence.") 
	DEF(clipAt, 2, 1, "(seq index(es) --> value(s)) looks up item(s) in sequence at index(es). out of range indexes return items from the cyclic mirrored sequence.") 
	DEF(degkey, 2, 1, "(degree scale --> converts scale degree(s) to keys, given a scale");
	DEF(keydeg, 2, 1, "(key scale --> converts key(s) to scale degree(s), given a scale");

	
	DEF(sort, 1, 1, "(in --> out) ascending order sort of the input list.");
	DEFAM(sortf, ak, "(in fun --> out) sort of the input list using a compare function.");
	DEFN("sort>", 1, 1, sort_gt_, "(in --> out) descending order sort of the input list.");
	
	DEF(grade, 1, 1, "(in --> out) ascending order sorted indices of the input list.");
	DEFAM(gradef, ak, "(in fun --> out) sorted indices of the input list using a compare function.");
	DEFN("grade>", 1, 1, grade_gt_, "(in --> out) descending order sorted indices of the input list.");

	vm.addBifHelp("\n*** event list operations ***");
	DEFAM(evmerge, aak, "(a b t --> c) merges event list 'b' with delay 't' with event list 'a' according to their delta times")
	DEFAM(evdelay, ak, "(a t --> c) delay an event list by adding a preceeding rest of duration 't'")
	DEFAM(evrest, aak, "(t --> c) returns a rest event for duration 't'.")
	
	vm.addBifHelp("\n*** dsp operations ***");
	
	DEFMCX(kaiser, 2, "(n stopBandAttenuation --> out) returns a signal filled with a kaiser window with the given stop band attenuation.")
	DEFMCX(hanning, 1, "(n --> out) returns a signal filled with a Hanning window.")
	DEFMCX(hamming, 1, "(n --> out) returns a signal filled with a Hamming window.")
	DEFMCX(blackman, 1, "(n --> out) returns a signal filled with a Blackman window.")
	DEFMCX(fft, 2, "(re im --> out) returns the complex FFT of two vectors (one real and one imaginary) which are a power of two length.")		
	DEFMCX(ifft, 2, "(re im --> out) returns the complex IFFT of two vectors (one real and one imaginary) which are a power of two length.")		

	DEFAM(seg, zaa, "(in hops durs --> out) divide input signal in to a stream of signal segments of given duration stepping by hop time.")
	DEFAM(wseg, zaz, "(in hops window --> out) divide input signal in to a stream of windowed signal segments of lengths equal to the window length, stepping by hop time.")

	vm.addBifHelp("\n*** audio I/O operations ***");
	DEF(play, 1, 0, "(channels -->) plays the audio to the hardware.")
	DEF(record, 2, 0, "(channels filename -->) plays the audio to the hardware and records it to a file.")
	DEFnoeach(stop, 0, 0, "(-->) stops any audio playing.")
	vm.def("sf>", 1, 0, sfread_, "(filename -->) read channels from an audio file. not real time.");
	vm.def(">sf", 2, 0, sfwrite_, "(channels filename -->) writes the audio to a file.");
	vm.def(">sfo", 2, 0, sfwriteopen_, "(channels filename -->) writes the audio to a file and opens it in the default application.");
	//vm.def("sf>", 2, sfread_);
	DEF(bench, 1, 0, "(channels -->) prints the amount of CPU required to compute a segment of audio. audio must be of finite duration.")
#ifdef SAPF_AUDIOTOOLBOX
	vm.def("sgram", 3, 0, sgram_, "(signal dBfloor filename -->) writes a spectrogram to a file and opens it.");
#endif // SAPF_AUDIOTOOLBOX

	setSessionTime();

}

