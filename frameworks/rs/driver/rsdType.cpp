/*
 * Copyright (C) 2014 The Android Open Source Project
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


#include "rsdCore.h"
#include "rsdSampler.h"

#include "rsContext.h"
#include "rsSampler.h"

#ifndef RS_COMPATIBILITY_LIB
#include "rsProgramVertex.h"
#include "rsProgramFragment.h"

#include <GLES/gl.h>
#include <GLES/glext.h>
#endif

using namespace android;
using namespace android::renderscript;


bool rsdTypeInit(const Context *, const Type *t) {
    return true;
}

void rsdTypeDestroy(const Context *rsc, const Type *t) {
}

void rsdTypeUpdateCachedObject(const Context *rsc,
                               const Type *t,
                               rs_type *obj)
{
    obj->p = t;
#ifdef __LP64__
    obj->r = nullptr;
    obj->v1 = nullptr;
    obj->v2 = nullptr;
#endif
}

