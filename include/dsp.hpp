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

#ifndef __taggeddoubles__dsp__
#define __taggeddoubles__dsp__

#ifdef SAPF_ACCELERATE
#include <Accelerate/Accelerate.h>
#else
#include <fftw3.h>
#endif // SAPF_ACCELERATE

const int kMinFFTLogSize = 2;
const int kMaxFFTLogSize = 16;

class FFT {
public:
    ~FFT();
    void init(size_t log2n);
    void forward(double *inReal, double *inImag, double *outReal, double *outImag);
    void backward(double *inReal, double *inImag, double *outReal, double *outImag);
    void forward_in_place(double *ioReal, double *ioImag);
    void backward_in_place(double *ioReal, double *ioImag);
    void forward_real(double *inReal, double *outReal, double *outImag);
    void backward_real(double *inReal, double *inImag, double *outReal);

    size_t n;
    size_t log2n;
private:
#ifdef SAPF_ACCELERATE
    FFTSetupD setup;
#else
    fftw_complex *in;
    fftw_complex *out;
    fftw_plan forward_plan;
    fftw_plan backward_plan;
#endif // SAPF_ACCELERATE    
};

extern FFT ffts[kMaxFFTLogSize+1];

void initFFT();
void fft (int n, double* ioReal, double* ioImag);
void ifft(int n, double* ioReal, double* ioImag);

void fft (int n, double* inReal, double* inImag, double* outReal, double* outImag);
void ifft(int n, double* inReal, double* inImag, double* outReal, double* outImag);

void rfft(int n, double* inReal, double* outReal, double* outImag);
void rifft(int n, double* inReal, double* inImag, double* outReal);

#endif /* defined(__taggeddoubles__dsp__) */
