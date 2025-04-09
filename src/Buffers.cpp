#include "Buffers.hpp"

#if defined(SAPF_AUDIOTOOLBOX)
AUBuffers::AUBuffers(AudioBufferList *inIoData)
    : ioData(inIoData)
{}

uint32_t AUBuffers::count() const {
    return this->ioData->mNumberBuffers;
}

float *AUBuffers::data(int channel) const {
    return (float*) this->ioData->mBuffers[channel].mData;
}

uint32_t AUBuffers::size(int channel) const {
    return this->ioData->mBuffers[channel].mDataByteSize;
}
#else

RtBuffers::RtBuffers(float *inOut, uint32_t inCount, uint32_t inSize)
    : out(inOut), theCount(inCount), theSize(inSize)
{}

RtBuffers::RtBuffers(uint32_t inCount, uint32_t inSize)
    : out(new float[inCount*inSize]), theCount(inCount), theSize(inSize) {
}

uint32_t RtBuffers::count() const {
    return this->theCount;
}

float *RtBuffers::data(int channel) const {
    return this->out + channel * this->theSize;
}

uint32_t RtBuffers::size(int channel) const {
    return this->theSize;
}
#endif