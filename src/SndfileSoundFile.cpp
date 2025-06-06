#ifndef SAPF_AUDIOTOOLBOX
#include "SndfileSoundFile.hpp"
#include "SoundFiles.hpp"
#include <memory>

#include "SoundFiles.hpp"

using Resampler = r8b::CDSPResampler;
using namespace std;

// de-interleave audio data from interleaved format to channel buffers
void SndfileSoundFile::deInterleaveAudio(const double *const interleavedData, double *const channelBuffer,
                                         const int numFrames, const int numChannels, const int channelIndex) {
	for (sf_count_t frame = 0; frame < numFrames; ++frame) {
        channelBuffer[frame] = interleavedData[frame * numChannels + channelIndex];
    }
}

// read frames from file into interleaved buffer
int SndfileSoundFile::readFramesFromFile(SNDFILE* const sndfile, double* const interleavedBuffer, const int framesToRead) {
	return static_cast<int>(sf_readf_double(sndfile, interleavedBuffer, framesToRead));
}

std::vector<std::unique_ptr<r8b::CDSPResampler> > SndfileSoundFile::initResamplers(
	const int numChannels, const double fileSampleRate, const double threadSampleRate, const int resamplerInputBufLen) {

	std::vector<std::unique_ptr<r8b::CDSPResampler> > resamplers;
	if (std::abs(fileSampleRate - threadSampleRate) > 1e-9) {
		for (int i = 0; i < numChannels; ++i) {
			resamplers.emplace_back(make_unique<Resampler>(fileSampleRate, threadSampleRate, resamplerInputBufLen));
		}
	}
	return resamplers;
}

std::vector<std::vector<double> > SndfileSoundFile::initResamplerInputs(
	const int numChannels, const double fileSampleRate, const double threadSampleRate, const int resamplerInputBufLen) {

	std::vector<std::vector<double> > resamplerInputs;
	if (std::abs(fileSampleRate - threadSampleRate) > 1e-9) {
		for (int i = 0; i < numChannels; ++i) {
			resamplerInputs.emplace_back(resamplerInputBufLen);
		}
	}
	return resamplerInputs;
}

SndfileSoundFile::SndfileSoundFile(std::string path, std::unique_ptr<AsyncAudioFileWriter> writer, SNDFILE *inSndfile,
	const int inNumChannels, const double inFileSampleRate,
	const double inThreadSampleRate, const int maxBufLen) :
	mPath{std::move(path)}, mWriter{std::move(writer)},
	mSndfile{inSndfile}, mNumChannels{inNumChannels},
	mDestToSrcSampleRateRatio(inThreadSampleRate / inFileSampleRate), mResamplerInputBufLen(maxBufLen / mDestToSrcSampleRateRatio),
	mFileFramesRead{0}, mTotalFramesOutput{0}, mExpectedTotalFramesOutput{0}, mAtEndOfFile{false},
	mResamplers(initResamplers(inNumChannels, inFileSampleRate, inThreadSampleRate, mResamplerInputBufLen)),
	mResamplerInputs(initResamplerInputs(inNumChannels, inFileSampleRate, inThreadSampleRate, mResamplerInputBufLen)) {

	mResampleSamplesBeforeOutput = mResamplers.empty() ? 0 : mResamplers[0]->getInLenBeforeOutPos(0);
}

SndfileSoundFile::~SndfileSoundFile() {
	if (this->mSndfile) {
		sf_close(this->mSndfile);
	}
}

uint32_t SndfileSoundFile::numChannels() const {
	return this->mNumChannels;
}

/*
 * Reads data from the input file until hitting the end of the file
 * or the resamplers are going to produce output on their next call.
 * This accounts for the lookahead of the resampler.
 */
void SndfileSoundFile::readUntilResamplerOutput(double* const interleaved) {
	int framesReadFromFile{0};
	while (mResampleSamplesBeforeOutput > 0) {
		const auto framesToRead = min(mResamplerInputBufLen, mResampleSamplesBeforeOutput);
		framesReadFromFile = readFramesFromFile(
			this->mSndfile,
			interleaved,
			framesToRead
		);
		mFileFramesRead += framesReadFromFile;

		if (framesReadFromFile < framesToRead) {
			endOfInputFile();
		}
		if (framesReadFromFile == 0) return;

		// De-interleave and feed in each channel to the resampler
		for (int ch = 0; ch < this->mNumChannels; ++ch) {
			auto resamplerInputBuf = mResamplerInputs[ch].data();
			deInterleaveAudio(interleaved, resamplerInputBuf, framesReadFromFile, this->mNumChannels, ch);
			double *resamplerOutput;
			mResamplers[ch]->process(resamplerInputBuf, framesReadFromFile, resamplerOutput);
		}

		mResampleSamplesBeforeOutput -= framesReadFromFile;
	}
}

// update necessary state when we have finished reading everything from the input file.
void SndfileSoundFile::endOfInputFile() {
	mAtEndOfFile = true;
	mExpectedTotalFramesOutput = static_cast<sf_count_t>(static_cast<double>(mFileFramesRead) * mDestToSrcSampleRateRatio);
}

// direct read without resampling and de-interleave
void SndfileSoundFile::pullWithoutResampling(uint32_t *framesRead, PortableBuffers &buffers,
                                             const int requestedOutputFrames) const {
	buffers.interleaved.resize(requestedOutputFrames * this->mNumChannels * sizeof(double));
	const auto interleaved = reinterpret_cast<double *>(buffers.interleaved.data());

	const auto framesReallyRead = readFramesFromFile(this->mSndfile, interleaved, requestedOutputFrames);

  *framesRead = framesReallyRead;

	// De-interleave each channel
	for (int ch = 0; ch < this->mNumChannels; ++ch) {
		auto *buf = static_cast<double *>(buffers.buffers[ch].data);
		deInterleaveAudio(interleaved, buf, framesReallyRead, this->mNumChannels, ch);
	}
}

// read some data from the input file if there still is some, until we hit the end of the file
// or we've filled the output buffer.
// Returns number of frames emitted.
uint32_t SndfileSoundFile::readAndResample(PortableBuffers &buffers, const int requestedOutputFrames, double * const interleaved) {
	const int inputFramesToRead = static_cast<int>(requestedOutputFrames / mDestToSrcSampleRateRatio);
	const int fileFramesRead = readFramesFromFile(this->mSndfile, interleaved, inputFramesToRead);
	mFileFramesRead += fileFramesRead;

	if (fileFramesRead < inputFramesToRead) {
		endOfInputFile();
	}
	if (fileFramesRead > 0) {
		int outputSamples = 0;
		for (int ch = 0; ch < this->mNumChannels; ++ch) {
			auto resamplerInputBuf = mResamplerInputs[ch].data();
			deInterleaveAudio(interleaved, resamplerInputBuf, fileFramesRead, this->mNumChannels, ch);
			double *resamplerOutput;
			outputSamples = mResamplers[ch]->process(resamplerInputBuf, fileFramesRead, resamplerOutput);

			// Copy from resampler output buffer to final output buffer.
			// Note because the resampler uses its own internal buffer to hold its output, we cannot
			// emit the resampler output DIRECTLY to the target - hence the need for copying.
			if (outputSamples > 0) {
				memcpy(buffers.buffers[ch].data, resamplerOutput, outputSamples * sizeof(double));
			}
		}
		mTotalFramesOutput += outputSamples;
		return outputSamples;
	}
	return 0;
}

// if we're at the end of the file but we haven't yet output the "tail" of the resampler
// output caused by lookahead,
// feed 0s to the resampler until we fill the buffer or we reach the end of expected output. This
// is needed to consume the "tail" of the resampler (since it uses lookahead)
// Returns number of samples output.
uint32_t SndfileSoundFile::resampleTail(uint32_t framesAlreadyOutput, PortableBuffers &buffers, const int requestedOutputFrames) {
	const int framesToOutput = min(static_cast<int>(mExpectedTotalFramesOutput - mTotalFramesOutput),
	                               requestedOutputFrames - static_cast<int>(framesAlreadyOutput));
	const int framesToInput = static_cast<int>(static_cast<double>(framesToOutput) / mDestToSrcSampleRateRatio);

	int outputSamples{0};
	for (int ch = 0; ch < this->mNumChannels; ++ch) {
		auto resamplerInputBuf = mResamplerInputs[ch].data();
		memset(resamplerInputBuf, 0, framesToInput * sizeof(double));
		double *resamplerOutput;
		outputSamples = mResamplers[ch]->process(resamplerInputBuf, framesToInput, resamplerOutput);

		if (outputSamples > 0) {
			// it's + framesAlreadyOutput because in the event we hit the end of the file this iteration,
			// we may not have filled the buffer but we may have read some samples already, so we
			// we need to preserve what we already read and append to those values.
			memcpy(static_cast<double *>(buffers.buffers[ch].data) + framesAlreadyOutput, resamplerOutput, outputSamples * sizeof(double));
		}
	}

	mTotalFramesOutput += outputSamples;
	return outputSamples;
}

void SndfileSoundFile::pullWithResampling(uint32_t *framesRead, PortableBuffers &buffers, const int requestedOutputFrames) {
	buffers.interleaved.resize(mResamplerInputBufLen * this->mNumChannels * sizeof(double));
	const auto interleaved = reinterpret_cast<double *>(buffers.interleaved.data());

	if (mResampleSamplesBeforeOutput > 0) {
		readUntilResamplerOutput(interleaved);
	}

	if (!mAtEndOfFile) {
		*framesRead += readAndResample(buffers, requestedOutputFrames, interleaved);
	}


	if (mAtEndOfFile &&
	    mTotalFramesOutput < mExpectedTotalFramesOutput) {
		*framesRead += resampleTail(*framesRead, buffers, requestedOutputFrames);
	}
}

int SndfileSoundFile::pull(uint32_t *framesRead, PortableBuffers& buffers) {
	const int requestedOutputFrames = static_cast<int>(*framesRead);
	*framesRead = 0;

    if (!mResamplers.empty()) {
        pullWithResampling(framesRead, buffers, requestedOutputFrames);
    } else {
		pullWithoutResampling(framesRead, buffers, requestedOutputFrames);
    }

	return 0;
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

unique_ptr<SndfileSoundFile> SndfileSoundFile::open(const char *path, const double threadSampleRate, const int maxBufLen) {
	SNDFILE *sndfile = nullptr;
	SF_INFO sfinfo = {0};

	if ((sndfile = sf_open(path, SFM_READ, &sfinfo)) == nullptr) {
		post("failed to open file %s\n", sf_strerror(NULL));
		sf_close(sndfile);
		return nullptr;
	}

	uint32_t numChannels = sfinfo.channels;

	sf_count_t seek_result;
	if ((seek_result = sf_seek(sndfile, 0, SEEK_SET) < 0)) {
		post("failed to seek file %d\n", seek_result);
		sf_close(sndfile);
		return nullptr;
	}

	return make_unique<SndfileSoundFile>(path, nullptr, sndfile, numChannels, sfinfo.samplerate, threadSampleRate,
	                                     maxBufLen);
}

// NOTE: ATTOW, the fileSampleRate is always passed as 0, so the thread sample rate is always used.
//  (>sf / >sfo doesn't even provide a way to specify the sample rate)
unique_ptr<SndfileSoundFile> SndfileSoundFile::create(const char *path, const int numChannels,
                                                      const double threadSampleRate, double fileSampleRate,
                                                      const bool async) {
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

	return make_unique<SndfileSoundFile>(path, std::move(writer), sndfile, numChannels, fileSampleRate, threadSampleRate, 0);
}
#endif // SAPF_AUDIOTOOLBOX
