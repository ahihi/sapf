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

#ifndef __taggeddoubles__SoundFiles__
#define __taggeddoubles__SoundFiles__

#include "VM.hpp"

#include "AudioToolboxBuffers.hpp"
#include "PortableBuffers.hpp"
#ifdef SAPF_AUDIOTOOLBOX
typedef AudioToolboxBuffers AudioBuffers;
#else
typedef PortableBuffers AudioBuffers;
#endif

#include "AudioToolboxSoundFile.hpp"
#include "SndfileSoundFile.hpp"
#ifdef SAPF_AUDIOTOOLBOX
typedef AudioToolboxSoundFile SoundFile;
#else
typedef SndfileSoundFile SoundFile;
#endif

const int kMaxSFChannels = 1024;
const int kBufSize = 1024;

void makeRecordingPath(Arg filename, char* path, int len);

#ifdef SAPF_AUDIOTOOLBOX
std::unique_ptr<SoundFile> sfcreate(Thread& th, const char* path, int numChannels, double fileSampleRate, bool interleaved);
#else
// async indicates whether async writing should be supported, or only synchronous writing. They are mutually exclusive.
std::unique_ptr<SoundFile> sfcreate(Thread& th, const char* path, int numChannels, double fileSampleRate, bool interleaved, bool async);
#endif
void sfwrite(Thread& th, V& v, Arg filename, bool openIt);
void sfread(Thread& th, Arg filename, int64_t offset, int64_t frames);

#endif /* defined(__taggeddoubles__SoundFiles__) */
