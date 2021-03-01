/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include "EglManager.h"

#include "Caches.h"
#include "Properties.h"
#include "RenderThread.h"
#include "renderstate/RenderState.h"

#include <cutils/log.h>
#include <cutils/properties.h>
#include <EGL/eglext.h>

#define PROPERTY_RENDER_DIRTY_REGIONS "debug.hwui.render_dirty_regions"
#define GLES_VERSION 2

#define WAIT_FOR_GPU_COMPLETION 0

// Android-specific addition that is used to show when frames began in systrace
EGLAPI void EGLAPIENTRY eglBeginFrame(EGLDisplay dpy, EGLSurface surface);

namespace android {
namespace uirenderer {
namespace renderthread {

#define ERROR_CASE(x) case x: return #x;
static const char* egl_error_str(EGLint error) {
    switch (error) {
        ERROR_CASE(EGL_SUCCESS)
        ERROR_CASE(EGL_NOT_INITIALIZED)
        ERROR_CASE(EGL_BAD_ACCESS)
        ERROR_CASE(EGL_BAD_ALLOC)
        ERROR_CASE(EGL_BAD_ATTRIBUTE)
        ERROR_CASE(EGL_BAD_CONFIG)
        ERROR_CASE(EGL_BAD_CONTEXT)
        ERROR_CASE(EGL_BAD_CURRENT_SURFACE)
        ERROR_CASE(EGL_BAD_DISPLAY)
        ERROR_CASE(EGL_BAD_MATCH)
        ERROR_CASE(EGL_BAD_NATIVE_PIXMAP)
        ERROR_CASE(EGL_BAD_NATIVE_WINDOW)
        ERROR_CASE(EGL_BAD_PARAMETER)
        ERROR_CASE(EGL_BAD_SURFACE)
        ERROR_CASE(EGL_CONTEXT_LOST)
    default:
        return "Unknown error";
    }
}
static const char* egl_error_str() {
    return egl_error_str(eglGetError());
}

static bool load_dirty_regions_property() {
    char buf[PROPERTY_VALUE_MAX];
    int len = property_get(PROPERTY_RENDER_DIRTY_REGIONS, buf, "true");
    return !strncasecmp("true", buf, len);
}

EglManager::EglManager(RenderThread& thread)
        : mRenderThread(thread)
        , mEglDisplay(EGL_NO_DISPLAY)
        , mEglConfig(nullptr)
        , mEglContext(EGL_NO_CONTEXT)
        , mPBufferSurface(EGL_NO_SURFACE)
        , mAllowPreserveBuffer(load_dirty_regions_property())
        , mCurrentSurface(EGL_NO_SURFACE)
        , mAtlasMap(nullptr)
        , mAtlasMapSize(0) {
    mCanSetPreserveBuffer = mAllowPreserveBuffer;
    ALOGD("Use EGL_SWAP_BEHAVIOR_PRESERVED: %s", mAllowPreserveBuffer ? "true" : "false");
}

void EglManager::initialize() {
    if (hasEglContext()) return;

    ATRACE_NAME("Creating EGLContext");

    mEglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    LOG_ALWAYS_FATAL_IF(mEglDisplay == EGL_NO_DISPLAY,
            "Failed to get EGL_DEFAULT_DISPLAY! err=%s", egl_error_str());

    EGLint major, minor;
    LOG_ALWAYS_FATAL_IF(eglInitialize(mEglDisplay, &major, &minor) == EGL_FALSE,
            "Failed to initialize display %p! err=%s", mEglDisplay, egl_error_str());

    ALOGI("Initialized EGL, version %d.%d", (int)major, (int)minor);

    loadConfig();
    createContext();
    createPBufferSurface();
    makeCurrent(mPBufferSurface);
    mRenderThread.renderState().onGLContextCreated();
    initAtlas();
}

bool EglManager::hasEglContext() {
    return mEglDisplay != EGL_NO_DISPLAY;
}

void EglManager::loadConfig() {
    EGLint swapBehavior = mCanSetPreserveBuffer ? EGL_SWAP_BEHAVIOR_PRESERVED_BIT : 0;
    EGLint attribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 0,
            EGL_CONFIG_CAVEAT, EGL_NONE,
            EGL_STENCIL_SIZE, Stencil::getStencilSize(),
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT | swapBehavior,
            EGL_NONE
    };

    EGLint num_configs = 1;
    if (!eglChooseConfig(mEglDisplay, attribs, &mEglConfig, num_configs, &num_configs)
            || num_configs != 1) {
        // Failed to get a valid config
        if (mCanSetPreserveBuffer) {
            ALOGW("Failed to choose config with EGL_SWAP_BEHAVIOR_PRESERVED, retrying without...");
            // Try again without dirty regions enabled
            mCanSetPreserveBuffer = false;
            loadConfig();
        } else {
            LOG_ALWAYS_FATAL("Failed to choose config, error = %s", egl_error_str());
        }
    }
}

void EglManager::createContext() {
    EGLint attribs[] = { EGL_CONTEXT_CLIENT_VERSION, GLES_VERSION, EGL_NONE };
    mEglContext = eglCreateContext(mEglDisplay, mEglConfig, EGL_NO_CONTEXT, attribs);
    LOG_ALWAYS_FATAL_IF(mEglContext == EGL_NO_CONTEXT,
        "Failed to create context, error = %s", egl_error_str());
}

void EglManager::setTextureAtlas(const sp<GraphicBuffer>& buffer,
        int64_t* map, size_t mapSize) {

    // Already initialized
    if (mAtlasBuffer.get()) {
        ALOGW("Multiple calls to setTextureAtlas!");
        delete map;
        return;
    }

    mAtlasBuffer = buffer;
    mAtlasMap = map;
    mAtlasMapSize = mapSize;

    if (hasEglContext()) {
        initAtlas();
    }
}

void EglManager::initAtlas() {
    if (mAtlasBuffer.get()) {
        mRenderThread.renderState().assetAtlas().init(mAtlasBuffer,
                mAtlasMap, mAtlasMapSize);
    }
}

void EglManager::createPBufferSurface() {
    LOG_ALWAYS_FATAL_IF(mEglDisplay == EGL_NO_DISPLAY,
            "usePBufferSurface() called on uninitialized GlobalContext!");

    if (mPBufferSurface == EGL_NO_SURFACE) {
        EGLint attribs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
        mPBufferSurface = eglCreatePbufferSurface(mEglDisplay, mEglConfig, attribs);
    }
}

EGLSurface EglManager::createSurface(EGLNativeWindowType window) {
    initialize();
    EGLSurface surface = eglCreateWindowSurface(mEglDisplay, mEglConfig, window, nullptr);
    LOG_ALWAYS_FATAL_IF(surface == EGL_NO_SURFACE,
            "Failed to create EGLSurface for window %p, eglErr = %s",
            (void*) window, egl_error_str());
    return surface;
}

void EglManager::destroySurface(EGLSurface surface) {
    if (isCurrent(surface)) {
        makeCurrent(EGL_NO_SURFACE);
    }
    if (!eglDestroySurface(mEglDisplay, surface)) {
        ALOGW("Failed to destroy surface %p, error=%s", (void*)surface, egl_error_str());
    }
}

void EglManager::destroy() {
    if (mEglDisplay == EGL_NO_DISPLAY) return;

    mRenderThread.renderState().onGLContextDestroyed();
    eglDestroyContext(mEglDisplay, mEglContext);
    eglDestroySurface(mEglDisplay, mPBufferSurface);
    eglMakeCurrent(mEglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglTerminate(mEglDisplay);
    eglReleaseThread();

    mEglDisplay = EGL_NO_DISPLAY;
    mEglContext = EGL_NO_CONTEXT;
    mPBufferSurface = EGL_NO_SURFACE;
    mCurrentSurface = EGL_NO_SURFACE;
}

bool EglManager::makeCurrent(EGLSurface surface, EGLint* errOut) {
    if (isCurrent(surface)) return false;

    if (surface == EGL_NO_SURFACE) {
        // Ensure we always have a valid surface & context
        surface = mPBufferSurface;
    }
    if (!eglMakeCurrent(mEglDisplay, surface, surface, mEglContext)) {
        if (errOut) {
            *errOut = eglGetError();
            ALOGW("Failed to make current on surface %p, error=%s",
                    (void*)surface, egl_error_str(*errOut));
        } else {
            LOG_ALWAYS_FATAL("Failed to make current on surface %p, error=%s",
                    (void*)surface, egl_error_str());
        }
    }
    mCurrentSurface = surface;
    return true;
}

void EglManager::beginFrame(EGLSurface surface, EGLint* width, EGLint* height) {
    LOG_ALWAYS_FATAL_IF(surface == EGL_NO_SURFACE,
            "Tried to beginFrame on EGL_NO_SURFACE!");
    makeCurrent(surface);
    if (width) {
        eglQuerySurface(mEglDisplay, surface, EGL_WIDTH, width);
    }
    if (height) {
        eglQuerySurface(mEglDisplay, surface, EGL_HEIGHT, height);
    }
    eglBeginFrame(mEglDisplay, surface);
}

bool EglManager::swapBuffers(EGLSurface surface, const SkRect& dirty,
        EGLint width, EGLint height) {

#if WAIT_FOR_GPU_COMPLETION
    {
        ATRACE_NAME("Finishing GPU work");
        fence();
    }
#endif

#ifdef EGL_KHR_swap_buffers_with_damage
    if (CC_LIKELY(Properties::swapBuffersWithDamage)) {
        SkIRect idirty;
        dirty.roundOut(&idirty);
        /*
         * EGL_KHR_swap_buffers_with_damage spec states:
         *
         * The rectangles are specified relative to the bottom-left of the surface
         * and the x and y components of each rectangle specify the bottom-left
         * position of that rectangle.
         *
         * HWUI does everything with 0,0 being top-left, so need to map
         * the rect
         */
        EGLint y = height - (idirty.y() + idirty.height());
        // layout: {x, y, width, height}
        EGLint rects[4] = { idirty.x(), y, idirty.width(), idirty.height() };
        EGLint numrects = dirty.isEmpty() ? 0 : 1;
        eglSwapBuffersWithDamageKHR(mEglDisplay, surface, rects, numrects);
    } else {
        eglSwapBuffers(mEglDisplay, surface);
    }
#else
    eglSwapBuffers(mEglDisplay, surface);
#endif

    EGLint err = eglGetError();
    if (CC_LIKELY(err == EGL_SUCCESS)) {
        return true;
    }
    if (err == EGL_BAD_SURFACE) {
        // For some reason our surface was destroyed out from under us
        // This really shouldn't happen, but if it does we can recover easily
        // by just not trying to use the surface anymore
        ALOGW("swapBuffers encountered EGL_BAD_SURFACE on %p, halting rendering...", surface);
        return false;
    }
    LOG_ALWAYS_FATAL("Encountered EGL error %d %s during rendering",
            err, egl_error_str(err));
    // Impossible to hit this, but the compiler doesn't know that
    return false;
}

void EglManager::fence() {
    EGLSyncKHR fence = eglCreateSyncKHR(mEglDisplay, EGL_SYNC_FENCE_KHR, NULL);
    eglClientWaitSyncKHR(mEglDisplay, fence,
            EGL_SYNC_FLUSH_COMMANDS_BIT_KHR, EGL_FOREVER_KHR);
    eglDestroySyncKHR(mEglDisplay, fence);
}

bool EglManager::setPreserveBuffer(EGLSurface surface, bool preserve) {
    if (CC_UNLIKELY(!mAllowPreserveBuffer)) return false;

    bool preserved = false;
    if (mCanSetPreserveBuffer) {
        preserved = eglSurfaceAttrib(mEglDisplay, surface, EGL_SWAP_BEHAVIOR,
                preserve ? EGL_BUFFER_PRESERVED : EGL_BUFFER_DESTROYED);
        if (CC_UNLIKELY(!preserved)) {
            ALOGW("Failed to set EGL_SWAP_BEHAVIOR on surface %p, error=%s",
                    (void*) surface, egl_error_str());
        }
    }
    if (CC_UNLIKELY(!preserved)) {
        // Maybe it's already set?
        EGLint swapBehavior;
        if (eglQuerySurface(mEglDisplay, surface, EGL_SWAP_BEHAVIOR, &swapBehavior)) {
            preserved = (swapBehavior == EGL_BUFFER_PRESERVED);
        } else {
            ALOGW("Failed to query EGL_SWAP_BEHAVIOR on surface %p, error=%p",
                                (void*) surface, egl_error_str());
        }
    }

    return preserved;
}

} /* namespace renderthread */
} /* namespace uirenderer */
} /* namespace android */
