#include "PortableMidiPacket.hpp"

#ifdef SAPF_COREMIDI
PortableMidiPacket::PortableMidiPacket(const MIDIPacket* midiPacket)
    : mMidiPacket{midiPacket} {}

const uint8_t* PortableMidiPacket::bytes() const {
    return (uint8_t*)this->mMidiPacket->data;
}

int PortableMidiPacket::length() const {
    return this->mMidiPacket->length;
}
#else
PortableMidiPacket::PortableMidiPacket(const std::vector<unsigned char> &message)
    : mMessage{message} {}

const uint8_t* PortableMidiPacket::bytes() const {
    return this->mMessage.data();
}

int PortableMidiPacket::length() const {
    return static_cast<int>(this->mMessage.size());
}
#endif


