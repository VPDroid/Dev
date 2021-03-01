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

package com.android.rs.image;

import java.lang.Math;

import android.graphics.Bitmap;
import android.renderscript.Allocation;
import android.renderscript.Element;
import android.renderscript.RenderScript;
import android.renderscript.ScriptIntrinsicBlur;
import android.renderscript.ScriptIntrinsicColorMatrix;
import android.renderscript.Type;
import android.renderscript.Matrix4f;
import android.util.Log;
import android.widget.SeekBar;
import android.widget.TextView;

public class Blur25G extends TestBase {
    private final int MAX_RADIUS = 25;
    private float mRadius = MAX_RADIUS;

    private ScriptIntrinsicBlur mIntrinsic;

    private ScriptIntrinsicColorMatrix mCM;
    private Allocation mScratchPixelsAllocation1;
    private Allocation mScratchPixelsAllocation2;


    public Blur25G() {
    }

    public boolean onBar1Setup(SeekBar b, TextView t) {
        t.setText("Radius");
        b.setProgress(100);
        return true;
    }


    public void onBar1Changed(int progress) {
        mRadius = ((float)progress) / 100.0f * MAX_RADIUS;
        if (mRadius <= 0.10f) {
            mRadius = 0.10f;
        }
        mIntrinsic.setRadius(mRadius);
    }


    public void createTest(android.content.res.Resources res) {
        int width = mInPixelsAllocation.getType().getX();
        int height = mInPixelsAllocation.getType().getY();

        Type.Builder tb = new Type.Builder(mRS, Element.U8(mRS));
        tb.setX(width);
        tb.setY(height);
        mScratchPixelsAllocation1 = Allocation.createTyped(mRS, tb.create());
        mScratchPixelsAllocation2 = Allocation.createTyped(mRS, tb.create());

        mCM = ScriptIntrinsicColorMatrix.create(mRS);
        mCM.forEach(mInPixelsAllocation, mScratchPixelsAllocation1);

        mIntrinsic = ScriptIntrinsicBlur.create(mRS, Element.U8(mRS));
        mIntrinsic.setRadius(MAX_RADIUS);
        mIntrinsic.setInput(mScratchPixelsAllocation1);
    }

    public void runTest() {
        mIntrinsic.forEach(mScratchPixelsAllocation2);
    }

    public void setupBenchmark() {
        mIntrinsic.setRadius(MAX_RADIUS);
    }

    public void exitBenchmark() {
        mIntrinsic.setRadius(mRadius);
    }

    public void updateBitmap(Bitmap b) {
        Matrix4f m = new Matrix4f();
        m.set(0, 0, 1.f);
        m.set(0, 1, 1.f);
        m.set(0, 2, 1.f);

        m.set(1, 1, 0.f);
        m.set(2, 2, 0.f);
        m.set(3, 3, 0.f);
        mCM.setColorMatrix(m);
        mCM.setAdd(0.f, 0.f, 0.f, 1.f);

        mCM.forEach(mScratchPixelsAllocation2, mOutPixelsAllocation);
        mOutPixelsAllocation.copyTo(b);
    }

}

