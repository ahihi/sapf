
#include "ImageHelpers.hpp"
#include <Cocoa/Cocoa.h>

ImageData loadJpegImage(const char* path) {
    ImageData result;
    
    // Use Cocoa to load the image
    const auto imagePath = [NSString stringWithUTF8String:path];
    const auto imageData = [NSData dataWithContentsOfFile:imagePath];

    if (!imageData) {
        return result;
    }
    
    const auto rep = [NSBitmapImageRep imageRepWithData:imageData];
    if (!rep) {
        [rep release];
        return result;
    }

    result.width = [rep pixelsWide];
    result.height = [rep pixelsHigh];
    
    // Copy image data
    const auto srcData = [rep bitmapData];
    const auto bytesPerRow = [rep bytesPerRow];

    result.data.resize(result.width * result.height * 3);

    for (int y = 0; y < result.height; y++) {
        for (int x = 0; x < result.width; x++) {
            // note it's x * 4 because internally Cocoa has an "unused" alpha channel even though
            // this is a jpeg with no alpha channel
            const auto srcOffset = y * bytesPerRow + x * 4;
            const auto dstOffset = (y * result.width + x) * 3;
            
            result.data[dstOffset] = srcData[srcOffset];         // R
            result.data[dstOffset + 1] = srcData[srcOffset + 1]; // G
            result.data[dstOffset + 2] = srcData[srcOffset + 2]; // B
        }
    }
    
    [rep release];
    return result;
}