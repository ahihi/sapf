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

#ifndef __taggeddoubles__Midi__
#define __taggeddoubles__Midi__

#include <stdint.h>

#include "PortableMidiClient.hpp"
#include "PortableMidiPacket.hpp"

void AddMidiOps();

//below is only really needed for doctest
struct MidiChanState
{
  uint8_t control[128];
  uint8_t polytouch[128];
  uint8_t keyvel[128];
  uint32_t numKeysDown;
  uint16_t bend;
  uint8_t touch;
  uint8_t program;
  uint8_t lastkey;
  uint8_t lastvel;
};

extern MidiChanState gMidiState[kMaxMidiPorts][16];
extern bool gMidiDebug;
extern uint8_t gRunningStatus;
extern bool gSysexFlag;

void midiProcessPacket(const PortableMidiPacket& pkt, int srcIndex);
int midiProcessSystemPacket(const PortableMidiPacket& pkt, int chan);

#endif /* defined(__taggeddoubles__Midi__) */
