#pragma once
#include <cstdint>
#ifdef SAPF_COREMIDI
#include <CoreMidi/CoreMidi.h>
#else
#include <vector>
#endif

// Cross platform midi packet
struct PortableMidiPacket {
public:
    #ifdef SAPF_COREMIDI
        PortableMidiPacket(const MIDIPacket* midiPacket);
    #else
        explicit PortableMidiPacket(const std::vector<unsigned char>& message);
    #endif
    // number of MIDI bytes in this packet
    [[nodiscard]] int length() const;
    // variable length stream of midi messages
    [[nodiscard]] const uint8_t* bytes() const;
private:
    #ifdef SAPF_COREMIDI
        const MIDIPacket* mMidiPacket;
    #else
        const std::vector<unsigned char> mMessage;
    #endif
};