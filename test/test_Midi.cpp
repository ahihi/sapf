
#include "doctest.h"
#include "Midi.hpp"
#include "PortableMidiPacket.hpp"
#include <vector>

void resetMidiState()
{
    memset(gMidiState, 0, sizeof(gMidiState));
}


#ifdef SAPF_COREMIDI
static MIDIPacket packet;
#else
static std::vector<unsigned char> packet;
#endif

PortableMidiPacket midiPacket(std::initializer_list<unsigned char> bytes)
{
#ifdef SAPF_COREMIDI
    std::fill_n(packet.data, packet.length, 0);
    std::copy(bytes.begin(), bytes.end(), packet.data);
    packet.length = bytes.size();
    packet.timeStamp = 0;
    return PortableMidiPacket{&packet};
#else
    packet = bytes;
    return PortableMidiPacket{packet};
#endif
}

void processPacket(const int status,
    const int channel,
    const int data1,
    const int data2) {
    const auto packet = midiPacket({static_cast<unsigned char>(status | channel),
        static_cast<unsigned char>(data1), static_cast<unsigned char>(data2)});
    midiProcessPacket(packet, 0);
}

void processPacket(const int status,
    const int channel,
    const int data1) {
    const auto packet = midiPacket({static_cast<unsigned char>(status | channel),
        static_cast<unsigned char>(data1)});
    midiProcessPacket(packet, 0);
}

void processRunningStatus(const int data1, const int data2) {
    const auto packet = midiPacket({
        static_cast<unsigned char>(data1), static_cast<unsigned char>(data2)
    });
    midiProcessPacket(packet, 0);
}

void processNoteOn(const int channel, const int note, const int velocity) {
    processPacket(0x90, channel, note, velocity);
}

void processNoteOff(const int channel, const int note) {
    processPacket(0x80, channel, note, 0);
}

void processControlChange(const int channel, const int control, const int value) {
    processPacket(0xB0, channel, control, value);
}

void processProgramChange(const int channel, const int program) {
    processPacket(0xC0, channel, program);
}

void processChannelPressure(const int channel, const int pressure) {
    processPacket(0xD0, channel, pressure);
}

void processPolyPressure(const int channel, const int note, const int pressure) {
    processPacket(0xA0, channel, note, pressure);
}

void processPitchBend(const int channel, const int lsb, const int msb) {
    processPacket(0xE0, channel, lsb, msb);
}

TEST_SUITE("MIDI Packet Processing") {
    TEST_CASE("Test Note On/Off processing") {
        resetMidiState();
        const int channel = 0;
        const int noteNumber = 60;  // Middle C
        const int velocity = 100;

        processNoteOn(channel, noteNumber, velocity);

        CHECK(gMidiState[0][channel].keyvel[noteNumber] == velocity);
        CHECK(gMidiState[0][channel].numKeysDown == 1);
        CHECK(gMidiState[0][channel].lastkey == noteNumber);
        CHECK(gMidiState[0][channel].lastvel == velocity);

        processNoteOff(channel, noteNumber);

        CHECK(gMidiState[0][channel].keyvel[noteNumber] == 0.0f);
        CHECK(gMidiState[0][channel].numKeysDown == 0);
    }

    TEST_CASE("Test Note On with velocity 0 (equivalent to Note Off)") {
        resetMidiState();
        const int channel = 0;
        const int noteNumber = 60;

        processNoteOn(channel, noteNumber, 100);

        CHECK(gMidiState[0][channel].numKeysDown == 1);

        // Now send Note On with velocity 0 (should act as Note Off)

        processNoteOn(channel, noteNumber, 0);

        CHECK(gMidiState[0][channel].keyvel[noteNumber] == 0.0f);
        CHECK(gMidiState[0][channel].numKeysDown == 0);
    }

    TEST_CASE("Test Control Change") {
        resetMidiState();
        const int channel = 1;
        const int controlNumber = 7;  // Volume
        const int controlValue = 100;

        // cc channel 1
        processControlChange(channel, controlNumber, controlValue);

        CHECK(gMidiState[0][channel].control[controlNumber] == controlValue);
    }

    TEST_CASE("Test Program Change") {
        resetMidiState();
        const int channel = 2;
        const int programNumber = 42;

        processProgramChange(channel, programNumber);

        CHECK(gMidiState[0][channel].program == programNumber);
    }

    TEST_CASE("Test Channel Pressure (Aftertouch)") {
        resetMidiState();
        const int channel = 3;
        const int pressureValue = 90;

        processChannelPressure(channel, pressureValue);

        CHECK(gMidiState[0][channel].touch == pressureValue);
    }

    TEST_CASE("Test Poly Pressure (Poly Aftertouch)") {
        // Setup
        resetMidiState();
        const int channel = 4;
        const int noteNumber = 64;
        const int pressureValue = 80;

        processPolyPressure(channel, noteNumber, pressureValue);

        CHECK(gMidiState[0][channel].polytouch[noteNumber] == pressureValue);
    }

    TEST_CASE("Test Pitch Bend") {
        resetMidiState();
        const int channel = 5;
        const int lsb = 0;
        const int msb = 64;  // Center value

        processPitchBend(channel, lsb, msb);

        // Calculate expected value (centered at 0)
        const int bendValue = (msb << 7) | lsb;
        const float expectedValue = (bendValue - 8192) / 8191.0f;

        // Verify Pitch Bend state
        CHECK(gMidiState[0][channel].bend == expectedValue);
    }

    TEST_CASE("Test Running Status") {
        resetMidiState();
        const int channel = 0;

        // First packet with status byte (Note On)
        processNoteOn(0, 60, 100);

        // Verify first note
        CHECK(gMidiState[0][channel].keyvel[60] == 100);
        CHECK(gMidiState[0][channel].numKeysDown == 1);

        // Second packet with running status (status byte of 0 indicates
        // "use previous channel / status"
        processRunningStatus(64, 80);

        // Verify second note - running status should have used previous Note On status
        CHECK(gMidiState[0][channel].keyvel[64] == 80);
        CHECK(gMidiState[0][channel].numKeysDown == 2);
    }

    TEST_CASE("Test Multiple Channels") {
        resetMidiState();

        // Send Note On to channel 0
        processNoteOn(0, 60, 100);

        // Send Control Change to channel 1
        processControlChange(1, 7, 120);

        // Verify states are correctly separated by channel
        CHECK(gMidiState[0][0].keyvel[60] == 100);
        CHECK(gMidiState[0][0].numKeysDown == 1);

        CHECK(gMidiState[0][1].control[7] == 120);
        CHECK(gMidiState[0][1].numKeysDown == 0);
    }
}
