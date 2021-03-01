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

package com.android.rs.image;

import java.lang.Math;

import android.renderscript.Allocation;
import android.renderscript.Element;
import android.renderscript.RenderScript;
import android.renderscript.Script;
import android.renderscript.ScriptC;
import android.renderscript.Type;
import android.util.Log;

public class Mirror extends TestBase {
    private ScriptC_mirror mScript;
    private int mWidth;
    private int mHeight;

    public void createTest(android.content.res.Resources res) {
        mScript = new ScriptC_mirror(mRS);

        mWidth = mInPixelsAllocation.getType().getX();
        mHeight = mInPixelsAllocation.getType().getY();

        mScript.set_gIn(mInPixelsAllocation);
        mScript.set_gWidth(mWidth);
        mScript.set_gHeight(mHeight);
    }

    public void runTest() {
        mScript.forEach_mirror(mOutPixelsAllocation);
    }

}
