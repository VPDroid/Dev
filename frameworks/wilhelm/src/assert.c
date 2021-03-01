/*
 * Copyright (C) 2011 The Android Open Source Project
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

// Replace bionic/libc/stdlib/assert.c which logs to stderr with our version which does LOGF.
// To be effective, add CFLAGS += -UNDEBUG and explicitly link in assert.c in all build modules.

#include <utils/Log.h>

#pragma GCC visibility push(default)

void __assert(const char *file, int line, const char *failedexpr)
{
    LOG_ALWAYS_FATAL("assertion \"%s\" failed: file \"%s\", line %d", failedexpr, file, line);
    // not reached
}

void __assert2(const char *file, int line, const char *func, const char *failedexpr)
{
    LOG_ALWAYS_FATAL("assertion \"%s\" failed: file \"%s\", line %d, function \"%s\"",
            failedexpr, file, line, func);
    // not reached
}

#pragma GCC visibility pop
