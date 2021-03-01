/**
 * Copyright (c) 2014, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package android.app.usage;

import android.app.usage.UsageEvents;
import android.content.pm.ParceledListSlice;

/**
 * System private API for talking with the UsageStatsManagerService.
 *
 * {@hide}
 */
interface IUsageStatsManager {
    ParceledListSlice queryUsageStats(int bucketType, long beginTime, long endTime,
            String callingPackage);
    ParceledListSlice queryConfigurationStats(int bucketType, long beginTime, long endTime,
            String callingPackage);
    UsageEvents queryEvents(long beginTime, long endTime, String callingPackage);
    void setAppInactive(String packageName, boolean inactive, int userId);
    boolean isAppInactive(String packageName, int userId);
    void whitelistAppTemporarily(String packageName, long duration, int userId);
}
