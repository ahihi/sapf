#pragma once

#include <vector>

// Simple structure to hold image data
struct ImageData {
	int width;
	int height;
	std::vector<unsigned char> data; // RGB format
};

// Function to load a JPEG image
ImageData loadJpegImage(const char* path);