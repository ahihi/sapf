#ifndef SAPF_AUDIOTOOLBOX
#include "SndfileSoundFile.hpp"

SndfileSoundFile::SndfileSoundFile(SNDFILE *inSndfile, int inNumChannels)
	: mSndfile(inSndfile), mNumChannels(inNumChannels)
{}
	
SndfileSoundFile::~SndfileSoundFile() {
	sf_close(this->mSndfile);
}

uint32_t SndfileSoundFile::numChannels() {
	return this->mNumChannels;
}

int SndfileSoundFile::pull(uint32_t *framesRead, PortableBuffers& buffers) {
	buffers.interleaved.resize(*framesRead * this->mNumChannels * sizeof(double));
	double *interleaved = (double *) buffers.interleaved.data();
	sf_count_t framesReallyRead = sf_readf_double(this->mSndfile, interleaved, *framesRead);

	int result = 0;
	if(framesReallyRead >= 0) {
		*framesRead = framesReallyRead;
	} else {
		*framesRead = 0;
		result = framesReallyRead;
	}

	for(int ch = 0; ch < this->mNumChannels; ch++) {
		double *buf = (double *) buffers.buffers[ch].data;
		for(sf_count_t frame = 0; frame < framesReallyRead; frame++) {
			buf[frame] = interleaved[frame * this->mNumChannels + ch];
		}
	}
	
	return result;
}

std::unique_ptr<SndfileSoundFile> SndfileSoundFile::open(const char *path, double threadSampleRate) {
	SNDFILE *sndfile = nullptr;
	SF_INFO sfinfo = {0};
		
	if((sndfile = sf_open(path, SFM_READ, &sfinfo)) == nullptr) {
		post("failed to open file %s\n", sf_strerror(NULL));
		sf_close(sndfile);
		return nullptr;
	}

	uint32_t numChannels = sfinfo.channels;

	sf_count_t seek_result;
	if((seek_result = sf_seek(sndfile, 0, SEEK_SET) < 0)) {
		post("failed to seek file %d\n", seek_result);
		sf_close(sndfile);
		return nullptr;
	}
		
	return std::make_unique<SndfileSoundFile>(sndfile, numChannels);
}

std::unique_ptr<SndfileSoundFile> SndfileSoundFile::create(const char *path, int numChannels, double threadSampleRate, double fileSampleRate, bool interleaved) {
	// if (fileSampleRate == 0.)
	//	fileSampleRate = threadSampleRate;

	// CFStringRef cfpath = CFStringCreateWithFileSystemRepresentation(0, path);
	// if (!cfpath) {
	//	post("failed to create path '%s'\n", path);
	//	return nullptr;
	// }
	// CFReleaser cfpathReleaser(cfpath);
	
	// CFURLRef url = CFURLCreateWithFileSystemPath(0, cfpath, kCFURLPOSIXPathStyle, false);
	// if (!url) {
	//	post("failed to create url\n");
	//	return nullptr;
	// }
	// CFReleaser urlReleaser(url);
	
	// AudioStreamBasicDescription fileFormat = {
	//	fileSampleRate,
	//	kAudioFormatLinearPCM,
	//	kAudioFormatFlagsNativeFloatPacked,
	//	static_cast<UInt32>(sizeof(float) * numChannels),
	//	1,
	//	static_cast<UInt32>(sizeof(float) * numChannels),
	//	static_cast<UInt32>(numChannels),
	//	32,
	//	0
	// };
	
	// int interleavedChannels = interleaved ? numChannels : 1;
	// UInt32 interleavedBit = interleaved ? 0 : kAudioFormatFlagIsNonInterleaved;
	
	// AudioStreamBasicDescription clientFormat = {
	//	threadSampleRate,
	//	kAudioFormatLinearPCM,
	//	kAudioFormatFlagsNativeFloatPacked | interleavedBit,
	//	static_cast<UInt32>(sizeof(float) * interleavedChannels),
	//	1,
	//	static_cast<UInt32>(sizeof(float) * interleavedChannels),
	//	static_cast<UInt32>(numChannels),
	//	32,
	//	0
	// };
		
	// ExtAudioFileRef xaf;
	// OSStatus err = ExtAudioFileCreateWithURL(url, kAudioFileWAVEType, &fileFormat, nullptr, kAudioFileFlags_EraseFile, &xaf);
	
	// if (err) {
	//	post("failed to create file '%s'. err: %d\n", path, (int)err);
	//	return nullptr;
	// }
	
	// err = ExtAudioFileSetProperty(xaf, kExtAudioFileProperty_ClientDataFormat, sizeof(clientFormat), &clientFormat);
	// if (err) {
	//	post("failed to set client data format\n");
	//	ExtAudioFileDispose(xaf);
	//	return nullptr;
	// }
	
	// return new AudioToolboxSoundFile(xaf, numChannels);

	return std::make_unique<SndfileSoundFile>(nullptr, numChannels);
}
#endif // SAPF_AUDIOTOOLBOX
