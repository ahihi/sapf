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

#include "Spectrogram.hpp"
#include "makeImage.hpp"
#ifdef SAPF_ACCELERATE
#include <Accelerate/Accelerate.h>
#else
#include <fftw3.h>
#endif // SAPF_ACCELERATE
#include <math.h>
#include <stdio.h>
#include <vector>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>

static void makeColorTable(unsigned char* table);

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

static double i0(double x)
{
	const double epsilon = 1e-18;
	int n = 1;
	double S = 1., D = 1., T;

	while (D > epsilon * S) {
		T = x / (2 * n++);
		D *= T * T;
		S += D;
	}
	return S;
}

static void calcKaiserWindowD(size_t size, double* window, double stopBandAttenuation)
{
	size_t M = size - 1;
	size_t N = M-1;
#if VERBOSE
	printf("FillKaiser %d %g\n", M, stopBandAttenuation);
#endif

	double alpha = 0.;
	if (stopBandAttenuation <= -50.)
		alpha = 0.1102 * (-stopBandAttenuation - 8.7);
    else if (stopBandAttenuation < -21.)
		alpha = 0.5842 * pow(-stopBandAttenuation - 21., 0.4) + 0.07886 * (-stopBandAttenuation - 21.);

	double p = N / 2;
	double kk = 1.0 / i0(alpha);

	for(unsigned int k = 0; k < M; k++ )
	{
		double x = (k-p) / p;
		
		// Kaiser window
		window[k+1] *= kk * bessi0(alpha * sqrt(1.0 - x*x) );
	}
	window[0] = 0.;
	window[size-1] = 0.;
#if VERBOSE
	printf("done\n");
#endif
}

const int border = 8;

void spectrogram(const int size, const double* data, const int width, const int log2bins, const char* path, const double dBfloor)
{
    const int numRealFreqs = 1 << log2bins;
    const int log2n = log2bins + 1;
    const int n = 1 << log2n;
    const int nOver2 = n / 2;
    const double scale = 1./nOver2;
    const int64_t paddedSize = size + n;
    
    std::vector paddedData(paddedSize, 0.0);
    memcpy(paddedData.data() + nOver2, data, size * sizeof(double));

    std::vector dBMags(numRealFreqs + 1, 0.0);

    const double hopSize = size <= n ? 0 : static_cast<double>(size - n) / static_cast<double>(width - 1);

    std::vector window(n, 1.0);
    calcKaiserWindowD(n, window.data(), -180.);

    std::array<unsigned char, 1028> table{};
    makeColorTable(table.data());

    constexpr int heightOfAmplitudeView{128};
    const int heightOfFFT{numRealFreqs + 1};
    const int totalHeight{heightOfAmplitudeView + heightOfFFT + 3 * border};
    constexpr int topOfSpectrum{heightOfAmplitudeView + 2 * border};
    const int totalWidth{width + 2 * border};
    
    Bitmap b{totalWidth, totalHeight};
    b.fillRect(0, 0, totalWidth, totalHeight, 160, 160, 160);
    b.fillRect(border, border, width, heightOfAmplitudeView, 0, 0, 0);

    std::vector windowedData(n, 0.0);

#ifdef SAPF_ACCELERATE
	FFTSetupD fftSetup = vDSP_create_fftsetupD(log2n, kFFTRadix2);
	double* interleavedData = (double*)calloc(n, sizeof(double));
	double* resultData = (double*)calloc(n, sizeof(double));
	DSPDoubleSplitComplex interleaved;
	interleaved.realp = interleavedData;
	interleaved.imagp = interleavedData + nOver2;
	DSPDoubleSplitComplex result;
	result.realp = resultData;
	result.imagp = resultData + nOver2;
#else
    auto fft_out = static_cast<fftw_complex*>(fftw_malloc(sizeof(fftw_complex) * (nOver2 + 1)));
    auto fft_plan = fftw_plan_dft_r2c_1d(n, windowedData.data(), fft_out, FFTW_ESTIMATE);
#endif
    
    double maxmag{0.0};
    double hpos{static_cast<double>(nOver2)};
    
    for (int i = 0; i < width; ++i) {
        const auto ihpos = static_cast<size_t>(hpos);
        
        // Find peak
        double peak{1e-20};
        for (int w = 0; w < n; ++w) {
            double x = std::fabs(paddedData[w + ihpos]);
            peak = std::max(peak, x);
        }
        
        // Apply window function to the data
        for (int64_t w = 0; w < n; ++w) {
            windowedData[w] = window[w] * paddedData[w + ihpos];
        }
        
        // Execute the FFT plan
#ifdef SAPF_ACCELERATE
    	vDSP_ctozD((DSPDoubleComplex*)windowedData.data(), 2, &interleaved, 1, nOver2);
    	vDSP_fft_zropD(fftSetup, &interleaved, 1, &result, 1, log2n, kFFTDirection_Forward);
    	dBMags[0] = result.realp[0] * scale;
    	dBMags[numRealFreqs] = result.imagp[0] * scale;
#else
        fftw_execute(fft_plan);
    	// Process FFT results - DC component (0 frequency)
    	dBMags[0] = fft_out[0][0] * scale; // Real part
    	dBMags[numRealFreqs] = fft_out[0][1] * scale; // Imaginary part
#endif
        
        maxmag = std::max({maxmag, dBMags[0], dBMags[numRealFreqs]});
        
        // Process the remaining frequencies
        for (int64_t j = 1; j < numRealFreqs; ++j) {
#ifdef SAPF_ACCELERATE
        	const double x = result.realp[j] * scale;
        	const double y = result.imagp[j] * scale;
#else
            const double x = fft_out[j][0] * scale; // Real part
            const double y = fft_out[j][1] * scale; // Imaginary part
#endif
            dBMags[j] = std::sqrt(x*x + y*y);     // Magnitude
            maxmag = std::max(maxmag, dBMags[j]);
        }

        // Convert to dB scale
        const double invmag{1.0};
        auto to_dB = [invmag](double val) { return 20.0 * std::log10(val * invmag); };
        
        dBMags[0] = to_dB(dBMags[0]);
        dBMags[numRealFreqs] = to_dB(dBMags[numRealFreqs]);
        
        for (int64_t j = 1; j < numRealFreqs; ++j) {
            dBMags[j] = to_dB(dBMags[j]);
        }
        
        // Set pixels for amplitude view
        {
            const double peakdB = to_dB(peak);
            int peakColorIndex = static_cast<int>(256.0 - peakdB * (256.0 / dBfloor));
            int peakIndex = static_cast<int>(heightOfAmplitudeView - peakdB * (heightOfAmplitudeView / dBfloor));
            
            peakIndex = std::clamp(peakIndex, 0, heightOfAmplitudeView);
            peakColorIndex = std::clamp(peakColorIndex, 0, 255);

            unsigned char* t = table.data() + 4 * peakColorIndex;
            b.fillRect(i + border, border + 128 - peakIndex, 1, peakIndex, t[0], t[1], t[2]);
        }
        
        // Set pixels for spectrogram
        for (int j = 0; j < numRealFreqs; ++j) {
            int colorIndex = static_cast<int>(256.0 - dBMags[j] * (256.0 / dBfloor)); 
            colorIndex = std::clamp(colorIndex, 0, 255);
            
            unsigned char* t = table.data() + 4 * colorIndex;
            b.setPixel(i + border, numRealFreqs - j + topOfSpectrum, t[0], t[1], t[2]);
        }
        
        hpos += hopSize;
    }
    
	b.write(path);
	
#ifdef SAPF_ACCELERATE
	free(interleavedData);
	free(resultData);
#else
    fftw_destroy_plan(fft_plan);
    fftw_free(fft_out);
#endif
}

static void makeColorTable(unsigned char* table)
{
	// white >> red >> yellow >> green >> cyan >> blue >> magenta >> pink >> black
	// 0        -20    -40       -60      -80     -100    -120       -140    -160
	// 255      224    192       160      128     96      64         32      0
	
	int colors[9][4] = {
		{  0,   0,  64, 255},	// dk blue
		{  0,   0, 255, 255},	// blue
		{255,   0,   0, 255},	// red
		{255, 255,   0, 255},	// yellow
		{255, 255, 255, 255}	// white
	};
	
	for (int j = 0; j < 4; ++j) {
		for (int i = 0; i < 64; ++i) {
			for (int k = 0; k < 4; ++k) {
				int x = (colors[j][k] * (64 - i) + colors[j+1][k] * i) / 64;
				if (x > 255) x = 255;
				table[j*64*4 + i*4 + k + 4] = x;
			}
		}
	}
	
	table[0] = 0;
	table[1] = 0;
	table[2] = 0;
	table[3] = 255;
}