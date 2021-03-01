/*
 * Copyright (C) 2012 The Android Open Source Project
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

package com.android.rs.imagejb;

import java.lang.Math;

import android.renderscript.Allocation;

public class Contrast extends TestBase {
    private ScriptC_contrast mScript;

    public void createTest(android.content.res.Resources res) {
        mScript = new ScriptC_contrast(mRS);
    }

    public void runTest() {
        mScript.invoke_setBright(50.f);
        mScript.forEach_contrast(mInPixelsAllocation, mOutPixelsAllocation);
    }

}
