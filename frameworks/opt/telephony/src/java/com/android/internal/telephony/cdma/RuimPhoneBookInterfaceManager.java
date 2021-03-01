/*
** Copyright 2007, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

package com.android.internal.telephony.cdma;

import java.util.concurrent.atomic.AtomicBoolean;

import android.os.Message;
import android.telephony.Rlog;

import com.android.internal.telephony.IccPhoneBookInterfaceManager;
import com.android.internal.telephony.uicc.IccFileHandler;

/**
 * RuimPhoneBookInterfaceManager to provide an inter-process communication to
 * access ADN-like SIM records.
 */


public class RuimPhoneBookInterfaceManager extends IccPhoneBookInterfaceManager {
    static final String LOG_TAG = "RuimPhoneBookIM";

    public RuimPhoneBookInterfaceManager(CDMAPhone phone) {
        super(phone);
        //NOTE service "simphonebook" added by IccSmsInterfaceManagerProxy
    }

    @Override
    public void dispose() {
        super.dispose();
    }

    @Override
    protected void finalize() {
        try {
            super.finalize();
        } catch (Throwable throwable) {
            Rlog.e(LOG_TAG, "Error while finalizing:", throwable);
        }
        if(DBG) Rlog.d(LOG_TAG, "RuimPhoneBookInterfaceManager finalized");
    }

    @Override
    public int[] getAdnRecordsSize(int efid) {
        if (DBG) logd("getAdnRecordsSize: efid=" + efid);
        synchronized(mLock) {
            checkThread();
            mRecordSize = new int[3];

            //Using mBaseHandler, no difference in EVENT_GET_SIZE_DONE handling
            AtomicBoolean status = new AtomicBoolean(false);
            Message response = mBaseHandler.obtainMessage(EVENT_GET_SIZE_DONE, status);

            IccFileHandler fh = mPhone.getIccFileHandler();
            //IccFileHandler can be null if there is no icc card present.
            if (fh != null) {
                fh.getEFLinearRecordSize(efid, response);
                waitForResult(status);
            }
        }

        return mRecordSize;
    }

    @Override
    protected void logd(String msg) {
        Rlog.d(LOG_TAG, "[RuimPbInterfaceManager] " + msg);
    }

    @Override
    protected void loge(String msg) {
        Rlog.e(LOG_TAG, "[RuimPbInterfaceManager] " + msg);
    }
}

