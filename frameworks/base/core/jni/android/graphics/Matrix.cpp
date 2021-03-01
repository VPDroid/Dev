/* libs/android_runtime/android/graphics/Matrix.cpp
**
** Copyright 2006, The Android Open Source Project
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

#include "jni.h"
#include "GraphicsJNI.h"
#include <core_jni_helpers.h>

#include "SkMatrix.h"
#include "SkTemplates.h"

#include "Matrix.h"

#include <Caches.h>

namespace android {

class SkMatrixGlue {
public:

    static void finalizer(JNIEnv* env, jobject clazz, jlong objHandle) {
        SkMatrix* obj = reinterpret_cast<SkMatrix*>(objHandle);
        delete obj;
    }

    static jlong create(JNIEnv* env, jobject clazz, jlong srcHandle) {
        const SkMatrix* src = reinterpret_cast<SkMatrix*>(srcHandle);
        SkMatrix* obj = new SkMatrix();
        if (src)
            *obj = *src;
        else
            obj->reset();
        return reinterpret_cast<jlong>(obj);
    }

    static jboolean isIdentity(JNIEnv* env, jobject clazz, jlong objHandle) {
        SkMatrix* obj = reinterpret_cast<SkMatrix*>(objHandle);
        return obj->isIdentity() ? JNI_TRUE : JNI_FALSE;
    }

    static jboolean isAffine(JNIEnv* env, jobject clazz, jlong objHandle) {
        SkMatrix* obj = reinterpret_cast<SkMatrix*>(objHandle);
        return obj->asAffine(NULL) ? JNI_TRUE : JNI_FALSE;
    }

    static jboolean rectStaysRect(JNIEnv* env, jobject clazz, jlong objHandle) {
        SkMatrix* obj = reinterpret_cast<SkMatrix*>(objHandle);
        return obj->rectStaysRect() ? JNI_TRUE : JNI_FALSE;
    }

    static void reset(JNIEnv* env, jobject clazz, jlong objHandle) {
        SkMatrix* obj = reinterpret_cast<SkMatrix*>(objHandle);
        obj->reset();
    }
     static void set(JNIEnv* env, jobject clazz, jlong objHandle, jlong otherHandle) {
        SkMatrix* obj = reinterpret_cast<SkMatrix*>(objHandle);
        SkMatrix* other = reinterpret_cast<SkMatrix*>(otherHandle);
        *obj = *other;
    }
     static void setTranslate(JNIEnv* env, jobject clazz, jlong objHandle, jfloat dx, jfloat dy) {
        SkMatrix* obj = reinterpret_cast<SkMatrix*>(objHandle);
        obj->setTranslate(dx, dy);
    }
     static void setScale__FFFF(JNIEnv* env, jobject clazz, jlong objHandle, jfloat sx, jfloat sy, jfloat px, jfloat py) {
        SkMatrix* obj = reinterpret_cast<SkMatrix*>(objHandle);
        obj->setScale(sx, sy, px, py);
    }
     static void setScale__FF(JNIEnv* env, jobject clazz, jlong objHandle, jfloat sx, jfloat sy) {
        SkMatrix* obj = reinterpret_cast<SkMatrix*>(objHandle);
        obj->setScale(sx, sy);
    }
     static void setRotate__FFF(JNIEnv* env, jobject clazz, jlong objHandle, jfloat degrees, jfloat px, jfloat py) {
        SkMatrix* obj = reinterpret_cast<SkMatrix*>(objHandle);
        obj->setRotate(degrees, px, py);
    }
     static void setRotate__F(JNIEnv* env, jobject clazz, jlong objHandle, jfloat degrees) {
        SkMatrix* obj = reinterpret_cast<SkMatrix*>(objHandle);
        obj->setRotate(degrees);
    }
     static void setSinCos__FFFF(JNIEnv* env, jobject clazz, jlong objHandle, jfloat sinValue, jfloat cosValue, jfloat px, jfloat py) {
        SkMatrix* obj = reinterpret_cast<SkMatrix*>(objHandle);
        obj->setSinCos(sinValue, cosValue, px, py);
    }
     static void setSinCos__FF(JNIEnv* env, jobject clazz, jlong objHandle, jfloat sinValue, jfloat cosValue) {
        SkMatrix* obj = reinterpret_cast<SkMatrix*>(objHandle);
        obj->setSinCos(sinValue, cosValue);
    }
     static void setSkew__FFFF(JNIEnv* env, jobject clazz, jlong objHandle, jfloat kx, jfloat ky, jfloat px, jfloat py) {
        SkMatrix* obj = reinterpret_cast<SkMatrix*>(objHandle);
        obj->setSkew(kx, ky, px, py);
    }
     static void setSkew__FF(JNIEnv* env, jobject clazz, jlong objHandle, jfloat kx, jfloat ky) {
        SkMatrix* obj = reinterpret_cast<SkMatrix*>(objHandle);
        obj->setSkew(kx, ky);
    }
     static void setConcat(JNIEnv* env, jobject clazz, jlong objHandle, jlong aHandle, jlong bHandle) {
        SkMatrix* obj = reinterpret_cast<SkMatrix*>(objHandle);
        SkMatrix* a = reinterpret_cast<SkMatrix*>(aHandle);
        SkMatrix* b = reinterpret_cast<SkMatrix*>(bHandle);
        obj->setConcat(*a, *b);
    }

    static void preTranslate(JNIEnv* env, jobject clazz, jlong objHandle, jfloat dx, jfloat dy) {
        SkMatrix* obj = reinterpret_cast<SkMatrix*>(objHandle);
        obj->preTranslate(dx, dy);
    }

    static void preScale__FFFF(JNIEnv* env, jobject clazz, jlong objHandle, jfloat sx, jfloat sy, jfloat px, jfloat py) {
        SkMatrix* obj = reinterpret_cast<SkMatrix*>(objHandle);
        obj->preScale(sx, sy, px, py);
    }

    static void preScale__FF(JNIEnv* env, jobject clazz, jlong objHandle, jfloat sx, jfloat sy) {
        SkMatrix* obj = reinterpret_cast<SkMatrix*>(objHandle);
        obj->preScale(sx, sy);
    }

    static void preRotate__FFF(JNIEnv* env, jobject clazz, jlong objHandle, jfloat degrees, jfloat px, jfloat py) {
        SkMatrix* obj = reinterpret_cast<SkMatrix*>(objHandle);
        obj->preRotate(degrees, px, py);
    }

    static void preRotate__F(JNIEnv* env, jobject clazz, jlong objHandle, jfloat degrees) {
        SkMatrix* obj = reinterpret_cast<SkMatrix*>(objHandle);
        obj->preRotate(degrees);
    }

    static void preSkew__FFFF(JNIEnv* env, jobject clazz, jlong objHandle, jfloat kx, jfloat ky, jfloat px, jfloat py) {
        SkMatrix* obj = reinterpret_cast<SkMatrix*>(objHandle);
        obj->preSkew(kx, ky, px, py);
    }

    static void preSkew__FF(JNIEnv* env, jobject clazz, jlong objHandle, jfloat kx, jfloat ky) {
        SkMatrix* obj = reinterpret_cast<SkMatrix*>(objHandle);
        obj->preSkew(kx, ky);
    }

    static void preConcat(JNIEnv* env, jobject clazz, jlong objHandle, jlong otherHandle) {
        SkMatrix* obj = reinterpret_cast<SkMatrix*>(objHandle);
        SkMatrix* other = reinterpret_cast<SkMatrix*>(otherHandle);
        obj->preConcat(*other);
    }

    static void postTranslate(JNIEnv* env, jobject clazz, jlong objHandle, jfloat dx, jfloat dy) {
        SkMatrix* obj = reinterpret_cast<SkMatrix*>(objHandle);
        obj->postTranslate(dx, dy);
    }

    static void postScale__FFFF(JNIEnv* env, jobject clazz, jlong objHandle, jfloat sx, jfloat sy, jfloat px, jfloat py) {
        SkMatrix* obj = reinterpret_cast<SkMatrix*>(objHandle);
        obj->postScale(sx, sy, px, py);
    }

    static void postScale__FF(JNIEnv* env, jobject clazz, jlong objHandle, jfloat sx, jfloat sy) {
        SkMatrix* obj = reinterpret_cast<SkMatrix*>(objHandle);
        obj->postScale(sx, sy);
    }

    static void postRotate__FFF(JNIEnv* env, jobject clazz, jlong objHandle, jfloat degrees, jfloat px, jfloat py) {
        SkMatrix* obj = reinterpret_cast<SkMatrix*>(objHandle);
        obj->postRotate(degrees, px, py);
    }

    static void postRotate__F(JNIEnv* env, jobject clazz, jlong objHandle, jfloat degrees) {
        SkMatrix* obj = reinterpret_cast<SkMatrix*>(objHandle);
        obj->postRotate(degrees);
    }

    static void postSkew__FFFF(JNIEnv* env, jobject clazz, jlong objHandle, jfloat kx, jfloat ky, jfloat px, jfloat py) {
        SkMatrix* obj = reinterpret_cast<SkMatrix*>(objHandle);
        obj->postSkew(kx, ky, px, py);
    }

    static void postSkew__FF(JNIEnv* env, jobject clazz, jlong matrixHandle, jfloat kx, jfloat ky) {
        SkMatrix* matrix = reinterpret_cast<SkMatrix*>(matrixHandle);
        matrix->postSkew(kx, ky);
    }

    static void postConcat(JNIEnv* env, jobject clazz, jlong matrixHandle, jlong otherHandle) {
        SkMatrix* matrix = reinterpret_cast<SkMatrix*>(matrixHandle);
        SkMatrix* other = reinterpret_cast<SkMatrix*>(otherHandle);
        matrix->postConcat(*other);
    }

    static jboolean setRectToRect(JNIEnv* env, jobject clazz, jlong matrixHandle, jobject src, jobject dst, jint stfHandle) {
        SkMatrix* matrix = reinterpret_cast<SkMatrix*>(matrixHandle);
        SkMatrix::ScaleToFit stf = static_cast<SkMatrix::ScaleToFit>(stfHandle);
        SkRect src_;
        GraphicsJNI::jrectf_to_rect(env, src, &src_);
        SkRect dst_;
        GraphicsJNI::jrectf_to_rect(env, dst, &dst_);
        return matrix->setRectToRect(src_, dst_, stf) ? JNI_TRUE : JNI_FALSE;
    }

    static jboolean setPolyToPoly(JNIEnv* env, jobject clazz, jlong matrixHandle,
                                  jfloatArray jsrc, jint srcIndex,
                                  jfloatArray jdst, jint dstIndex, jint ptCount) {
        SkMatrix* matrix = reinterpret_cast<SkMatrix*>(matrixHandle);
        SkASSERT(srcIndex >= 0);
        SkASSERT(dstIndex >= 0);
        SkASSERT((unsigned)ptCount <= 4);

        AutoJavaFloatArray autoSrc(env, jsrc, srcIndex + (ptCount << 1), kRO_JNIAccess);
        AutoJavaFloatArray autoDst(env, jdst, dstIndex + (ptCount << 1), kRW_JNIAccess);
        float* src = autoSrc.ptr() + srcIndex;
        float* dst = autoDst.ptr() + dstIndex;
        bool result;

#ifdef SK_SCALAR_IS_FLOAT
        result = matrix->setPolyToPoly((const SkPoint*)src, (const SkPoint*)dst,
                                     ptCount);
#else
        SkASSERT(false);
#endif
        return result ? JNI_TRUE : JNI_FALSE;
    }

    static jboolean invert(JNIEnv* env, jobject clazz, jlong matrixHandle, jlong inverseHandle) {
        SkMatrix* matrix = reinterpret_cast<SkMatrix*>(matrixHandle);
        SkMatrix* inverse = reinterpret_cast<SkMatrix*>(inverseHandle);
        return matrix->invert(inverse);
    }

    static void mapPoints(JNIEnv* env, jobject clazz, jlong matrixHandle,
                              jfloatArray dst, jint dstIndex,
                              jfloatArray src, jint srcIndex,
                              jint ptCount, jboolean isPts) {
        SkMatrix* matrix = reinterpret_cast<SkMatrix*>(matrixHandle);
        SkASSERT(ptCount >= 0);
        AutoJavaFloatArray autoSrc(env, src, srcIndex + (ptCount << 1), kRO_JNIAccess);
        AutoJavaFloatArray autoDst(env, dst, dstIndex + (ptCount << 1), kRW_JNIAccess);
        float* srcArray = autoSrc.ptr() + srcIndex;
        float* dstArray = autoDst.ptr() + dstIndex;
#ifdef SK_SCALAR_IS_FLOAT
        if (isPts)
            matrix->mapPoints((SkPoint*)dstArray, (const SkPoint*)srcArray,
                              ptCount);
        else
            matrix->mapVectors((SkVector*)dstArray, (const SkVector*)srcArray,
                               ptCount);
#else
        SkASSERT(false);
#endif
    }

    static jboolean mapRect__RectFRectF(JNIEnv* env, jobject clazz, jlong matrixHandle, jobjectArray dst, jobject src) {
        SkMatrix* matrix = reinterpret_cast<SkMatrix*>(matrixHandle);
        SkRect dst_, src_;
        GraphicsJNI::jrectf_to_rect(env, src, &src_);
        jboolean rectStaysRect = matrix->mapRect(&dst_, src_);
        GraphicsJNI::rect_to_jrectf(dst_, env, dst);
        return rectStaysRect ? JNI_TRUE : JNI_FALSE;
    }

    static jfloat mapRadius(JNIEnv* env, jobject clazz, jlong matrixHandle, jfloat radius) {
        SkMatrix* matrix = reinterpret_cast<SkMatrix*>(matrixHandle);
        float result;
        result = SkScalarToFloat(matrix->mapRadius(radius));
        return static_cast<jfloat>(result);
    }
    static void getValues(JNIEnv* env, jobject clazz, jlong matrixHandle, jfloatArray values) {
        SkMatrix* matrix = reinterpret_cast<SkMatrix*>(matrixHandle);
        AutoJavaFloatArray autoValues(env, values, 9, kRW_JNIAccess);
        float* dst = autoValues.ptr();
#ifdef SK_SCALAR_IS_FLOAT
        for (int i = 0; i < 9; i++) {
            dst[i] = matrix->get(i);
        }
#else
        SkASSERT(false);
#endif
    }

    static void setValues(JNIEnv* env, jobject clazz, jlong matrixHandle, jfloatArray values) {
        SkMatrix* matrix = reinterpret_cast<SkMatrix*>(matrixHandle);
        AutoJavaFloatArray autoValues(env, values, 9, kRO_JNIAccess);
        const float* src = autoValues.ptr();

#ifdef SK_SCALAR_IS_FLOAT
        for (int i = 0; i < 9; i++) {
            matrix->set(i, src[i]);
        }
#else
        SkASSERT(false);
#endif
    }

    static jboolean equals(JNIEnv* env, jobject clazz, jlong aHandle, jlong bHandle) {
        const SkMatrix* a = reinterpret_cast<SkMatrix*>(aHandle);
        const SkMatrix* b = reinterpret_cast<SkMatrix*>(bHandle);
        return *a == *b;
    }
 };

static JNINativeMethod methods[] = {
    {"finalizer", "(J)V", (void*) SkMatrixGlue::finalizer},
    {"native_create","(J)J", (void*) SkMatrixGlue::create},

    {"native_isIdentity","!(J)Z", (void*) SkMatrixGlue::isIdentity},
    {"native_isAffine","!(J)Z", (void*) SkMatrixGlue::isAffine},
    {"native_rectStaysRect","!(J)Z", (void*) SkMatrixGlue::rectStaysRect},
    {"native_reset","!(J)V", (void*) SkMatrixGlue::reset},
    {"native_set","!(JJ)V", (void*) SkMatrixGlue::set},
    {"native_setTranslate","!(JFF)V", (void*) SkMatrixGlue::setTranslate},
    {"native_setScale","!(JFFFF)V", (void*) SkMatrixGlue::setScale__FFFF},
    {"native_setScale","!(JFF)V", (void*) SkMatrixGlue::setScale__FF},
    {"native_setRotate","!(JFFF)V", (void*) SkMatrixGlue::setRotate__FFF},
    {"native_setRotate","!(JF)V", (void*) SkMatrixGlue::setRotate__F},
    {"native_setSinCos","!(JFFFF)V", (void*) SkMatrixGlue::setSinCos__FFFF},
    {"native_setSinCos","!(JFF)V", (void*) SkMatrixGlue::setSinCos__FF},
    {"native_setSkew","!(JFFFF)V", (void*) SkMatrixGlue::setSkew__FFFF},
    {"native_setSkew","!(JFF)V", (void*) SkMatrixGlue::setSkew__FF},
    {"native_setConcat","!(JJJ)V", (void*) SkMatrixGlue::setConcat},
    {"native_preTranslate","!(JFF)V", (void*) SkMatrixGlue::preTranslate},
    {"native_preScale","!(JFFFF)V", (void*) SkMatrixGlue::preScale__FFFF},
    {"native_preScale","!(JFF)V", (void*) SkMatrixGlue::preScale__FF},
    {"native_preRotate","!(JFFF)V", (void*) SkMatrixGlue::preRotate__FFF},
    {"native_preRotate","!(JF)V", (void*) SkMatrixGlue::preRotate__F},
    {"native_preSkew","!(JFFFF)V", (void*) SkMatrixGlue::preSkew__FFFF},
    {"native_preSkew","!(JFF)V", (void*) SkMatrixGlue::preSkew__FF},
    {"native_preConcat","!(JJ)V", (void*) SkMatrixGlue::preConcat},
    {"native_postTranslate","!(JFF)V", (void*) SkMatrixGlue::postTranslate},
    {"native_postScale","!(JFFFF)V", (void*) SkMatrixGlue::postScale__FFFF},
    {"native_postScale","!(JFF)V", (void*) SkMatrixGlue::postScale__FF},
    {"native_postRotate","!(JFFF)V", (void*) SkMatrixGlue::postRotate__FFF},
    {"native_postRotate","!(JF)V", (void*) SkMatrixGlue::postRotate__F},
    {"native_postSkew","!(JFFFF)V", (void*) SkMatrixGlue::postSkew__FFFF},
    {"native_postSkew","!(JFF)V", (void*) SkMatrixGlue::postSkew__FF},
    {"native_postConcat","!(JJ)V", (void*) SkMatrixGlue::postConcat},
    {"native_setRectToRect","!(JLandroid/graphics/RectF;Landroid/graphics/RectF;I)Z", (void*) SkMatrixGlue::setRectToRect},
    {"native_setPolyToPoly","!(J[FI[FII)Z", (void*) SkMatrixGlue::setPolyToPoly},
    {"native_invert","!(JJ)Z", (void*) SkMatrixGlue::invert},
    {"native_mapPoints","!(J[FI[FIIZ)V", (void*) SkMatrixGlue::mapPoints},
    {"native_mapRect","!(JLandroid/graphics/RectF;Landroid/graphics/RectF;)Z", (void*) SkMatrixGlue::mapRect__RectFRectF},
    {"native_mapRadius","!(JF)F", (void*) SkMatrixGlue::mapRadius},
    {"native_getValues","!(J[F)V", (void*) SkMatrixGlue::getValues},
    {"native_setValues","!(J[F)V", (void*) SkMatrixGlue::setValues},
    {"native_equals", "!(JJ)Z", (void*) SkMatrixGlue::equals}
};

static jfieldID sNativeInstanceField;

int register_android_graphics_Matrix(JNIEnv* env) {
    int result = RegisterMethodsOrDie(env, "android/graphics/Matrix", methods, NELEM(methods));

    jclass clazz = FindClassOrDie(env, "android/graphics/Matrix");
    sNativeInstanceField = GetFieldIDOrDie(env, clazz, "native_instance", "J");

    return result;
}

SkMatrix* android_graphics_Matrix_getSkMatrix(JNIEnv* env, jobject matrixObj) {
    return reinterpret_cast<SkMatrix*>(env->GetLongField(matrixObj, sNativeInstanceField));
}

}
