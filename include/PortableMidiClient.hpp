#pragma once

#ifdef SAPF_COREMIDI
    #include <CoreMidi/CoreMidi.h>
    #include "object.hpp"
#else
    #include <vector>
    #include "RtMidi.h"
#endif
#include <memory>

const int kMaxMidiPorts = 16;

// represents some kind of
// stateful midi client with associated input and output ports that
// can be connected
class PortableMidiClient {
public:
#ifdef SAPF_COREMIDI
    PortableMidiClient(int numIn, int numOut, MIDIReadProc midiReadProc, MIDINotifyProc midiNotifyProc);
#else
    PortableMidiClient(int numIn, int numOut, RtMidiIn::RtMidiCallback midiCallback);
#endif
    ~PortableMidiClient();

    [[nodiscard]] int numMidiInPorts() const;
    [[nodiscard]] int numMidiOutPorts() const;

    // connect the specified MIDI input endpoint to this client's input port
    void connectInputPort(int uid, int inputIndex) const;
    void disconnectInputPort(int uid, int inputIndex) const;

    // print the list of midi endpoints to stdout
    static void printMIDIEndpoints();
private:
    #ifdef SAPF_COREMIDI
        int mNumMidiInPorts;
        int mNumMidiOutPorts;
        MIDIClientRef mMIDIClient;
        MIDIPortRef mMIDIInPort[kMaxMidiPorts], mMIDIOutPort[kMaxMidiPorts];
    #else
        std::vector<std::unique_ptr<RtMidiIn>> mMIDIInPorts;
        std::vector<std::unique_ptr<RtMidiOut>> mMIDIOutPorts;
    #endif

};