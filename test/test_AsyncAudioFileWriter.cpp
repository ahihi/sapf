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

#include "doctest.h"
#include "AsyncAudioFileWriter.hpp"
#include <filesystem>
#include <fstream>
#include <cstring>
#include <sndfile.h>

using std::string, std::filesystem::exists, std::filesystem::remove, std::filesystem::file_size;

TEST_CASE("AsyncAudioFileWriter writing tests") {
    const string testFileName{"test_async_audio_output.wav"};
    constexpr int sampleRate{44100};
    uint32_t bufferSize, numChannels, numFrames;

    if (exists(testFileName)) {
        remove(testFileName);
    }

    SUBCASE("small single channel") {
        bufferSize = 1024;
        numChannels = 1;
        numFrames = 1024;
    }

    SUBCASE("large single channel") {
        bufferSize = 1024;
        numChannels = 1;
        // value that should exceed the max ring buffer size (1024 * 1024)
        numFrames = 1024*1200;
    }

    SUBCASE("large stereo channel") {
        bufferSize = 1024;
        numChannels = 2;
        numFrames = 1024*520;
    }

    SUBCASE("small 7.1 channel") {
        bufferSize = 1024;
        numChannels = 8;
        numFrames = 1024;
    }

    SUBCASE("large 7.1 channel") {
        bufferSize = 1024;
        numChannels = 8;
        numFrames = 1024*140;
    }

    SUBCASE("6 channels") {
        bufferSize = 1024;
        numChannels = 6;
        numFrames = 1024*100;
    }

    SUBCASE("9 channels") {
        bufferSize = 1024;
        numChannels = 9;
        numFrames = 1024*100;
    }

    CAPTURE(numChannels);
    CAPTURE(numFrames);
    CAPTURE(bufferSize);

    // fill the buffers with a non-repeating pattern that also differs between each channel and keep writing
    // until we've written the desired number of frames
    {
        RtBuffers buffers{numChannels, bufferSize};
        AsyncAudioFileWriter writer(testFileName, sampleRate, numChannels);
        for (size_t bufStartFrame = 0; bufStartFrame < numFrames; bufStartFrame+=bufferSize) {
            for (size_t frame = 0; frame < bufferSize; frame++) {
                for (int channel = 0; channel < numChannels; channel++) {
                    buffers.data(channel)[frame] = -1.0f + ((frame + bufStartFrame + channel) / numFrames)*2;
                }
            }
            writer.writeAsync(buffers, bufferSize);
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

    std::vector<float> buffer(bufferSize * numChannels);

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

#endif
