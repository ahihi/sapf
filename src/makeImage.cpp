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

#include "makeImage.hpp"
#include <stdio.h>
#include <jpeglib.h>
#include <fstream>
#include "Object.hpp"

// Bitmap class implementation
Bitmap::Bitmap(const int width, const int height) 
    : mWidth(width), mHeight(height), mBytesPerRow(width * 3) {
    // Initialize the vector with zeros
    mData.resize(mBytesPerRow * height, 0);
}

Bitmap::~Bitmap() = default;

void Bitmap::setPixel(const int x, const int y, const int r, const int g, const int b) {
    if (x < 0 || x >= mWidth || y < 0 || y >= mHeight) {
        return;  // Out of bounds
    }
    
    const auto index = mBytesPerRow * y + 3 * x;
    mData[index + 0] = r;
    mData[index + 1] = g;
    mData[index + 2] = b;
}

void Bitmap::fillRect(const int x, const int y, const int width, const int height, const int r,
    const int g, const int b) {
    // Clip rectangle to bitmap boundaries
    const auto x1 = std::max(0, x);
    const auto y1 = std::max(0, y);
    const auto x2 = std::min(mWidth, x + width);
    const auto y2 = std::min(mHeight, y + height);
    
    for (int j = y1; j < y2; ++j) {
        size_t index = mBytesPerRow * j + 3 * x1;
        for (int i = x1; i < x2; ++i) {
            mData[index + 0] = r;
            mData[index + 1] = g;
            mData[index + 2] = b;
            index += 3;
        }
    }
}

void Bitmap::write(const char* path) const {
    // Open the output file
    const auto outfile = fopen(path, "wb");
    if (!outfile) {
        post("could not open '%s'\n", path);
        return;
    }

    // Initialize JPEG compression structures
    jpeg_compress_struct cinfo{};
    jpeg_error_mgr jerr{};
    
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, outfile);
    
    // Set image parameters
    cinfo.image_width = mWidth;
    cinfo.image_height = mHeight;
    cinfo.input_components = 3;  // RGB without alpha as JPEG doesn't support alpha
    cinfo.in_color_space = JCS_RGB;
    
    // Set default compression parameters
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 90, TRUE);
    
    // Start compression
    jpeg_start_compress(&cinfo, TRUE);
    
    // Create a temporary buffer for RGB data
    std::vector<unsigned char> row(mWidth * 3);
    
    // Write scanlines
    while (cinfo.next_scanline < cinfo.image_height) {
        const unsigned char* src = mData.data() + cinfo.next_scanline * mBytesPerRow;
        
        for (int i = 0; i < mWidth; i++) {
            row[i*3 + 0] = src[i*3 + 0];  // R
            row[i*3 + 1] = src[i*3 + 1];  // G
            row[i*3 + 2] = src[i*3 + 2];  // B
        }
        
        JSAMPROW row_pointer = row.data();
        jpeg_write_scanlines(&cinfo, &row_pointer, 1);
    }
    
    // Finish compression
    jpeg_finish_compress(&cinfo);
    
    // Close the file
    fclose(outfile);
    
    // Release the JPEG compression object
    jpeg_destroy_compress(&cinfo);
}