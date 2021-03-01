/*
 * Copyright (C) 2007 The Android Open Source Project
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

/**
 * A pointer array which intelligently expands its capacity ad needed.
 */

#ifndef __CONTAINER_H
#define __CONTAINER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
enum {
    RMNET_DATA0 = 0,
    RMNET_DATA1 = 1,
    WLAN0 = 2,
    FISSION_INTERFACE_NUM
};

#define SYSTEM_TYPE_HOST    (100)
#define SYSTEM_TYPE_SECURITY    (1)
#define SYSTEM_TYPE_GENERAY     (0)
int getInterfacePrefix(const int netIdx);
const char *getInterfaceIp(const int netIdx, const int sysId);
const char *getInterfaceGate(const int netIdx, const int sysId);
const char *getInterfaceName(const int netIdx);
const char *getInterfaceHostName(const int netIdx, const int sysId);
int getInterfaceRouteTable(const int netIdx);
int getInterfaceRulePrio(const int netIdx);
int getInterfaceIdx(const char *ifname);
int get_container_mode(int *pmode);
int configHostVeth(const int netIdx, const int sysId, int isUp);
int isHostSystem(void);
#ifdef __cplusplus
}
#endif

#endif /* __CONTAINER_H */
