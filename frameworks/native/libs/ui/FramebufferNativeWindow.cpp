/*
**
** Copyright 2007 The Android Open Source Project
**
** Licensed under the Apache License Version 2.0(the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing software
** distributed under the License is distributed on an "AS IS" BASIS
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#define LOG_TAG "FramebufferNativeWindow"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <cutils/log.h>
#include <cutils/atomic.h>
#include <utils/threads.h>
#include <utils/RefBase.h>

#include <ui/ANativeObjectBase.h>
#include <ui/Fence.h>
#define INCLUDED_FROM_FRAMEBUFFER_NATIVE_WINDOW_CPP
#include <ui/FramebufferNativeWindow.h>
#undef INCLUDED_FROM_FRAMEBUFFER_NATIVE_WINDOW_CPP
#include <ui/Rect.h>

#include <EGL/egl.h>

#include <hardware/hardware.h>
#include <hardware/gralloc.h>

// ----------------------------------------------------------------------------
namespace android {
// ----------------------------------------------------------------------------

class NativeBuffer final
    : public ANativeObjectBase<
        ANativeWindowBuffer,
        NativeBuffer,
        LightRefBase<NativeBuffer>>
{
public:
    NativeBuffer(int w, int h, int f, int u) : BASE() {
        ANativeWindowBuffer::width  = w;
        ANativeWindowBuffer::height = h;
        ANativeWindowBuffer::format = f;
        ANativeWindowBuffer::usage  = u;
    }
private:
    friend class LightRefBase<NativeBuffer>;
};


/*
 * This implements the (main) framebuffer management. This class is used
 * mostly by SurfaceFlinger, but also by command line GL application.
 *
 * In fact this is an implementation of ANativeWindow on top of
 * the framebuffer.
 *
 * Currently it is pretty simple, it manages only two buffers (the front and
 * back buffer).
 *
 */

FramebufferNativeWindow::FramebufferNativeWindow()
    : BASE(), fbDev(0), grDev(0), mUpdateOnDemand(false)
{
    hw_module_t const* module;
    if (hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module) == 0) {
        int err;
        int i;
        err = framebuffer_open(module, &fbDev);
        ALOGE_IF(err, "couldn't open framebuffer HAL (%s)", strerror(-err));

        err = gralloc_open(module, &grDev);
        ALOGE_IF(err, "couldn't open gralloc HAL (%s)", strerror(-err));

        // bail out if we can't initialize the modules
        if (!fbDev || !grDev)
            return;

        mUpdateOnDemand = (fbDev->setUpdateRect != 0);

        // initialize the buffer FIFO
        if(fbDev->numFramebuffers >= MIN_NUM_FRAME_BUFFERS &&
           fbDev->numFramebuffers <= MAX_NUM_FRAME_BUFFERS){
            mNumBuffers = fbDev->numFramebuffers;
        } else {
            mNumBuffers = MIN_NUM_FRAME_BUFFERS;
        }
        mNumFreeBuffers = mNumBuffers;
        mBufferHead = mNumBuffers-1;

        /*
         * This does not actually change the framebuffer format. It merely
         * fakes this format to surfaceflinger so that when it creates
         * framebuffer surfaces it will use this format. It's really a giant
         * HACK to allow interworking with buggy gralloc+GPU driver
         * implementations. You should *NEVER* need to set this for shipping
         * devices.
         */
#ifdef FRAMEBUFFER_FORCE_FORMAT
        *((uint32_t *)&fbDev->format) = FRAMEBUFFER_FORCE_FORMAT;
#endif

        for (i = 0; i < mNumBuffers; i++) {
            buffers[i] = new NativeBuffer(
                    static_cast<int>(fbDev->width),
                    static_cast<int>(fbDev->height),
                    fbDev->format, GRALLOC_USAGE_HW_FB);
        }

        for (i = 0; i < mNumBuffers; i++) {
            err = grDev->alloc(grDev,
                    static_cast<int>(fbDev->width),
                    static_cast<int>(fbDev->height),
                    fbDev->format, GRALLOC_USAGE_HW_FB,
                    &buffers[i]->handle, &buffers[i]->stride);

            ALOGE_IF(err, "fb buffer %d allocation failed w=%d, h=%d, err=%s",
                    i, fbDev->width, fbDev->height, strerror(-err));

            if (err) {
                mNumBuffers = i;
                mNumFreeBuffers = i;
                mBufferHead = mNumBuffers-1;
                break;
            }
        }

        const_cast<uint32_t&>(ANativeWindow::flags) = fbDev->flags;
        const_cast<float&>(ANativeWindow::xdpi) = fbDev->xdpi;
        const_cast<float&>(ANativeWindow::ydpi) = fbDev->ydpi;
        const_cast<int&>(ANativeWindow::minSwapInterval) =
            fbDev->minSwapInterval;
        const_cast<int&>(ANativeWindow::maxSwapInterval) =
            fbDev->maxSwapInterval;
    } else {
        ALOGE("Couldn't get gralloc module");
    }

    ANativeWindow::setSwapInterval = setSwapInterval;
    ANativeWindow::dequeueBuffer = dequeueBuffer;
    ANativeWindow::queueBuffer = queueBuffer;
    ANativeWindow::query = query;
    ANativeWindow::perform = perform;

    ANativeWindow::dequeueBuffer_DEPRECATED = dequeueBuffer_DEPRECATED;
    ANativeWindow::lockBuffer_DEPRECATED = lockBuffer_DEPRECATED;
    ANativeWindow::queueBuffer_DEPRECATED = queueBuffer_DEPRECATED;
}

FramebufferNativeWindow::~FramebufferNativeWindow()
{
    if (grDev) {
        for(int i = 0; i < mNumBuffers; i++) {
            if (buffers[i] != NULL) {
                grDev->free(grDev, buffers[i]->handle);
            }
        }
        gralloc_close(grDev);
    }

    if (fbDev) {
        framebuffer_close(fbDev);
    }
}

status_t FramebufferNativeWindow::setUpdateRectangle(const Rect& r)
{
    if (!mUpdateOnDemand) {
        return INVALID_OPERATION;
    }
    return fbDev->setUpdateRect(fbDev, r.left, r.top, r.width(), r.height());
}

status_t FramebufferNativeWindow::compositionComplete()
{
    if (fbDev->compositionComplete) {
        return fbDev->compositionComplete(fbDev);
    }
    return INVALID_OPERATION;
}

int FramebufferNativeWindow::setSwapInterval(
        ANativeWindow* window, int interval)
{
    framebuffer_device_t* fb = getSelf(window)->fbDev;
    return fb->setSwapInterval(fb, interval);
}

void FramebufferNativeWindow::dump(String8& result) {
    if (fbDev->common.version >= 1 && fbDev->dump) {
        const size_t SIZE = 4096;
        char buffer[SIZE];

        fbDev->dump(fbDev, buffer, SIZE);
        result.append(buffer);
    }
}

// only for debugging / logging
int FramebufferNativeWindow::getCurrentBufferIndex() const
{
    Mutex::Autolock _l(mutex);
    const int index = mCurrentBufferIndex;
    return index;
}

int FramebufferNativeWindow::dequeueBuffer_DEPRECATED(ANativeWindow* window,
        ANativeWindowBuffer** buffer)
{
    int fenceFd = -1;
    int result = dequeueBuffer(window, buffer, &fenceFd);
    sp<Fence> fence(new Fence(fenceFd));
    int waitResult = fence->wait(Fence::TIMEOUT_NEVER);
    if (waitResult != OK) {
        ALOGE("dequeueBuffer_DEPRECATED: Fence::wait returned an "
                "error: %d", waitResult);
        return waitResult;
    }
    return result;
}

int FramebufferNativeWindow::dequeueBuffer(ANativeWindow* window,
        ANativeWindowBuffer** buffer, int* fenceFd)
{
    FramebufferNativeWindow* self = getSelf(window);
    Mutex::Autolock _l(self->mutex);

    int index = self->mBufferHead++;
    if (self->mBufferHead >= self->mNumBuffers)
        self->mBufferHead = 0;

    // wait for a free non-front buffer
    while (self->mNumFreeBuffers < 2) {
        self->mCondition.wait(self->mutex);
    }
    ALOG_ASSERT(self->buffers[index] != self->front, "");

    // get this buffer
    self->mNumFreeBuffers--;
    self->mCurrentBufferIndex = index;

    *buffer = self->buffers[index].get();
    *fenceFd = -1;

    return 0;
}

int FramebufferNativeWindow::lockBuffer_DEPRECATED(ANativeWindow* /*window*/,
        ANativeWindowBuffer* /*buffer*/)
{
    return NO_ERROR;
}

int FramebufferNativeWindow::queueBuffer_DEPRECATED(ANativeWindow* window,
        ANativeWindowBuffer* buffer)
{
    return queueBuffer(window, buffer, -1);
}

int FramebufferNativeWindow::queueBuffer(ANativeWindow* window,
        ANativeWindowBuffer* buffer, int fenceFd)
{
    FramebufferNativeWindow* self = getSelf(window);
    Mutex::Autolock _l(self->mutex);
    framebuffer_device_t* fb = self->fbDev;
    buffer_handle_t handle = static_cast<NativeBuffer*>(buffer)->handle;

    sp<Fence> fence(new Fence(fenceFd));
    fence->wait(Fence::TIMEOUT_NEVER);

    int res = fb->post(fb, handle);
    self->front = static_cast<NativeBuffer*>(buffer);
    self->mNumFreeBuffers++;
    self->mCondition.broadcast();
    return res;
}

int FramebufferNativeWindow::query(const ANativeWindow* window,
        int what, int* value)
{
    const FramebufferNativeWindow* self = getSelf(window);
    Mutex::Autolock _l(self->mutex);
    framebuffer_device_t* fb = self->fbDev;
    switch (what) {
        case NATIVE_WINDOW_WIDTH:
            *value = static_cast<int>(fb->width);
            return NO_ERROR;
        case NATIVE_WINDOW_HEIGHT:
            *value = static_cast<int>(fb->height);
            return NO_ERROR;
        case NATIVE_WINDOW_FORMAT:
            *value = fb->format;
            return NO_ERROR;
        case NATIVE_WINDOW_CONCRETE_TYPE:
            *value = NATIVE_WINDOW_FRAMEBUFFER;
            return NO_ERROR;
        case NATIVE_WINDOW_QUEUES_TO_WINDOW_COMPOSER:
            *value = 0;
            return NO_ERROR;
        case NATIVE_WINDOW_DEFAULT_WIDTH:
            *value = static_cast<int>(fb->width);
            return NO_ERROR;
        case NATIVE_WINDOW_DEFAULT_HEIGHT:
            *value = static_cast<int>(fb->height);
            return NO_ERROR;
        case NATIVE_WINDOW_TRANSFORM_HINT:
            *value = 0;
            return NO_ERROR;
    }
    *value = 0;
    return BAD_VALUE;
}

int FramebufferNativeWindow::perform(ANativeWindow* /*window*/,
        int operation, ...)
{
    switch (operation) {
        case NATIVE_WINDOW_CONNECT:
        case NATIVE_WINDOW_DISCONNECT:
        case NATIVE_WINDOW_SET_USAGE:
        case NATIVE_WINDOW_SET_BUFFERS_GEOMETRY:
        case NATIVE_WINDOW_SET_BUFFERS_DIMENSIONS:
        case NATIVE_WINDOW_SET_BUFFERS_FORMAT:
        case NATIVE_WINDOW_SET_BUFFERS_TRANSFORM:
        case NATIVE_WINDOW_API_CONNECT:
        case NATIVE_WINDOW_API_DISCONNECT:
            // TODO: we should implement these
            return NO_ERROR;

        case NATIVE_WINDOW_LOCK:
        case NATIVE_WINDOW_UNLOCK_AND_POST:
        case NATIVE_WINDOW_SET_CROP:
        case NATIVE_WINDOW_SET_BUFFER_COUNT:
        case NATIVE_WINDOW_SET_BUFFERS_TIMESTAMP:
        case NATIVE_WINDOW_SET_SCALING_MODE:
            return INVALID_OPERATION;
    }
    return NAME_NOT_FOUND;
}

// ----------------------------------------------------------------------------
}; // namespace android
// ----------------------------------------------------------------------------

using android::sp;
using android::FramebufferNativeWindow;

EGLNativeWindowType android_createDisplaySurface(void)
{
    FramebufferNativeWindow* w;
    w = new FramebufferNativeWindow();
    if (w->getDevice() == NULL) {
        // get a ref so it can be destroyed when we exit this block
        sp<FramebufferNativeWindow> ref(w);
        return NULL;
    }
    return static_cast<EGLNativeWindowType>(w);
}
