/*
 * Copyright (C) 2009-2012 The Android Open Source Project
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

#include "rsContext.h"
#include "rsScriptC.h"
#include "rsMatrix4x4.h"
#include "rsMatrix3x3.h"
#include "rsMatrix2x2.h"
#include "rsgApiStructs.h"

#if !defined(RS_SERVER) && !defined(RS_COMPATIBILITY_LIB)
#include "utils/Timers.h"
#endif

#include <time.h>

using namespace android;
using namespace android::renderscript;


namespace android {
namespace renderscript {


//////////////////////////////////////////////////////////////////////////////
// Math routines
//////////////////////////////////////////////////////////////////////////////

#if 0
static float SC_sinf_fast(float x) {
    const float A =   1.0f / (2.0f * M_PI);
    const float B = -16.0f;
    const float C =   8.0f;

    // scale angle for easy argument reduction
    x *= A;

    if (fabsf(x) >= 0.5f) {
        // argument reduction
        x = x - ceilf(x + 0.5f) + 1.0f;
    }

    const float y = B * x * fabsf(x) + C * x;
    return 0.2215f * (y * fabsf(y) - y) + y;
}

static float SC_cosf_fast(float x) {
    x += float(M_PI / 2);

    const float A =   1.0f / (2.0f * M_PI);
    const float B = -16.0f;
    const float C =   8.0f;

    // scale angle for easy argument reduction
    x *= A;

    if (fabsf(x) >= 0.5f) {
        // argument reduction
        x = x - ceilf(x + 0.5f) + 1.0f;
    }

    const float y = B * x * fabsf(x) + C * x;
    return 0.2215f * (y * fabsf(y) - y) + y;
}
#endif

//////////////////////////////////////////////////////////////////////////////
// Time routines
//////////////////////////////////////////////////////////////////////////////

time_t rsrTime(Context *rsc, time_t *timer) {
    return time(timer);
}

tm* rsrLocalTime(Context *rsc, tm *local, time_t *timer) {
    if (!local) {
      return nullptr;
    }

    // The native localtime function is not thread-safe, so we
    // have to apply locking for proper behavior in RenderScript.
    pthread_mutex_lock(&rsc->gLibMutex);
    tm *tmp = localtime(timer);
    memcpy(local, tmp, sizeof(int)*9);
    pthread_mutex_unlock(&rsc->gLibMutex);
    return local;
}

int64_t rsrUptimeMillis(Context *rsc) {
#ifndef RS_SERVER
    return nanoseconds_to_milliseconds(systemTime(SYSTEM_TIME_MONOTONIC));
#else
    return 0;
#endif
}

int64_t rsrUptimeNanos(Context *rsc) {
#ifndef RS_SERVER
    return systemTime(SYSTEM_TIME_MONOTONIC);
#else
    return 0;
#endif
}

float rsrGetDt(Context *rsc, const Script *sc) {
#ifndef RS_SERVER
    int64_t l = sc->mEnviroment.mLastDtTime;
    sc->mEnviroment.mLastDtTime = systemTime(SYSTEM_TIME_MONOTONIC);
    return ((float)(sc->mEnviroment.mLastDtTime - l)) / 1.0e9;
#else
    return 0.f;
#endif
}

//////////////////////////////////////////////////////////////////////////////
//
//////////////////////////////////////////////////////////////////////////////

static void SetObjectRef(const Context *rsc, const ObjectBase *dst, const ObjectBase *src) {
    //ALOGE("setObjectRef  %p,%p  %p", rsc, dst, src);
    if (src) {
        CHECK_OBJ(src);
        src->incSysRef();
    }
    if (dst) {
        CHECK_OBJ(dst);
        dst->decSysRef();
    }
}

// Legacy, remove when drivers are updated
void rsrClearObject(const Context *rsc, void *dst) {
    ObjectBase **odst = (ObjectBase **)dst;
    if (ObjectBase::gDebugReferences) {
        ALOGE("rsrClearObject  %p,%p", odst, *odst);
    }
    if (odst[0]) {
        CHECK_OBJ(odst[0]);
        odst[0]->decSysRef();
    }
    *odst = nullptr;
}

void rsrClearObject(const Context *rsc, rs_object_base *dst) {
    if (ObjectBase::gDebugReferences) {
        ALOGE("rsrClearObject  %p,%p", dst, dst->p);
    }
    if (dst->p) {
        CHECK_OBJ(dst->p);
        dst->p->decSysRef();
    }
    dst->p = nullptr;
}

// Legacy, remove when drivers are updated
void rsrSetObject(const Context *rsc, void *dst, ObjectBase *src) {
    if (src == nullptr) {
        rsrClearObject(rsc, dst);
        return;
    }

    ObjectBase **odst = (ObjectBase **)dst;
    if (ObjectBase::gDebugReferences) {
        ALOGE("rsrSetObject (base) %p,%p  %p", dst, *odst, src);
    }
    SetObjectRef(rsc, odst[0], src);
    src->callUpdateCacheObject(rsc, dst);
}

void rsrSetObject(const Context *rsc, rs_object_base *dst, const ObjectBase *src) {
    if (src == nullptr) {
        rsrClearObject(rsc, dst);
        return;
    }

    ObjectBase **odst = (ObjectBase **)dst;
    if (ObjectBase::gDebugReferences) {
        ALOGE("rsrSetObject (base) %p,%p  %p", dst, *odst, src);
    }
    SetObjectRef(rsc, odst[0], src);
    src->callUpdateCacheObject(rsc, dst);
}

// Legacy, remove when drivers are updated
bool rsrIsObject(const Context *, ObjectBase* src) {
    ObjectBase **osrc = (ObjectBase **)src;
    return osrc != nullptr;
}

bool rsrIsObject(const Context *rsc, rs_object_base o) {
    return o.p != nullptr;
}



uint32_t rsrToClient(Context *rsc, int cmdID, const void *data, int len) {
    //ALOGE("SC_toClient %i %i %i", cmdID, len);
    return rsc->sendMessageToClient(data, RS_MESSAGE_TO_CLIENT_USER, cmdID, len, false);
}

uint32_t rsrToClientBlocking(Context *rsc, int cmdID, const void *data, int len) {
    //ALOGE("SC_toClientBlocking %i %i", cmdID, len);
    return rsc->sendMessageToClient(data, RS_MESSAGE_TO_CLIENT_USER, cmdID, len, true);
}

// Keep these two routines (using non-const void pointers) so that we can
// still use existing GPU drivers.
uint32_t rsrToClient(Context *rsc, int cmdID, void *data, int len) {
    return rsrToClient(rsc, cmdID, (const void *)data, len);
}

uint32_t rsrToClientBlocking(Context *rsc, int cmdID, void *data, int len) {
    return rsrToClientBlocking(rsc, cmdID, (const void *)data, len);
}

void rsrAllocationIoSend(Context *rsc, Allocation *src) {
    src->ioSend(rsc);
}

void rsrAllocationIoReceive(Context *rsc, Allocation *src) {
    src->ioReceive(rsc);
}

void rsrForEach(Context *rsc,
                Script *target,
                Allocation *in, Allocation *out,
                const void *usr, uint32_t usrBytes,
                const RsScriptCall *call) {

    if (in == nullptr) {
        target->runForEach(rsc, /* root slot */ 0, nullptr, 0, out, usr,
                           usrBytes, call);

    } else {
        const Allocation *ins[1] = {in};
        target->runForEach(rsc, /* root slot */ 0, ins,
                           sizeof(ins) / sizeof(RsAllocation), out, usr,
                           usrBytes, call);
    }
}

void rsrAllocationSyncAll(Context *rsc, Allocation *a, RsAllocationUsageType usage) {
    a->syncAll(rsc, usage);
}

void rsrAllocationCopy1DRange(Context *rsc, Allocation *dstAlloc,
                              uint32_t dstOff,
                              uint32_t dstMip,
                              uint32_t count,
                              Allocation *srcAlloc,
                              uint32_t srcOff, uint32_t srcMip) {
    rsi_AllocationCopy2DRange(rsc, dstAlloc, dstOff, 0,
                              dstMip, 0, count, 1,
                              srcAlloc, srcOff, 0, srcMip, 0);
}

void rsrAllocationCopy2DRange(Context *rsc, Allocation *dstAlloc,
                              uint32_t dstXoff, uint32_t dstYoff,
                              uint32_t dstMip, uint32_t dstFace,
                              uint32_t width, uint32_t height,
                              Allocation *srcAlloc,
                              uint32_t srcXoff, uint32_t srcYoff,
                              uint32_t srcMip, uint32_t srcFace) {
    rsi_AllocationCopy2DRange(rsc, dstAlloc, dstXoff, dstYoff,
                              dstMip, dstFace, width, height,
                              srcAlloc, srcXoff, srcYoff, srcMip, srcFace);
}


}
}
