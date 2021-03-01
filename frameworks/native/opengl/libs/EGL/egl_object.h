/*
 ** Copyright 2007, The Android Open Source Project
 **
 ** Licensed under the Apache License, Version 2.0 (the "License");
 ** you may not use this file except in compliance with the License.
 ** You may obtain a copy of the License at
 **
 **     http://www.apache.org/licenses/LICENSE-2.0
 **
 ** Unless required by applicable law or agreed to in writing, software
 ** distributed under the License is distributed on an "AS IS" BASIS,
 ** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 ** See the License for the specific language governing permissions and
 ** limitations under the License.
 */

#ifndef ANDROID_EGL_OBJECT_H
#define ANDROID_EGL_OBJECT_H


#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <utils/threads.h>
#include <utils/String8.h>
#include <utils/Vector.h>

#include <system/window.h>

#include "egl_display.h"

// ----------------------------------------------------------------------------
namespace android {
// ----------------------------------------------------------------------------

struct egl_display_t;

class egl_object_t {
    egl_display_t *display;
    mutable volatile int32_t count;

protected:
    virtual ~egl_object_t();

public:
    egl_object_t(egl_display_t* display);
    void destroy();

    inline int32_t incRef() { return android_atomic_inc(&count); }
    inline int32_t decRef() { return android_atomic_dec(&count); }
    inline egl_display_t* getDisplay() const { return display; }

private:
    void terminate();
    static bool get(egl_display_t const* display, egl_object_t* object);

public:
    template <typename N, typename T>
    class LocalRef {
        egl_object_t* ref;
        LocalRef();
        LocalRef(const LocalRef* rhs);
    public:
        ~LocalRef();
        explicit LocalRef(egl_object_t* rhs);
        explicit LocalRef(egl_display_t const* display, T o) : ref(0) {
            egl_object_t* native = reinterpret_cast<N*>(o);
            if (o && egl_object_t::get(display, native)) {
                ref = native;
            }
        }
        inline N* get() {
            return static_cast<N*>(ref);
        }
        void acquire() const;
        void release() const;
        void terminate();
    };
    template <typename N, typename T>
    friend class LocalRef;
};

template<typename N, typename T>
egl_object_t::LocalRef<N, T>::LocalRef(egl_object_t* rhs) : ref(rhs) {
    if (ref) {
        ref->incRef();
    }
}

template <typename N, typename T>
egl_object_t::LocalRef<N,T>::~LocalRef() {
    if (ref) {
        ref->destroy();
    }
}

template <typename N, typename T>
void egl_object_t::LocalRef<N,T>::acquire() const {
    if (ref) {
        ref->incRef();
    }
}

template <typename N, typename T>
void egl_object_t::LocalRef<N,T>::release() const {
    if (ref) {
        if (ref->decRef() == 1) {
            // shouldn't happen because this is called from LocalRef
            ALOGE("LocalRef::release() removed the last reference!");
        }
    }
}

template <typename N, typename T>
void egl_object_t::LocalRef<N,T>::terminate() {
    if (ref) {
        ref->terminate();
    }
}

// ----------------------------------------------------------------------------

class egl_surface_t : public egl_object_t {
protected:
    ~egl_surface_t();
public:
    typedef egl_object_t::LocalRef<egl_surface_t, EGLSurface> Ref;

    egl_surface_t(egl_display_t* dpy, EGLConfig config,
            EGLNativeWindowType win, EGLSurface surface,
            egl_connection_t const* cnx);

    EGLSurface surface;
    EGLConfig config;
    sp<ANativeWindow> win;
    egl_connection_t const* cnx;
};

class egl_context_t: public egl_object_t {
protected:
    ~egl_context_t() {}
public:
    typedef egl_object_t::LocalRef<egl_context_t, EGLContext> Ref;

    egl_context_t(EGLDisplay dpy, EGLContext context, EGLConfig config,
            egl_connection_t const* cnx, int version);

    void onLooseCurrent();
    void onMakeCurrent(EGLSurface draw, EGLSurface read);

    EGLDisplay dpy;
    EGLContext context;
    EGLConfig config;
    EGLSurface read;
    EGLSurface draw;
    egl_connection_t const* cnx;
    int version;
    String8 gl_extensions;
    Vector<String8> tokenized_gl_extensions;
};

// ----------------------------------------------------------------------------

typedef egl_surface_t::Ref  SurfaceRef;
typedef egl_context_t::Ref  ContextRef;

// ----------------------------------------------------------------------------

template<typename NATIVE, typename EGL>
static inline NATIVE* egl_to_native_cast(EGL arg) {
    return reinterpret_cast<NATIVE*>(arg);
}

static inline
egl_surface_t* get_surface(EGLSurface surface) {
    return egl_to_native_cast<egl_surface_t>(surface);
}

static inline
egl_context_t* get_context(EGLContext context) {
    return egl_to_native_cast<egl_context_t>(context);
}

// ----------------------------------------------------------------------------
}; // namespace android
// ----------------------------------------------------------------------------

#endif // ANDROID_EGL_OBJECT_H
