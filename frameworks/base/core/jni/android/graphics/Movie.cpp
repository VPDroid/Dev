#include "Canvas.h"
#include "ScopedLocalRef.h"
#include "SkFrontBufferedStream.h"
#include "SkMovie.h"
#include "SkStream.h"
#include "GraphicsJNI.h"
#include "SkTemplates.h"
#include "SkUtils.h"
#include "Utils.h"
#include "CreateJavaOutputStreamAdaptor.h"
#include "Paint.h"

#include <androidfw/Asset.h>
#include <androidfw/ResourceTypes.h>
#include <netinet/in.h>

#include "core_jni_helpers.h"

static jclass       gMovie_class;
static jmethodID    gMovie_constructorMethodID;
static jfieldID     gMovie_nativeInstanceID;

jobject create_jmovie(JNIEnv* env, SkMovie* moov) {
    if (NULL == moov) {
        return NULL;
    }
    return env->NewObject(gMovie_class, gMovie_constructorMethodID,
            static_cast<jlong>(reinterpret_cast<uintptr_t>(moov)));
}

static SkMovie* J2Movie(JNIEnv* env, jobject movie) {
    SkASSERT(env);
    SkASSERT(movie);
    SkASSERT(env->IsInstanceOf(movie, gMovie_class));
    SkMovie* m = (SkMovie*)env->GetLongField(movie, gMovie_nativeInstanceID);
    SkASSERT(m);
    return m;
}

///////////////////////////////////////////////////////////////////////////////

static jint movie_width(JNIEnv* env, jobject movie) {
    NPE_CHECK_RETURN_ZERO(env, movie);
    return static_cast<jint>(J2Movie(env, movie)->width());
}

static jint movie_height(JNIEnv* env, jobject movie) {
    NPE_CHECK_RETURN_ZERO(env, movie);
    return static_cast<jint>(J2Movie(env, movie)->height());
}

static jboolean movie_isOpaque(JNIEnv* env, jobject movie) {
    NPE_CHECK_RETURN_ZERO(env, movie);
    return J2Movie(env, movie)->isOpaque() ? JNI_TRUE : JNI_FALSE;
}

static jint movie_duration(JNIEnv* env, jobject movie) {
    NPE_CHECK_RETURN_ZERO(env, movie);
    return static_cast<jint>(J2Movie(env, movie)->duration());
}

static jboolean movie_setTime(JNIEnv* env, jobject movie, jint ms) {
    NPE_CHECK_RETURN_ZERO(env, movie);
    return J2Movie(env, movie)->setTime(ms) ? JNI_TRUE : JNI_FALSE;
}

static void movie_draw(JNIEnv* env, jobject movie, jlong canvasHandle,
                       jfloat fx, jfloat fy, jlong paintHandle) {
    NPE_CHECK_RETURN_VOID(env, movie);

    android::Canvas* c = reinterpret_cast<android::Canvas*>(canvasHandle);
    const android::Paint* p = reinterpret_cast<android::Paint*>(paintHandle);

    // Canvas should never be NULL. However paint is an optional parameter and
    // therefore may be NULL.
    SkASSERT(c != NULL);

    SkMovie* m = J2Movie(env, movie);
    const SkBitmap& b = m->bitmap();

    c->drawBitmap(b, fx, fy, p);
}

static jobject movie_decodeAsset(JNIEnv* env, jobject clazz, jlong native_asset) {
    android::Asset* asset = reinterpret_cast<android::Asset*>(native_asset);
    if (asset == NULL) return NULL;
    SkAutoTDelete<SkStreamRewindable> stream(new android::AssetStreamAdaptor(asset));
    SkMovie* moov = SkMovie::DecodeStream(stream.get());
    return create_jmovie(env, moov);
}

static jobject movie_decodeStream(JNIEnv* env, jobject clazz, jobject istream) {

    NPE_CHECK_RETURN_ZERO(env, istream);

    jbyteArray byteArray = env->NewByteArray(16*1024);
    ScopedLocalRef<jbyteArray> scoper(env, byteArray);
    SkStream* strm = CreateJavaInputStreamAdaptor(env, istream, byteArray);
    if (NULL == strm) {
        return 0;
    }

    // Need to buffer enough input to be able to rewind as much as might be read by a decoder
    // trying to determine the stream's format. The only decoder for movies is GIF, which
    // will only read 6.
    // FIXME: Get this number from SkImageDecoder
    // bufferedStream takes ownership of strm
    SkAutoTDelete<SkStreamRewindable> bufferedStream(SkFrontBufferedStream::Create(strm, 6));
    SkASSERT(bufferedStream.get() != NULL);

    SkMovie* moov = SkMovie::DecodeStream(bufferedStream);
    return create_jmovie(env, moov);
}

static jobject movie_decodeByteArray(JNIEnv* env, jobject clazz,
                                     jbyteArray byteArray,
                                     jint offset, jint length) {

    NPE_CHECK_RETURN_ZERO(env, byteArray);

    int totalLength = env->GetArrayLength(byteArray);
    if ((offset | length) < 0 || offset + length > totalLength) {
        doThrowAIOOBE(env);
        return 0;
    }

    AutoJavaByteArray   ar(env, byteArray);
    SkMovie* moov = SkMovie::DecodeMemory(ar.ptr() + offset, length);
    return create_jmovie(env, moov);
}

static void movie_destructor(JNIEnv* env, jobject, jlong movieHandle) {
    SkMovie* movie = (SkMovie*) movieHandle;
    delete movie;
}

//////////////////////////////////////////////////////////////////////////////////////////////

static JNINativeMethod gMethods[] = {
    {   "width",    "()I",  (void*)movie_width  },
    {   "height",   "()I",  (void*)movie_height  },
    {   "isOpaque", "()Z",  (void*)movie_isOpaque  },
    {   "duration", "()I",  (void*)movie_duration  },
    {   "setTime",  "(I)Z", (void*)movie_setTime  },
    {   "nDraw",    "(JFFJ)V",
                            (void*)movie_draw  },
    { "nativeDecodeAsset", "(J)Landroid/graphics/Movie;",
                            (void*)movie_decodeAsset },
    { "nativeDecodeStream", "(Ljava/io/InputStream;)Landroid/graphics/Movie;",
                            (void*)movie_decodeStream },
    { "nativeDestructor","(J)V", (void*)movie_destructor },
    { "decodeByteArray", "([BII)Landroid/graphics/Movie;",
                            (void*)movie_decodeByteArray },
};

int register_android_graphics_Movie(JNIEnv* env)
{
    gMovie_class = android::FindClassOrDie(env, "android/graphics/Movie");
    gMovie_class = android::MakeGlobalRefOrDie(env, gMovie_class);

    gMovie_constructorMethodID = android::GetMethodIDOrDie(env, gMovie_class, "<init>", "(J)V");

    gMovie_nativeInstanceID = android::GetFieldIDOrDie(env, gMovie_class, "mNativeMovie", "J");

    return android::RegisterMethodsOrDie(env, "android/graphics/Movie", gMethods, NELEM(gMethods));
}
