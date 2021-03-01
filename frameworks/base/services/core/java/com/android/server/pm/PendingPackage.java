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

package com.android.server.pm;

import java.io.File;

final class PendingPackage extends PackageSettingBase {
    final int sharedId;

    PendingPackage(String name, String realName, File codePath, File resourcePath,
            String legacyNativeLibraryPathString, String primaryCpuAbiString,
            String secondaryCpuAbiString, String cpuAbiOverrideString, int sharedId,
            int pVersionCode, int pkgFlags, int pkgPrivateFlags) {
        super(name, realName, codePath, resourcePath, legacyNativeLibraryPathString,
                primaryCpuAbiString, secondaryCpuAbiString, cpuAbiOverrideString,
                pVersionCode, pkgFlags, pkgPrivateFlags);
        this.sharedId = sharedId;
    }
}
