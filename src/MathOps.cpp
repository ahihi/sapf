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

#include "MathOps.hpp"
#include "clz.hpp"
#include <ctype.h>
#include "primes.hpp"
#ifdef SAPF_ACCELERATE
#include <Accelerate/Accelerate.h>
#else
#include <Eigen/Dense>
#include "ZArr.hpp"
#endif

V BinaryOp::makeVList(Thread& th, Arg a, Arg b)
{
	return new List(new BinaryOpGen(th, this, a, b));
}

V BinaryOp::makeZList(Thread& th, Arg a, Arg b)
{
	return new List(new BinaryOpZGen(th, this, a, b));
}

V BinaryOpLink::makeVList(Thread& th, Arg a, Arg b)
{
	return new List(new BinaryOpLinkGen(th, this, a, b));
}

V BinaryOpLink::makeZList(Thread& th, Arg a, Arg b)
{
	return new List(new BinaryOpLinkZGen(th, this, a, b));
}

void UnaryOp::loop(Thread& th, int n, V *a, int astride, V *out)
{
	LOOP(i, n) {
		out[i] = a->unaryOp(th, this);
		a += astride;
	} 
}

void BinaryOp::loop(Thread& th, int n, V *a, int astride, V *b, int bstride, V *out)
{
	LOOP(i, n) {
		out[i] = a->binaryOp(th, this, *b);
		a += astride;
		b += bstride;
	} 
}

void BinaryOp::loopzv(Thread& th, int n, Z *aa, int astride, V *bb, int bstride, V *out) 
{
	LOOP(i,n) { 
		Arg a = *aa;
		Arg b = *bb; 
		out[i] = a.binaryOp(th, this, b); 
		aa += astride; 
		bb += bstride; 
	}
}

void BinaryOp::loopvz(Thread& th, int n, V *aa, int astride, Z *bb, int bstride, V *out) 
{
	LOOP(i,n) { 
		Arg a = *aa; 
		Arg b = *bb;
		out[i] = a.binaryOp(th, this, b); 
		aa += astride; 
		bb += bstride; 
	}
}


void BinaryOp::scan(Thread& th, int n, V& z, V *a, int astride, V *out)
{
	V x = z;
	LOOP(i, n) {
		out[i] = x = x.binaryOp(th, this, *a);
		a += astride;
	}
	z = x;
}

void BinaryOp::pairs(Thread& th, int n, V& z, V *a, int astride, V *out)
{
	V x = z;
	LOOP(i, n) {
		out[i] = a->binaryOp(th, this, x);
		x = *a;
		a += astride;
	}
	z = x;
}

void BinaryOp::reduce(Thread& th, int n, V& z, V *a, int astride)
{
	V x = z;
	LOOP(i, n) {
		x = x.binaryOp(th, this, *a);
		a += astride;
	}
	z = x;
}


void UnaryOpZGen::pull(Thread& th) {
	int framesToFill = mBlockSize;
	Z* out = mOut->fulfillz(framesToFill);
	while (framesToFill) {
		int n = framesToFill;
		int astride;
		Z *a;
		if (_a(th, n,astride, a)) {
			setDone();
			break;
		} else {
			op->loopz(n, a, astride, out);
			_a.advance(n);
			framesToFill -= n;
			out += n;
		}
	}
	produce(framesToFill);
}


void BinaryOpZGen::pull(Thread& th)
{
	int framesToFill = mBlockSize;
	Z* out = mOut->fulfillz(framesToFill);
	while (framesToFill) {
		int n = framesToFill;
		int astride, bstride;
		Z *a, *b;
		if (_a(th, n,astride, a) || _b(th, n,bstride, b)) {
			setDone();
			break;
		} else {
			op->loopz(n, a, astride, b, bstride, out);
			_a.advance(n);
			_b.advance(n);
			framesToFill -= n;
			out += n;
		}
	}
	produce(framesToFill);
}

void BinaryOpLinkZGen::pull(Thread& th)
{
	int framesToFill = mBlockSize;
	Z* out = mOut->fulfillz(framesToFill);
	while (framesToFill) {
		int n = framesToFill;
		int astride, bstride;
		Z *a, *b;
		if (_a(th, n,astride, a)) {
			produce(framesToFill);
			_b.link(th, mOut);
			setDone();
			break;
		} else if (_b(th, n,bstride, b)) {
			produce(framesToFill);
			_a.link(th, mOut);
			setDone();
			break;
		} else {
			op->loopz(n, a, astride, b, bstride, out);
			_a.advance(n);
			_b.advance(n);
			framesToFill -= n;
			out += n;
		}
	}
	produce(framesToFill);
}


static void DoPairwise(Thread& th, BinaryOp* op)
{
	V a = th.pop();
	if (a.isVList()) {
		O s = new List(new PairsOpGen(th, a, op));
		th.push(s);
	} else if (a.isZList()) {
		O s = new List(new PairsOpZGen(th, a, op));
		th.push(s);
	} else if (a.isReal() || a.isString()) {
		th.push(a);
	} else {
		std::string s = op->Name();
		s += "^ : a";
		wrongType(s.c_str(), "Real, List or String", a);
	}
}


static void DoScan(Thread& th, BinaryOp* op)
{
	V a = th.pop();
	if (a.isVList()) {
		O s = new List(new ScanOpGen(th, a, op));
		th.push(s);
	} else if (a.isZList()) {
		O s = new List(new ScanOpZGen(th, a, op));
		th.push(s);
	} else if (a.isReal() || a.isString()) {
		th.push(a);
	} else {
		std::string s = op->Name();
		s += "\\ : a";
		wrongType(s.c_str(), "Real, List or String", a);
	}
}

static void DoIPairwise(Thread& th, BinaryOp* op)
{
	V b = th.pop();
	V a = th.pop();
	if (a.isVList()) {
		O s = new List(new IPairsOpGen(th, a, b, op));
		th.push(s);
	} else if (a.isZList()) {
		O s = new List(new IPairsOpZGen(th, a, b.asFloat(), op));
		th.push(s);
	} else if (a.isReal() || a.isString()) {
		th.push(a);
	} else {
		std::string s = op->Name();
		s += "^ : a";
		wrongType(s.c_str(), "Real, List or String", a);
	}
}


static void DoIScan(Thread& th, BinaryOp* op)
{
	V b = th.pop();
	V a = th.pop();
	if (a.isVList()) {
		O s = new List(new IScanOpGen(th, a, b, op));
		th.push(s);
	} else if (a.isZList()) {
		O s = new List(new IScanOpZGen(th, a, b.asFloat(), op));
		th.push(s);
	} else if (a.isReal() || a.isString()) {
		th.push(a);
	} else {
		std::string s = op->Name();
		s += "\\ : a";
		wrongType(s.c_str(), "Real, List or String", a);
	}
}


static void DoReduce(Thread& th, BinaryOp* op)
{
	int n;
	V a = th.pop();
	if (a.isVList()) {
		if (!a.isFinite()) indefiniteOp(op->Name(), "/");
		V z, *x;
		int xstride;
		VIn _a(a);
		n = 1;
		if (!_a(th, n,xstride,x)) {
			z = *x;
			_a.advance(n);
			while(1) {
				n = kDefaultVBlockSize;
				if (_a(th, n,xstride, x)) break;
				op->reduce(th, n, z, x, xstride);
				_a.advance(n);
			}	
		}
		th.push(z);
	} else if (a.isZList()) {
		if (!a.isFinite()) indefiniteOp(op->Name(), "/");
		Z z = 0., *x;
		int xstride;
		ZIn _a(a);
		n = 1;
		if (!_a(th, n,xstride,x)) {
			z = *x;
			_a.advance(n);
			while(1) {
				n = th.rate.blockSize;
				if (_a(th, n,xstride, x)) break;
				op->reducez(n, z, x, xstride);
				_a.advance(n);
			}	
		}
		th.push(z);
	} else if (a.isReal() || a.isString()) {
		th.push(a);
	} else {
		std::string s = op->Name();
		s += "\\ : a";
		wrongType(s.c_str(), "Real, List or String", a);
	}
}

static void DoIReduce(Thread& th, BinaryOp* op)
{
	int n;
	V b = th.pop();
	V a = th.pop();
	if (a.isVList()) {
		if (!a.isFinite()) indefiniteOp(op->Name(), "/");
		V z = b, *x;
		b = 0.;
		int xstride;
		VIn _a(a);
		while(1) {
			n = kDefaultVBlockSize;
			if (_a(th, n,xstride, x)) break;
			op->reduce(th, n, z, x, xstride);
			_a.advance(n);
		}	
		th.push(z);
	} else if (a.isZList()) {
		if (!a.isFinite()) indefiniteOp(op->Name(), "/");
		Z z = b.asFloat(), *x;
		b = 0.;
		int xstride;
		ZIn _a(a);
		while(1) {
			n = th.rate.blockSize;
			if (_a(th, n,xstride, x)) break;
			op->reducez(n, z, x, xstride);
			_a.advance(n);
		}	
		th.push(z);
	} else if (a.isReal() || a.isString()) {
		th.push(a);
	} else {
		std::string s = op->Name();
		s += "\\ : a";
		wrongType(s.c_str(), "Real, List or String", a);
	}
}

#define UNARY_OP_PRIM(NAME) \
	static void NAME##_(Thread& th, Prim* prim) \
	{ \
		V a = th.pop(); \
		V c = a.unaryOp(th, &gUnaryOp_##NAME); \
		th.push(c); \
	} \


#define BINARY_OP_PRIM(NAME) \
	static void NAME##_(Thread& th, Prim* prim) \
	{ \
		V b = th.pop(); \
		V a = th.pop(); \
		V c = a.binaryOp(th, &gBinaryOp_##NAME, b); \
		th.push(c); \
	} \
	static void NAME##_reduce_(Thread& th, Prim* prim) \
	{ \
		DoReduce(th, &gBinaryOp_##NAME); \
	} \
	static void NAME##_scan_(Thread& th, Prim* prim) \
	{ \
		DoScan(th, &gBinaryOp_##NAME); \
	} \
	static void NAME##_pairs_(Thread& th, Prim* prim) \
	{ \
		DoPairwise(th, &gBinaryOp_##NAME); \
	} \
	static void NAME##_iscan_(Thread& th, Prim* prim) \
	{ \
		DoIScan(th, &gBinaryOp_##NAME); \
	} \
	static void NAME##_ipairs_(Thread& th, Prim* prim) \
	{ \
		DoIPairwise(th, &gBinaryOp_##NAME); \
	} \

#define DEFINE_UNOP_FLOAT(NAME, CODE) \
	struct UnaryOp_##NAME : public UnaryOp { \
		virtual const char *Name() { return #NAME; } \
		virtual double op(double a) { return CODE; } \
		virtual void loopz(int n, const Z *aa, int astride, Z *out) { \
			LOOP(i,n) { Z a = *aa; out[i] = CODE; aa += astride; } \
		} \
	}; \
	UnaryOp_##NAME gUnaryOp_##NAME; \
	UnaryOp* gUnaryOpPtr_##NAME = &gUnaryOp_##NAME; \
	UNARY_OP_PRIM(NAME)

#ifdef SAPF_ACCELERATE
#define DEFINE_UNOP_FLOATVV(NAME, CODE, VVCODE_ACCELERATE, VVCODE_EIGEN) \
	struct UnaryOp_##NAME : public UnaryOp { \
		virtual const char *Name() { return #NAME; } \
		virtual double op(double a) { return CODE; } \
		virtual void loopz(int n, const Z *aa, int astride, Z *out) { \
			if (astride == 1) { \
				VVCODE_ACCELERATE; \
			} else { \
				LOOP(i,n) { Z a = *aa; out[i] = CODE; aa += astride; } \
			} \
		} \
	}; \
	UnaryOp_##NAME gUnaryOp_##NAME; \
	UnaryOp* gUnaryOpPtr_##NAME = &gUnaryOp_##NAME; \
	UNARY_OP_PRIM(NAME)
#else

#define ZARR_BINOP(op, n, aa, astride, bb, bstride, out) \
	do { \
		const CZArr A = czarr(aa, n, astride); \
		const CZArr B = czarr(bb, n, bstride); \
		ZArr R = zarr(out, n, 1); \
		R = op; \
	} while (0)

#define ZARR_UNOP(op, n, aa, astride, out) \
	do { \
		const CZArr A = czarr(aa, n, astride); \
		ZArr R = zarr(out, n, 1); \
		R = op; \
	} while (0)

#define DEFINE_UNOP_FLOATVV(NAME, CODE, VVCODE_ACCELERATE, VVCODE_EIGEN) \
	struct UnaryOp_##NAME : public UnaryOp { \
		virtual const char *Name() { return #NAME; } \
		virtual double op(double a) { return CODE; } \
		virtual void loopz(int n, const Z *aa, int astride, Z *out) { \
			if (astride == 1) { \
				ZARR_UNOP(VVCODE_EIGEN, n, aa, astride, out); \
			} else { \
				LOOP(i,n) { Z a = *aa; out[i] = CODE; aa += astride; } \
			} \
		} \
	}; \
	UnaryOp_##NAME gUnaryOp_##NAME; \
	UnaryOp* gUnaryOpPtr_##NAME = &gUnaryOp_##NAME; \
	UNARY_OP_PRIM(NAME)

// OP should be some code which takes ZBatch A and produces ZBatch R
// TODO: By switching the relevant vars to use an aligned allocator provided by xsimd,
//		we could use load/store_aligned and improve performance
#define DEFINE_UNOP_FLOATVV_XSIMD(NAME, CODE, OP) \
	struct UnaryOp_##NAME : public UnaryOp { \
		virtual const char *Name() { return #NAME; } \
		virtual double op(double a) { return CODE; } \
		virtual void loopz(int n, const Z *aa, int astride, Z *out) { \
			if (astride == 1) { \
				size_t vec_size = n - n % zbatch_size; \
				for (size_t i = 0; i < vec_size; i += zbatch_size) { \
					ZBatch A = ZBatch::load_unaligned(&aa[i]); \
					OP \
					R.store_unaligned(&out[i]); \
				} \
				for(size_t i = vec_size; i < n; ++i) { \
					Z a = aa[i]; \
					out[i] = CODE; \
				} \
			} else { \
				LOOP(i,n) { Z a = *aa; out[i] = CODE; aa += astride; } \
			} \
		} \
	}; \
	UnaryOp_##NAME gUnaryOp_##NAME; \
	UnaryOp* gUnaryOpPtr_##NAME = &gUnaryOp_##NAME; \
	UNARY_OP_PRIM(NAME)

#endif // SAPF_ACCELERATE


#define DEFINE_UNOP_INT(NAME, CODE) \
	struct UnaryOp_##NAME : public UnaryOp { \
		virtual const char *Name() { return #NAME; } \
		virtual double op(double aa) { \
			int64_t a = (int64_t)aa; \
			return (double)(CODE); \
		} \
		virtual void loopz(int n, const Z *aa, int astride, Z *out) { \
			LOOP(i,n) { int64_t a = (int64_t)*aa; out[i] = (Z)(CODE); aa += astride; } \
		} \
	}; \
	UnaryOp_##NAME gUnaryOp_##NAME; \
	UnaryOp* gUnaryOpPtr_##NAME = &gUnaryOp_##NAME; \
	UNARY_OP_PRIM(NAME)

#define DEFINE_UNOP_BOOL_FLOAT(NAME, CODE) \
	struct UnaryOp_##NAME : public UnaryOp { \
		virtual const char *Name() { return #NAME; } \
		virtual double op(double a) { return (CODE) ? 1. : 0.; } \
		virtual void loopz(int n, const Z *aa, int astride, Z *out) { \
			LOOP(i,n) { Z a = *aa; out[i] = (CODE) ? 1. : 0.; aa += astride; } \
		} \
	}; \
	UnaryOp_##NAME gUnaryOp_##NAME; \
	UnaryOp* gUnaryOpPtr_##NAME = &gUnaryOp_##NAME; \
	UNARY_OP_PRIM(NAME)


#define DEFINE_UNOP_BOOL_INT(NAME, CODE) \
	struct UnaryOp_##NAME : public UnaryOp { \
		virtual const char *Name() { return #NAME; } \
		virtual double op(double aa) { \
			int64_t a = (int64_t)aa; \
			return (CODE) ? 1. : 0.; \
		} \
		virtual void loopz(int n, const Z *aa, int astride, Z *out) { \
			LOOP(i,n) { int64_t a = (int64_t)*aa; out[i] = (CODE) ? 1. : 0.; aa += astride; } \
		} \
	}; \
	UnaryOp_##NAME gUnaryOp_##NAME; \
	UnaryOp* gUnaryOpPtr_##NAME = &gUnaryOp_##NAME; \
	UNARY_OP_PRIM(NAME)







#define DEFINE_BINOP_FLOAT(NAME, CODE) \
	struct BinaryOp_##NAME : public BinaryOp { \
		virtual const char *Name() { return #NAME; } \
		virtual double op(double a, double b) { return CODE; } \
		virtual void loopz(int n, const Z *aa, int astride, const Z *bb, int bstride, Z *out) { \
			LOOP(i,n) { Z a = *aa; Z b = *bb; out[i] = CODE; aa += astride; bb += bstride; } \
		} \
		virtual void pairsz(int n, Z& z, Z *aa, int astride, Z *out) { \
			Z b = z; \
			LOOP(i,n) { Z a = *aa; out[i] = CODE; b = a; aa += astride; } \
			z = b; \
		} \
		virtual void scanz(int n, Z& z, Z *aa, int astride, Z *out) { \
			Z a = z; \
			LOOP(i,n) { Z b = *aa; out[i] = a = CODE; aa += astride; } \
			z = a; \
		} \
		virtual void reducez(int n, Z& z, Z *aa, int astride) { \
			Z a = z; \
			LOOP(i,n) { Z b = *aa; a = CODE; aa += astride; } \
			z = a; \
		} \
	}; \
	BinaryOp_##NAME gBinaryOp_##NAME; \
	BinaryOp* gBinaryOpPtr_##NAME = &gBinaryOp_##NAME; \
	BINARY_OP_PRIM(NAME)


#define DEFINE_BINOP_FLOAT_STRING(NAME, CODE, STRCODE) \
	struct BinaryOp_##NAME : public BinaryOp { \
		virtual const char *Name() { return #NAME; } \
		virtual double op(double a, double b) { return CODE; } \
		virtual void loopz(int n, const Z *aa, int astride, const Z *bb, int bstride, Z *out) { \
			LOOP(i,n) { Z a = *aa; Z b = *bb; out[i] = CODE; aa += astride; bb += bstride; } \
		} \
		virtual void pairsz(int n, Z& z, Z *aa, int astride, Z *out) { \
			Z b = z; \
			LOOP(i,n) { Z a = *aa; out[i] = CODE; b = a; aa += astride; } \
			z = b; \
		} \
		virtual void scanz(int n, Z& z, Z *aa, int astride, Z *out) { \
			Z a = z; \
			LOOP(i,n) { Z b = *aa; out[i] = a = CODE; aa += astride; } \
			z = a; \
		} \
		virtual void reducez(int n, Z& z, Z *aa, int astride) { \
			Z a = z; \
			LOOP(i,n) { Z b = *aa; a = CODE; aa += astride; } \
			z = a; \
		} \
		virtual V stringOp(P<String> const& aa, P<String> const& bb) { \
			const char* a = aa->s; \
			const char* b = bb->s; \
			return (STRCODE); \
		} \
	}; \
	BinaryOp_##NAME gBinaryOp_##NAME; \
	BinaryOp* gBinaryOpPtr_##NAME = &gBinaryOp_##NAME; \
	BINARY_OP_PRIM(NAME)


#ifdef SAPF_ACCELERATE
#define DEFINE_BINOP_FLOATVV(NAME, REQUIRE_STRIDE_1, CODE, VVCODE_ACCELERATE, VVCODE_EIGEN) \
	struct BinaryOp_##NAME : public BinaryOp { \
		virtual const char *Name() { return #NAME; } \
		virtual double op(double a, double b) { return CODE; } \
		virtual void loopz(int n, const Z *aa, int astride, const Z *bb, int bstride, Z *out) { \
			if (!REQUIRE_STRIDE_1 || (astride == 1 && bstride == 1)) { \
				 VVCODE_ACCELERATE; \
			} else { \
				LOOP(i,n) { Z a = *aa; Z b = *bb; out[i] = CODE; aa += astride; bb += bstride; } \
			} \
		} \
		virtual void pairsz(int n, Z& z, Z *aa, int astride, Z *out) { \
			Z b = z; \
			LOOP(i,n) { Z a = *aa; out[i] = CODE; b = a; aa += astride; } \
			z = b; \
		} \
		virtual void scanz(int n, Z& z, Z *aa, int astride, Z *out) { \
			Z a = z; \
			LOOP(i,n) { Z b = *aa; out[i] = a = CODE; aa += astride; } \
			z = a; \
		} \
		virtual void reducez(int n, Z& z, Z *aa, int astride) { \
			Z a = z; \
			LOOP(i,n) { Z b = *aa; a = CODE; aa += astride; } \
			z = a; \
		} \
	}; \
	BinaryOp_##NAME gBinaryOp_##NAME; \
	BinaryOp* gBinaryOpPtr_##NAME = &gBinaryOp_##NAME; \
	BINARY_OP_PRIM(NAME)
#else
// TODO: We could also vectorize the astride=1/bstride=0 (and vice versa) cases if desired
//	since it's easy to change that via Eigen.
#define DEFINE_BINOP_FLOATVV(NAME, REQUIRE_STRIDE_1, CODE, VVCODE_ACCELERATE, VVCODE_EIGEN) \
	struct BinaryOp_##NAME : public BinaryOp { \
		virtual const char *Name() { return #NAME; } \
		virtual double op(double a, double b) { return CODE; } \
		virtual void loopz(int n, const Z *aa, int astride, const Z *bb, int bstride, Z *out) { \
			if (astride == 1 && bstride == 1) { \
				 ZARR_BINOP(VVCODE_EIGEN, n, aa, astride, bb, bstride, out); \
			} else { \
				LOOP(i,n) { Z a = *aa; Z b = *bb; out[i] = CODE; aa += astride; bb += bstride; } \
			} \
		} \
		virtual void pairsz(int n, Z& z, Z *aa, int astride, Z *out) { \
			Z b = z; \
			LOOP(i,n) { Z a = *aa; out[i] = CODE; b = a; aa += astride; } \
			z = b; \
		} \
		virtual void scanz(int n, Z& z, Z *aa, int astride, Z *out) { \
			Z a = z; \
			LOOP(i,n) { Z b = *aa; out[i] = a = CODE; aa += astride; } \
			z = a; \
		} \
		virtual void reducez(int n, Z& z, Z *aa, int astride) { \
			Z a = z; \
			LOOP(i,n) { Z b = *aa; a = CODE; aa += astride; } \
			z = a; \
		} \
	}; \
	BinaryOp_##NAME gBinaryOp_##NAME; \
	BinaryOp* gBinaryOpPtr_##NAME = &gBinaryOp_##NAME; \
	BINARY_OP_PRIM(NAME)

// op should take a ZBatch A and ZBatch B and
// populate a ZBatch R with the result.
// TODO: By switching the relevant vars to use an aligned allocator provided by xsimd,
//		we could use load/store_aligned and improve performance
#define DEFINE_BINOP_FLOATVV_XSIMD(NAME, CODE, OP) \
	struct BinaryOp_##NAME : public BinaryOp { \
		virtual const char *Name() { return #NAME; } \
		virtual double op(double a, double b) { return CODE; } \
		virtual void loopz(int n, const Z *aa, int astride, const Z *bb, int bstride, Z *out) { \
			if (astride == 1 && bstride == 1) { \
				size_t vec_size = n - n % zbatch_size; \
				for (size_t i = 0; i < vec_size; i += zbatch_size) { \
					ZBatch A = ZBatch::load_unaligned(&aa[i]); \
					ZBatch B = ZBatch::load_unaligned(&bb[i]); \
					OP \
					R.store_unaligned(&out[i]); \
				} \
				for(size_t i = vec_size; i < n; ++i) { \
					Z a = aa[i]; \
					Z b = bb[i]; \
					out[i] = CODE; \
				} \
			} else { \
				LOOP(i,n) { Z a = *aa; Z b = *bb; out[i] = CODE; aa += astride; bb += bstride; } \
			} \
		} \
		virtual void pairsz(int n, Z& z, Z *aa, int astride, Z *out) { \
			Z b = z; \
			LOOP(i,n) { Z a = *aa; out[i] = CODE; b = a; aa += astride; } \
			z = b; \
		} \
		virtual void scanz(int n, Z& z, Z *aa, int astride, Z *out) { \
			Z a = z; \
			LOOP(i,n) { Z b = *aa; out[i] = a = CODE; aa += astride; } \
			z = a; \
		} \
		virtual void reducez(int n, Z& z, Z *aa, int astride) { \
			Z a = z; \
			LOOP(i,n) { Z b = *aa; a = CODE; aa += astride; } \
			z = a; \
		} \
	}; \
	BinaryOp_##NAME gBinaryOp_##NAME; \
	BinaryOp* gBinaryOpPtr_##NAME = &gBinaryOp_##NAME; \
	BINARY_OP_PRIM(NAME)

	
#endif // SAPF_ACCELERATE

#define DEFINE_BINOP_INT(NAME, CODE) \
	struct BinaryOp_##NAME : public BinaryOp { \
		virtual const char *Name() { return #NAME; } \
		virtual double op(double aa, double bb) { \
			int64_t a = (int64_t)aa; \
			int64_t b = (int64_t)bb; \
			return (double)(CODE); \
		} \
		virtual void loopz(int n, const Z *aa, int astride, const Z *bb, int bstride, Z *out) { \
			LOOP(i,n) { int64_t a = (int64_t)*aa; int64_t b = (int64_t)*bb; out[i] = (Z)(CODE); aa += astride; bb += bstride; } \
		} \
		virtual void pairsz(int n, Z& z, Z *aa, int astride, Z *out) { \
			int64_t b = (int64_t)z; \
			LOOP(i,n) { int64_t a = (int64_t)*aa; out[i] = CODE; b = a; aa += astride; } \
			z = (Z)b; \
		} \
		virtual void scanz(int n, Z& z, Z *aa, int astride, Z *out) { \
			int64_t a = (int64_t)z; \
			LOOP(i,n) { int64_t b = (int64_t)*aa; Z x = CODE; out[i] = x; a = (int64_t)x; aa += astride; } \
			z = a; \
		} \
		virtual void reducez(int n, Z& z, Z *aa, int astride) { \
			int64_t a = (int64_t)z; \
			LOOP(i,n) { int64_t b = (int64_t)*aa; a = (int64_t)(CODE); aa += astride; } \
			z = (Z)a; \
		} \
	}; \
	BinaryOp_##NAME gBinaryOp_##NAME; \
	BinaryOp* gBinaryOpPtr_##NAME = &gBinaryOp_##NAME; \
	BINARY_OP_PRIM(NAME)

#define DEFINE_BINOP_BOOL_FLOAT(NAME, CODE, STRCODE) \
	struct BinaryOp_##NAME : public BinaryOp { \
		virtual const char *Name() { return #NAME; } \
		virtual double op(double a, double b) { return (CODE) ? 1. : 0.; } \
		virtual void loopz(int n, const Z *aa, int astride, const Z *bb, int bstride, Z *out) { \
			LOOP(i,n) { Z a = *aa; Z b = *bb; out[i] = (CODE) ? 1. : 0.; aa += astride; bb += bstride; } \
		} \
		virtual void pairsz(int n, Z& z, Z *aa, int astride, Z *out) { \
			Z b = z; \
			LOOP(i,n) { Z a = *aa; out[i] = (CODE) ? 1. : 0.; b = a; aa += astride; } \
			z = b; \
		} \
		virtual void scanz(int n, Z& z, Z *aa, int astride, Z *out) { \
			Z a = z; \
			LOOP(i,n) { Z b = *aa; out[i] = a = (CODE) ? 1. : 0.; aa += astride; } \
			z = a; \
		} \
		virtual void reducez(int n, Z& z, Z *aa, int astride) { \
			Z a = z; \
			LOOP(i,n) { Z b = *aa; a = (CODE) ? 1. : 0.; aa += astride; } \
			z = a; \
		} \
		virtual V stringOp(P<String> const& aa, P<String> const& bb) { \
			const char* a = aa->s; \
			const char* b = bb->s; \
			return (STRCODE) ? 1. : 0.; \
		} \
	}; \
	BinaryOp_##NAME gBinaryOp_##NAME; \
	BinaryOp* gBinaryOpPtr_##NAME = &gBinaryOp_##NAME; \
	BINARY_OP_PRIM(NAME)


DEFINE_UNOP_BOOL_INT(isalnum, isalnum((int)a))
DEFINE_UNOP_BOOL_INT(isalpha, isalpha((int)a))
DEFINE_UNOP_BOOL_INT(isblank, isblank((int)a))
DEFINE_UNOP_BOOL_INT(iscntrl, iscntrl((int)a))
DEFINE_UNOP_BOOL_INT(isdigit, isdigit((int)a))
DEFINE_UNOP_BOOL_INT(isgraph, isgraph((int)a))
DEFINE_UNOP_BOOL_INT(islower, islower((int)a))
DEFINE_UNOP_BOOL_INT(isprint, isprint((int)a))
DEFINE_UNOP_BOOL_INT(ispunct, ispunct((int)a))
DEFINE_UNOP_BOOL_INT(isspace, isspace((int)a))
DEFINE_UNOP_BOOL_INT(isupper, isupper((int)a))
DEFINE_UNOP_BOOL_INT(isxdigit, isxdigit((int)a))
#ifdef _WIN32
	#pragma push_macro("isascii")
	#undef isascii
	DEFINE_UNOP_BOOL_INT(isascii, __isascii((int)a))
	#pragma pop_macro("isascii")
#else
	DEFINE_UNOP_BOOL_INT(isascii, isascii((int)a))
#endif


DEFINE_UNOP_BOOL_FLOAT(not, a == 0.)
DEFINE_UNOP_BOOL_FLOAT(nonneg, a >= 0.)
DEFINE_UNOP_BOOL_FLOAT(nonpos, a <= 0.)
DEFINE_UNOP_BOOL_FLOAT(isneg, a < 0.)
DEFINE_UNOP_BOOL_FLOAT(ispos, a > 0.)
DEFINE_UNOP_BOOL_FLOAT(iszero, a == 0.)
DEFINE_UNOP_BOOL_FLOAT(isint, (a - floor(a)) == 0.)
DEFINE_UNOP_BOOL_INT(iseven, !(a & 1))
DEFINE_UNOP_BOOL_INT(isodd, (a & 1))
DEFINE_UNOP_BOOL_INT(isprime, isprime(a))

DEFINE_UNOP_BOOL_FLOAT(isfinite, std::isfinite(a))
DEFINE_UNOP_BOOL_FLOAT(isinf, std::isinf(a))
DEFINE_UNOP_BOOL_FLOAT(isnormal, std::isnormal(a))
DEFINE_UNOP_BOOL_FLOAT(isnan, std::isnan(a))
DEFINE_UNOP_BOOL_FLOAT(signbit, std::signbit(a))

struct UnaryOp_ToZero : public UnaryOp {
	virtual const char *Name() { return "ToZero"; }
	virtual double op(double a) { return 0.; }
	virtual void loopz(int n, const Z *aa, int astride, Z *out) {
		LOOP(i,n) { out[i] = 0.; }
	}
};
UnaryOp_ToZero gUnaryOp_ToZero; 

DEFINE_UNOP_FLOATVV(neg, -a, vDSP_vnegD(const_cast<Z*>(aa), astride, out, 1, n), A * -1)
DEFINE_UNOP_FLOAT(sgn, sc_sgn(a))
DEFINE_UNOP_FLOATVV(abs, fabs(a), vvfabs(out, aa, &n), A.abs())

DEFINE_UNOP_INT(tolower, tolower((int)a))
DEFINE_UNOP_INT(toupper, toupper((int)a))
// under mingw64, "toascii" is actually a #define for __toascii, so the macro doesn't do what's desired 
#ifdef _WIN32
	#pragma push_macro("toascii")
	#undef toascii
	DEFINE_UNOP_BOOL_INT(toascii, __toascii((int)a))
	#pragma pop_macro("toascii")
#else
	DEFINE_UNOP_BOOL_INT(toascii, toascii((int)a))
#endif

DEFINE_UNOP_FLOATVV(frac, a - floor(a), vvfloor(out, aa, &n); vDSP_vsubD(out, 1, aa, astride, out, 1, n), A - A.floor())
DEFINE_UNOP_FLOATVV(floor, floor(a), vvfloor(out, aa, &n), A.floor())
DEFINE_UNOP_FLOATVV(ceil, ceil(a), vvceil(out, aa, &n), A.ceil())
DEFINE_UNOP_FLOATVV(rint, rint(a), vvnint(out, aa, &n), A.round())

DEFINE_UNOP_FLOAT(erf, erf(a))
DEFINE_UNOP_FLOAT(erfc, erfc(a))

DEFINE_UNOP_FLOATVV(recip, 1./a, vvrec(out, aa, &n), 1. / A)
DEFINE_UNOP_FLOATVV(sqrt, sc_sqrt(a), vvsqrt(out, aa, &n), A.sqrt())
DEFINE_UNOP_FLOATVV(rsqrt, 1./sc_sqrt(a), vvrsqrt(out, aa, &n), A.rsqrt())
DEFINE_UNOP_FLOATVV(ssq, copysign(a*a, a), vDSP_vssqD(aa, astride, out, 1, n), A.sign() * A.square())
DEFINE_UNOP_FLOATVV(sq, a*a, vDSP_vsqD(aa, astride, out, 1, n), A.square())
DEFINE_UNOP_FLOAT(cbrt, cbrt(a))

DEFINE_UNOP_FLOAT(cb, a*a*a)
DEFINE_UNOP_FLOAT(pow4, sc_fourth(a))
DEFINE_UNOP_FLOAT(pow5, sc_fifth(a))
DEFINE_UNOP_FLOAT(pow6, sc_sixth(a))
DEFINE_UNOP_FLOAT(pow7, sc_seventh(a))
DEFINE_UNOP_FLOAT(pow8, sc_eighth(a))
DEFINE_UNOP_FLOAT(pow9, sc_ninth(a))

DEFINE_UNOP_FLOATVV(exp, exp(a), vvexp(out, aa, &n), A.exp())
DEFINE_UNOP_FLOATVV(exp2, exp2(a), vvexp2(out, aa, &n), pow(2,A))
DEFINE_UNOP_FLOATVV(expm1, expm1(a), vvexpm1(out, aa, &n), A.exp() - 1)
DEFINE_UNOP_FLOATVV(log, sc_log(a), vvlog(out, aa, &n), A.log())
DEFINE_UNOP_FLOATVV(log2, sc_log2(a), vvlog2(out, aa, &n), A.log() / sc_log(2.0))
DEFINE_UNOP_FLOATVV(log10, sc_log10(a), vvlog10(out, aa, &n), A.log10())
DEFINE_UNOP_FLOATVV(log1p, log1p(a), vvlog1p(out, aa, &n), A.log1p())
DEFINE_UNOP_FLOAT(exp10, pow(10., a))

#ifdef SAPF_ACCELERATE
	DEFINE_UNOP_FLOATVV(logb, logb(a), vvlogb(out, aa, &n), "todo")
#else
	// Extract IEEE 754 exponent bits, then unbias by subtracting
	#if SAMPLE_IS_DOUBLE
		DEFINE_UNOP_FLOATVV_XSIMD(logb, logb(a),
			auto bits = xsimd::bitwise_cast<Z_INT_EQUIV>(A);
			auto R = xsimd::to_float((bits >> 52) & 0x7FF) - 1023.0;
		)
	#else
		DEFINE_UNOP_FLOATVV_XSIMD(logb, logb(a),
			auto bits = xsimd::bitwise_cast<Z_INT_EQUIV>(A);
			auto R = xsimd::to_float((bits >> 23) & 0xFF) - 127.0f;
		)
	#endif
#endif

DEFINE_UNOP_FLOAT(sinc, sc_sinc(a))
DEFINE_UNOP_FLOATVV(sin, sin(a), vvsin(out, aa, &n), A.sin())
DEFINE_UNOP_FLOATVV(cos, cos(a), vvcos(out, aa, &n), A.cos())
DEFINE_UNOP_FLOATVV(sin1, sin(a * kTwoPi), Z b = kTwoPi; vDSP_vsmulD(const_cast<Z*>(aa), astride, &b, out, 1, n); vvsin(out, out, &n), (kTwoPi * A).sin())
DEFINE_UNOP_FLOATVV(cos1, cos(a * kTwoPi), Z b = kTwoPi; vDSP_vsmulD(const_cast<Z*>(aa), astride, &b, out, 1, n); vvcos(out, out, &n), (kTwoPi * A).cos())
DEFINE_UNOP_FLOATVV(tan, tan(a), vvtan(out, aa, &n), A.tan())
DEFINE_UNOP_FLOATVV(asin, asin(a), vvasin(out, aa, &n), A.asin())
DEFINE_UNOP_FLOATVV(acos, acos(a), vvacos(out, aa, &n), A.acos())
DEFINE_UNOP_FLOATVV(atan, atan(a), vvatan(out, aa, &n), A.atan())
DEFINE_UNOP_FLOATVV(sinh, sinh(a), vvsinh(out, aa, &n), A.sinh())
DEFINE_UNOP_FLOATVV(cosh, cosh(a), vvcosh(out, aa, &n), A.cosh())
DEFINE_UNOP_FLOATVV(tanh, tanh(a), vvtanh(out, aa, &n), A.tanh())
DEFINE_UNOP_FLOATVV(asinh, asinh(a), vvasinh(out, aa, &n), A.asinh())
DEFINE_UNOP_FLOATVV(acosh, acosh(a), vvacosh(out, aa, &n), A.acosh())
DEFINE_UNOP_FLOATVV(atanh, atanh(a), vvatanh(out, aa, &n), A.atanh())

#ifdef _WIN32
	DEFINE_UNOP_FLOAT(J0, _j0(a))
	DEFINE_UNOP_FLOAT(J1, _j1(a))
	DEFINE_UNOP_FLOAT(Y0, _y0(a))
	DEFINE_UNOP_FLOAT(Y1, _y1(a))
#else
	DEFINE_UNOP_FLOAT(J0, j0(a))
	DEFINE_UNOP_FLOAT(J1, j1(a))
	DEFINE_UNOP_FLOAT(Y0, y0(a))
	DEFINE_UNOP_FLOAT(Y1, y1(a))
#endif

DEFINE_UNOP_FLOAT(tgamma, tgamma(a))
DEFINE_UNOP_FLOAT(lgamma, lgamma(a))

static void sc_clipv(int n, const Z* in, Z* out, Z a, Z b)
{
	for (int i = 0; i < n; ++i) {
		out[i] = std::clamp(in[i], a, b);
	}
}

DEFINE_UNOP_FLOATVV(inc, a+1, Z b = 1.; vDSP_vsaddD(const_cast<Z*>(aa), astride, &b, out, 1, n), A + 1)
DEFINE_UNOP_FLOATVV(dec, a-1, Z b = -1.; vDSP_vsaddD(const_cast<Z*>(aa), astride, &b, out, 1, n), A - 1)
DEFINE_UNOP_FLOATVV(half, a*.5, Z b = .5; vDSP_vsmulD(aa, astride, &b, out, 1, n), A * .5)
DEFINE_UNOP_FLOATVV(twice, a*2., Z b = 2.; vDSP_vsmulD(const_cast<Z*>(aa), astride, &b, out, 1, n), A * 2.)

DEFINE_UNOP_FLOATVV(biuni, a*.5+.5, Z b = .5; vDSP_vsmulD(const_cast<Z*>(aa), astride, &b, out, 1, n); vDSP_vsaddD(out, 1, &b, out, 1, n), A * .5 + .5)
DEFINE_UNOP_FLOATVV(unibi, a*2.-1., Z b = 2.; Z c = -1.; vDSP_vsmulD(aa, astride, &b, out, 1, n); vDSP_vsaddD(out, 1, &c, out, 1, n), A * 2. - 1.)
DEFINE_UNOP_FLOATVV(biunic, std::clamp(a,-1.,1.)*.5+.5, Z b = .5; sc_clipv(n, aa, out, -1., 1.); vDSP_vsmulD(out, astride, &b, out, 1, n); vDSP_vsaddD(out, 1, &b, out, 1, n), A.min(1.).max(-1.) * .5 + .5)
DEFINE_UNOP_FLOATVV(unibic, std::clamp(a,0.,1.)*2.-1., Z b = 2.; Z c = -1.; sc_clipv(n, aa, out, 0., 1.); vDSP_vsmulD(out, astride, &b, out, 1, n); vDSP_vsaddD(out, 1, &c, out, 1, n), A.min(1.).max(0.) * 2. - 1.)
DEFINE_UNOP_FLOAT(cmpl, 1.-a)

DEFINE_UNOP_FLOATVV(ampdb, sc_ampdb(a), Z b = 1.; vDSP_vdbconD(const_cast<Z*>(aa), astride, &b, out, 1, n, 1),  A.log10() * 20.)
DEFINE_UNOP_FLOAT(dbamp,     sc_dbamp(a))

DEFINE_UNOP_FLOAT(hzo,   sc_hzoct(a))
DEFINE_UNOP_FLOAT(ohz,   sc_octhz(a))

DEFINE_UNOP_FLOAT(hzst,   sc_hzkey(a))
DEFINE_UNOP_FLOAT(sthz,   sc_keyhz(a))

DEFINE_UNOP_FLOAT(hznn,   sc_hznn(a))
DEFINE_UNOP_FLOAT(nnhz,   sc_nnhz(a))

DEFINE_UNOP_FLOAT(centsratio, sc_centsratio(a))
DEFINE_UNOP_FLOAT(ratiocents, sc_ratiocents(a))

DEFINE_UNOP_FLOAT(semiratio, sc_semiratio(a))
DEFINE_UNOP_FLOAT(ratiosemi, sc_ratiosemi(a))

DEFINE_UNOP_FLOATVV(degrad, a*kDegToRad, Z b = kDegToRad; vDSP_vsmulD(aa, astride, &b, out, 1, n), A * kDegToRad)
DEFINE_UNOP_FLOATVV(raddeg, a*kRadToDeg, Z b = kRadToDeg; vDSP_vsmulD(aa, astride, &b, out, 1, n), A * kRadToDeg)
DEFINE_UNOP_FLOATVV(minsec, a*kMinToSecs, Z b = kMinToSecs; vDSP_vsmulD(aa, astride, &b, out, 1, n), A * kMinToSecs)
DEFINE_UNOP_FLOATVV(secmin, a*kSecsToMin, Z b = kSecsToMin; vDSP_vsmulD(aa, astride, &b, out, 1, n), A * kSecsToMin)
DEFINE_UNOP_FLOATVV(bpmsec, kMinToSecs / a, Z b = kMinToSecs; vDSP_svdivD(&b, const_cast<double*>(aa), astride, out, 1, n), kMinToSecs / A)

DEFINE_UNOP_FLOAT(distort,  sc_distort(a))
DEFINE_UNOP_FLOAT(softclip, sc_softclip(a))

DEFINE_UNOP_FLOAT(rectWin,  sc_rectWindow(a))
DEFINE_UNOP_FLOAT(triWin,   sc_triWindow(a))
DEFINE_UNOP_FLOAT(bitriWin, sc_bitriWindow(a))
DEFINE_UNOP_FLOAT(hanWin,   sc_hanWindow(a))
DEFINE_UNOP_FLOAT(sinWin,   sc_sinWindow(a))
DEFINE_UNOP_FLOAT(ramp,     sc_ramp(a))
DEFINE_UNOP_FLOAT(scurve,   sc_scurve(a))
DEFINE_UNOP_FLOAT(sigm,		a/sqrt(1.+a*a))

DEFINE_UNOP_FLOAT(zapgremlins, zapgremlins(a))

////////////////////////////////////////////////////////////////////////////////////////////////////////

DEFINE_BINOP_BOOL_FLOAT(lt, a <  b, strcmp(a, b) < 0)
DEFINE_BINOP_BOOL_FLOAT(le, a <= b, strcmp(a, b) <= 0)
DEFINE_BINOP_BOOL_FLOAT(gt, a >  b, strcmp(a, b) > 0)
DEFINE_BINOP_BOOL_FLOAT(ge, a >= b, strcmp(a, b) >= 0)
DEFINE_BINOP_BOOL_FLOAT(eq, a == b, strcmp(a, b) == 0)
DEFINE_BINOP_BOOL_FLOAT(ne, a != b, strcmp(a, b) != 0)
DEFINE_BINOP_FLOAT_STRING(cmp,  sc_cmp(a, b), sc_sgn(strcmp(a, b)))

DEFINE_BINOP_FLOATVV(copysign, 1, copysign(a, b), vvcopysign(out, const_cast<Z*>(aa), bb, &n), A.abs() * B.sign()) // bug in vForce.h requires const_cast

#ifdef SAPF_ACCELERATE
	DEFINE_BINOP_FLOATVV(nextafter, 1, nextafter(a, b), vvnextafter(out, const_cast<Z*>(aa), bb, &n), "todo") // bug in vForce.h requires const_cast
#else
    using int_batch  = xsimd::batch<Z_INT_EQUIV>;
    using bool_batch = xsimd::batch_bool<Z_INT_EQUIV>;

    const xsimd::batch<Z_INT_EQUIV> sign_mask = xsimd::bitwise_cast<Z_INT_EQUIV>(xsimd::batch<Z>{-0.0});
    const xsimd::batch<Z_INT_EQUIV> zero_mask{0};
    const xsimd::batch<Z_INT_EQUIV> one_mask{1};
    const xsimd::batch<Z_INT_EQUIV> neg_one_mask{-1};

    DEFINE_BINOP_FLOATVV_XSIMD(nextafter, nextafter(a, b),
        // Handle case where from == to
        auto equal_mask = (A == B);

        // Bitwise view
        auto from_bits = xsimd::bitwise_cast<Z_INT_EQUIV>(A);
        auto to_bits   = xsimd::bitwise_cast<Z_INT_EQUIV>(B);

        // Handle -0.0 correctly: treat it as 0
        auto is_neg_zero = (from_bits == sign_mask);
        auto cleaned_from_bits = xsimd::select(is_neg_zero, zero_mask, from_bits);

        // Compare bitwise: from_bits < to_bits
        bool_batch direction_mask = (cleaned_from_bits < to_bits);

        // Select +1 or -1 based on direction
        auto increment = xsimd::select(direction_mask, one_mask, neg_one_mask);

        auto adjusted_bits = cleaned_from_bits + increment;

        // Reinterpret as Z
        auto result = xsimd::bitwise_cast<Z>(adjusted_bits);

        // Use original comparison to handle from == to
        auto R = xsimd::select(equal_mask, B, result);
    )
#endif

// identity optimizations of basic operators.

	struct BinaryOp_plus : public BinaryOp {
		virtual const char *Name() { return "plus"; }
		virtual double op(double a, double b) { return a + b; }
		virtual void loopz(int n, const Z *aa, int astride, const Z *bb, int bstride, Z *out) {
			if (astride == 0) {
				if (*aa == 0.) {
					memcpy(out, bb, n * sizeof(Z));
					//LOOP(i,n) { out[i] = *bb; bb += bstride; }
				} else {
#ifdef SAPF_ACCELERATE
					vDSP_vsaddD(const_cast<Z*>(bb), bstride, const_cast<Z*>(aa), out, 1, n);
#else
					ZARR_BINOP(A + B, n, aa, astride, bb, bstride, out);
#endif // SAPF_ACCELERATE
				}
			} else if (bstride == 0 ) {
				if (*bb == 0.) {
					memcpy(out, aa, n * sizeof(Z));
				} else {
#ifdef SAPF_ACCELERATE
					vDSP_vsaddD(const_cast<Z*>(aa), astride, const_cast<Z*>(bb), out, 1, n);
#else
                    ZARR_BINOP(A + B, n, aa, astride, bb, bstride, out);
#endif // SAPF_ACCELERATE
				}
			} else {
#ifdef SAPF_ACCELERATE
				vDSP_vaddD(aa, astride, bb, bstride, out, 1, n);
#else
					ZARR_BINOP(A + B, n, aa, astride, bb, bstride, out);
#endif // SAPF_ACCELERATE
			}
        }
        virtual void pairsz(int n, Z& z, Z *aa, int astride, Z *out) {
			Z b = z;
			LOOP(i,n) { Z a = *aa; out[i] = a + b; b = a; aa += astride; }
			z = b;
		}
		virtual void scanz(int n, Z& z, Z *aa, int astride, Z *out) {
			Z a = z;
			LOOP(i,n) { Z b = *aa; out[i] = a = a + b; aa += astride; }
			z = a;
		}
		virtual void reducez(int n, Z& z, Z *aa, int astride) {
			Z a = z;
			LOOP(i,n) { Z b = *aa; a = a + b; aa += astride; }
			z = a;
		}
		virtual V makeVList(Thread& th, Arg a, Arg b)
		{
			if (a.isReal() && a.f == 0.) return b;
			if (b.isReal() && b.f == 0.) return a;
			return new List(new BinaryOpGen(th, this, a, b));
		}

		virtual V makeZList(Thread& th, Arg a, Arg b)
		{
			if (a.isReal() && a.f == 0.) return b;
			if (b.isReal() && b.f == 0.) return a;
			return new List(new BinaryOpZGen(th, this, a, b));
		}
	};
	BinaryOp_plus gBinaryOp_plus;
	BinaryOp* gBinaryOpPtr_plus = &gBinaryOp_plus;
	BINARY_OP_PRIM(plus)

	struct BinaryOp_plus_link : public BinaryOp {
		virtual const char *Name() { return "plus"; }
		virtual double op(double a, double b) { return a + b; }
		virtual void loopz(int n, const Z *aa, int astride, const Z *bb, int bstride, Z *out) {
			if (astride == 0) {
				if (*aa == 0.) {
					memcpy(out, bb, n * sizeof(Z));
					//LOOP(i,n) { out[i] = *bb; bb += bstride; }
				} else {
#ifdef SAPF_ACCELERATE
					vDSP_vsaddD(const_cast<Z*>(bb), bstride, const_cast<Z*>(aa), out, 1, n);
#else
					ZARR_BINOP(A + B, n, aa, astride, bb, bstride, out);
#endif // SAPF_ACCELERATE
				}
			} else if (bstride == 0 ) {
				if (*bb == 0.) {
					memcpy(out, aa, n * sizeof(Z));
					//LOOP(i,n) { out[i] = *aa; aa += bstride; }
				} else {
#ifdef SAPF_ACCELERATE
					vDSP_vsaddD(const_cast<Z*>(aa), astride, const_cast<Z*>(bb), out, 1, n);
#else
					ZARR_BINOP(A + B, n, aa, astride, bb, bstride, out);
#endif // SAPF_ACCELERATE
				}
			} else {
#ifdef SAPF_ACCELERATE
				vDSP_vaddD(aa, astride, bb, bstride, out, 1, n);
#else
				ZARR_BINOP(A + B, n, aa, astride, bb, bstride, out);
#endif // SAPF_ACCELERATE
			}
		}
		virtual void pairsz(int n, Z& z, Z *aa, int astride, Z *out) {
			Z b = z;
			LOOP(i,n) { Z a = *aa; out[i] = a + b; b = a; aa += astride; }
			z = b;
		}
		virtual void scanz(int n, Z& z, Z *aa, int astride, Z *out) {
			Z a = z;
			LOOP(i,n) { Z b = *aa; out[i] = a = a + b; aa += astride; }
			z = a;
		}
		virtual void reducez(int n, Z& z, Z *aa, int astride) {
			Z a = z;
			LOOP(i,n) { Z b = *aa; a = a + b; aa += astride; }
			z = a;
		}
		virtual V makeVList(Thread& th, Arg a, Arg b)
		{
			if (a.isReal() && a.f == 0.) return b;
			if (b.isReal() && b.f == 0.) return a;
			return new List(new BinaryOpLinkGen(th, this, a, b));
		}

		virtual V makeZList(Thread& th, Arg a, Arg b)
		{
			if (a.isReal() && a.f == 0.) return b;
			if (b.isReal() && b.f == 0.) return a;
			return new List(new BinaryOpLinkZGen(th, this, a, b));
		}
	};
	BinaryOp_plus_link gBinaryOp_plus_link;
	BinaryOp* gBinaryOpPtr_plus_link = &gBinaryOp_plus_link;
	BINARY_OP_PRIM(plus_link)

	struct BinaryOp_minus : public BinaryOp {
		virtual const char *Name() { return "minus"; }
		virtual double op(double a, double b) { return a - b; }
		virtual void loopz(int n, const Z *aa, int astride, const Z *bb, int bstride, Z *out) {
			if (astride == 0) {
#ifdef SAPF_ACCELERATE
				vDSP_vnegD(const_cast<Z*>(bb), bstride, out, 1, n);
#else
				ZARR_BINOP(A - B, n, aa, astride, bb, bstride, out);
#endif // SAPF_ACCELERATE
				if (*aa != 0.) {
#ifdef SAPF_ACCELERATE
					vDSP_vsaddD(const_cast<Z*>(out), 1, const_cast<Z*>(aa), out, 1, n);
#else
					ZARR_BINOP(A - B, n, aa, astride, bb, bstride, out);
#endif // SAPF_ACCELERATE
				}
			} else if (bstride == 0 ) {
				memcpy(out, aa, n * sizeof(Z));
				if (*bb != 0.) {
					Z b = -*bb;
#ifdef SAPF_ACCELERATE
					vDSP_vsaddD(const_cast<Z*>(out), 1, &b, out, 1, n);
#else
					ZARR_BINOP(A - B, n, aa, astride, bb, bstride, out);
#endif // SAPF_ACCELERATE
				}
			} else {
#ifdef SAPF_ACCELERATE
				vDSP_vsubD(bb, bstride, aa, astride, out, 1, n);
#else
				ZARR_BINOP(A - B, n, aa, astride, bb, bstride, out);
#endif // SAPF_ACCELERATE
			}
		}
		virtual void pairsz(int n, Z& z, Z *aa, int astride, Z *out) {
			Z b = z;
			LOOP(i,n) { Z a = *aa; out[i] = a - b; b = a; aa += astride; }
			z = b;
		}
		virtual void scanz(int n, Z& z, Z *aa, int astride, Z *out) {
			Z a = z;
			LOOP(i,n) { Z b = *aa; out[i] = a = a - b; aa += astride; }
			z = a;
		}
		virtual void reducez(int n, Z& z, Z *aa, int astride) {
			Z a = z;
			LOOP(i,n) { Z b = *aa; a = a - b; aa += astride; }
			z = a;
		}
		
		virtual V makeVList(Thread& th, Arg a, Arg b)
		{
			if (a.isReal() && a.f == 0.) return new List(new UnaryOpGen(th, &gUnaryOp_neg, b));
			if (b.isReal() && b.f == 0.) return a;
			return new List(new BinaryOpGen(th, this, a, b));
		}

		virtual V makeZList(Thread& th, Arg a, Arg b)
		{
			if (a.isReal() && a.f == 0.) return new List(new UnaryOpZGen(th, &gUnaryOp_neg, b));
			if (b.isReal() && b.f == 0.) return a;
			return new List(new BinaryOpZGen(th, this, a, b));
		}
	};
	BinaryOp_minus gBinaryOp_minus;
	BinaryOp* gBinaryOpPtr_minus = &gBinaryOp_minus;
	BINARY_OP_PRIM(minus)

	struct BinaryOp_mul : public BinaryOp {
		virtual const char *Name() { return "mul"; }
		virtual double op(double a, double b) { return a * b; }
		virtual void loopz(int n, const Z *aa, int astride, const Z *bb, int bstride, Z *out) {
			if (astride == 0) {
				if (*aa == 1.) {
					LOOP(i,n) { out[i] = *bb; bb += bstride; }
				} else if (*aa == 0.) {
					LOOP(i,n) { out[i] = 0.; }
				} else {
#ifdef SAPF_ACCELERATE
					vDSP_vsmulD(bb, bstride, aa, out, 1, n);
#else
					ZARR_BINOP(A * B, n, aa, astride, bb, bstride, out);
#endif // SAPF_ACCELERATE
				}
			} else if (bstride == 0) {
				if (*bb == 1.) {
					LOOP(i,n) { out[i] = *aa; aa += astride; }
				} else if (*bb == 0.) {
					LOOP(i,n) { out[i] = 0.; }
				} else {
#ifdef SAPF_ACCELERATE
					vDSP_vsmulD(aa, astride, bb, out, 1, n);
#else
                    ZARR_BINOP(A * B, n, aa, astride, bb, bstride, out);
#endif // SAPF_ACCELERATE
				}
			} else {
#ifdef SAPF_ACCELERATE
				vDSP_vmulD(aa, astride, bb, bstride, out, 1, n);
#else
				ZARR_BINOP(A * B, n, aa, astride, bb, bstride, out);
#endif // SAPF_ACCELERATE
			}
		}
		virtual void pairsz(int n, Z& z, Z *aa, int astride, Z *out) {
			Z b = z;
			LOOP(i,n) { Z a = *aa; out[i] = a * b; b = a; aa += astride; }
			z = b;
		}
		virtual void scanz(int n, Z& z, Z *aa, int astride, Z *out) {
			Z a = z;
			LOOP(i,n) { Z b = *aa; out[i] = a = a * b; aa += astride; }
			z = a;
		}
		virtual void reducez(int n, Z& z, Z *aa, int astride) {
			Z a = z;
			LOOP(i,n) { Z b = *aa; a = a * b; aa += astride; }
			z = a;
		}
		
		virtual V makeVList(Thread& th, Arg a, Arg b)
		{
			if (a.isReal()) {
				if (a.f == 1.) return b;
				if (a.f == 0.) return new List(new UnaryOpGen(th, &gUnaryOp_ToZero, b));
				if (a.f == -1.) return new List(new UnaryOpGen(th, &gUnaryOp_neg, b));
			}
			if (b.isReal()) {
				if (b.f == 1.) return a;
				if (b.f == 0.) return new List(new UnaryOpGen(th, &gUnaryOp_ToZero, a));
				if (b.f == -1.) return new List(new UnaryOpGen(th, &gUnaryOp_neg, a));
			}
			return new List(new BinaryOpGen(th, this, a, b));
		}

		virtual V makeZList(Thread& th, Arg a, Arg b)
		{
			if (a.isReal()) {
				if (a.f == 1.) return b;
				if (a.f == 0.) return new List(new UnaryOpZGen(th, &gUnaryOp_ToZero, b));
				if (a.f == -1.) return new List(new UnaryOpZGen(th, &gUnaryOp_neg, b));
			}
			if (b.isReal()) {
				if (b.f == 1.) return a;
				if (b.f == 0.) return new List(new UnaryOpZGen(th, &gUnaryOp_ToZero, a));
				if (b.f == -1.) return new List(new UnaryOpZGen(th, &gUnaryOp_neg, a));
			}
			return new List(new BinaryOpZGen(th, this, a, b));
		}
	};
	BinaryOp_mul gBinaryOp_mul;
	BinaryOp* gBinaryOpPtr_mul = &gBinaryOp_mul;
	BINARY_OP_PRIM(mul)

	struct BinaryOp_div : public BinaryOp {
		virtual const char *Name() { return "div"; }
		virtual double op(double a, double b) { return a / b; }
		virtual void loopz(int n, const Z *aa, int astride, const Z *bb, int bstride, Z *out) {
			if (astride == 0 && *aa == 0.) {
				LOOP(i,n) { out[i] = 0.; }
			} else if (bstride == 0) {
				if (*bb == 1.) {
					LOOP(i,n) { out[i] = *aa; aa += bstride; }
				} else {
					Z rb = 1. / *bb;
#ifdef SAPF_ACCELERATE
					vDSP_vsmulD(const_cast<Z*>(aa), astride, &rb, out, 1, n);
#else
                    ZARR_BINOP(A / B, n, aa, astride, bb, bstride, out);
#endif // SAPF_ACCELERATE
				}
			} else {
#ifdef SAPF_ACCELERATE
				vDSP_vdivD(const_cast<Z*>(bb), bstride, const_cast<Z*>(aa), astride, out, 1, n);
#else
                 ZARR_BINOP(A / B, n, aa, astride, bb, bstride, out);
#endif // SAPF_ACCELERATE
			}
		}
		virtual void pairsz(int n, Z& z, Z *aa, int astride, Z *out) {
			Z b = z;
			LOOP(i,n) { Z a = *aa; out[i] = a / b; b = a; aa += astride; }
			z = b;
		}
		virtual void scanz(int n, Z& z, Z *aa, int astride, Z *out) {
			Z a = z;
			LOOP(i,n) { Z b = *aa; out[i] = a = a / b; aa += astride; }
			z = a;
		}
		virtual void reducez(int n, Z& z, Z *aa, int astride) {
			Z a = z;
			LOOP(i,n) { Z b = *aa; a = a / b; aa += astride; }
			z = a;
		}
		virtual V makeVList(Thread& th, Arg a, Arg b)
		{
			if (a.isReal() && a.f == 0.) return new List(new UnaryOpGen(th, &gUnaryOp_ToZero, b));
			if (b.isReal() && b.f == 1.) return a;
			return new List(new BinaryOpGen(th, this, a, b));
		}

		virtual V makeZList(Thread& th, Arg a, Arg b)
		{
			if (a.isReal() && a.f == 0.) return new List(new UnaryOpZGen(th, &gUnaryOp_ToZero, b));
			if (b.isReal() && b.f == 1.) return a;
			return new List(new BinaryOpZGen(th, this, a, b));
		}
	};
	BinaryOp_div gBinaryOp_div;
	BinaryOp* gBinaryOpPtr_div = &gBinaryOp_div;
	BINARY_OP_PRIM(div)

DEFINE_BINOP_FLOAT(mod, sc_fmod(a, b))
DEFINE_BINOP_FLOAT(remainder, remainder(a, b))

DEFINE_BINOP_INT(idiv, sc_div(a, b))
DEFINE_BINOP_INT(imod, sc_imod(a, b))

DEFINE_BINOP_FLOATVV(pow, 1, sc_pow(a, b), vvpow(out, bb, aa, &n), pow(A, B))

#ifdef SAPF_ACCELERATE
	DEFINE_BINOP_FLOATVV(atan2, 1, atan2(a, b), vvatan2(out, aa, bb, &n), "todo")
#else
	DEFINE_BINOP_FLOATVV_XSIMD(atan2, atan2(a, b),
		auto R = xsimd::atan2(A, B);
	)
#endif


#ifdef _WIN32
	DEFINE_BINOP_FLOAT(Jn, _jn((int)b, a))
	DEFINE_BINOP_FLOAT(Yn, _yn((int)b, a))
#else
	DEFINE_BINOP_FLOAT(Jn, jn((int)b, a))
	DEFINE_BINOP_FLOAT(Yn, yn((int)b, a))
#endif

DEFINE_BINOP_FLOATVV(min, 0, fmin(a, b), vDSP_vminD(const_cast<Z*>(aa), astride, const_cast<Z*>(bb), bstride, out, 1, n), A.min(B))
DEFINE_BINOP_FLOATVV(max, 0, fmax(a, b), vDSP_vmaxD(const_cast<Z*>(aa), astride, const_cast<Z*>(bb), bstride, out, 1, n), A.max(B))
DEFINE_BINOP_FLOAT(dim, fdim(a, b))
DEFINE_BINOP_FLOAT(xor, fdim(a, b))

DEFINE_BINOP_FLOAT(avg2, (a + b) * .5)
DEFINE_BINOP_FLOAT(absdif, fabs(a - b))
DEFINE_BINOP_FLOATVV(hypot, 0, hypot(a, b), vDSP_vdistD(const_cast<Z*>(aa), astride, const_cast<Z*>(bb), bstride, out, 1, n), (A.square() + B.square()).sqrt())
DEFINE_BINOP_FLOAT(sumsq, a*a + b*b)
DEFINE_BINOP_FLOAT(difsq, a*a - b*b)
DEFINE_BINOP_FLOAT(sqsum, sc_squared(a + b))
DEFINE_BINOP_FLOAT(sqdif, sc_squared(a - b))

DEFINE_BINOP_FLOAT(thresh,   a <  b  ? 0. : a)
DEFINE_BINOP_FLOAT(absthresh,  fabs(a) <  b  ? 0. : a)
DEFINE_BINOP_FLOAT(amclip,   b <= 0. ? 0. : a * b)
DEFINE_BINOP_FLOAT(scaleneg, a <  0. ? a * b : a)

DEFINE_BINOP_FLOAT(ring1, a * b + a)
DEFINE_BINOP_FLOAT(ring2, a * b + a + b)
DEFINE_BINOP_FLOAT(ring3, a*a*b)
DEFINE_BINOP_FLOAT(ring4, a*b*(a - b))

DEFINE_BINOP_INT(gcd, sc_gcd(a, b))
DEFINE_BINOP_INT(lcm, sc_lcm(a, b))

DEFINE_BINOP_FLOAT(clip2, std::clamp(a, -b, b))
DEFINE_BINOP_FLOAT(wrap2, sc_wrap(a, -b, b))
DEFINE_BINOP_FLOAT(fold2, sc_fold(a, -b, b))
DEFINE_BINOP_INT(iwrap2, sc_iwrap(a, -b, b))
DEFINE_BINOP_INT(ifold2, sc_ifold(a, -b, b))
DEFINE_BINOP_FLOAT(excess, a - std::clamp(a, -b, b))

DEFINE_BINOP_FLOAT(clip0, std::clamp(a, 0., b))
DEFINE_BINOP_FLOAT(wrap0, sc_wrap(a, 0., b))
DEFINE_BINOP_FLOAT(fold0, sc_fold(a, 0., b))

DEFINE_BINOP_FLOAT(round, sc_round(a, b))
DEFINE_BINOP_FLOAT(roundUp, sc_roundUp(a, b))
DEFINE_BINOP_FLOAT(trunc, sc_trunc(a, b))

#define DEFN(FUNNAME, OPNAME, HELP) 	vm.def(OPNAME, 1, 1, FUNNAME##_, "(x --> z) " HELP);
#define DEFNa(FUNNAME, OPNAME, HELP) 	DEFN(FUNNAME, #OPNAME, HELP)
#define DEF(NAME, HELP) 	DEFNa(NAME, NAME, HELP); 

#define DEFNa2(FUNNAME, OPNAME, HELP) 	\
	(vm.def(#OPNAME, 2, 1, FUNNAME##_, "(x y --> z) " HELP), \
	vm.def(#OPNAME "/", 1, 1, FUNNAME##_reduce_, nullptr), \
	vm.def(#OPNAME "\\", 1, 1, FUNNAME##_scan_, nullptr), \
	vm.def(#OPNAME "^", 1, 1, FUNNAME##_pairs_, nullptr), \
	vm.def(#OPNAME "\\i", 2, 1, FUNNAME##_iscan_, nullptr), \
	vm.def(#OPNAME "^i", 1, 1, FUNNAME##_ipairs_, nullptr));

#define DEF2(NAME, HELP) 	DEFNa2(NAME, NAME, HELP);
#define DEFAM(NAME, MASK, HELP) 	vm.defautomap(#NAME, #MASK, NAME##_, HELP);

void AddMathOps();
void AddMathOps()
{	
	fillSineTable();
	fillDBAmpTable();
	fillDecayTable();
    fillFirstOrderCoeffTable();

	vm.addBifHelp("\n*** unary math ops ***");
	DEF(isalnum, "return whether an ASCII value is alphanumeric.")
	DEF(isalpha, "return whether an ASCII value is alphabetic.")
	DEF(isblank, "return whether an ASCII value is a space or tab character.")
	DEF(iscntrl, "return whether an ASCII value is a control character.")
	DEF(isdigit, "return whether an ASCII value is a digit.")
	DEF(isgraph, "return whether an ASCII value is a graphic character.");
	DEF(islower, "return whether an ASCII value is lower case.")
	DEF(isprint, "return whether an ASCII value is a printable character.")
	DEF(ispunct, "return whether an ASCII value is a punctuation character.")
	DEF(isspace, "return whether an ASCII value is a graphic character.")
	DEF(isupper, "return whether an ASCII value is upper case.")
	DEF(isxdigit, "return whether an ASCII value is a hexadecimal digit.")
	#ifdef _WIN32
		#pragma push_macro("isascii")
		#undef isascii
		DEF(isascii, "return whether a value is ASCII")
		#pragma pop_macro("isascii")
	#else
		DEF(isascii, "return whether a value is ASCII")
	#endif
	

	DEF(tolower, "convert an ASCII character value to lower case.")
	DEF(toupper, "convert an ASCII character value to upper case.")
	#ifdef _WIN32
		#pragma push_macro("toascii")
		#undef toascii
		DEF(toascii, "convert a value to ASCII by stripping the upper bits.")
		#pragma pop_macro("toascii")
	#else
		DEF(toascii, "convert a value to ASCII by stripping the upper bits.")
	#endif

	DEFN(nonpos, "0<=", "less than or equal to zero.")
	DEFN(nonneg, "0>=", "greater than or equal to zero.")
	DEFN(isneg, "0<", "less than zero.")
	DEFN(ispos, "0>", "greater than zero.")
	DEFN(iszero, "0=", "equal to zero.")
	DEFN(iseven, "even?", "is even.")
	DEFN(isodd, "odd?", "is odd.")
	DEFN(isprime, "prime?", "is prime.")
	DEFN(isint, "int?", "is integer.")
	
	DEF(isfinite, "is x a finite number.")
	DEF(isinf, "is x an infinity.")
	DEF(isnan, "is x not a number.")
	DEF(isnormal, "is x a normalized number (as opposed to denormals).")
	DEF(signbit, "sign bit of x.")
		
	DEF(abs, "absolute value.")
	DEF(sgn, "signum function. returns -1 when x < 0, 0 when x == 0, 1 when x > 0.")
	DEFN(not, "~", "logical negation. returns 1 when x == 0, else returns 0.")
	DEF(neg, "negative. -x")
	DEF(sqrt, "square root.")
	DEF(cbrt, "cube root.")
	DEF(rsqrt, "reciprocal square root.")
	DEF(sq, "square. x x *")
	DEF(ssq, "signed square. x x abs *")
	DEF(cb, "x cubed. x 3 ^")
	DEFN(sq, "^2", "x squared. x x *")
	DEFN(cb, "^3", "x cubed. x 3 ^")
	DEFN(pow4, "^4", "x to the fourth power. x 4 ^")
	DEFN(pow5, "^5", "x to the fifth power. x 5 ^")
	DEFN(pow6, "^6", "x to the sixth power. x 6 ^")
	DEFN(pow7, "^7", "x to the seventh power. x 7 ^")
	DEFN(pow8, "^8", "x to the eighth power. x 8 ^")
	DEFN(pow9, "^9", "x to the ninth power. x 9 ^")

	DEF(recip, "reciprocal.")
	DEFN(recip, "1/", "reciprocal. 1 x /")
	DEF(exp, "e to the x.")
	DEF(exp2, "2 to the x.")
	DEF(exp10, "10 to the x.")
	DEFN(exp, "e^", "e to the x.")
	DEFN(exp2, "2^", "2 to the x.")
	DEFN(exp10, "10^", "10 to the x.")
	DEF(expm1, "computes exp(x-1) accurately even for very small values of x.")
	DEF(log, "base e log of x.")
	DEF(log2, "base 2 log of x.")
	DEF(log10, "base 10 log of x.")
	DEF(log1p, "computes the value of log(1+x) accurately even for very small values of x.")
	DEF(logb, "x log2 floor")

	DEF(frac, "fractional part.")
	DEF(floor, "nearest integer <= x.")
	DEF(ceil, "nearest integer >= x.")
	DEF(rint, "nearest integer.")
	DEF(erf, "the error function.")
	DEF(erfc, "the complement of the error function.")

	DEF(sinc, "sinc. x sin x /")
	DEF(sin, "sine.")
	DEF(cos, "cosine.")
	DEF(sin1, "sine(x * 2pi).")
	DEF(cos1, "cosine(x * 2pi).")
	DEF(tan, "tangent.")
	DEF(asin, "arcsine.")
	DEF(acos, "arccosine.")
	DEF(atan, "arctangent.")
	DEF(sinh, "hyperbolic sine.")
	DEF(cosh, "hyperbolic cosine.")
	DEF(tanh, "hyperbolic tangent.")
	DEF(asinh, "hyperbolic arcsine.")
	DEF(acosh, "hyperbolic arccosine.")
	DEF(atanh, "hyperbolic arctangent.")
	
	DEF(J0, "zeroth Bessel function of the first kind evaluated at x.")
	DEF(J1, "first Bessel function of the first kind evaluated at x.")
	DEF(Y0, "zeroth Bessel function of the second kind evaluated at x.")
	DEF(Y1, "first Bessel function of the second kind evaluated at x.")

	DEF(tgamma, "the gamma function.")
	DEF(lgamma, "natural logarithm of the absolute value of the gamma function.")

	DEF(inc, "increment. x 1 +")
	DEF(dec, "decrement. x 1 -")
	DEF(half, "x .5 *")
	DEF(twice, "x 2 *")
	DEFN(inc, "++", "increment. x 1 +")
	DEFN(dec, "--", "decrement. x 1 -")
	DEFN(half, "/2", "half.")
	DEFN(twice, "*2", "twice.")
	DEF(biuni, "convert bipolar to unipolar. .5 * .5 +")
	DEF(unibi, "convert unipolar to bipolar. 2 * 1 -")
	DEF(biunic, "convert bipolar to unipolar with clipping to range. -1 1 clip .5 * .5 +")
	DEF(unibic, "convert unipolar to bipolar with clipping to range. 0 1 clip 2 * 1 -")
	DEF(cmpl, "unipolar complement. 1 x -")

	DEF(ampdb, "convert linear amplitude to decibels.")
	DEF(dbamp, "convert decibels to linear amplitude.")
	
	DEF(ohz, "convert octaves to Hertz. Octave 0.0 is middle C.")
	DEF(hzo, "convert Hertz to octaves. Octave 0.0 is middle C.")
	DEF(nnhz, "convert MIDI note numbers to Hertz. 60 is middle C.")
	DEF(hznn, "convert Hertz to MIDI note numbers. 60 is middle C.")

	DEF(centsratio, "convert an interval in cents to a ratio.")
	DEF(ratiocents, "convert a ratio to an interval in cents.")

	DEF(semiratio, "convert an interval in semitones to a ratio.")
	DEF(ratiosemi, "a ratio to an interval in semitones.")

	DEF(minsec, "convert from minutes to seconds. also for converting from bps to bpm")
	DEF(secmin, "convert from seconds to minutes. also for converting from bpm to bps.")
	DEF(bpmsec, "convert from beats per minute to a period in seconds(e.g. for delay times)")
	DEF(degrad, "convert from degrees to radians.")
	DEF(raddeg, "convert from radians to degrees.")

	DEF(distort, "sigmoid wave distortion function. x/sqrt(1 + x^2)")
	DEF(softclip, "sigmoid wave distortion function. returns x when abs(x) < .5, else returns (abs(x) - .25) / x")
	DEF(sigm, "sigmoid wave distortion function. x/sqrt(1+x*x).")

	DEF(rectWin, "rectangular window for x in the interval [0,1].")
	DEF(triWin, "triangular window for x in the interval [0,1].")
	DEF(bitriWin, "triangular window for x in the interval [-1,1]")
	DEF(hanWin, "hanning window for x in the interval [0,1]")
	DEF(sinWin, "sine window for x in the interval [0,1]")
	DEF(ramp, "return 0 when x <= 0, return x when 0 < x < 1, return 1 when x > 1.")
	DEF(scurve, "return 0 when x <= 0, return 3*x*x - 2*x*x*x when 0 < x < 1, return 1 when x > 1.")

	DEF(zapgremlins, "")

	/////////////////////////////////////////////////////

	vm.addBifHelp("\n*** binary math ops ***");
	vm.addBifHelp("\n   All built-in binary math operators have the following variations defined:");
	vm.addBifHelp("      op/   (list --> z) reducing math operator.");
	vm.addBifHelp("      op\\   (list --> z) scanning math operator.");
	vm.addBifHelp("      op^   (list --> z) pairwise math operator.");
	vm.addBifHelp("      op/i  (list init --> z) reducing math operator with initial value.");
	vm.addBifHelp("      op\\i  (list init --> z) scanning math operator with initial value.");
	vm.addBifHelp("      op^i  (list init --> z) pairwise math operator with initial value.");
	vm.addBifHelp("   For example, + has the following variations: +/  +\\  +^  +/i  +\\i  +^i");
	vm.addBifHelp("");
	

	vm.plusFun = DEFNa2(plus, +, "addition.")
	DEFNa2(plus_link, +>, "addition. For lists, acts as if shorter list were extended with zeroes.")
	DEFNa2(minus, -, "subtraction.")
	vm.mulFun = DEFNa2(mul, *, "multiplication.")
	DEFNa2(div, /, "real division.")
	DEFNa2(mod, %, "modulo.")
	DEF2(idiv, "integer division.")
	DEF2(imod, "integer modulo.")
	DEF2(remainder, "remainder.")

	DEFNa2(lt, <, "less than.")
	DEFNa2(le, <=, "less than or equal.")
	DEFNa2(gt, >, "greater than.")
	DEFNa2(ge, >=, "greater than or equal.")
	DEFNa2(eq, ==, "equal.")
	DEFNa2(ne, !=, "not equal.")

	DEF2(cmp, "returns -1 when x < y, returns 1 when x > y, returns 0 when x == y.")

	DEF2(copysign, "copy the sign of y to the value of x.")
	DEF2(nextafter, "return the next machine representable number from x in direction y.")

	DEF2(pow, "x to the power y.")
	DEFNa2(pow, ^, "x to the power y.")
	DEF2(atan2, "arctangent of y/x.")
	
	DEF2(Jn, "yth Bessel function of the first kind evaluated at x.")
	DEF2(Yn, "yth Bessel function of the second kind evaluated at x.")

	vm.minFun = DEFNa2(min, &, "return the minimum of x and y. functions as logical AND.")
	vm.maxFun = DEFNa2(max, |, "return the maximum of x and y. functions as logical OR.")

	DEF2(avg2, "x y + .5 *")
	DEF2(dim, "positive difference of x and y. x y - 0 |")
	DEF2(absdif, "x y - abs")
	DEF2(hypot, "x sq y sq + sqrt")
	DEF2(sumsq, "x sq y sq +")
	DEF2(difsq, "x sq y sq -")
	DEF2(sqsum, "x y + sq")
	DEF2(sqdif, "x y - sq")

	DEF2(thresh, "returns 0 when x < y, else returns x.")
	DEF2(absthresh, "returns 0 when |x| < y, else returns x.")
	DEF2(amclip, "returns 0 when y <= 0, else returns x*y.")
	DEF2(scaleneg, "returns x*y when x < 0, else returns x.")

	DEF2(ring1, "x y * x +")
	DEF2(ring2, "x y * x + y +")
	DEF2(ring3, "x sq y *")
	DEF2(ring4, "x y * x y - *")

	DEF2(gcd, "greatest common divisor.")
	DEF2(lcm, "least common multiple.")

	DEF2(clip0, "clip x between 0 and y.")
	DEF2(wrap0, "wrap x between 0 and y.")
	DEF2(fold0, "fold x between 0 and y.")

	DEF2(clip2, "clip x between -y and y.")
	DEF2(wrap2, "wrap x between -y and y.")
	DEF2(fold2, "fold x between -y and y.")
	DEF2(iwrap2, "wrap integer x between -y and y.")
	DEF2(ifold2, "fold integer x between -y and y.")
	DEF2(excess, "return the excess after clipping. x x y clip2 -")

	DEF2(round, "round x to nearest multiple of y.")
	DEF2(roundUp, "round x to nearest multiple of y >= x.")
	DEF2(trunc, "round x to nearest multiple of y <= x")

}

