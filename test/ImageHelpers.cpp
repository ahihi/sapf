
#if defined(SAPF_DOCTEST_H)
#include SAPF_DOCTEST_H
#else
#include <doctest.h>
#endif
#include "helpers/ImageHelpers.hpp"
#include <stdio.h>
#include <jpeglib.h>
#include <memory>
#include <cstring>

ImageData loadJpegImage(const char* path) {
    ImageData result;
    // Use libjpeg to load the image
    const auto infile = fopen(path, "rb");
    if (!infile) {
        FAIL("Failed to open image file: " << path);
        return result;
    }
    
    // Initialize libjpeg structures
    jpeg_decompress_struct cinfo{};
    jpeg_error_mgr jerr{};
    
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, infile);
    
    // Read JPEG header
    if (const auto rc = jpeg_read_header(&cinfo, TRUE); rc != 1) {
        fclose(infile);
        jpeg_destroy_decompress(&cinfo);
        FAIL("Failed to read JPEG header: " << path);
        return result;
    }
    
    // Start decompression
    jpeg_start_decompress(&cinfo);
    
    result.width = cinfo.output_width;
    result.height = cinfo.output_height;
    
    // Allocate memory for the image data
    result.data.resize(result.width * result.height * 3);
    
    // Read scanlines
    JSAMPARRAY buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo, JPOOL_IMAGE, 
                                                 result.width * cinfo.output_components, 1);
    
    int y = 0;
    while (cinfo.output_scanline < cinfo.output_height) {
        jpeg_read_scanlines(&cinfo, buffer, 1);
        
        for (int x = 0; x < result.width; x++) {
            const auto srcOffset = x * cinfo.output_components;
            const auto dstOffset = (y * result.width + x) * 3;
            
            result.data[dstOffset] = buffer[0][srcOffset];         // R
            result.data[dstOffset + 1] = buffer[0][srcOffset + 1]; // G
            result.data[dstOffset + 2] = buffer[0][srcOffset + 2]; // B
        }
        
        y++;
    }
    
    // Finish decompression
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(infile);

    return result;
}