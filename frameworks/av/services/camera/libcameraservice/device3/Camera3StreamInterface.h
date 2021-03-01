/*
 * Copyright (C) 2013 The Android Open Source Project
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

#ifndef ANDROID_SERVERS_CAMERA3_STREAM_INTERFACE_H
#define ANDROID_SERVERS_CAMERA3_STREAM_INTERFACE_H

#include <utils/RefBase.h>
#include "Camera3StreamBufferListener.h"

struct camera3_stream_buffer;

namespace android {

namespace camera3 {

class StatusTracker;

/**
 * An interface for managing a single stream of input and/or output data from
 * the camera device.
 */
class Camera3StreamInterface : public virtual RefBase {
  public:

    enum {
        ALLOCATE_PIPELINE_MAX = 0, // Allocate max buffers used by a given surface
    };

    /**
     * Get the stream's ID
     */
    virtual int      getId() const = 0;

    /**
     * Get the stream's dimensions and format
     */
    virtual uint32_t getWidth() const = 0;
    virtual uint32_t getHeight() const = 0;
    virtual int      getFormat() const = 0;
    virtual android_dataspace getDataSpace() const = 0;

    /**
     * Start the stream configuration process. Returns a handle to the stream's
     * information to be passed into the HAL device's configure_streams call.
     *
     * Until finishConfiguration() is called, no other methods on the stream may
     * be called. The usage and max_buffers fields of camera3_stream may be
     * modified between start/finishConfiguration, but may not be changed after
     * that. The priv field of camera3_stream may be modified at any time after
     * startConfiguration.
     *
     * Returns NULL in case of error starting configuration.
     */
    virtual camera3_stream* startConfiguration() = 0;

    /**
     * Check if the stream is mid-configuration (start has been called, but not
     * finish).  Used for lazy completion of configuration.
     */
    virtual bool    isConfiguring() const = 0;

    /**
     * Completes the stream configuration process. During this call, the stream
     * may call the device's register_stream_buffers() method. The stream
     * information structure returned by startConfiguration() may no longer be
     * modified after this call, but can still be read until the destruction of
     * the stream.
     *
     * Returns:
     *   OK on a successful configuration
     *   NO_INIT in case of a serious error from the HAL device
     *   NO_MEMORY in case of an error registering buffers
     *   INVALID_OPERATION in case connecting to the consumer failed
     */
    virtual status_t finishConfiguration(camera3_device *hal3Device) = 0;

    /**
     * Cancels the stream configuration process. This returns the stream to the
     * initial state, allowing it to be configured again later.
     * This is done if the HAL rejects the proposed combined stream configuration
     */
    virtual status_t cancelConfiguration() = 0;

    /**
     * Determine whether the stream has already become in-use (has received
     * a valid filled buffer), which determines if a stream can still have
     * prepareNextBuffer called on it.
     */
    virtual bool     isUnpreparable() = 0;

    /**
     * Start stream preparation. May only be called in the CONFIGURED state,
     * when no valid buffers have yet been returned to this stream. Prepares
     * up to maxCount buffers, or the maximum number of buffers needed by the
     * pipeline if maxCount is ALLOCATE_PIPELINE_MAX.
     *
     * If no prepartion is necessary, returns OK and does not transition to
     * PREPARING state. Otherwise, returns NOT_ENOUGH_DATA and transitions
     * to PREPARING.
     *
     * Returns:
     *    OK if no more buffers need to be preallocated
     *    NOT_ENOUGH_DATA if calls to prepareNextBuffer are needed to finish
     *        buffer pre-allocation, and transitions to the PREPARING state.
     *    NO_INIT in case of a serious error from the HAL device
     *    INVALID_OPERATION if called when not in CONFIGURED state, or a
     *        valid buffer has already been returned to this stream.
     */
    virtual status_t startPrepare(int maxCount) = 0;

    /**
     * Check if the stream is mid-preparing.
     */
    virtual bool     isPreparing() const = 0;

    /**
     * Continue stream buffer preparation by allocating the next
     * buffer for this stream.  May only be called in the PREPARED state.
     *
     * Returns OK and transitions to the CONFIGURED state if all buffers
     * are allocated after the call concludes. Otherwise returns NOT_ENOUGH_DATA.
     *
     * Returns:
     *    OK if no more buffers need to be preallocated, and transitions
     *        to the CONFIGURED state.
     *    NOT_ENOUGH_DATA if more calls to prepareNextBuffer are needed to finish
     *        buffer pre-allocation.
     *    NO_INIT in case of a serious error from the HAL device
     *    INVALID_OPERATION if called when not in CONFIGURED state, or a
     *        valid buffer has already been returned to this stream.
     */
    virtual status_t prepareNextBuffer() = 0;

    /**
     * Cancel stream preparation early. In case allocation needs to be
     * stopped, this method transitions the stream back to the CONFIGURED state.
     * Buffers that have been allocated with prepareNextBuffer remain that way,
     * but a later use of prepareNextBuffer will require just as many
     * calls as if the earlier prepare attempt had not existed.
     *
     * Returns:
     *    OK if cancellation succeeded, and transitions to the CONFIGURED state
     *    INVALID_OPERATION if not in the PREPARING state
     *    NO_INIT in case of a serious error from the HAL device
     */
    virtual status_t cancelPrepare() = 0;

    /**
     * Tear down memory for this stream. This frees all unused gralloc buffers
     * allocated for this stream, but leaves it ready for operation afterward.
     *
     * May only be called in the CONFIGURED state, and keeps the stream in
     * the CONFIGURED state.
     *
     * Returns:
     *    OK if teardown succeeded.
     *    INVALID_OPERATION if not in the CONFIGURED state
     *    NO_INIT in case of a serious error from the HAL device
     */
    virtual status_t tearDown() = 0;

    /**
     * Fill in the camera3_stream_buffer with the next valid buffer for this
     * stream, to hand over to the HAL.
     *
     * This method may only be called once finishConfiguration has been called.
     * For bidirectional streams, this method applies to the output-side
     * buffers.
     *
     */
    virtual status_t getBuffer(camera3_stream_buffer *buffer) = 0;

    /**
     * Return a buffer to the stream after use by the HAL.
     *
     * This method may only be called for buffers provided by getBuffer().
     * For bidirectional streams, this method applies to the output-side buffers
     */
    virtual status_t returnBuffer(const camera3_stream_buffer &buffer,
            nsecs_t timestamp) = 0;

    /**
     * Fill in the camera3_stream_buffer with the next valid buffer for this
     * stream, to hand over to the HAL.
     *
     * This method may only be called once finishConfiguration has been called.
     * For bidirectional streams, this method applies to the input-side
     * buffers.
     *
     */
    virtual status_t getInputBuffer(camera3_stream_buffer *buffer) = 0;

    /**
     * Return a buffer to the stream after use by the HAL.
     *
     * This method may only be called for buffers provided by getBuffer().
     * For bidirectional streams, this method applies to the input-side buffers
     */
    virtual status_t returnInputBuffer(const camera3_stream_buffer &buffer) = 0;

    /**
     * Get the buffer producer of the input buffer queue.
     *
     * This method only applies to input streams.
     */
    virtual status_t getInputBufferProducer(sp<IGraphicBufferProducer> *producer) = 0;

    /**
     * Whether any of the stream's buffers are currently in use by the HAL,
     * including buffers that have been returned but not yet had their
     * release fence signaled.
     */
    virtual bool     hasOutstandingBuffers() const = 0;

    enum {
        TIMEOUT_NEVER = -1
    };

    /**
     * Set the state tracker to use for signaling idle transitions.
     */
    virtual status_t setStatusTracker(sp<StatusTracker> statusTracker) = 0;

    /**
     * Disconnect stream from its non-HAL endpoint. After this,
     * start/finishConfiguration must be called before the stream can be used
     * again. This cannot be called if the stream has outstanding dequeued
     * buffers.
     */
    virtual status_t disconnect() = 0;

    /**
     * Debug dump of the stream's state.
     */
    virtual void     dump(int fd, const Vector<String16> &args) const = 0;

    virtual void     addBufferListener(
            wp<Camera3StreamBufferListener> listener) = 0;
    virtual void     removeBufferListener(
            const sp<Camera3StreamBufferListener>& listener) = 0;
};

} // namespace camera3

} // namespace android

#endif
