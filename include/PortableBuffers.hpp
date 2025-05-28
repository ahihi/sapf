#pragma once

#ifndef SAPF_AUDIOTOOLBOX
#include <cstdint>
#include <vector>

#include "VM.hpp"

struct PortableBuffer {
	// this is unused unless interleaved audio data is being put into a single buffer, in which
	// case it indicates the number of channels in that interleaved audio data.
	uint32_t numChannels;
	// note: I THINK it's intended that this is a size in BYTES, not elements - similar
	// to the interleaved vector (it is sized for bytes)
	uint32_t size;
	// TODO: changing this to be a unique_ptr or vector, and using a template to parameterize
	//  the type, might make this easier to work with. Currently, the user is required to ensure that
	//  the backing buffer's lifetime matches the lifetime of this object and to perform casting.
	void *data;

	PortableBuffer();
};

class PortableBuffers {
public:
	explicit PortableBuffers(int inNumChannels);
	~PortableBuffers();

	uint32_t numChannels();
	void setNumChannels(size_t i, uint32_t numChannels);
	void setData(size_t i, void *data);
	void setSize(size_t i, uint32_t size);
	
	std::vector<PortableBuffer> buffers;
	// TODO: switching this to be a template parameter (float or double) might improve the usability of this field.
	//  Currently, the user has to remember that it's sized for bytes even though it actually holds doubles / floats
	//  and has to keep that in mind when resizing.
	std::vector<uint8_t> interleaved;
};
#endif // SAPF_AUDIOTOOLBOX
