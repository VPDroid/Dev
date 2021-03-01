/*
 * Copyright (C) 2011-2012 The Android Open Source Project
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

package com.example.android.rs.computeperf;

import android.app.Activity;
import android.os.Bundle;
import android.graphics.BitmapFactory;
import android.graphics.Bitmap;
import android.renderscript.RenderScript;
import android.renderscript.Allocation;
import android.util.Log;
import android.widget.ImageView;

public class ComputePerf extends Activity {
    private LaunchTest mLT;
    private Mandelbrot mMandel;
    private RenderScript mRS;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.main);

        final int numTries = 100;

        long timesXLW = 0;
        long timesXYW = 0;

        mRS = RenderScript.create(this);
        mLT = new LaunchTest(mRS, getResources());
        mLT.XLW();
        mLT.XYW();
        for (int i = 0; i < numTries; i++) {
            timesXLW += mLT.XLW();
            timesXYW += mLT.XYW();
        }

        timesXLW /= numTries;
        timesXYW /= numTries;

        // XLW and XYW running times should match pretty closely
        Log.v("ComputePerf", "xlw launch test " + timesXLW + "ms");
        Log.v("ComputePerf", "xyw launch test " + timesXYW + "ms");

        mMandel = new Mandelbrot(mRS, getResources());
        mMandel.run();
    }
}
