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
#include <Cocoa/Cocoa.h>

// Implementation of the wrapper class
class CocoaImageRepWrapper {
public:
    NSBitmapImageRep* mRep;

    CocoaImageRepWrapper(int width, int height) {
        mRep = [[NSBitmapImageRep alloc]
               initWithBitmapDataPlanes:NULL
               pixelsWide:width
               pixelsHigh:height
               bitsPerSample:8
               samplesPerPixel:3
               hasAlpha:NO
               isPlanar:NO
               colorSpaceName:NSCalibratedRGBColorSpace
               bytesPerRow:0
               bitsPerPixel:24];
    }

    ~CocoaImageRepWrapper() {
        [mRep release];
    }
};


// Implementation of Bitmap methods for Cocoa
Bitmap::Bitmap(int width, int height) {
    mWidth = width;
    mHeight = height;
    mRepWrapper = new CocoaImageRepWrapper(width, height);
    mData = [mRepWrapper->mRep bitmapData];
    mBytesPerRow = [mRepWrapper->mRep bytesPerRow];
}

Bitmap::~Bitmap() {
    delete mRepWrapper;
}


void Bitmap::setPixel(const int x, const int y, const int r, const int g, const int b)
{
    size_t index = mBytesPerRow * y + 3 * x;
    unsigned char* data = mData;
	
    data[index+0] = r;
    data[index+1] = g;
    data[index+2] = b;
}

void Bitmap::fillRect(const int x, const int y, const int width, const int height, const int r,
    const int g, const int b)
{
    unsigned char* data = mData;
    for (int j = y; j < y + height; ++j) {
        size_t index = mBytesPerRow * j + 3 * x;
        for (int i = x; i < x + width; ++i) {
            data[index+0] = r;
            data[index+1] = g;
            data[index+2] = b;
            index += 3;
        }
    }
}

void Bitmap::write(const char *path) const
{
    //NSData* data = [bitmap->rep TIFFRepresentation];
    //NSDictionary* properties = @{ NSImageCompressionFactor: @.9 };
    NSDictionary* properties = nullptr;
    NSData* data = [mRepWrapper->mRep representationUsingType: NSJPEGFileType properties: properties];
    NSString* nsstr = [NSString stringWithUTF8String: path];
    [data writeToFile: nsstr atomically: YES];
}