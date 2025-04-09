#pragma once

#ifndef SAPF_AUDIOTOOLBOX
#include "ringbuffer.hpp"
#include "Object.hpp"
#include "Buffers.hpp"
#include <sndfile.h>
#include <thread>
#include <mutex>
#include <condition_variable>

/*!
 * A cross-platform alternative to ExtAudioFileWriteAsync.
 * This is NOT thread safe - only one thread should be writing to a given
 * file via this object.
 * Writes the data from RtBuffers to file (one buffer per channel).
 * Use writeAsync to capture the current values in the buffers. The values are stored in a
 * ring buffer and are asynchronously written out to the file.
 * Upon destruction, it blocks until the buffer is flushed.
 * The file is always written in wav format as floats.
 */
constexpr size_t ringBufferSize{1024 * 1024};
// max number of values to write per write call.
constexpr size_t maxChunkSize{4096};
class AsyncAudioFileWriter {
private:
    SNDFILE* mFile;
    std::thread mWriterThread;
    int mNumChannels;

    // number of values per each chunk that gets written to the ring buffer + written to disk.
    // it has to be a multiple of the number of channels as libsndfile won't allow
    // writing a partial segment, but we don't want it to be greater than maxChunkSize
    size_t mChunkSize;

    // uses a ptr so this doesn't get allocated on the stack
    std::unique_ptr<jnk0le::Ringbuffer<float, ringBufferSize>> mRingBuffer;
    // a chunk of interleaved audio data from the incoming write request,
    // waiting to be written to the ring buffer.
    std::vector<float> mRingWriteBuffer;
    // a chunk of interleaved audio data just removed from the ring buffer,
    // waiting to be written to file
    std::vector<float> mWriteBuffer;

    std::mutex mBufferMutex;
    std::condition_variable mDataAvailableCondition;
    std::condition_variable mSpaceAvailableCondition;

    bool mRunning;

public:
    AsyncAudioFileWriter(const std::string& path, int samplerate, int numChannels);
    ~AsyncAudioFileWriter();
    // capture the current data in the buffers and submit it to be written asynchronously.
    void writeAsync(const RtBuffers& buffers, unsigned int nBufferFrames);

private:
    void writeLoop();
};

#endif