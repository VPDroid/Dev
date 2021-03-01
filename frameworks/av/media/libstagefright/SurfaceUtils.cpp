/*
 * Copyright 2015 The Android Open Source Project
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

//#define LOG_NDEBUG 0
#define LOG_TAG "SurfaceUtils"
#include <utils/Log.h>

#include <media/stagefright/SurfaceUtils.h>

#include <gui/Surface.h>

namespace android {

status_t setNativeWindowSizeFormatAndUsage(
        ANativeWindow *nativeWindow /* nonnull */,
        int width, int height, int format, int rotation, int usage) {
    status_t err = native_window_set_buffers_dimensions(nativeWindow, width, height);
    if (err != NO_ERROR) {
        ALOGE("native_window_set_buffers_dimensions failed: %s (%d)", strerror(-err), -err);
        return err;
    }

    err = native_window_set_buffers_format(nativeWindow, format);
    if (err != NO_ERROR) {
        ALOGE("native_window_set_buffers_format failed: %s (%d)", strerror(-err), -err);
        return err;
    }

    int transform = 0;
    if ((rotation % 90) == 0) {
        switch ((rotation / 90) & 3) {
            case 1:  transform = HAL_TRANSFORM_ROT_90;  break;
            case 2:  transform = HAL_TRANSFORM_ROT_180; break;
            case 3:  transform = HAL_TRANSFORM_ROT_270; break;
            default: transform = 0;                     break;
        }
    }

    err = native_window_set_buffers_transform(nativeWindow, transform);
    if (err != NO_ERROR) {
        ALOGE("native_window_set_buffers_transform failed: %s (%d)", strerror(-err), -err);
        return err;
    }

    // Make sure to check whether either Stagefright or the video decoder
    // requested protected buffers.
    if (usage & GRALLOC_USAGE_PROTECTED) {
        // Verify that the ANativeWindow sends images directly to
        // SurfaceFlinger.
        int queuesToNativeWindow = 0;
        err = nativeWindow->query(
                nativeWindow, NATIVE_WINDOW_QUEUES_TO_WINDOW_COMPOSER, &queuesToNativeWindow);
        if (err != NO_ERROR) {
            ALOGE("error authenticating native window: %s (%d)", strerror(-err), -err);
            return err;
        }
        if (queuesToNativeWindow != 1) {
            ALOGE("native window could not be authenticated");
            return PERMISSION_DENIED;
        }
    }

    int consumerUsage = 0;
    err = nativeWindow->query(nativeWindow, NATIVE_WINDOW_CONSUMER_USAGE_BITS, &consumerUsage);
    if (err != NO_ERROR) {
        ALOGW("failed to get consumer usage bits. ignoring");
        err = NO_ERROR;
    }

    int finalUsage = usage | consumerUsage;
    ALOGV("gralloc usage: %#x(producer) + %#x(consumer) = %#x", usage, consumerUsage, finalUsage);
    err = native_window_set_usage(nativeWindow, finalUsage);
    if (err != NO_ERROR) {
        ALOGE("native_window_set_usage failed: %s (%d)", strerror(-err), -err);
        return err;
    }

    err = native_window_set_scaling_mode(
            nativeWindow, NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW);
    if (err != NO_ERROR) {
        ALOGE("native_window_set_scaling_mode failed: %s (%d)", strerror(-err), -err);
        return err;
    }

    ALOGD("set up nativeWindow %p for %dx%d, color %#x, rotation %d, usage %#x",
            nativeWindow, width, height, format, rotation, finalUsage);
    return NO_ERROR;
}

status_t pushBlankBuffersToNativeWindow(ANativeWindow *nativeWindow /* nonnull */) {
    status_t err = NO_ERROR;
    ANativeWindowBuffer* anb = NULL;
    int numBufs = 0;
    int minUndequeuedBufs = 0;

    // We need to reconnect to the ANativeWindow as a CPU client to ensure that
    // no frames get dropped by SurfaceFlinger assuming that these are video
    // frames.
    err = native_window_api_disconnect(nativeWindow, NATIVE_WINDOW_API_MEDIA);
    if (err != NO_ERROR) {
        ALOGE("error pushing blank frames: api_disconnect failed: %s (%d)", strerror(-err), -err);
        return err;
    }

    err = native_window_api_connect(nativeWindow, NATIVE_WINDOW_API_CPU);
    if (err != NO_ERROR) {
        ALOGE("error pushing blank frames: api_connect failed: %s (%d)", strerror(-err), -err);
        (void)native_window_api_connect(nativeWindow, NATIVE_WINDOW_API_MEDIA);
        return err;
    }

    err = setNativeWindowSizeFormatAndUsage(
            nativeWindow, 1, 1, HAL_PIXEL_FORMAT_RGBX_8888, 0, GRALLOC_USAGE_SW_WRITE_OFTEN);
    if (err != NO_ERROR) {
        goto error;
    }

    static_cast<Surface*>(nativeWindow)->getIGraphicBufferProducer()->allowAllocation(true);

    err = nativeWindow->query(nativeWindow,
            NATIVE_WINDOW_MIN_UNDEQUEUED_BUFFERS, &minUndequeuedBufs);
    if (err != NO_ERROR) {
        ALOGE("error pushing blank frames: MIN_UNDEQUEUED_BUFFERS query "
                "failed: %s (%d)", strerror(-err), -err);
        goto error;
    }

    numBufs = minUndequeuedBufs + 1;
    err = native_window_set_buffer_count(nativeWindow, numBufs);
    if (err != NO_ERROR) {
        ALOGE("error pushing blank frames: set_buffer_count failed: %s (%d)", strerror(-err), -err);
        goto error;
    }

    // We push numBufs + 1 buffers to ensure that we've drawn into the same
    // buffer twice.  This should guarantee that the buffer has been displayed
    // on the screen and then been replaced, so an previous video frames are
    // guaranteed NOT to be currently displayed.
    for (int i = 0; i < numBufs + 1; i++) {
        err = native_window_dequeue_buffer_and_wait(nativeWindow, &anb);
        if (err != NO_ERROR) {
            ALOGE("error pushing blank frames: dequeueBuffer failed: %s (%d)",
                    strerror(-err), -err);
            break;
        }

        sp<GraphicBuffer> buf(new GraphicBuffer(anb, false));

        // Fill the buffer with the a 1x1 checkerboard pattern ;)
        uint32_t *img = NULL;
        err = buf->lock(GRALLOC_USAGE_SW_WRITE_OFTEN, (void**)(&img));
        if (err != NO_ERROR) {
            ALOGE("error pushing blank frames: lock failed: %s (%d)", strerror(-err), -err);
            break;
        }

        *img = 0;

        err = buf->unlock();
        if (err != NO_ERROR) {
            ALOGE("error pushing blank frames: unlock failed: %s (%d)", strerror(-err), -err);
            break;
        }

        err = nativeWindow->queueBuffer(nativeWindow, buf->getNativeBuffer(), -1);
        if (err != NO_ERROR) {
            ALOGE("error pushing blank frames: queueBuffer failed: %s (%d)", strerror(-err), -err);
            break;
        }

        anb = NULL;
    }

error:

    if (anb != NULL) {
        nativeWindow->cancelBuffer(nativeWindow, anb, -1);
        anb = NULL;
    }

    // Clean up after success or error.
    status_t err2 = native_window_api_disconnect(nativeWindow, NATIVE_WINDOW_API_CPU);
    if (err2 != NO_ERROR) {
        ALOGE("error pushing blank frames: api_disconnect failed: %s (%d)", strerror(-err2), -err2);
        if (err == NO_ERROR) {
            err = err2;
        }
    }

    err2 = native_window_api_connect(nativeWindow, NATIVE_WINDOW_API_MEDIA);
    if (err2 != NO_ERROR) {
        ALOGE("error pushing blank frames: api_connect failed: %s (%d)", strerror(-err), -err);
        if (err == NO_ERROR) {
            err = err2;
        }
    }

    return err;
}

}  // namespace android

