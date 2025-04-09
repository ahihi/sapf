#ifndef SAPF_AUDIOTOOLBOX
#include "AsyncAudioFileWriter.hpp"

AsyncAudioFileWriter::AsyncAudioFileWriter(const std::string &path, const int samplerate, const int numChannels)
    : mNumChannels{numChannels}, mChunkSize{maxChunkSize - maxChunkSize % numChannels},
      mRingBuffer{std::make_unique<jnk0le::Ringbuffer<float, ringBufferSize> >()},
      mRingWriteBuffer(mChunkSize), mWriteBuffer(mChunkSize),
      mRunning{true} {
    SF_INFO sfinfo;
    sfinfo.channels = numChannels;
    sfinfo.samplerate = samplerate;
    sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

    mFile = sf_open(path.c_str(), SFM_WRITE, &sfinfo);
    if (!mFile) {
        printf("failed to open %s\n", path.c_str());
        throw errNotFound;
    }

    mWriterThread = std::thread(&AsyncAudioFileWriter::writeLoop, this);
}


// stop async thread and flush all remaining data to file
AsyncAudioFileWriter::~AsyncAudioFileWriter() {
    {
        std::lock_guard lock{mBufferMutex};
        mRunning = false;
    }
    mDataAvailableCondition.notify_one();
    mWriterThread.join();

    sf_close(mFile);
}

// TODO: note writeBuff has ability to pass a callback, which could potentially be used
//  to notify the writeLoop earlier, but would require picking a "count_to_callback".
//  Would have to see if it performs better this way.
void AsyncAudioFileWriter::writeAsync(const RtBuffers& buffers, const unsigned int nBufferFrames) {
    const auto totalValuesToWrite{mNumChannels * nBufferFrames};
    assert(static_cast<uint32_t>(mNumChannels) == buffers.count());
    // index of the interleaved value in the final interleaved audio data
    size_t valueIdx{0};
    while (valueIdx < totalValuesToWrite) {
        size_t valuesToWrite{0};
        for (; valuesToWrite < mChunkSize && valueIdx < totalValuesToWrite; ++valuesToWrite,++valueIdx) {
            mRingWriteBuffer[valuesToWrite] = buffers.data(static_cast<int>(valueIdx % mNumChannels))[valueIdx /  mNumChannels];
        }

        // optimistically write to the ring buffer until we're done or we failed to make progress
        size_t totalWritten{0};
        size_t written{0};
        do {
            written = mRingBuffer->writeBuff(mRingWriteBuffer.data() + totalWritten, valuesToWrite - totalWritten);
            // it's possible this is "missed" by the writeLoop, but that's okay - this is just
            // an optimistic attempt to write. We'll revert to locking later.
            mDataAvailableCondition.notify_one();
            totalWritten += written;
        } while (written > 0 && totalWritten != valuesToWrite);

        // either we're done or we weren't able to write
        // if we're done, we can return. As mentioned earlier, it's possible that the writeLoop missed
        // the notification. That's fine. The thread will get notified again the next call, or it
        // will be notified in the destructor.
        if (totalWritten == valuesToWrite) {continue;}

        do {
            // block until we can write more. We can't be greedy and wait until we can write EVERYTHING
            // we have remaining, because the ring buffer is of limited size. So we have to wait until we can
            // at least fill the buffer, or finish off what we have remaining
            // WARNING: if this blocks, meaning the writeLoop can't keep up with
            // the audio thread, it will block the realtime audio thread! There's nothing we can really do in that
            // case - increasing the ring buffer size just delays the issue temporarily.
            {
                std::unique_lock lock{mBufferMutex};
                mSpaceAvailableCondition.wait(lock, [this, totalWritten, valuesToWrite] {
                    return mRingBuffer->writeAvailable() >= valuesToWrite - totalWritten;
                });
            }
            // write again from where we left off
            totalWritten += mRingBuffer->writeBuff(mRingWriteBuffer.data() + totalWritten, valuesToWrite - totalWritten);
            mDataAvailableCondition.notify_one();
        } while (totalWritten != valuesToWrite);
    }
    // same reasoning goes here for writeLoop potentially missing the notification, but that being okay.
}

void AsyncAudioFileWriter::writeLoop() {
    while (true) {
        // optimistic attempt to read from the buffer
        auto read{mRingBuffer->readBuff(mWriteBuffer.data(), mChunkSize)};
        if (read == 0) {
            // either we've stopped, or we have no data - time to wait and see
            {
                std::unique_lock lock{mBufferMutex};
                mDataAvailableCondition.wait(lock, [this] { return !mRunning || mRingBuffer->readAvailable(); });
            }
            // Since we're the only consumer, we know running is still false, or readAvailable is still positive.
            // so we don't still need to hold the lock
            // TODO: We copy from ringBuffer into writeBuffer, then write to file.
            //  Technically, it may be possible to skip this intermediate copy, but it would require
            //  dealing with cases where we "wrap around" the end of the ring, thus needing to modify
            //  ringbuffer to support this.
            read = mRingBuffer->readBuff(mWriteBuffer.data(), mChunkSize);
            if (read == 0) {
                // implies readAvailable was 0, so running was definitely false.
                // we've stopped, so we can exit
                return;
            }
        }

        if (const auto written{sf_write_float(mFile, mWriteBuffer.data(), static_cast<sf_count_t>(read))}; written <= 0) {
            const auto error{sf_strerror(mFile)};
            printf("failed to write audio data to file - %s\n", error);
        }
        mSpaceAvailableCondition.notify_one();
    }
}

#endif