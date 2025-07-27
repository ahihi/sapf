
#if defined(SAPF_DOCTEST_H)
#include SAPF_DOCTEST_H
#else
#include <doctest.h>
#endif
#include "Spectrogram.hpp"
#include "ImageHelpers.hpp"
#include <cmath>
#include <vector>
#include <string>
#include <cstdio>

TEST_CASE("Spectrogram generates correct image for sine wave") {
    // Test parameters
    constexpr int sampleRate{44100};
    constexpr double frequency{440.0}; // A4 note
    constexpr int durationSeconds{1};
    constexpr int size{sampleRate * durationSeconds};
    constexpr int width{3200};
    constexpr int log2bins{11}; // 512 frequency bins
    constexpr double dBfloor{-100.0};
    const std::string outputPath{"test_spectrogram_440hz.jpg"};
    
    // Generate a 440 Hz sine wave
    std::vector<double> audioData(size);
    for (int i = 0; i < size; i++) {
        const auto t = static_cast<double>(i) / sampleRate;
        audioData[i] = sin(2.0 * M_PI * frequency * t);
    }
    
    // Generate spectrogram
    spectrogram(size, audioData.data(), width, log2bins, outputPath.c_str(), dBfloor);
    
    // Load the generated image
    const auto image = loadJpegImage(outputPath.c_str());
    
    // Verify image dimensions
    // there is a border so width isn't exact
    CHECK(image.width == 3216);
    CHECK(image.height == 2201); 
    
    // Verify the image content - look for a strong signal at our frequency
    // The frequency is 440Hz, which in a standard spectrogram would be 
    // approximately 1% of the Nyquist frequency (sampleRate/2)
    // So we expect a bright horizontal line around 1% of the height of the image
    
    // For a 440Hz tone in a spectrum with Nyquist frequency of 22050Hz:
    const auto expectedFreqBin = static_cast<int>((frequency / (sampleRate / 2.0)) * image.height);
    
    // Define the frequency bin range where we expect the bright line
    constexpr int minBrightBin{2149};
    constexpr int maxBrightBin{2153};
    
    // Define thresholds for what we consider "bright" and "dark"
    constexpr int brightThreshold{120}; // RGB values above this are considered bright
    constexpr int darkThreshold{10};    // RGB values below this are considered dark
    
    // Counters for checking image content
    int brightPixelsInExpectedRegion{0};
    int brightPixelsOutsideExpectedRegion{0};
    int totalPixelsInExpectedRegion{0};
    int totalPixelsOutsideExpectedRegion{0};

    // Check the entire image (ignoring the upper part and the borders)
    for (int x = 50; x < image.width - 50; ++x) {
        for (int y = 1000; y < image.height - 20; ++y) {
            const auto pixelIdx = (y * image.width + x) * 3;
            
            // Calculate the average brightness of this pixel
            const auto brightness = (image.data[pixelIdx] + 
                                    image.data[pixelIdx + 1] + 
                                    image.data[pixelIdx + 2]) / 3;

            if (y >= minBrightBin && y <= maxBrightBin) {
                // This pixel is in the expected bright region
                totalPixelsInExpectedRegion++;
                if (brightness > brightThreshold) {
                    brightPixelsInExpectedRegion++;
                }
            } else {
                // This pixel should be dark
                totalPixelsOutsideExpectedRegion++;
                if (brightness > darkThreshold) {
                    brightPixelsOutsideExpectedRegion++;
                }
            }
        }
    }
    
    // Calculate percentages of pixels that match our expectations
    const auto percentBrightInExpectedRegion = 
        static_cast<double>(brightPixelsInExpectedRegion) / totalPixelsInExpectedRegion * 100.0;
    
    const auto percentDarkOutsideExpectedRegion = 
        static_cast<double>(totalPixelsOutsideExpectedRegion - brightPixelsOutsideExpectedRegion) / 
        totalPixelsOutsideExpectedRegion * 100.0;
    
    // Output useful information for debugging
    INFO("Expected frequency bin: " << expectedFreqBin << 
         " (range: " << minBrightBin << "-" << maxBrightBin << ")");
    INFO("Bright pixels in expected region: " << percentBrightInExpectedRegion << "%");
    INFO("Dark pixels outside expected region: " << percentDarkOutsideExpectedRegion << "%");
    
    CHECK(percentBrightInExpectedRegion == doctest::Approx(100.0));
    CHECK(percentDarkOutsideExpectedRegion > 99);
    
    // Clean up - remove the test image
    std::remove(outputPath.c_str());
}