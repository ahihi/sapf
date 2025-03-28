#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "Object.hpp"
#include "doctest.h"
#include <array>


#define CHECK_ARR(expected, actual, n) \
	do { \
		LOOP(i,n) { CHECK(out[i] == doctest::Approx(expected[i]).epsilon(1e-9)); } \
	} while (0)


// so we can access ops directly
extern BinaryOp* gBinaryOpPtr_plus;
extern BinaryOp* gBinaryOpPtr_minus;
extern BinaryOp* gBinaryOpPtr_mul;
extern BinaryOp* gBinaryOpPtr_div;

using std::array;

void check_binop_loopz(BinaryOp& op, const std::array<Z, 3> a, int astride, const std::array<Z, 3> b, int bstride) {
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

TEST_CASE("identity binops") {
	CHECK_IDENTITY_BINOP(plus);
	CHECK_IDENTITY_BINOP(minus);
	CHECK_IDENTITY_BINOP(div);
	CHECK_IDENTITY_BINOP(mul);
}