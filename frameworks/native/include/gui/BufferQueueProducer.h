/*
 * Copyright 2014 The Android Open Source Project
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

#ifndef ANDROID_GUI_BUFFERQUEUEPRODUCER_H
#define ANDROID_GUI_BUFFERQUEUEPRODUCER_H

#include <gui/BufferQueueDefs.h>
#include <gui/IGraphicBufferProducer.h>

namespace android {

class BufferSlot;

class BufferQueueProducer : public BnGraphicBufferProducer,
                            private IBinder::DeathRecipient {
public:
    friend class BufferQueue; // Needed to access binderDied

    BufferQueueProducer(const sp<BufferQueueCore>& core);
    virtual ~BufferQueueProducer();

    // requestBuffer returns the GraphicBuffer for slot N.
    //
    // In normal operation, this is called the first time slot N is returned
    // by dequeueBuffer.  It must be called again if dequeueBuffer returns
    // flags indicating that previously-returned buffers are no longer valid.
    virtual status_t requestBuffer(int slot, sp<GraphicBuffer>* buf);

    // setBufferCount updates the number of available buffer slots.  If this
    // method succeeds, buffer slots will be both unallocated and owned by
    // the BufferQueue object (i.e. they are not owned by the producer or
    // consumer).
    //
    // This will fail if the producer has dequeued any buffers, or if
    // bufferCount is invalid.  bufferCount must generally be a value
    // between the minimum undequeued buffer count (exclusive) and NUM_BUFFER_SLOTS
    // (inclusive).  It may also be set to zero (the default) to indicate
    // that the producer does not wish to set a value.  The minimum value
    // can be obtained by calling query(NATIVE_WINDOW_MIN_UNDEQUEUED_BUFFERS,
    // ...).
    //
    // This may only be called by the producer.  The consumer will be told
    // to discard buffers through the onBuffersReleased callback.
    virtual status_t setBufferCount(int bufferCount);

    // dequeueBuffer gets the next buffer slot index for the producer to use.
    // If a buffer slot is available then that slot index is written to the
    // location pointed to by the buf argument and a status of OK is returned.
    // If no slot is available then a status of -EBUSY is returned and buf is
    // unmodified.
    //
    // The outFence parameter will be updated to hold the fence associated with
    // the buffer. The contents of the buffer must not be overwritten until the
    // fence signals. If the fence is Fence::NO_FENCE, the buffer may be
    // written immediately.
    //
    // The width and height parameters must be no greater than the minimum of
    // GL_MAX_VIEWPORT_DIMS and GL_MAX_TEXTURE_SIZE (see: glGetIntegerv).
    // An error due to invalid dimensions might not be reported until
    // updateTexImage() is called.  If width and height are both zero, the
    // default values specified by setDefaultBufferSize() are used instead.
    //
    // If the format is 0, the default format will be used.
    //
    // The usage argument specifies gralloc buffer usage flags.  The values
    // are enumerated in gralloc.h, e.g. GRALLOC_USAGE_HW_RENDER.  These
    // will be merged with the usage flags specified by setConsumerUsageBits.
    //
    // The return value may be a negative error value or a non-negative
    // collection of flags.  If the flags are set, the return values are
    // valid, but additional actions must be performed.
    //
    // If IGraphicBufferProducer::BUFFER_NEEDS_REALLOCATION is set, the
    // producer must discard cached GraphicBuffer references for the slot
    // returned in buf.
    // If IGraphicBufferProducer::RELEASE_ALL_BUFFERS is set, the producer
    // must discard cached GraphicBuffer references for all slots.
    //
    // In both cases, the producer will need to call requestBuffer to get a
    // GraphicBuffer handle for the returned slot.
    virtual status_t dequeueBuffer(int *outSlot, sp<Fence>* outFence,
            bool async, uint32_t width, uint32_t height, PixelFormat format,
            uint32_t usage);

    // See IGraphicBufferProducer::detachBuffer
    virtual status_t detachBuffer(int slot);

    // See IGraphicBufferProducer::detachNextBuffer
    virtual status_t detachNextBuffer(sp<GraphicBuffer>* outBuffer,
            sp<Fence>* outFence);

    // See IGraphicBufferProducer::attachBuffer
    virtual status_t attachBuffer(int* outSlot, const sp<GraphicBuffer>& buffer);

    // queueBuffer returns a filled buffer to the BufferQueue.
    //
    // Additional data is provided in the QueueBufferInput struct.  Notably,
    // a timestamp must be provided for the buffer. The timestamp is in
    // nanoseconds, and must be monotonically increasing. Its other semantics
    // (zero point, etc) are producer-specific and should be documented by the
    // producer.
    //
    // The caller may provide a fence that signals when all rendering
    // operations have completed.  Alternatively, NO_FENCE may be used,
    // indicating that the buffer is ready immediately.
    //
    // Some values are returned in the output struct: the current settings
    // for default width and height, the current transform hint, and the
    // number of queued buffers.
    virtual status_t queueBuffer(int slot,
            const QueueBufferInput& input, QueueBufferOutput* output);

    // cancelBuffer returns a dequeued buffer to the BufferQueue, but doesn't
    // queue it for use by the consumer.
    //
    // The buffer will not be overwritten until the fence signals.  The fence
    // will usually be the one obtained from dequeueBuffer.
    virtual void cancelBuffer(int slot, const sp<Fence>& fence);

    // Query native window attributes.  The "what" values are enumerated in
    // window.h (e.g. NATIVE_WINDOW_FORMAT).
    virtual int query(int what, int* outValue);

    // connect attempts to connect a producer API to the BufferQueue.  This
    // must be called before any other IGraphicBufferProducer methods are
    // called except for getAllocator.  A consumer must already be connected.
    //
    // This method will fail if connect was previously called on the
    // BufferQueue and no corresponding disconnect call was made (i.e. if
    // it's still connected to a producer).
    //
    // APIs are enumerated in window.h (e.g. NATIVE_WINDOW_API_CPU).
    virtual status_t connect(const sp<IProducerListener>& listener,
            int api, bool producerControlledByApp, QueueBufferOutput* output);

    // disconnect attempts to disconnect a producer API from the BufferQueue.
    // Calling this method will cause any subsequent calls to other
    // IGraphicBufferProducer methods to fail except for getAllocator and connect.
    // Successfully calling connect after this will allow the other methods to
    // succeed again.
    //
    // This method will fail if the the BufferQueue is not currently
    // connected to the specified producer API.
    virtual status_t disconnect(int api);

    // Attaches a sideband buffer stream to the IGraphicBufferProducer.
    //
    // A sideband stream is a device-specific mechanism for passing buffers
    // from the producer to the consumer without using dequeueBuffer/
    // queueBuffer. If a sideband stream is present, the consumer can choose
    // whether to acquire buffers from the sideband stream or from the queued
    // buffers.
    //
    // Passing NULL or a different stream handle will detach the previous
    // handle if any.
    virtual status_t setSidebandStream(const sp<NativeHandle>& stream);

    // See IGraphicBufferProducer::allocateBuffers
    virtual void allocateBuffers(bool async, uint32_t width, uint32_t height,
            PixelFormat format, uint32_t usage);

    // See IGraphicBufferProducer::allowAllocation
    virtual status_t allowAllocation(bool allow);

    // See IGraphicBufferProducer::setGenerationNumber
    virtual status_t setGenerationNumber(uint32_t generationNumber);

    // See IGraphicBufferProducer::getConsumerName
    virtual String8 getConsumerName() const override;

private:
    // This is required by the IBinder::DeathRecipient interface
    virtual void binderDied(const wp<IBinder>& who);

    // waitForFreeSlotThenRelock finds the oldest slot in the FREE state. It may
    // block if there are no available slots and we are not in non-blocking
    // mode (producer and consumer controlled by the application). If it blocks,
    // it will release mCore->mMutex while blocked so that other operations on
    // the BufferQueue may succeed.
    status_t waitForFreeSlotThenRelock(const char* caller, bool async,
            int* found, status_t* returnFlags) const;

    sp<BufferQueueCore> mCore;

    // This references mCore->mSlots. Lock mCore->mMutex while accessing.
    BufferQueueDefs::SlotsType& mSlots;

    // This is a cached copy of the name stored in the BufferQueueCore.
    // It's updated during connect and dequeueBuffer (which should catch
    // most updates).
    String8 mConsumerName;

    uint32_t mStickyTransform;

    // This saves the fence from the last queueBuffer, such that the
    // next queueBuffer call can throttle buffer production. The prior
    // queueBuffer's fence is not nessessarily available elsewhere,
    // since the previous buffer might have already been acquired.
    sp<Fence> mLastQueueBufferFence;

    // Take-a-ticket system for ensuring that onFrame* callbacks are called in
    // the order that frames are queued. While the BufferQueue lock
    // (mCore->mMutex) is held, a ticket is retained by the producer. After
    // dropping the BufferQueue lock, the producer must wait on the condition
    // variable until the current callback ticket matches its retained ticket.
    Mutex mCallbackMutex;
    int mNextCallbackTicket; // Protected by mCore->mMutex
    int mCurrentCallbackTicket; // Protected by mCallbackMutex
    Condition mCallbackCondition;

}; // class BufferQueueProducer

} // namespace android

#endif
