#pragma once

#ifdef SAPF_AUDIOTOOLBOX
#include <memory>
#include <AudioToolbox/ExtendedAudioFile.h>

class AudioToolboxSoundFile {
public:
	AudioToolboxSoundFile(ExtAudioFileRef inXAF, uint32_t inNumChannels, std::string inPath);
	~AudioToolboxSoundFile();

	uint32_t numChannels();
	int pull(uint32_t *framesRead, AudioBuffers& buffers);
	
	ExtAudioFileRef mXAF;
	uint32_t mNumChannels;
	std::string mPath;

	static std::unique_ptr<AudioToolboxSoundFile> open(const char* path, double threadSampleRate);
	static std::unique_ptr<AudioToolboxSoundFile> create(const char *path, int numChannels, double threadSampleRate, double fileSampleRate, bool interleaved);
};

#endif // SAPF_AUDIOTOOLBOX
