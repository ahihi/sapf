#ifdef SAPF_AUDIOTOOLBOX
#include "AudioToolboxBuffers.hpp"

AudioToolboxBuffers::AudioToolboxBuffers(int inNumChannels) {
	this->abl = (AudioBufferList*)calloc(1, sizeof(AudioBufferList) + (inNumChannels - 1) * sizeof(AudioBuffer));
	this->abl->mNumberBuffers = inNumChannels;
}

AudioToolboxBuffers::~AudioToolboxBuffers() {
	free(this->abl);
}

uint32_t AudioToolboxBuffers::numChannels() {
	return this->abl->mNumberBuffers;
}

void AudioToolboxBuffers::setNumChannels(size_t i, uint32_t numChannels) {
	this->abl->mBuffers[i].mNumberChannels = numChannels;
}

void AudioToolboxBuffers::setData(size_t i, void *data) {
	this->abl->mBuffers[i].mData = data;
}

void AudioToolboxBuffers::setSize(size_t i, uint32_t size) {
	this->abl->mBuffers[i].mDataByteSize = size;
}
#endif
