/*
 * Copyright (C) 2010 The Android Open Source Project
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

package com.android.rs.test_v16;

import android.content.Context;
import android.content.res.Resources;
import android.renderscript.*;

public class UT_fp_mad extends UnitTest {
    private Resources mRes;

    protected UT_fp_mad(RSTestCore rstc, Resources res, Context ctx) {
        super(rstc, "Fp_Mad", ctx);
        mRes = res;
    }

    public void run() {
        RenderScript pRS = RenderScript.create(mCtx);
        ScriptC_fp_mad s = new ScriptC_fp_mad(pRS);
        pRS.setMessageHandler(mRsMessage);
        s.invoke_fp_mad_test(0, 0);
        pRS.finish();
        waitForMessage();
        pRS.destroy();
    }
}
