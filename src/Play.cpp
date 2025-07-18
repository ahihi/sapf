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

#include "Play.hpp"
#if defined(SAPF_AUDIOTOOLBOX)
#include <AudioToolbox/AudioToolbox.h>
#elif defined(SAPF_RTAUDIO_H)
#include SAPF_RTAUDIO_H
#else
#include <RtAudio.h>
#endif
#include <pthread.h>
#include <atomic>
#include <thread>

#include "AsyncAudioFileWriter.hpp"
#include "SoundFiles.hpp"
#include "Buffers.hpp"

#if defined(SAPF_AUDIOTOOLBOX)
static OSStatus inputCallback(
	void *inRefCon,
	AudioUnitRenderActionFlags *ioActionFlags,
	const AudioTimeStamp *inTimeStamp,
	UInt32 inBusNumber,
	UInt32 inNumberFrames,
	AudioBufferList *ioData
);

struct AUPlayerBackend
{
	AUPlayerBackend(int inNumChannels);
	~AUPlayerBackend();

	int32_t createGraph();
	void stop();
	
	void *player;
	int numChannels;
	AudioComponentInstance outputUnit;
};

typedef AUPlayerBackend PlayerBackend;

#else
int rtPlayerBackendCallback(
	void *outputBuffer,
	void *inputBuffer,
	unsigned int nBufferFrames,
	double streamTime,
	RtAudioStreamStatus status,
	void *userData
);

class RtPlayerBackend {
public:
	// without this, it defaults to ASIO which will be often ASIO4ALL, which won't respect
	// the OS-level "default output" setting - so defaulting to WASAPI allows us to have a more sensible default
	// since ultra low latency isn't super important when initially using it
	RtPlayerBackend(int inNumChannels)
	#ifdef _WIN32
		: player(nullptr), numChannels(inNumChannels), audio(RtAudio::WINDOWS_WASAPI)
	#else
		: player(nullptr), numChannels(inNumChannels)
	#endif

	{}
	
	int32_t createGraph() {
		if(this->audio.getDeviceCount() < 1) {
			std::cout << "\nNo audio devices found!\n";
			exit(0);
		}

		// TODO: below will enumerate devices.
		// It defaults to ASIO. And it's going to play through the "first channel" which may
		// not be the user's default audio device.
		// We could enhance this by getting it to default to a (slower) windows API, and also
		// giving the ability to somehow pick the driver + device (and maybe "remembering" the settings)
		// auto deviceIds = this->audio.getDeviceIds();
		// for (auto & element : deviceIds) {
		// 	auto deviceInfo = this->audio.getDeviceInfo(element);
		// 	printf("%s id: %d outputs: %d \n", deviceInfo.name.c_str(), element, deviceInfo.outputChannels);
		// }


		RtAudio::StreamParameters parameters;
		parameters.deviceId = this->audio.getDefaultOutputDevice();
		parameters.nChannels = this->numChannels;
		parameters.firstChannel = 0;
		unsigned int sampleRate = vm.ar.sampleRate;
		unsigned int bufferFrames = 256; // 256 sample frames
		RtAudio::StreamOptions options;
		options.flags = RTAUDIO_NONINTERLEAVED /* | RTAUDIO_MINIMIZE_LATENCY | RTAUDIO_SCHEDULE_REALTIME */;
 
		this->audio.openStream(&parameters, NULL, RTAUDIO_FLOAT32, sampleRate, &bufferFrames, &rtPlayerBackendCallback, this->player, &options);
		this->audio.startStream();

		post("start output unit OK\n");
 
		return 0;
	}
	
	void stop() {
		if(this->audio.isStreamRunning()) {
			this->audio.stopStream();  // or could call this->audio.abortStream();
		}
 
		if(this->audio.isStreamOpen()) {
			this->audio.closeStream();
		}		
	}
	
	void *player;
	int numChannels;
	RtAudio audio;
};

typedef RtPlayerBackend PlayerBackend;
#endif

const int kMaxChannels = 32;

struct Player {
	Player(const Thread& inThread, int numChannels, std::unique_ptr<SoundFile> soundFile);
	// create without an output file
	Player(const Thread& inThread, int numChannels);
	~Player();

	int numChannels();
	int32_t createGraph();
	void stop();
	
	Thread th;
	int count; // unused?
	bool done;
	Player* prev;
	Player* next;
	PlayerBackend backend;
	ZIn in[kMaxChannels];
	std::unique_ptr<SoundFile> soundFile;

};

#if defined(SAPF_AUDIOTOOLBOX)
static AudioComponentInstance openAU(UInt32 inType, UInt32 inSubtype, UInt32 inManuf)
{
	AudioComponentDescription desc;
	desc.componentType = inType;
	desc.componentSubType = inSubtype;
	desc.componentManufacturer = inManuf;
	desc.componentFlags = 0;
	desc.componentFlagsMask = 0;

	AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
	if (!comp) {
		return nullptr;
	}

	AudioComponentInstance au = nullptr;
	AudioComponentInstanceNew(comp, &au);

	return au;
}

AUPlayerBackend::AUPlayerBackend(int inNumChannels)
	: player(nullptr), numChannels(inNumChannels), outputUnit(nullptr)
{}

AUPlayerBackend::~AUPlayerBackend() {
}

int32_t AUPlayerBackend::createGraph() {
	OSStatus err = noErr;
	AudioComponentInstance outputUnit = openAU('auou', 'def ', 'appl');
	if (!outputUnit) {
		post("open output unit failed\n");
		return 'fail';
	}

	this->outputUnit = outputUnit;

	UInt32 flags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
	AudioStreamBasicDescription fmt = { vm.ar.sampleRate, kAudioFormatLinearPCM, flags, 4, 1, 4, (UInt32)this->numChannels, 32, 0 };

	// mFormatID = 1819304813
	// mFormatFlags = 41
	// mSampleRate = 96000
	// mBitsPerChannel = 32
	// mBytesPerFrame = 4
	// mChannelsPerFrame = 1
	// mBytesPerPacket = 4
	// mFramesPerPacket = 1

	err = AudioUnitSetProperty(outputUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt, sizeof(fmt));
	if (err) {
		post("set outputUnit client format failed\n");
		return err;
	}

	AURenderCallbackStruct cbs;

	cbs.inputProc = inputCallback;
	cbs.inputProcRefCon = this->player;

	err = AudioUnitSetProperty(outputUnit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cbs, sizeof(cbs));
	if (err) {
		post("set render callback failed\n");
		return err;
	}

	err = AudioUnitInitialize(outputUnit);
	if (err) {
		post("initialize output unit failed\n");
		return err;
	}

	err = AudioOutputUnitStart(outputUnit);
	if (err) {
		post("start output unit failed\n");
		return err;
	}

	post("start output unit OK\n");

	return noErr;
}

void AUPlayerBackend::stop() {
	AudioComponentInstance outputUnit = this->outputUnit;
	this->outputUnit = nullptr;
	if (outputUnit) {
		OSStatus err = AudioOutputUnitStop(outputUnit);
		if (err) post("AudioOutputUnitStop err %d\n", (int)err);
		err = AudioComponentInstanceDispose(outputUnit);
		if (err) post("AudioComponentInstanceDispose outputUnit err %d\n", (int)err);
	}
}
#endif

static bool fillBufferList(Player *player, int inNumberFrames, Buffers *buffers);

struct Player* gAllPlayers = nullptr;

Player::Player(const Thread& inThread, const int numChannels, std::unique_ptr<SoundFile> soundFile)
	: th(inThread), count(0), done(false), prev(nullptr), next(gAllPlayers), backend(numChannels), soundFile(std::move(soundFile))
{
	this->backend.player = this;
	gAllPlayers = this;
	if (next) next->prev = this; 
}

Player::Player(const Thread& inThread, const int numChannels)
	: th(inThread), count(0), done(false), prev(nullptr), next(gAllPlayers), backend(numChannels), soundFile(nullptr)
{
	this->backend.player = this;
	gAllPlayers = this;
	if (next) next->prev = this;
}

Player::~Player() {
	if (next) next->prev = prev;
	
	if (prev) prev->next = next;
	else gAllPlayers = next;
}

int Player::numChannels() {
	return this->backend.numChannels;
}

int32_t Player::createGraph() {
	return this->backend.createGraph();
}

void Player::stop() {
	this->backend.stop();
}

pthread_mutex_t gPlayerMutex = PTHREAD_MUTEX_INITIALIZER;

#ifdef SAPF_AUDIOTOOLBOX
static void recordPlayer(Player* player, int inNumberFrames, AudioBufferList const* inData);

static OSStatus inputCallback(	void *							inRefCon,
	                              AudioUnitRenderActionFlags *	ioActionFlags,
	                              const AudioTimeStamp *			inTimeStamp,
	                              UInt32							inBusNumber,
	                              UInt32							inNumberFrames,
	                              AudioBufferList *				ioData)
{
	
	Player* player = (Player*)inRefCon;
	Buffers buffers(ioData);
		
	bool done = fillBufferList(player, inNumberFrames, &buffers);
	recordPlayer(player, inNumberFrames, ioData);

	if (done) {
		player->done = true;
	}
	return noErr;
}
#else
static void recordPlayer(const Player& player, const int nBufferFrames, const RtBuffers& buffers) {
	if (!player.soundFile) return;
	player.soundFile->writeAsync(buffers, nBufferFrames);
}
int rtPlayerBackendCallback(
	void *outputBuffer,
	void *inputBuffer,
	unsigned int nBufferFrames,
	double streamTime,
	RtAudioStreamStatus status,
	void *userData
) {
	Player *player = (Player *) userData;
	RtBuffers buffers((float *) outputBuffer, player->numChannels(), nBufferFrames);
 
	if(status) {
		std::cout << "Stream underflow detected!" << std::endl;
	}

	bool done = fillBufferList(player, nBufferFrames, &buffers);
	recordPlayer(*player, nBufferFrames, buffers);

	if (done) {
		player->done = true;
	}
	return 0;
}
#endif

static void stopPlayer(Player* player)
{
	player->stop();
	delete player;
}

void stopPlaying()
{
	Locker lock(&gPlayerMutex);

	Player* player = gAllPlayers;
	while (player) {
		Player* next = player->next;
		stopPlayer(player);
		player = next;
	}
}

void stopPlayingIfDone()
{
#ifdef SAPF_AUDIOTOOLBOX
    Locker lock(&gPlayerMutex);
	
	Player* player = gAllPlayers;
	while (player) {
		Player* next = player->next;
		if (player->done)
			stopPlayer(player);
		player = next;
	}
#else
        // TODO: cross platform playback
#endif // SAPF_AUDIOTOOLBOX
}

static bool fillBufferList(Player *player, int inNumberFrames, Buffers *buffers)
{
	if (player->done) {
	zeroAll:
		for (int i = 0; i < (int)buffers->count(); ++i) {
			memset(buffers->data(i), 0, inNumberFrames * sizeof(float));
		}
		return true;
	}
	ZIn* in = player->in;
	bool done = true;
	for (int i = 0; i < (int)buffers->count(); ++i) {
		int n = inNumberFrames;
		if (i >= player->numChannels()) {
			memset(buffers->data(i), 0, buffers->size(i));
		} else {
			try {
				float* buf = buffers->data(i);
				bool imdone = in[i].fill(player->th, n, buf, 1);
				if (n < inNumberFrames) {
					memset(buffers->data(i) + n, 0, (inNumberFrames - n) * sizeof(float));
				}
				done = done && imdone;
			} catch (int err) {
				if (err <= -1000 && err > -1000 - kNumErrors) {
					post("\nerror: %s\n", errString[-1000 - err]);
				} else {
					post("\nerror: %d\n", err);
				}
				post("exception in real time. stopping player.\n");
				done = true;
				goto zeroAll;
			} catch (std::bad_alloc& xerr) {
				post("\nnot enough memory\n");
				post("exception in real time. stopping player.\n");
				done = true;
				goto zeroAll;
			} catch (...) {
				post("\nunknown error\n");
				post("exception in real time. stopping player.\n");
				done = true;
				goto zeroAll;
			}
		}
	}
	
	return done;
}

bool gWatchdogRunning = false;
pthread_t watchdog;

static void* stopDonePlayers(void* x)
{
	using namespace std::chrono_literals;
	
	while(1) {
		std::this_thread::sleep_for(1s);
		stopPlayingIfDone();
	}
}

#ifdef SAPF_AUDIOTOOLBOX

static void recordPlayer(Player* player, int inNumberFrames, AudioBufferList const* inData)
{
	if (!player->soundFile) return;
		
	OSStatus err = ExtAudioFileWriteAsync(player->soundFile->mXAF, inNumberFrames, inData); // initialize async.
	if (err) printf("ExtAudioFileWriteAsync err %d\n", (int)err);
}

#endif // SAPF_AUDIOTOOLBOX

void playWithPlayer(Thread& th, V& v)
{
	if (!v.isList()) wrongType("play : s", "List", v);

	Locker lock(&gPlayerMutex);
	
	Player *player;
	
	if (v.isZList()) {
		player = new Player(th, 1);
		player->in[0].set(v);
	} else {
		if (!v.isFinite()) indefiniteOp("play : s", "");
		P<List> s = (List*)v.o();
		s = s->pack(th, kMaxChannels);
		if (!s()) {
			post("Too many channels. Max is %d.\n", kMaxChannels);
			return;
		}
		Array* a = s->mArray();
		
		int asize = (int)a->size();
		
		player = new Player(th, asize);
		for (int i = 0; i < asize; ++i) {
			player->in[i].set(a->at(i));
		}
	}
	v.o = nullptr; // try to prevent leak.
    
	std::atomic_thread_fence(std::memory_order_seq_cst);
		
	if (!gWatchdogRunning) {
		pthread_create(&watchdog, nullptr, stopDonePlayers, nullptr);
		gWatchdogRunning = true;
	}

	{
		int32_t err = 0;
		err = player->createGraph();
		if (err) {
			post("play failed: %d '%4.4s'\n", (int)err, (char*)&err);
			throw errFailed;
		}
	}
}


void recordWithPlayer(Thread& th, V& v, Arg filename)
{
#ifdef SAPF_AUDIOTOOLBOX
	if (!v.isList()) wrongType("play : s", "List", v);

	Locker lock(&gPlayerMutex);
	
	Player *player;

	char path[1024];
	std::unique_ptr<SoundFile> soundfile = nullptr;
	
	if (v.isZList()) {
		makeRecordingPath(filename, path, 1024);
		soundfile = sfcreate(th, path, 1, 0., false);
		if (!soundfile) {
			printf("couldn't create recording file \"%s\"\n", path);
			return;
		}

		player = new Player(th, 1, std::move(soundfile));
		player->in[0].set(v);
		player->backend.numChannels = 1;
	} else {
		if (!v.isFinite()) indefiniteOp("play : s", "");
		P<List> s = (List*)v.o();
		s = s->pack(th, kMaxChannels);
		if (!s()) {
			post("Too many channels. Max is %d.\n", kMaxChannels);
			return;
		}
		Array* a = s->mArray();
		
		int numChannels = (int)a->size();

		makeRecordingPath(filename, path, 1024);
		soundfile = sfcreate(th, path, numChannels, 0., false);
		if (!soundfile) {
			printf("couldn't create recording file \"%s\"\n", path);
			return;
		}
		
		player = new Player(th, numChannels, std::move(soundfile));
		for (int i = 0; i < numChannels; ++i) {
			player->in[i].set(a->at(i));
		}
		s = nullptr;
		a = nullptr;
	}
	v.o = nullptr; // try to prevent leak.

	{
		OSStatus err = ExtAudioFileWriteAsync(player->soundFile->mXAF, 0, nullptr); // initialize async.
		if (err) printf("init ExtAudioFileWriteAsync err %d\n", (int)err);
    }
	
    std::atomic_thread_fence(std::memory_order_seq_cst);
		
	if (!gWatchdogRunning) {
		pthread_create(&watchdog, nullptr, stopDonePlayers, nullptr);
		gWatchdogRunning = true;
	}

	{
		OSStatus err = noErr;
		err = player->createGraph();
		if (err) {
			post("play failed: %d '%4.4s'\n", (int)err, (char*)&err);
			throw errFailed;
		}
	}
#else
	if (!v.isList()) wrongType("play : s", "List", v);

	Locker lock(&gPlayerMutex);

	Player *player;

	char path[1024];
	std::unique_ptr<SoundFile> soundfile = nullptr;

	if (v.isZList()) {
		makeRecordingPath(filename, path, 1024);
		soundfile = sfcreate(th, path, 1, 0., false, true);
		player = new Player(th, 1, std::move(soundfile));
		player->in[0].set(v);
	} else {
		if (!v.isFinite()) indefiniteOp("play : s", "");
		P<List> s = (List*)v.o();
		s = s->pack(th, kMaxChannels);
		if (!s()) {
			post("Too many channels. Max is %d.\n", kMaxChannels);
			return;
		}
		Array* a = s->mArray();

		int numChannels = (int)a->size();

		makeRecordingPath(filename, path, 1024);
		soundfile = sfcreate(th, path, numChannels, 0., false, true);

		player = new Player(th, numChannels, std::move(soundfile));
		for (int i = 0; i < numChannels; ++i) {
			player->in[i].set(a->at(i));
		}
		s = nullptr;
		a = nullptr;
	}
	v.o = nullptr; // try to prevent leak.

	std::atomic_thread_fence(std::memory_order_seq_cst);

	if (!gWatchdogRunning) {
		pthread_create(&watchdog, nullptr, stopDonePlayers, nullptr);
		gWatchdogRunning = true;
	}

	{
		int32_t err{errNone};
		err = player->createGraph();
		if (err) {
			post("play failed: %d '%4.4s'\n", (int)err, (char*)&err);
			throw errFailed;
		}
	}
#endif // SAPF_AUDIOTOOLBOX
}

