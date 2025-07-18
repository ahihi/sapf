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

#ifndef SAPF_AUDIOTOOLBOX

#if defined(SAPF_DOCTEST_H)
#include SAPF_DOCTEST_H
#else
#include <doctest.h>
#endif
#include "SndfileSoundFile.hpp"
#include <filesystem>
#include <fstream>
#include <valarray>
#include <sndfile.h>
#include <fftw3.h>
#include <complex>

using std::string, std::filesystem::exists, std::filesystem::remove, std::filesystem::file_size,
    std::unique_ptr, std::vector, std::pair, std::make_pair, std::move;

TEST_CASE("SndfileSoundFile file writing") {
    const string testFileName{"test_async_audio_output.wav"};
    constexpr int sampleRate{44100};
    constexpr uint32_t bufferSize{1024};
    uint32_t numChannels, numFrames;

    if (exists(testFileName)) {
        remove(testFileName);
    }

    SUBCASE("small single channel") {
        numChannels = 1;
        numFrames = 1024;
    }

    SUBCASE("large single channel") {
        numChannels = 1;
        numFrames = 1024*1200;
    }

    SUBCASE("large stereo channel") {
        numChannels = 2;
        numFrames = 1024*520;
    }

    SUBCASE("small 7.1 channel") {
        numChannels = 8;
        numFrames = 1024;
    }

    SUBCASE("large 7.1 channel") {
        numChannels = 8;
        numFrames = 1024*140;
    }

    SUBCASE("6 channels") {
        numChannels = 6;
        numFrames = 1024*100;
    }

    SUBCASE("9 channels") {
        numChannels = 9;
        numFrames = 1024*100;
    }

    CAPTURE(numChannels);
    CAPTURE(numFrames);

    // fill the buffer (interleaved) with a non-repeating pattern that also differs between each channel and keep writing
    // until we've written the desired number of frames
    {
		vector<float> buf(numChannels * bufferSize, 0);
        PortableBuffers bufs{1};
		const auto sndfile = SndfileSoundFile::create(testFileName.c_str(), numChannels,
		    sampleRate, 0., false);
		bufs.setNumChannels(0, numChannels);
		bufs.setData(0, &buf[0]);
		bufs.setSize(0, bufferSize * sizeof(float));


        for (size_t bufStartFrame = 0; bufStartFrame < numFrames; bufStartFrame+=bufferSize) {
            for (size_t frame = 0; frame < bufferSize; frame++) {
                for (int channel = 0; channel < numChannels; channel++) {
                    buf[frame * numChannels + channel] = -1.0f + ((frame + bufStartFrame + channel) / numFrames)*2;
                }
            }
			sndfile->write(bufferSize, bufs);
        }
    }

    // ensure file was closed (by trying to open it again)
    {
        std::ifstream testFile{testFileName, std::ios::binary};
        CHECK(testFile.is_open());
    }

    // check the file contents
    SF_INFO sfinfo;
    SNDFILE *sndfile = sf_open(testFileName.c_str(), SFM_READ, &sfinfo);
    REQUIRE(sndfile != nullptr);
    CHECK(sfinfo.channels == numChannels);
    CHECK(sfinfo.samplerate == sampleRate);

    vector<float> buffer(bufferSize * numChannels);

    sf_count_t framesRead{0};
    sf_count_t totalFrames{0};

    while ((framesRead = sf_readf_float(sndfile, buffer.data(), bufferSize)) > 0) {
        for (size_t frame = 0; frame < framesRead; frame++) {
            for (size_t channel = 0; channel < numChannels; channel++) {
                float expectedValue = -1.0f + ((frame + totalFrames + channel) / numFrames) * 2;
                CHECK(buffer[frame * numChannels + channel] == doctest::Approx(expectedValue).epsilon(1e-5));
            }
        }
        totalFrames += framesRead;
    }

    CHECK(totalFrames == numFrames);
    sf_close(sndfile);

    // Clean up the test file
    if (exists(testFileName)) {
        remove(testFileName);
    }
}

double find_dominant_frequency(const vector<std::complex<double>>& spectrum, const int bufferSize, const double sampleRate) {
    auto peakBin{0};
    auto maxMag{0.0};

    std::vector<double> magnitudes(spectrum.size());
    for (int i = 0; i < spectrum.size(); ++i) {
        const auto re{spectrum[i].real()};
        const auto im{spectrum[i].imag()};
        magnitudes[i] = std::sqrt(re * re + im * im);
        if (magnitudes[i] > maxMag) {
            maxMag = magnitudes[i];
            peakBin = i;
        }
    }

    // Do parabolic interpolation around the peak (if not on edge)
    auto delta = 0.0;
    if (peakBin > 0 && peakBin < spectrum.size() - 1) {
        const auto a{magnitudes[peakBin - 1]};
        const auto b{magnitudes[peakBin]};
        const auto c{magnitudes[peakBin + 1]};

        if (b > 0) {
            delta = 0.5 * (a - c) / (a - 2 * b + c);
        }
    }

    const auto interpBin{peakBin + delta};
    return interpBin * sampleRate / bufferSize;
}

// resampling is audio-based, so we need to write something that actually resembles sound.
// We will write a SinOsc at 440 + 100 * channel Hz to each channel and see if it remains the most dominant
// frequency in the output fft
void writeTestSignal(const string &fileName, const int numChannels, const int numInputFrames, const double srcSampleRate,
                     const int bufferSize) {
    SF_INFO sfinfo{
        .samplerate = static_cast<int>(srcSampleRate),
        .channels = numChannels,
        .format = SF_FORMAT_WAV | SF_FORMAT_DOUBLE
    };
    SNDFILE *outfile = sf_open(fileName.c_str(), SFM_WRITE, &sfinfo);

    vector<double> buf(numChannels * bufferSize, 0);

    // Since we're writing to file first, the signal needs
    // to be interleaved from the start
    int framesWritten{0};
    while (framesWritten < numInputFrames) {
        const int framesToWrite  = std::min(numInputFrames - framesWritten, bufferSize);
        for (int frame = 0; frame < framesToWrite; frame++) {
            for (int channel = 0; channel < numChannels; channel++) {
                constexpr auto amp = 0.9;
                const auto t = (framesWritten + frame) / srcSampleRate;
                const auto freq = 440.0 + 100 * channel;
                buf[frame * numChannels + channel] = amp * sin(2.0 * M_PI * freq * t);
            }
        }
        sf_writef_double(outfile, buf.data(), framesToWrite);
        framesWritten += framesToWrite;
    }
    sf_close(outfile);
}

// return the buffers with their actual backing vector.
// because portablebuffers do not carry their own backing vector with them for RAII,
// the caller must ensure these pair elements share the same lifetime.
pair<unique_ptr<PortableBuffers>, vector<vector<double>>> createPortableBuffers(const int numChannels, const int bufferSize) {
    auto portableBufs = std::make_unique<PortableBuffers>(numChannels);
    vector<vector<double>> bufData(numChannels);
    for (int channel = 0; channel < numChannels; ++channel) {
        bufData[channel].resize(bufferSize);
        portableBufs->setData(channel, bufData[channel].data());
        portableBufs->setSize(channel, bufferSize * sizeof(double));
    }

    return {move(portableBufs), move(bufData)};
}

SF_INFO createSndfileInfo(const int numChannels, const double sampleRate) {
    return SF_INFO {
        .samplerate = static_cast<int>(sampleRate),
        .channels = numChannels,
        .format = SF_FORMAT_WAV | SF_FORMAT_DOUBLE};
}

pair<SNDFILE*,vector<double>> openResampleOutputFile(
    const int numChannels, const double dstSampleRate, const int bufferSize) {
    auto info = createSndfileInfo(numChannels, dstSampleRate);
    const string testDstFileName{"test_read_resampled.wav"};
    const auto resampledFile = sf_open(testDstFileName.c_str(), SFM_WRITE, &info);
    vector<double> outbuf(numChannels * bufferSize, 0);
    return {resampledFile, move(outbuf)};
}

pair<vector<fftw_plan>,vector<std::complex<double>>> createFFTs(const int numChannels,
    const int bufferSize, const PortableBuffers& portableBufs) {
    vector<fftw_plan> fftPlans;
    vector<std::complex<double>> fftOutputBuf(bufferSize/2 + 1);
    for (int channel = 0; channel < numChannels; channel++) {
        fftPlans.emplace_back(fftw_plan_dft_r2c_1d(bufferSize, static_cast<double *>(portableBufs.buffers[channel].data),
            reinterpret_cast<fftw_complex*>(fftOutputBuf.data()), FFTW_ESTIMATE|FFTW_PRESERVE_INPUT));
    }
    return {move(fftPlans), move(fftOutputBuf)};
}

TEST_CASE("SndfileSoundFile file reading with resampling") {
    const string testFileName{"test_read_resample.wav"};
    constexpr int bufferSize{1024};
    int numChannels{0}, numInputFrames{0}, framesToPull{bufferSize};
    double srcSampleRate{44100};

    if (exists(testFileName)) {
        remove(testFileName);
    }

    SUBCASE("single channel") {
        numChannels = 1;
        numInputFrames = 1024*10;
    }

    SUBCASE("single channel upsample") {
        numChannels = 1;
        numInputFrames = 1024*10;
        srcSampleRate = 22050;
    }

    SUBCASE("single channel downsample") {
        numChannels = 1;
        numInputFrames = 1024*10;
        srcSampleRate = 88200;
    }

    SUBCASE("stereo channel") {
        numChannels = 2;
        numInputFrames = 1024*10;
    }

    SUBCASE("stereo channel upsample") {
        numChannels = 2;
        numInputFrames = 1024*10;
        srcSampleRate = 22050;
    }

    SUBCASE("stereo channel downsample") {
        numChannels = 2;
        numInputFrames = 1024*10;
        srcSampleRate = 88200;
    }

    SUBCASE("8 channel") {
        numChannels = 8;
        numInputFrames = 1024*10;
    }

    SUBCASE("8 channel upsample") {
        numChannels = 8;
        numInputFrames = 1024*10;
        srcSampleRate = 22050;
    }

    SUBCASE("8 channel downsample") {
        numChannels = 8;
        numInputFrames = 1024*10;
        srcSampleRate = 88200;
    }


    SUBCASE("8 channel upsample odd frames") {
        numChannels = 8;
        numInputFrames = 800*9+11;
        srcSampleRate = 22050;
        framesToPull = 777;
    }

    SUBCASE("8 channel downsample odd frames") {
        numChannels = 8;
        numInputFrames = 800*9+11;
        srcSampleRate = 88200;
        framesToPull = 777;
    }

    CAPTURE(numChannels);
    CAPTURE(numInputFrames);
    CAPTURE(bufferSize);
    CAPTURE(framesToPull);
    CAPTURE(srcSampleRate);

    writeTestSignal(testFileName, numChannels, numInputFrames, srcSampleRate, bufferSize);

    // read with sample rate conversion
    {
        // set below to true to capture the resampled output to
        // an output file for manual verification
        constexpr bool outputResampled{true};
        constexpr double dstSampleRate{44100};

        auto srcFile = SndfileSoundFile::open(testFileName.c_str(), dstSampleRate,
                                              bufferSize);

        auto [portableBufs, bufData] = createPortableBuffers(numChannels, bufferSize);

        SNDFILE* resampledFile;
        vector<double> resampledFileInputBuf;
        if (outputResampled) {
            std::tie(resampledFile, resampledFileInputBuf) = openResampleOutputFile(numChannels, dstSampleRate, bufferSize);
        }

        // since conversion is not exact at the sample level, it's kind of
        // hard to validate that resampling worked without some complicated
        // feature extraction, but we will use fft at least to check
        // the dominant frequency
        auto [fftPlans, fftOutputBuf] =
            createFFTs(numChannels, bufferSize, *portableBufs);

        const int expectedOutputFrames = static_cast<int>(dstSampleRate / srcSampleRate * numInputFrames);
        uint32_t framesPulled{0};
        uint32_t totalFramesPulled{0};

        int iteration{0};
        // pull until the end of input, with safeguard for infinite loop
        while (iteration++ < 10000) {
            framesPulled = framesToPull;
            const auto result = srcFile->pull(&framesPulled, *portableBufs);
            CHECK(result == 0);
            if (framesPulled == 0) break;
            for (int channel = 0; channel < numChannels; channel++) {
                // only bother checking once we have a sufficient amount of frames pulled for a good fft
                if (framesPulled == bufferSize) {
                    fftw_execute(fftPlans[channel]);
                    auto dominantFreq{find_dominant_frequency(fftOutputBuf, bufferSize, dstSampleRate)};
                    constexpr auto tolerance{10};
                    const auto expectedFreq{440. + 100 * channel};
                    CHECK(dominantFreq < expectedFreq + tolerance);
                    CHECK(dominantFreq > expectedFreq - tolerance);
                }

                if (outputResampled) {
                    for (int frame = 0; frame < framesPulled; frame++) {
                        const double actualSample{static_cast<double *>(portableBufs->buffers[channel].data)[frame]};
                        resampledFileInputBuf[frame * numChannels + channel] = actualSample;
                    }
                }
            }
            totalFramesPulled += framesPulled;
            if (outputResampled) {
                sf_writef_double(resampledFile, resampledFileInputBuf.data(), framesPulled);
            }
        }
        if (outputResampled) {
            sf_close(resampledFile);
        }
        for (int channel = 0; channel < numChannels; channel++) {
            fftw_destroy_plan(fftPlans[channel]);
        }
        CHECK(totalFramesPulled == expectedOutputFrames);
    }

    // ensure file was closed (by trying to open it again)
    {
        std::ifstream testFile{testFileName, std::ios::binary};
        CHECK(testFile.is_open());
    }

    if (exists(testFileName)) {
        remove(testFileName);
    }
}

#endif
