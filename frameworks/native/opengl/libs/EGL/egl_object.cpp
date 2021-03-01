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

#include <string>
#include <sstream>

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <utils/threads.h>

#include "egl_object.h"

// ----------------------------------------------------------------------------
namespace android {
// ----------------------------------------------------------------------------

egl_object_t::egl_object_t(egl_display_t* disp) :
    display(disp), count(1) {
    // NOTE: this does an implicit incRef
    display->addObject(this);
}

egl_object_t::~egl_object_t() {
}

void egl_object_t::terminate() {
    // this marks the object as "terminated"
    display->removeObject(this);
    if (decRef() == 1) {
        // shouldn't happen because this is called from LocalRef
        ALOGE("egl_object_t::terminate() removed the last reference!");
    }
}

void egl_object_t::destroy() {
    if (decRef() == 1) {
        delete this;
    }
}

bool egl_object_t::get(egl_display_t const* display, egl_object_t* object) {
    // used by LocalRef, this does an incRef() atomically with
    // checking that the object is valid.
    return display->getObject(object);
}

// ----------------------------------------------------------------------------

egl_surface_t::egl_surface_t(egl_display_t* dpy, EGLConfig config,
        EGLNativeWindowType win, EGLSurface surface,
        egl_connection_t const* cnx) :
    egl_object_t(dpy), surface(surface), config(config), win(win), cnx(cnx)
{
    if (win) {
        getDisplay()->onWindowSurfaceCreated();
    }
}

egl_surface_t::~egl_surface_t() {
    ANativeWindow* const window = win.get();
    if (window != NULL) {
        native_window_set_buffers_format(window, 0);
        if (native_window_api_disconnect(window, NATIVE_WINDOW_API_EGL)) {
            ALOGW("EGLNativeWindowType %p disconnect failed", window);
        }
        getDisplay()->onWindowSurfaceDestroyed();
    }
}

// ----------------------------------------------------------------------------

egl_context_t::egl_context_t(EGLDisplay dpy, EGLContext context, EGLConfig config,
        egl_connection_t const* cnx, int version) :
    egl_object_t(get_display_nowake(dpy)), dpy(dpy), context(context),
            config(config), read(0), draw(0), cnx(cnx), version(version) {
}

void egl_context_t::onLooseCurrent() {
    read = NULL;
    draw = NULL;
}

void egl_context_t::onMakeCurrent(EGLSurface draw, EGLSurface read) {
    this->read = read;
    this->draw = draw;

    /*
     * Here we cache the GL_EXTENSIONS string for this context and we
     * add the extensions always handled by the wrapper
     */

    if (gl_extensions.isEmpty()) {
        // call the implementation's glGetString(GL_EXTENSIONS)
        const char* exts = (const char *)gEGLImpl.hooks[version]->gl.glGetString(GL_EXTENSIONS);
        gl_extensions.setTo(exts);
        if (gl_extensions.find("GL_EXT_debug_marker") < 0) {
            String8 temp("GL_EXT_debug_marker ");
            temp.append(gl_extensions);
            gl_extensions.setTo(temp);
        }

        // tokenize the supported extensions for the glGetStringi() wrapper
        std::stringstream ss;
        std::string str;
        ss << gl_extensions.string();
        while (ss >> str) {
            tokenized_gl_extensions.push(String8(str.c_str()));
        }
    }
}

// ----------------------------------------------------------------------------
}; // namespace android
// ----------------------------------------------------------------------------
