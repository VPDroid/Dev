/******************************************************************************
 *
 *  Copyright (C) 2014 Google, Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/

#pragma once

#include <cutils/log.h>

#define LOG_VERBOSE(...) ALOGV(__VA_ARGS__)
#define LOG_DEBUG(...)   ALOGD(__VA_ARGS__)
#define LOG_INFO(...)    ALOGI(__VA_ARGS__)
#define LOG_WARN(...)    ALOGW(__VA_ARGS__)
#define LOG_ERROR(...)   ALOGE(__VA_ARGS__)
