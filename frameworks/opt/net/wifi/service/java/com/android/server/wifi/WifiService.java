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

package com.android.server.wifi;

import android.content.Context;
import android.net.wifi.ScanResult;
import android.net.wifi.WifiScanner;
import android.util.Log;
import com.android.server.SystemService;

import java.util.List;

import android.os.IBinder;
import android.os.RemoteException;
import android.os.ServiceManager;
import android.os.SystemClock;

public final class WifiService extends SystemService {

    private static final String TAG = "WifiService";
    final WifiServiceImpl mImpl;
    //final WatchDogVMSystemThread mWatchDogVM;

    public WifiService(Context context) {
        super(context);
        mImpl = new WifiServiceImpl(context);
        //mWatchDogVM = new WatchDogVMSystemThread("WatchDogVMSystem");
    }

    @Override
    public void onStart() {
        Log.i(TAG, "Registering " + Context.WIFI_SERVICE);
        publishBinderService(Context.WIFI_SERVICE, mImpl);
    }

    @Override
    public void onBootPhase(int phase) {
        if (phase == SystemService.PHASE_SYSTEM_SERVICES_READY) {
            mImpl.checkAndStartWifi();
            //mWatchDogVM.start();
        }
    }

    /*class WatchDogVMSystemThread extends Thread {
        public WatchDogVMSystemThread(String threadName) {
            super(threadName);
        }

        @Override
        public void run() {

            do{
                SystemClock.sleep(1000);

                IBinder binder = ServiceManager.getOtherSystemService(Context.WINDOW_SERVICE);
                if(binder != null){
                    break;
                }

            }while(true);

            mImpl.checkAndStartWifi();

            Log.d(TAG, "checkAndStartWifi !!! ");
        }
    }*/
}
