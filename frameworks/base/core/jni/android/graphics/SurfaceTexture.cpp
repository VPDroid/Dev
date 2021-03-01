/*
 * Copyright (C) 2010 The Android Open Source Project
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

#define LOG_TAG "SurfaceTexture"

#include <stdio.h>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <gui/GLConsumer.h>
#include <gui/Surface.h>

#include "core_jni_helpers.h"

#include <utils/Log.h>
#include <utils/misc.h>

#include "jni.h"
#include "JNIHelp.h"

// ----------------------------------------------------------------------------

namespace android {

static const char* const OutOfResourcesException =
    "android/view/Surface$OutOfResourcesException";
static const char* const IllegalStateException = "java/lang/IllegalStateException";
const char* const kSurfaceTextureClassPathName = "android/graphics/SurfaceTexture";

struct fields_t {
    jfieldID  surfaceTexture;
    jfieldID  producer;
    jfieldID  frameAvailableListener;
    jmethodID postEvent;
};
static fields_t fields;

// Get an ID that's unique within this process.
static int32_t createProcessUniqueId() {
    static volatile int32_t globalCounter = 0;
    return android_atomic_inc(&globalCounter);
}

// ----------------------------------------------------------------------------

static void SurfaceTexture_setSurfaceTexture(JNIEnv* env, jobject thiz,
        const sp<GLConsumer>& surfaceTexture)
{
    GLConsumer* const p =
        (GLConsumer*)env->GetLongField(thiz, fields.surfaceTexture);
    if (surfaceTexture.get()) {
        surfaceTexture->incStrong((void*)SurfaceTexture_setSurfaceTexture);
    }
    if (p) {
        p->decStrong((void*)SurfaceTexture_setSurfaceTexture);
    }
    env->SetLongField(thiz, fields.surfaceTexture, (jlong)surfaceTexture.get());
}

static void SurfaceTexture_setProducer(JNIEnv* env, jobject thiz,
        const sp<IGraphicBufferProducer>& producer)
{
    IGraphicBufferProducer* const p =
        (IGraphicBufferProducer*)env->GetLongField(thiz, fields.producer);
    if (producer.get()) {
        producer->incStrong((void*)SurfaceTexture_setProducer);
    }
    if (p) {
        p->decStrong((void*)SurfaceTexture_setProducer);
    }
    env->SetLongField(thiz, fields.producer, (jlong)producer.get());
}

static void SurfaceTexture_setFrameAvailableListener(JNIEnv* env,
        jobject thiz, sp<GLConsumer::FrameAvailableListener> listener)
{
    GLConsumer::FrameAvailableListener* const p =
        (GLConsumer::FrameAvailableListener*)
            env->GetLongField(thiz, fields.frameAvailableListener);
    if (listener.get()) {
        listener->incStrong((void*)SurfaceTexture_setSurfaceTexture);
    }
    if (p) {
        p->decStrong((void*)SurfaceTexture_setSurfaceTexture);
    }
    env->SetLongField(thiz, fields.frameAvailableListener, (jlong)listener.get());
}

sp<GLConsumer> SurfaceTexture_getSurfaceTexture(JNIEnv* env, jobject thiz) {
    return (GLConsumer*)env->GetLongField(thiz, fields.surfaceTexture);
}

sp<IGraphicBufferProducer> SurfaceTexture_getProducer(JNIEnv* env, jobject thiz) {
    return (IGraphicBufferProducer*)env->GetLongField(thiz, fields.producer);
}

sp<ANativeWindow> android_SurfaceTexture_getNativeWindow(JNIEnv* env, jobject thiz) {
    sp<GLConsumer> surfaceTexture(SurfaceTexture_getSurfaceTexture(env, thiz));
    sp<IGraphicBufferProducer> producer(SurfaceTexture_getProducer(env, thiz));
    sp<Surface> surfaceTextureClient(surfaceTexture != NULL ? new Surface(producer) : NULL);
    return surfaceTextureClient;
}

bool android_SurfaceTexture_isInstanceOf(JNIEnv* env, jobject thiz) {
    jclass surfaceTextureClass = env->FindClass(kSurfaceTextureClassPathName);
    return env->IsInstanceOf(thiz, surfaceTextureClass);
}

// ----------------------------------------------------------------------------

class JNISurfaceTextureContext : public GLConsumer::FrameAvailableListener
{
public:
    JNISurfaceTextureContext(JNIEnv* env, jobject weakThiz, jclass clazz);
    virtual ~JNISurfaceTextureContext();
    virtual void onFrameAvailable(const BufferItem& item);

private:
    static JNIEnv* getJNIEnv(bool* needsDetach);
    static void detachJNI();

    jobject mWeakThiz;
    jclass mClazz;
};

JNISurfaceTextureContext::JNISurfaceTextureContext(JNIEnv* env,
        jobject weakThiz, jclass clazz) :
    mWeakThiz(env->NewGlobalRef(weakThiz)),
    mClazz((jclass)env->NewGlobalRef(clazz))
{}

JNIEnv* JNISurfaceTextureContext::getJNIEnv(bool* needsDetach) {
    *needsDetach = false;
    JNIEnv* env = AndroidRuntime::getJNIEnv();
    if (env == NULL) {
        JavaVMAttachArgs args = {
            JNI_VERSION_1_4, "JNISurfaceTextureContext", NULL };
        JavaVM* vm = AndroidRuntime::getJavaVM();
        int result = vm->AttachCurrentThread(&env, (void*) &args);
        if (result != JNI_OK) {
            ALOGE("thread attach failed: %#x", result);
            return NULL;
        }
        *needsDetach = true;
    }
    return env;
}

void JNISurfaceTextureContext::detachJNI() {
    JavaVM* vm = AndroidRuntime::getJavaVM();
    int result = vm->DetachCurrentThread();
    if (result != JNI_OK) {
        ALOGE("thread detach failed: %#x", result);
    }
}

JNISurfaceTextureContext::~JNISurfaceTextureContext()
{
    bool needsDetach = false;
    JNIEnv* env = getJNIEnv(&needsDetach);
    if (env != NULL) {
        env->DeleteGlobalRef(mWeakThiz);
        env->DeleteGlobalRef(mClazz);
    } else {
        ALOGW("leaking JNI object references");
    }
    if (needsDetach) {
        detachJNI();
    }
}

void JNISurfaceTextureContext::onFrameAvailable(const BufferItem& /* item */)
{
    bool needsDetach = false;
    JNIEnv* env = getJNIEnv(&needsDetach);
    if (env != NULL) {
        env->CallStaticVoidMethod(mClazz, fields.postEvent, mWeakThiz);
    } else {
        ALOGW("onFrameAvailable event will not posted");
    }
    if (needsDetach) {
        detachJNI();
    }
}

// ----------------------------------------------------------------------------


#define ANDROID_GRAPHICS_SURFACETEXTURE_JNI_ID "mSurfaceTexture"
#define ANDROID_GRAPHICS_PRODUCER_JNI_ID "mProducer"
#define ANDROID_GRAPHICS_FRAMEAVAILABLELISTENER_JNI_ID \
                                         "mFrameAvailableListener"

static void SurfaceTexture_classInit(JNIEnv* env, jclass clazz)
{
    fields.surfaceTexture = env->GetFieldID(clazz,
            ANDROID_GRAPHICS_SURFACETEXTURE_JNI_ID, "J");
    if (fields.surfaceTexture == NULL) {
        ALOGE("can't find android/graphics/SurfaceTexture.%s",
                ANDROID_GRAPHICS_SURFACETEXTURE_JNI_ID);
    }
    fields.producer = env->GetFieldID(clazz,
            ANDROID_GRAPHICS_PRODUCER_JNI_ID, "J");
    if (fields.producer == NULL) {
        ALOGE("can't find android/graphics/SurfaceTexture.%s",
                ANDROID_GRAPHICS_PRODUCER_JNI_ID);
    }
    fields.frameAvailableListener = env->GetFieldID(clazz,
            ANDROID_GRAPHICS_FRAMEAVAILABLELISTENER_JNI_ID, "J");
    if (fields.frameAvailableListener == NULL) {
        ALOGE("can't find android/graphics/SurfaceTexture.%s",
                ANDROID_GRAPHICS_FRAMEAVAILABLELISTENER_JNI_ID);
    }

    fields.postEvent = env->GetStaticMethodID(clazz, "postEventFromNative",
            "(Ljava/lang/ref/WeakReference;)V");
    if (fields.postEvent == NULL) {
        ALOGE("can't find android/graphics/SurfaceTexture.postEventFromNative");
    }
}

static void SurfaceTexture_init(JNIEnv* env, jobject thiz, jboolean isDetached,
        jint texName, jboolean singleBufferMode, jobject weakThiz)
{
    sp<IGraphicBufferProducer> producer;
    sp<IGraphicBufferConsumer> consumer;
    BufferQueue::createBufferQueue(&producer, &consumer);

    if (singleBufferMode) {
        consumer->disableAsyncBuffer();
        consumer->setDefaultMaxBufferCount(1);
    }

    sp<GLConsumer> surfaceTexture;
    if (isDetached) {
        surfaceTexture = new GLConsumer(consumer, GL_TEXTURE_EXTERNAL_OES,
                true, true);
    } else {
        surfaceTexture = new GLConsumer(consumer, texName,
                GL_TEXTURE_EXTERNAL_OES, true, true);
    }

    if (surfaceTexture == 0) {
        jniThrowException(env, OutOfResourcesException,
                "Unable to create native SurfaceTexture");
        return;
    }
    surfaceTexture->setName(String8::format("SurfaceTexture-%d-%d-%d",
            (isDetached ? 0 : texName),
            getpid(),
            createProcessUniqueId()));

    SurfaceTexture_setSurfaceTexture(env, thiz, surfaceTexture);
    SurfaceTexture_setProducer(env, thiz, producer);

    jclass clazz = env->GetObjectClass(thiz);
    if (clazz == NULL) {
        jniThrowRuntimeException(env,
                "Can't find android/graphics/SurfaceTexture");
        return;
    }

    sp<JNISurfaceTextureContext> ctx(new JNISurfaceTextureContext(env, weakThiz,
            clazz));
    surfaceTexture->setFrameAvailableListener(ctx);
    SurfaceTexture_setFrameAvailableListener(env, thiz, ctx);
}

static void SurfaceTexture_finalize(JNIEnv* env, jobject thiz)
{
    sp<GLConsumer> surfaceTexture(SurfaceTexture_getSurfaceTexture(env, thiz));
    surfaceTexture->setFrameAvailableListener(0);
    SurfaceTexture_setFrameAvailableListener(env, thiz, 0);
    SurfaceTexture_setSurfaceTexture(env, thiz, 0);
    SurfaceTexture_setProducer(env, thiz, 0);
}

static void SurfaceTexture_setDefaultBufferSize(
        JNIEnv* env, jobject thiz, jint width, jint height) {
    sp<GLConsumer> surfaceTexture(SurfaceTexture_getSurfaceTexture(env, thiz));
    surfaceTexture->setDefaultBufferSize(width, height);
}

static void SurfaceTexture_updateTexImage(JNIEnv* env, jobject thiz)
{
    sp<GLConsumer> surfaceTexture(SurfaceTexture_getSurfaceTexture(env, thiz));
    status_t err = surfaceTexture->updateTexImage();
    if (err == INVALID_OPERATION) {
        jniThrowException(env, IllegalStateException, "Unable to update texture contents (see "
                "logcat for details)");
    } else if (err < 0) {
        jniThrowRuntimeException(env, "Error during updateTexImage (see logcat for details)");
    }
}

static void SurfaceTexture_releaseTexImage(JNIEnv* env, jobject thiz)
{
    sp<GLConsumer> surfaceTexture(SurfaceTexture_getSurfaceTexture(env, thiz));
    status_t err = surfaceTexture->releaseTexImage();
    if (err == INVALID_OPERATION) {
        jniThrowException(env, IllegalStateException, "Unable to release texture contents (see "
                "logcat for details)");
    } else if (err < 0) {
        jniThrowRuntimeException(env, "Error during updateTexImage (see logcat for details)");
    }
}

static jint SurfaceTexture_detachFromGLContext(JNIEnv* env, jobject thiz)
{
    sp<GLConsumer> surfaceTexture(SurfaceTexture_getSurfaceTexture(env, thiz));
    return surfaceTexture->detachFromContext();
}

static jint SurfaceTexture_attachToGLContext(JNIEnv* env, jobject thiz, jint tex)
{
    sp<GLConsumer> surfaceTexture(SurfaceTexture_getSurfaceTexture(env, thiz));
    return surfaceTexture->attachToContext((GLuint)tex);
}

static void SurfaceTexture_getTransformMatrix(JNIEnv* env, jobject thiz,
        jfloatArray jmtx)
{
    sp<GLConsumer> surfaceTexture(SurfaceTexture_getSurfaceTexture(env, thiz));
    float* mtx = env->GetFloatArrayElements(jmtx, NULL);
    surfaceTexture->getTransformMatrix(mtx);
    env->ReleaseFloatArrayElements(jmtx, mtx, 0);
}

static jlong SurfaceTexture_getTimestamp(JNIEnv* env, jobject thiz)
{
    sp<GLConsumer> surfaceTexture(SurfaceTexture_getSurfaceTexture(env, thiz));
    return surfaceTexture->getTimestamp();
}

static void SurfaceTexture_release(JNIEnv* env, jobject thiz)
{
    sp<GLConsumer> surfaceTexture(SurfaceTexture_getSurfaceTexture(env, thiz));
    surfaceTexture->abandon();
}

static jboolean SurfaceTexture_isReleased(JNIEnv* env, jobject thiz)
{
    sp<GLConsumer> surfaceTexture(SurfaceTexture_getSurfaceTexture(env, thiz));
    return surfaceTexture->isAbandoned();
}

// ----------------------------------------------------------------------------

static JNINativeMethod gSurfaceTextureMethods[] = {
    {"nativeClassInit",            "()V",   (void*)SurfaceTexture_classInit },
    {"nativeInit",                 "(ZIZLjava/lang/ref/WeakReference;)V", (void*)SurfaceTexture_init },
    {"nativeFinalize",             "()V",   (void*)SurfaceTexture_finalize },
    {"nativeSetDefaultBufferSize", "(II)V", (void*)SurfaceTexture_setDefaultBufferSize },
    {"nativeUpdateTexImage",       "()V",   (void*)SurfaceTexture_updateTexImage },
    {"nativeReleaseTexImage",      "()V",   (void*)SurfaceTexture_releaseTexImage },
    {"nativeDetachFromGLContext",  "()I",   (void*)SurfaceTexture_detachFromGLContext },
    {"nativeAttachToGLContext",    "(I)I",   (void*)SurfaceTexture_attachToGLContext },
    {"nativeGetTransformMatrix",   "([F)V", (void*)SurfaceTexture_getTransformMatrix },
    {"nativeGetTimestamp",         "()J",   (void*)SurfaceTexture_getTimestamp },
    {"nativeRelease",              "()V",   (void*)SurfaceTexture_release },
    {"nativeIsReleased",           "()Z",   (void*)SurfaceTexture_isReleased },
};

int register_android_graphics_SurfaceTexture(JNIEnv* env)
{
    return RegisterMethodsOrDie(env, kSurfaceTextureClassPathName, gSurfaceTextureMethods,
                                NELEM(gSurfaceTextureMethods));
}

} // namespace android
