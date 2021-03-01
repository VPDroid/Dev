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
#include "rsElement.h"

using namespace android;
using namespace android::renderscript;


bool rsdElementInit(const Context *, const Element *e) {
    return true;
}

void rsdElementDestroy(const Context *rsc, const Element *e) {
}

void rsdElementUpdateCachedObject(const Context *rsc,
                                  const Element *element,
                                  rs_element *obj)
{
    obj->p = element;
#ifdef __LP64__
    obj->r = nullptr;
    obj->v1 = nullptr;
    obj->v2 = nullptr;
#endif
}

