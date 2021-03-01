/*
 * Copyright (C) 2011-2012 The Android Open Source Project
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

#ifndef RSD_CORE_H
#define RSD_CORE_H

#include <rs_hal.h>

#include "../cpu_ref/rsd_cpu.h"

#include "rsMutex.h"
#include "rsSignal.h"

#ifndef RS_COMPATIBILITY_LIB
#include "rsdGL.h"
#endif

typedef struct ScriptTLSStructRec {
    android::renderscript::Context * mContext;
    android::renderscript::Script * mScript;
} ScriptTLSStruct;

typedef struct RsdHalRec {
    uint32_t version_major;
    uint32_t version_minor;
    bool mHasGraphics;

    ScriptTLSStruct mTlsStruct;
    android::renderscript::RsdCpuReference *mCpuRef;

#ifndef RS_COMPATIBILITY_LIB
    RsdGL gl;
#endif
} RsdHal;

void* rsdAllocRuntimeMem(size_t size, uint32_t flags);
void rsdFreeRuntimeMem(void* ptr);

#endif
