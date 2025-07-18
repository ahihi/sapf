#include <SoundFiles.hpp>
#ifdef SAPF_AUDIOTOOLBOX
#include "AudioToolboxSoundFile.hpp"

AudioToolboxSoundFile::AudioToolboxSoundFile(ExtAudioFileRef inXAF, uint32_t inNumChannels, std::string inPath)
	: mXAF(inXAF), mNumChannels(inNumChannels), mPath(inPath)
{}

AudioToolboxSoundFile::~AudioToolboxSoundFile() {
	ExtAudioFileDispose(this->mXAF);
}

uint32_t AudioToolboxSoundFile::numChannels() {
	return this->mNumChannels;
}

int AudioToolboxSoundFile::pull(uint32_t *framesRead, AudioBuffers& buffers) {
	return ExtAudioFileRead(this->mXAF, framesRead, buffers.abl);
}

std::unique_ptr<AudioToolboxSoundFile> AudioToolboxSoundFile::open(const char* path, const double threadSampleRate) {
	CFStringRef cfpath = CFStringCreateWithFileSystemRepresentation(0, path);
	if (!cfpath) {
		post("failed to create path\n");
		return nullptr;
	}
	CFReleaser cfpathReleaser(cfpath);
	
	CFURLRef url = CFURLCreateWithFileSystemPath(0, cfpath, kCFURLPOSIXPathStyle, false);
	if (!url) {
		post("failed to create url\n");
		return nullptr;
	}
	CFReleaser urlReleaser(url);
	
	ExtAudioFileRef xaf;
	OSStatus err = ExtAudioFileOpenURL(url, &xaf);

	cfpathReleaser.release();
	urlReleaser.release();
        
	if (err) {
		post("failed to open file %d\n", (int)err);
		return nullptr;
	}

	AudioStreamBasicDescription fileFormat;
	
	UInt32 propSize = sizeof(fileFormat);
	err = ExtAudioFileGetProperty(xaf, kExtAudioFileProperty_FileDataFormat, &propSize, &fileFormat);
	
	int numChannels = fileFormat.mChannelsPerFrame;

	AudioStreamBasicDescription clientFormat = {
		threadSampleRate,
		kAudioFormatLinearPCM,
		kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved,
		static_cast<UInt32>(sizeof(double)),
		1,
		static_cast<UInt32>(sizeof(double)),
		static_cast<UInt32>(numChannels),
		64,
		0
	};
	
	err = ExtAudioFileSetProperty(xaf, kExtAudioFileProperty_ClientDataFormat, sizeof(clientFormat), &clientFormat);
	if (err) {
		post("failed to set client data format\n");
		ExtAudioFileDispose(xaf);
		return {};
	}
	
	err = ExtAudioFileSeek(xaf, 0);
	if (err) {
		post("seek failed %d\n", (int)err);
		ExtAudioFileDispose(xaf);
		return {};
	}

	return std::make_unique<AudioToolboxSoundFile>(xaf, numChannels, path);
}

std::unique_ptr<AudioToolboxSoundFile> AudioToolboxSoundFile::create(const char* path, int numChannels,
                                                                     double threadSampleRate, double fileSampleRate,
                                                                     bool interleaved) {
	if (fileSampleRate == 0.)
		fileSampleRate = threadSampleRate;

	CFStringRef cfpath = CFStringCreateWithFileSystemRepresentation(0, path);
	if (!cfpath) {
		post("failed to create path '%s'\n", path);
		return nullptr;
	}
	CFReleaser cfpathReleaser(cfpath);
	
	CFURLRef url = CFURLCreateWithFileSystemPath(0, cfpath, kCFURLPOSIXPathStyle, false);
	if (!url) {
		post("failed to create url\n");
		return nullptr;
	}
	CFReleaser urlReleaser(url);
	
	AudioStreamBasicDescription fileFormat = {
		fileSampleRate,
		kAudioFormatLinearPCM,
		kAudioFormatFlagsNativeFloatPacked,
		static_cast<UInt32>(sizeof(float) * numChannels),
		1,
		static_cast<UInt32>(sizeof(float) * numChannels),
		static_cast<UInt32>(numChannels),
		32,
		0
	};
	
	int interleavedChannels = interleaved ? numChannels : 1;
	UInt32 interleavedBit = interleaved ? 0 : kAudioFormatFlagIsNonInterleaved;
	
	AudioStreamBasicDescription clientFormat = {
		threadSampleRate,
		kAudioFormatLinearPCM,
		kAudioFormatFlagsNativeFloatPacked | interleavedBit,
		static_cast<UInt32>(sizeof(float) * interleavedChannels),
		1,
		static_cast<UInt32>(sizeof(float) * interleavedChannels),
		static_cast<UInt32>(numChannels),
		32,
		0
	};
		
	ExtAudioFileRef xaf;
	OSStatus err = ExtAudioFileCreateWithURL(url, kAudioFileWAVEType, &fileFormat, nullptr, kAudioFileFlags_EraseFile, &xaf);
	
	if (err) {
		post("failed to create file '%s'. err: %d\n", path, (int)err);
		return nullptr;
	}
	
	err = ExtAudioFileSetProperty(xaf, kExtAudioFileProperty_ClientDataFormat, sizeof(clientFormat), &clientFormat);
	if (err) {
		post("failed to set client data format\n");
		ExtAudioFileDispose(xaf);
		return nullptr;
	}
	
	return std::make_unique<AudioToolboxSoundFile>(xaf, numChannels, path);
}
#endif // SAPF_AUDIOTOOLBOX
