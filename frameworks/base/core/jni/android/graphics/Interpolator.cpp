#include "jni.h"
#include "core_jni_helpers.h"

#include "GraphicsJNI.h"
#include "SkInterpolator.h"
#include "SkTemplates.h"

static jlong Interpolator_constructor(JNIEnv* env, jobject clazz, jint valueCount, jint frameCount)
{
    return reinterpret_cast<jlong>(new SkInterpolator(valueCount, frameCount));
}

static void Interpolator_destructor(JNIEnv* env, jobject clazz, jlong interpHandle)
{
    SkInterpolator* interp = reinterpret_cast<SkInterpolator*>(interpHandle);
    delete interp;
}

static void Interpolator_reset(JNIEnv* env, jobject clazz, jlong interpHandle, jint valueCount, jint frameCount)
{
    SkInterpolator* interp = reinterpret_cast<SkInterpolator*>(interpHandle);
    interp->reset(valueCount, frameCount);
}

static void Interpolator_setKeyFrame(JNIEnv* env, jobject clazz, jlong interpHandle, jint index, jint msec, jfloatArray valueArray, jfloatArray blendArray)
{
    SkInterpolator* interp = reinterpret_cast<SkInterpolator*>(interpHandle);

    AutoJavaFloatArray autoValues(env, valueArray);
    AutoJavaFloatArray autoBlend(env, blendArray, 4);
#ifdef SK_SCALAR_IS_FLOAT
    SkScalar* scalars = autoValues.ptr();
    SkScalar* blend = autoBlend.ptr();
#else
    #error Need to convert float array to SkScalar array before calling the following function.
#endif

    interp->setKeyFrame(index, msec, scalars, blend);
}

static void Interpolator_setRepeatMirror(JNIEnv* env, jobject clazz, jlong interpHandle, jfloat repeatCount, jboolean mirror)
{
    SkInterpolator* interp = reinterpret_cast<SkInterpolator*>(interpHandle);
    if (repeatCount > 32000)
        repeatCount = 32000;

    interp->setRepeatCount(repeatCount);
    interp->setMirror(mirror != 0);
}

static jint Interpolator_timeToValues(JNIEnv* env, jobject clazz, jlong interpHandle, jint msec, jfloatArray valueArray)
{
    SkInterpolator* interp = reinterpret_cast<SkInterpolator*>(interpHandle);
    SkInterpolatorBase::Result result;

    float* values = valueArray ? env->GetFloatArrayElements(valueArray, NULL) : NULL;
    result = interp->timeToValues(msec, (SkScalar*)values);

    if (valueArray) {
        int n = env->GetArrayLength(valueArray);
        for (int i = 0; i < n; i++) {
            values[i] = SkScalarToFloat(*(SkScalar*)&values[i]);
        }
        env->ReleaseFloatArrayElements(valueArray, values, 0);
    }

    return static_cast<jint>(result);
}

// ----------------------------------------------------------------------------

/*
 * JNI registration.
 */
static JNINativeMethod gInterpolatorMethods[] = {
    { "nativeConstructor",      "(II)J",        (void*)Interpolator_constructor     },
    { "nativeDestructor",       "(J)V",         (void*)Interpolator_destructor      },
    { "nativeReset",            "(JII)V",       (void*)Interpolator_reset           },
    { "nativeSetKeyFrame",      "(JII[F[F)V",   (void*)Interpolator_setKeyFrame     },
    { "nativeSetRepeatMirror",  "(JFZ)V",       (void*)Interpolator_setRepeatMirror },
    { "nativeTimeToValues",     "(JI[F)I",      (void*)Interpolator_timeToValues    }
};

int register_android_graphics_Interpolator(JNIEnv* env)
{
    return android::RegisterMethodsOrDie(env, "android/graphics/Interpolator",
                                         gInterpolatorMethods, NELEM(gInterpolatorMethods));
}
