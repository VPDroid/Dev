/*
 * Copyright (C) 2015 The Android Open Source Project
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

#ifndef PROCESS_INFO_H_

#define PROCESS_INFO_H_

#include <media/stagefright/foundation/ABase.h>
#include <media/stagefright/ProcessInfoInterface.h>

namespace android {

struct ProcessInfo : public ProcessInfoInterface {
    ProcessInfo();

    virtual bool getPriority(int pid, int* priority);

protected:
    virtual ~ProcessInfo();

private:
    DISALLOW_EVIL_CONSTRUCTORS(ProcessInfo);
};

}  // namespace android

#endif  // PROCESS_INFO_H_
