//    SAPF - Sound As Pure Form
//    Copyright (C) 2019 James McCartney
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "SoundFiles.hpp"
#include <valarray>

extern char gSessionTime[256];

class SFReaderOutputChannel;

class SFReader : public Object
{
	std::unique_ptr<SoundFile> mSoundFile;
	AudioBuffers mBuffers;
	SFReaderOutputChannel* mOutputs;
	int64_t mFramesRemaining;
	bool mFinished = false;
	
public:
	
	SFReader(std::unique_ptr<SoundFile> inSoundFile, int64_t inDuration);
	
	~SFReader();

	virtual const char* TypeName() const override { return "SFReader"; }

	P<List> createOutputs(Thread& th);
	
	bool pull(Thread& th);
	void fulfillOutputs(int blockSize);
	void produceOutputs(int shrinkBy);
};

class SFReaderOutputChannel : public Gen
{
	friend class SFReader;
	P<SFReader> mSFReader;
	SFReaderOutputChannel* mNextOutput = nullptr;
	Z* mDummy = nullptr;
	
public:	
	SFReaderOutputChannel(Thread& th, SFReader* inSFReader)
        : Gen(th, itemTypeZ, true), mSFReader(inSFReader)
	{
	}
	
	~SFReaderOutputChannel()
	{
		if (mDummy) free(mDummy);
	}
	
	virtual void norefs() override
	{
		mOut = nullptr; 
		mSFReader = nullptr;
	}
		
	virtual const char* TypeName() const override { return "SFReaderOutputChannel"; }
	
	virtual void pull(Thread& th) override
	{
		if (mSFReader->pull(th)) {
			end();
		}
	}
	
};

SFReader::SFReader(std::unique_ptr<SoundFile> inSoundFile, int64_t inDuration) :
	mSoundFile(std::move(inSoundFile)),
	mBuffers(mSoundFile->numChannels()),
	mFramesRemaining(inDuration)
{
	
}

SFReader::~SFReader()
{
	SFReaderOutputChannel* output = mOutputs;
	do {
		SFReaderOutputChannel* next = output->mNextOutput;
		delete output;
		output = next;
	} while (output);
}

void SFReader::fulfillOutputs(int blockSize)
{
	SFReaderOutputChannel* output = mOutputs;
	size_t bufSize = blockSize * sizeof(Z);
	for (int i = 0; output; ++i, output = output->mNextOutput){
		Z* out;
		if (output->mOut)
			out = output->mOut->fulfillz(blockSize);
		else {
			if (!output->mDummy)
				output->mDummy = (Z*)calloc(output->mBlockSize, sizeof(Z));

			out = output->mDummy;
		}

		this->mBuffers.setNumChannels(i, 1);
		this->mBuffers.setData(i, out);
		this->mBuffers.setSize(i, bufSize);
		
		memset(out, 0, bufSize);
	};
}

void SFReader::produceOutputs(int shrinkBy)
{
	SFReaderOutputChannel* output = mOutputs;
	do {
		if (output->mOut)
			output->produce(shrinkBy);
		output = output->mNextOutput;
	} while (output);
}

P<List> SFReader::createOutputs(Thread& th)
{
	const uint32_t numChannels = mSoundFile->numChannels();
	P<List> s = new List(itemTypeV, numChannels);
	
	// fill s->mArray with ola's output channels.
	SFReaderOutputChannel* last = nullptr;
	P<Array> a = s->mArray;
	for (uint32_t i = 0; i < numChannels; ++i) {
		SFReaderOutputChannel* c = new SFReaderOutputChannel(th, this);
		if (last) last->mNextOutput = c;
		else mOutputs = c;
		last = c;
		a->add(new List(c));
	}
	
	return s;
}

bool SFReader::pull(Thread& th)
{
	if (mFramesRemaining == 0) 
		mFinished = true;

	if (mFinished) 
		return true;
	
	SFReaderOutputChannel* output = mOutputs;
	int blockSize = output->mBlockSize;
	if (mFramesRemaining > 0)
		blockSize = (int)std::min(mFramesRemaining, (int64_t)blockSize);
	
	fulfillOutputs(blockSize);
	
	// read file here.
	uint32_t framesRead = blockSize;
	int err = mSoundFile->pull(&framesRead, mBuffers);
		
	if (err || framesRead == 0) {
		mFinished = true;
	}
	
	produceOutputs(blockSize - framesRead);
	if (mFramesRemaining > 0) mFramesRemaining -= blockSize;
	
	return mFinished; 
}

void sfread(Thread& th, Arg filename, int64_t offset, int64_t frames)
{
	const char* path = ((String*)filename.o())->s;

#ifdef SAPF_AUDIOTOOLBOX
	std::unique_ptr<SoundFile> soundFile = SoundFile::open(path, th.rate.sampleRate);
#else
	std::unique_ptr<SoundFile> soundFile = SoundFile::open(path, th.rate.sampleRate, th.rate.blockSize);
#endif

	if(soundFile != nullptr) {
		SFReader* sfr = new SFReader(std::move(soundFile), -1);
		th.push(sfr->createOutputs(th));
	}
}

#ifdef SAPF_AUDIOTOOLBOX
std::unique_ptr<SoundFile> sfcreate(Thread& th, const char* path, int numChannels, double fileSampleRate, bool interleaved)
{
	return SoundFile::create(path, numChannels, th.rate.sampleRate, fileSampleRate, interleaved);
}
#else
std::unique_ptr<SoundFile> sfcreate(Thread& th, const char* path, int numChannels, double fileSampleRate, bool interleaved, bool async)
{
	return SoundFile::create(path, numChannels, th.rate.sampleRate, fileSampleRate, async);
}
#endif

std::atomic<int32_t> gFileCount = 0;

void makeRecordingPath(Arg filename, char* path, int len)
{
	if (filename.isString()) {
		const char* recDir = getenv("SAPF_RECORDINGS");
		if (!recDir || strlen(recDir)==0) {
			#ifdef _WIN32
				recDir = getenv("TEMP");
				if (!recDir || strlen(recDir)==0)
					recDir = getenv("TMP");
			#else
				recDir = "/tmp";
			#endif
			if (!recDir || strlen(recDir)==0)
				recDir = ".";
		}
		snprintf(path, len, "%s/%s.wav", recDir, ((String*)filename.o())->s);
	} else {
		int32_t count = ++gFileCount;
		#ifdef _WIN32
			const char* tempDir = getenv("TEMP");
			if (!tempDir || strlen(tempDir)==0)
				tempDir = getenv("TMP");
			if (!tempDir || strlen(tempDir)==0)
				tempDir = ".";
		#else
			const char* tempDir = "/tmp";
		#endif
		snprintf(path, len, "%s/sapf-%s-%04d.wav", tempDir, gSessionTime, count);

	}
}

void sfwrite(Thread& th, V& v, Arg filename, bool openIt)
{
	std::vector<ZIn> in;
	
	int numChannels = 0;
		
	if (v.isZList()) {
		if (!v.isFinite()) indefiniteOp(">sf : s - indefinite number of frames", "");
		numChannels = 1;
		in.push_back(ZIn(v));
	} else {
		if (!v.isFinite()) indefiniteOp(">sf : s - indefinite number of channels", "");
		P<List> s = (List*)v.o();
		s = s->pack(th);
		Array* a = s->mArray();
		numChannels = (int)a->size();

		if (numChannels > kMaxSFChannels)
			throw errOutOfRange;
		
		bool allIndefinite = true;
		for (int i = 0; i < numChannels; ++i) {
			V va = a->at(i);
			if (va.isFinite()) allIndefinite = false;
			in.push_back(ZIn(va));
			va.o = nullptr;
		}

		s = nullptr;
		a = nullptr;
		
		if (allIndefinite) indefiniteOp(">sf : s - all channels have indefinite number of frames", "");
	}
	v.o = nullptr;

	char path[1024];
	
	makeRecordingPath(filename, path, 1024);


	#ifdef SAPF_AUDIOTOOLBOX
		std::unique_ptr<SoundFile> soundFile = sfcreate(th, path, numChannels, 0., true);
	#else
		std::unique_ptr<SoundFile> soundFile = sfcreate(th, path, numChannels, 0., true, false);
	#endif
	if (!soundFile) return;
	
	std::valarray<float> buf(0., numChannels * kBufSize);
	AudioBuffers bufs(1);
	bufs.setNumChannels(0, numChannels);
	bufs.setData(0, &buf[0]);
	bufs.setSize(0, kBufSize * sizeof(float));
		
	int64_t framesPulled = 0;
	int64_t framesWritten = 0;
	bool done = false;
	while (!done) {
		int minn = kBufSize;
		memset(&buf[0], 0, kBufSize * numChannels);
		for (int i = 0; i < numChannels; ++i) {
			int n = kBufSize;
			bool imdone = in[i].fill(th, n, &buf[0]+i, numChannels);
			framesPulled += n;
			if (imdone) done = true;
			minn = std::min(n, minn);
		}

		bufs.setSize(0, numChannels * minn * sizeof(float));
		// TODO: move into a SoundFile method
#ifdef SAPF_AUDIOTOOLBOX
		OSStatus err = ExtAudioFileWrite(soundFile->mXAF, minn, bufs.abl);
		if (err) {
			post("file writing failed %d\n", (int)err);
			break;
		}
#else
		soundFile->write(minn, bufs);
#endif // SAPF_AUDIOTOOLBOX
		framesWritten += minn;
	}
	
	post("wrote file '%s'  %d channels  %g secs\n", path, numChannels, framesWritten * th.rate.invSampleRate);

	soundFile = nullptr;
	
	if (openIt) {
		char cmd[1100];
		#ifdef _WIN32
			snprintf(cmd, 1100, "start \"\" \"%s\"", path);
		#else
			snprintf(cmd, 1100, "open \"%s\"", path);
		#endif
		system(cmd);
	}
}
