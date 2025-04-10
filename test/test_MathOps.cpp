#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "Object.hpp"
#include "doctest.h"
#include <array>

using std::array;

#define CHECK_ARR(expected, actual, n) \
	do { \
		LOOP(i,n) { CHECK(out[i] == doctest::Approx(expected[i]).epsilon(1e-9)); } \
	} while (0)

void check_unop_loopz(UnaryOp& op, const array<Z, 3> in) {
	double out[3];
	double expected[3] = {op.op(in[0]), op.op(in[1]), op.op(in[2])};
	op.loopz(3, in.data(), 1, out);
	CHECK_ARR(expected, out, 3);
}

void check_unop_loopz(UnaryOp& op) {
	check_unop_loopz(op, {1, 2, 3});
}

extern UnaryOp* gUnaryOpPtr_neg;
extern UnaryOp* gUnaryOpPtr_abs;
extern UnaryOp* gUnaryOpPtr_frac;
extern UnaryOp* gUnaryOpPtr_floor;
extern UnaryOp* gUnaryOpPtr_ceil;
extern UnaryOp* gUnaryOpPtr_rint;
extern UnaryOp* gUnaryOpPtr_recip;
extern UnaryOp* gUnaryOpPtr_sqrt;
extern UnaryOp* gUnaryOpPtr_rsqrt;
extern UnaryOp* gUnaryOpPtr_ssq;
extern UnaryOp* gUnaryOpPtr_sq;
extern UnaryOp* gUnaryOpPtr_exp;
extern UnaryOp* gUnaryOpPtr_exp2;
extern UnaryOp* gUnaryOpPtr_expm1;
extern UnaryOp* gUnaryOpPtr_log;
extern UnaryOp* gUnaryOpPtr_log2;
extern UnaryOp* gUnaryOpPtr_log10;
extern UnaryOp* gUnaryOpPtr_log1p;
extern UnaryOp* gUnaryOpPtr_sin;
extern UnaryOp* gUnaryOpPtr_cos;
extern UnaryOp* gUnaryOpPtr_sin1;
extern UnaryOp* gUnaryOpPtr_cos1;
extern UnaryOp* gUnaryOpPtr_tan;
extern UnaryOp* gUnaryOpPtr_atan;
extern UnaryOp* gUnaryOpPtr_sinh;
extern UnaryOp* gUnaryOpPtr_cosh;
extern UnaryOp* gUnaryOpPtr_tanh;
extern UnaryOp* gUnaryOpPtr_asinh;
extern UnaryOp* gUnaryOpPtr_acosh;
extern UnaryOp* gUnaryOpPtr_inc;
extern UnaryOp* gUnaryOpPtr_dec;
extern UnaryOp* gUnaryOpPtr_half;
extern UnaryOp* gUnaryOpPtr_twice;
extern UnaryOp* gUnaryOpPtr_biuni;
extern UnaryOp* gUnaryOpPtr_unibi;
extern UnaryOp* gUnaryOpPtr_biunic;
extern UnaryOp* gUnaryOpPtr_unibic;
extern UnaryOp* gUnaryOpPtr_ampdb;
extern UnaryOp* gUnaryOpPtr_degrad;
extern UnaryOp* gUnaryOpPtr_raddeg;
extern UnaryOp* gUnaryOpPtr_minsec;
extern UnaryOp* gUnaryOpPtr_secmin;
extern UnaryOp* gUnaryOpPtr_bpmsec;
extern UnaryOp* gUnaryOpPtr_asin;
extern UnaryOp* gUnaryOpPtr_acos;
extern UnaryOp* gUnaryOpPtr_atanh;

#define CHECK_UNOP(op) \
	do { \
		SUBCASE(#op) { \
			check_unop_loopz(*gUnaryOpPtr_##op); \
		} \
	} while (0)

#define CHECK_UNOP_NORMALIZED(op) \
	do { \
		SUBCASE(#op) { \
			check_unop_loopz(*gUnaryOpPtr_##op, {-.1, 0, .1}); \
		} \
	} while (0)

TEST_CASE("unary ops") {
	CHECK_UNOP(neg);
	CHECK_UNOP(abs);
	CHECK_UNOP(frac);
	CHECK_UNOP(floor);
	CHECK_UNOP(ceil);
	CHECK_UNOP(rint);
	CHECK_UNOP(recip);
	CHECK_UNOP(sqrt);
	CHECK_UNOP(rsqrt);
	CHECK_UNOP(ssq);
	CHECK_UNOP(sq);
	CHECK_UNOP(exp);
	CHECK_UNOP(exp2);
	CHECK_UNOP(expm1);
	CHECK_UNOP(log);
	CHECK_UNOP(log2);
	CHECK_UNOP(log10);
	CHECK_UNOP(log1p);
	CHECK_UNOP(sin);
	CHECK_UNOP(cos);
	CHECK_UNOP(sin1);
	CHECK_UNOP(cos1);
	CHECK_UNOP(tan);
	CHECK_UNOP(atan);
	CHECK_UNOP(sinh);
	CHECK_UNOP(cosh);
	CHECK_UNOP(tanh);
	CHECK_UNOP(asinh);
	CHECK_UNOP(acosh);
	CHECK_UNOP(inc);
	CHECK_UNOP(dec);
	CHECK_UNOP(half);
	CHECK_UNOP(twice);
	CHECK_UNOP(biuni);
	CHECK_UNOP(unibi);
	CHECK_UNOP(biunic);
	CHECK_UNOP(unibic);
	CHECK_UNOP(ampdb);
	CHECK_UNOP(degrad);
	CHECK_UNOP(raddeg);
	CHECK_UNOP(minsec);
	CHECK_UNOP(secmin);
	CHECK_UNOP(bpmsec);
	CHECK_UNOP_NORMALIZED(asin);
	CHECK_UNOP_NORMALIZED(acos);
	CHECK_UNOP_NORMALIZED(atanh);
}

extern BinaryOp* gBinaryOpPtr_plus;
extern BinaryOp* gBinaryOpPtr_minus;
extern BinaryOp* gBinaryOpPtr_mul;
extern BinaryOp* gBinaryOpPtr_div;
extern BinaryOp* gBinaryOpPtr_copysign;
extern BinaryOp* gBinaryOpPtr_pow;
extern BinaryOp* gBinaryOpPtr_min;
extern BinaryOp* gBinaryOpPtr_max;
extern BinaryOp* gBinaryOpPtr_hypot;

void check_binop_loopz(BinaryOp& op, const array<Z, 3> a, int astride, const array<Z, 3> b, int bstride) {
	double out[3];
	double expected[3] = {op.op(a[0], b[0]), op.op(a[1], b[1]), op.op(a[2], b[2])};
	op.loopz(3, a.data(), 1, b.data(), 1, out);
	CHECK_ARR(expected, out, 3);
}

void check_binop_loopz(BinaryOp& op, int astride, int bstride) {
	check_binop_loopz(op, {1, 2, 3}, astride, {4, 5, 6}, bstride);
}

#define CHECK_IDENTITY_BINOP(op) \
	do { \
		SUBCASE(#op) { \
			SUBCASE("stride 1") { check_binop_loopz(*gBinaryOpPtr_##op, 1, 1); } \
			SUBCASE("stride 0") { check_binop_loopz(*gBinaryOpPtr_##op, 0, 0); } \
			SUBCASE("astride 1") { check_binop_loopz(*gBinaryOpPtr_##op, 1, 0); } \
			SUBCASE("bstride 1") { check_binop_loopz(*gBinaryOpPtr_##op, 0, 1); } \
			SUBCASE("a 0") { check_binop_loopz(*gBinaryOpPtr_##op, {0}, 0, {4, 5, 6}, 1); } \
			SUBCASE("b 0") { check_binop_loopz(*gBinaryOpPtr_##op, {1, 2, 3}, 1, {4, 5, 6}, 0); } \
		} \
	} while (0)

TEST_CASE("identity optimized binops") {
	CHECK_IDENTITY_BINOP(plus);
	CHECK_IDENTITY_BINOP(minus);
	CHECK_IDENTITY_BINOP(div);
	CHECK_IDENTITY_BINOP(mul);
}

#define CHECK_BINOP(op) \
	do { \
		SUBCASE(#op) { \
			check_binop_loopz(*gBinaryOpPtr_##op, 1, 1); \
		} \
	} while (0)

TEST_CASE("other binops") {
	CHECK_BINOP(copysign);
	CHECK_BINOP(pow);
	CHECK_BINOP(min);
	CHECK_BINOP(max);
	CHECK_BINOP(hypot);
}

TEST_CASE("binop copysign negative handling") {
	check_binop_loopz(*gBinaryOpPtr_copysign, {-1, -2, -3}, 1, {4, 5, 6}, 1);
	check_binop_loopz(*gBinaryOpPtr_copysign, {1, 2, 3}, 1, {-4, -5, -6}, 1);
	check_binop_loopz(*gBinaryOpPtr_copysign, {-1, -2, -3}, 1, {-4, -5, -6}, 1);
}