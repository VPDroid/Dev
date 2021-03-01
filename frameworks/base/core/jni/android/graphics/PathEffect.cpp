#include <jni.h>
#include "GraphicsJNI.h"

#include "core_jni_helpers.h"

#include "SkPathEffect.h"
#include "SkCornerPathEffect.h"
#include "SkDashPathEffect.h"
#include "SkDiscretePathEffect.h"
#include "Sk1DPathEffect.h"
#include "SkTemplates.h"

class SkPathEffectGlue {
public:

    static void destructor(JNIEnv* env, jobject, jlong effectHandle) {
        SkPathEffect* effect = reinterpret_cast<SkPathEffect*>(effectHandle);
        SkSafeUnref(effect);
    }

    static jlong Compose_constructor(JNIEnv* env, jobject,
                                     jlong outerHandle, jlong innerHandle) {
        SkPathEffect* outer = reinterpret_cast<SkPathEffect*>(outerHandle);
        SkPathEffect* inner = reinterpret_cast<SkPathEffect*>(innerHandle);
        SkPathEffect* effect = SkComposePathEffect::Create(outer, inner);
        return reinterpret_cast<jlong>(effect);
    }

    static jlong Sum_constructor(JNIEnv* env, jobject,
                                 jlong firstHandle, jlong secondHandle) {
        SkPathEffect* first = reinterpret_cast<SkPathEffect*>(firstHandle);
        SkPathEffect* second = reinterpret_cast<SkPathEffect*>(secondHandle);
        SkPathEffect* effect = SkSumPathEffect::Create(first, second);
        return reinterpret_cast<jlong>(effect);
    }

    static jlong Dash_constructor(JNIEnv* env, jobject,
                                      jfloatArray intervalArray, jfloat phase) {
        AutoJavaFloatArray autoInterval(env, intervalArray);
        int         count = autoInterval.length() & ~1;  // even number
#ifdef SK_SCALAR_IS_FLOAT
        SkScalar*   intervals = autoInterval.ptr();
#else
        #error Need to convert float array to SkScalar array before calling the following function.
#endif
        SkPathEffect* effect = SkDashPathEffect::Create(intervals, count, phase);
        return reinterpret_cast<jlong>(effect);
    }

    static jlong OneD_constructor(JNIEnv* env, jobject,
                  jlong shapeHandle, jfloat advance, jfloat phase, jint style) {
        const SkPath* shape = reinterpret_cast<SkPath*>(shapeHandle);
        SkASSERT(shape != NULL);
        SkPathEffect* effect = SkPath1DPathEffect::Create(*shape, advance, phase,
                (SkPath1DPathEffect::Style)style);
        return reinterpret_cast<jlong>(effect);
    }

    static jlong Corner_constructor(JNIEnv* env, jobject, jfloat radius){
        SkPathEffect* effect = SkCornerPathEffect::Create(radius);
        return reinterpret_cast<jlong>(effect);
    }

    static jlong Discrete_constructor(JNIEnv* env, jobject,
                                      jfloat length, jfloat deviation) {
        SkPathEffect* effect = SkDiscretePathEffect::Create(length, deviation);
        return reinterpret_cast<jlong>(effect);
    }

};

////////////////////////////////////////////////////////////////////////////////////////////////////////

static JNINativeMethod gPathEffectMethods[] = {
    { "nativeDestructor", "(J)V", (void*)SkPathEffectGlue::destructor }
};

static JNINativeMethod gComposePathEffectMethods[] = {
    { "nativeCreate", "(JJ)J", (void*)SkPathEffectGlue::Compose_constructor }
};

static JNINativeMethod gSumPathEffectMethods[] = {
    { "nativeCreate", "(JJ)J", (void*)SkPathEffectGlue::Sum_constructor }
};

static JNINativeMethod gDashPathEffectMethods[] = {
    { "nativeCreate", "([FF)J", (void*)SkPathEffectGlue::Dash_constructor }
};

static JNINativeMethod gPathDashPathEffectMethods[] = {
    { "nativeCreate", "(JFFI)J", (void*)SkPathEffectGlue::OneD_constructor }
};

static JNINativeMethod gCornerPathEffectMethods[] = {
    { "nativeCreate", "(F)J", (void*)SkPathEffectGlue::Corner_constructor }
};

static JNINativeMethod gDiscretePathEffectMethods[] = {
    { "nativeCreate", "(FF)J", (void*)SkPathEffectGlue::Discrete_constructor }
};

int register_android_graphics_PathEffect(JNIEnv* env)
{
    android::RegisterMethodsOrDie(env, "android/graphics/PathEffect", gPathEffectMethods,
                         NELEM(gPathEffectMethods));
    android::RegisterMethodsOrDie(env, "android/graphics/ComposePathEffect",
                                  gComposePathEffectMethods, NELEM(gComposePathEffectMethods));
    android::RegisterMethodsOrDie(env, "android/graphics/SumPathEffect", gSumPathEffectMethods,
                                  NELEM(gSumPathEffectMethods));
    android::RegisterMethodsOrDie(env, "android/graphics/DashPathEffect", gDashPathEffectMethods,
                                  NELEM(gDashPathEffectMethods));
    android::RegisterMethodsOrDie(env, "android/graphics/PathDashPathEffect",
                                  gPathDashPathEffectMethods, NELEM(gPathDashPathEffectMethods));
    android::RegisterMethodsOrDie(env, "android/graphics/CornerPathEffect",
                                  gCornerPathEffectMethods, NELEM(gCornerPathEffectMethods));
    android::RegisterMethodsOrDie(env, "android/graphics/DiscretePathEffect",
                                  gDiscretePathEffectMethods, NELEM(gDiscretePathEffectMethods));

    return 0;
}
