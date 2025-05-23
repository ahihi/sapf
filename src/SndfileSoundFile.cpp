#ifndef SAPF_AUDIOTOOLBOX
#include "SndfileSoundFile.hpp"
#include "SoundFiles.hpp"
#include <memory>


SndfileSoundFile::SndfileSoundFile(std::string path, std::unique_ptr<AsyncAudioFileWriter> writer, SNDFILE *inSndfile,
	const int inNumChannels) :
	mPath{std::move(path)}, mWriter{std::move(writer)},
	mSndfile{inSndfile}, mNumChannels{inNumChannels} {
}
	
SndfileSoundFile::~SndfileSoundFile() {
	if (this->mSndfile) {
		sf_close(this->mSndfile);
	}
}

uint32_t SndfileSoundFile::numChannels() const {
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

void SndfileSoundFile::write(const int numFrames, const PortableBuffers& bufs) const {
	if (const auto written{sf_write_float(mSndfile, (const float*) bufs.buffers[0].data, numFrames * bufs.buffers[0].numChannels)}; written <= 0) {
		const auto error{sf_strerror(mSndfile)};
		printf("failed to write audio data to file - %s\n", error);
	}
}

void SndfileSoundFile::writeAsync(const RtBuffers& buffers, const unsigned int nBufferFrames) const {
	mWriter->writeAsync(buffers, nBufferFrames);
}

std::unique_ptr<SndfileSoundFile> SndfileSoundFile::open(const char *path) {
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

	// TODO: to be implemented in https://github.com/chairbender/sapf/pull/8
	return nullptr;
}

// NOTE: ATTOW, interleaved is always passed as true, and
//  the fileSampleRate is always passed as 0, so the thread sample rate is always used.
//  (>sf / >sfo doesn't even provide a way to specify the sample rate)
std::unique_ptr<SndfileSoundFile> SndfileSoundFile::create(const char *path, const int numChannels,
                                                           const double threadSampleRate, double fileSampleRate, const bool async) {
	if (fileSampleRate == 0.)
		fileSampleRate = threadSampleRate;

	std::unique_ptr<AsyncAudioFileWriter> writer;
	SNDFILE *sndfile = nullptr;
	if (async) {
		writer = std::make_unique<AsyncAudioFileWriter>(path, fileSampleRate, numChannels);
	} else {
		SF_INFO sfinfo{
			.samplerate = static_cast<int>(fileSampleRate),
			.channels = numChannels,
			.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT};
		sndfile = sf_open(path, SFM_WRITE, &sfinfo);
		if (!sndfile) {
			const auto error{sf_strerror(sndfile)};
			printf("failed to open %s: %s\n", path, error);
			throw errNotFound;
		}
	}

	return std::make_unique<SndfileSoundFile>(path, std::move(writer), sndfile, numChannels);
}
#endif // SAPF_AUDIOTOOLBOX
