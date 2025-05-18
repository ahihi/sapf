#pragma once

#include "Object.hpp"
#include "ZArr.hpp"

void hanning_(Thread& th, Prim* prim);
void hamming_(Thread& th, Prim* prim);
void blackman_(Thread& th, Prim* prim);
#ifdef SAPF_ACCELERATE
void wseg_apply_window(Z* segbuf, Z* window, int n);
#else
void wseg_apply_window(Z* segbuf, ZArr window, int n);
#endif