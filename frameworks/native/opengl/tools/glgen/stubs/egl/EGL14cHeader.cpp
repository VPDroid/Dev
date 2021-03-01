/*
** Copyright 2012, The Android Open Source Project
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

// This source file is automatically generated

#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-function"

#include "jni.h"
#include "JNIHelp.h"
#include <android_runtime/AndroidRuntime.h>
#include <android_runtime/android_view_Surface.h>
#include <android_runtime/android_graphics_SurfaceTexture.h>
#include <utils/misc.h>

#include <assert.h>
#include <EGL/egl.h>

#include <gui/Surface.h>
#include <gui/GLConsumer.h>
#include <gui/Surface.h>

#include <ui/ANativeObjectBase.h>

static int initialized = 0;

static jclass egldisplayClass;
static jclass eglcontextClass;
static jclass eglsurfaceClass;
static jclass eglconfigClass;

static jmethodID egldisplayGetHandleID;
static jmethodID eglcontextGetHandleID;
static jmethodID eglsurfaceGetHandleID;
static jmethodID eglconfigGetHandleID;

static jmethodID egldisplayConstructor;
static jmethodID eglcontextConstructor;
static jmethodID eglsurfaceConstructor;
static jmethodID eglconfigConstructor;

static jobject eglNoContextObject;
static jobject eglNoDisplayObject;
static jobject eglNoSurfaceObject;



/* Cache method IDs each time the class is loaded. */

static void
nativeClassInit(JNIEnv *_env, jclass glImplClass)
{
    jclass egldisplayClassLocal = _env->FindClass("android/opengl/EGLDisplay");
    egldisplayClass = (jclass) _env->NewGlobalRef(egldisplayClassLocal);
    jclass eglcontextClassLocal = _env->FindClass("android/opengl/EGLContext");
    eglcontextClass = (jclass) _env->NewGlobalRef(eglcontextClassLocal);
    jclass eglsurfaceClassLocal = _env->FindClass("android/opengl/EGLSurface");
    eglsurfaceClass = (jclass) _env->NewGlobalRef(eglsurfaceClassLocal);
    jclass eglconfigClassLocal = _env->FindClass("android/opengl/EGLConfig");
    eglconfigClass = (jclass) _env->NewGlobalRef(eglconfigClassLocal);

    egldisplayGetHandleID = _env->GetMethodID(egldisplayClass, "getNativeHandle", "()J");
    eglcontextGetHandleID = _env->GetMethodID(eglcontextClass, "getNativeHandle", "()J");
    eglsurfaceGetHandleID = _env->GetMethodID(eglsurfaceClass, "getNativeHandle", "()J");
    eglconfigGetHandleID = _env->GetMethodID(eglconfigClass, "getNativeHandle", "()J");


    egldisplayConstructor = _env->GetMethodID(egldisplayClass, "<init>", "(J)V");
    eglcontextConstructor = _env->GetMethodID(eglcontextClass, "<init>", "(J)V");
    eglsurfaceConstructor = _env->GetMethodID(eglsurfaceClass, "<init>", "(J)V");
    eglconfigConstructor = _env->GetMethodID(eglconfigClass, "<init>", "(J)V");

    jobject localeglNoContextObject = _env->NewObject(eglcontextClass, eglcontextConstructor, reinterpret_cast<jlong>(EGL_NO_CONTEXT));
    eglNoContextObject = _env->NewGlobalRef(localeglNoContextObject);
    jobject localeglNoDisplayObject = _env->NewObject(egldisplayClass, egldisplayConstructor, reinterpret_cast<jlong>(EGL_NO_DISPLAY));
    eglNoDisplayObject = _env->NewGlobalRef(localeglNoDisplayObject);
    jobject localeglNoSurfaceObject = _env->NewObject(eglsurfaceClass, eglsurfaceConstructor, reinterpret_cast<jlong>(EGL_NO_SURFACE));
    eglNoSurfaceObject = _env->NewGlobalRef(localeglNoSurfaceObject);


    jclass eglClass = _env->FindClass("android/opengl/EGL14");
    jfieldID noContextFieldID = _env->GetStaticFieldID(eglClass, "EGL_NO_CONTEXT", "Landroid/opengl/EGLContext;");
    _env->SetStaticObjectField(eglClass, noContextFieldID, eglNoContextObject);

    jfieldID noDisplayFieldID = _env->GetStaticFieldID(eglClass, "EGL_NO_DISPLAY", "Landroid/opengl/EGLDisplay;");
    _env->SetStaticObjectField(eglClass, noDisplayFieldID, eglNoDisplayObject);

    jfieldID noSurfaceFieldID = _env->GetStaticFieldID(eglClass, "EGL_NO_SURFACE", "Landroid/opengl/EGLSurface;");
    _env->SetStaticObjectField(eglClass, noSurfaceFieldID, eglNoSurfaceObject);
}

static void *
fromEGLHandle(JNIEnv *_env, jmethodID mid, jobject obj) {
    if (obj == NULL){
        jniThrowException(_env, "java/lang/IllegalArgumentException",
                          "Object is set to null.");
    }

    jlong handle = _env->CallLongMethod(obj, mid);
    return reinterpret_cast<void*>(handle);
}

static jobject
toEGLHandle(JNIEnv *_env, jclass cls, jmethodID con, void * handle) {
    if (cls == eglcontextClass &&
       (EGLContext)handle == EGL_NO_CONTEXT) {
           return eglNoContextObject;
    }

    if (cls == egldisplayClass &&
       (EGLDisplay)handle == EGL_NO_DISPLAY) {
           return eglNoDisplayObject;
    }

    if (cls == eglsurfaceClass &&
       (EGLSurface)handle == EGL_NO_SURFACE) {
           return eglNoSurfaceObject;
    }

    return _env->NewObject(cls, con, reinterpret_cast<jlong>(handle));
}

// --------------------------------------------------------------------------
