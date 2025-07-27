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
#pragma once

#include <vector>

// Forward declaration instead of including Cocoa directly
#ifdef SAPF_COCOA
    // Use opaque pointer instead of direct NSBitmapImageRep*
    class CocoaImageRepWrapper;
#endif

/*
 * Represents an RGB Bitmap which will be written to a JPEG.
 */
class Bitmap {
public:
    Bitmap(int width, int height);
    ~Bitmap();
    
    void setPixel(int x, int y, int r, int g, int b);
    void fillRect(int x, int y, int width, int height, int r, int g, int b);
    void write(const char* path) const;
private:
    int mWidth;
    int mHeight;
    int mBytesPerRow;
    #ifdef SAPF_COCOA
        CocoaImageRepWrapper* mRepWrapper;
        unsigned char* mData;
    #else
        std::vector<unsigned char> mData;
    #endif
};