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

// Definitions internal to Minikin

#ifndef MINIKIN_INTERNAL_H
#define MINIKIN_INTERNAL_H

#include <utils/Mutex.h>

namespace android {

// All external Minikin interfaces are designed to be thread-safe.
// Presently, that's implemented by through a global lock, and having
// all external interfaces take that lock.

extern Mutex gMinikinLock;

}

#endif  // MINIKIN_INTERNAL_H
