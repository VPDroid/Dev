/*
 * Copyright 2014, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_AUDIO_SPDIF_ENCODER_H
#define ANDROID_AUDIO_SPDIF_ENCODER_H

#include <stdint.h>
#include <hardware/audio.h>
#include <audio_utils/spdif/FrameScanner.h>

namespace android {

/**
 * Scan the incoming byte stream for a frame sync.
 * Then wrap the encoded frame in a data burst and send it as if it were PCM.
 * The receiver will see the data burst header and decode the wrapped frame.
 */
#define SPDIF_MAX_CHANNELS          8
#define SPDIF_ENCODED_CHANNEL_COUNT 2

class SPDIFEncoder {
public:

    SPDIFEncoder(audio_format_t format);
    // Defaults to AC3 format. Was in original API.
    SPDIFEncoder();

    virtual ~SPDIFEncoder();

    /**
     * Write encoded data to be wrapped for SPDIF.
     * The compressed frames do not have to be aligned.
     * @return number of bytes written or negative error
     */
    ssize_t write( const void* buffer, size_t numBytes );

    /**
     * Called by SPDIFEncoder when it is ready to output a data burst.
     * Must be implemented in the subclass.
     * @return number of bytes written or negative error
     */
    virtual ssize_t writeOutput( const void* buffer, size_t numBytes ) = 0;

    /**
     * Get ratio of the encoded data burst sample rate to the encoded rate.
     * For example, EAC3 data bursts are 4X the encoded rate.
     */
    uint32_t getRateMultiplier() const { return mRateMultiplier; }

    /**
     * @return number of PCM frames in a data burst
     */
    uint32_t getBurstFrames() const { return mBurstFrames; }

    /**
     * @return number of bytes per PCM frame for the data burst
     */
    int      getBytesPerOutputFrame();

    /**
     * @return  true if we can wrap this format in an SPDIF stream
     */
    static bool isFormatSupported(audio_format_t format);

    /**
     * Discard any data in the buffer. Reset frame scanners.
     * This should be called when seeking to a new position in the stream.
     */
    void reset();

protected:
    void   clearBurstBuffer();
    void   writeBurstBufferShorts(const uint16_t* buffer, size_t numBytes);
    void   writeBurstBufferBytes(const uint8_t* buffer, size_t numBytes);
    void   sendZeroPad();
    void   flushBurstBuffer();
    void   startDataBurst();
    size_t startSyncFrame();

    // Works with various formats including AC3.
    FrameScanner *mFramer;

    uint32_t  mSampleRate;
    size_t    mFrameSize;   // size of sync frame in bytes
    uint16_t *mBurstBuffer; // ALSA wants to get SPDIF data as shorts.
    size_t    mBurstBufferSizeBytes;
    uint32_t  mRateMultiplier;
    uint32_t  mBurstFrames;
    size_t    mByteCursor;  // cursor into data burst
    int       mBitstreamNumber;
    size_t    mPayloadBytesPending; // number of bytes needed to finish burst
    // state variable, true if scanning for start of frame
    bool      mScanning;

    static const unsigned short kSPDIFSync1; // Pa
    static const unsigned short kSPDIFSync2; // Pb
};

}  // namespace android

#endif  // ANDROID_AUDIO_SPDIF_ENCODER_H
