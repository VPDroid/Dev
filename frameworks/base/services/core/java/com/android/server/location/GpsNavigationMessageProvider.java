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
 * limitations under the License
 */

package com.android.server.location;

import android.location.GpsNavigationMessageEvent;
import android.location.IGpsNavigationMessageListener;
import android.os.Handler;
import android.os.RemoteException;
import android.util.Log;

/**
 * An base implementation for GPS navigation messages provider.
 * It abstracts out the responsibility of handling listeners, while still allowing technology
 * specific implementations to be built.
 *
 * @hide
 */
public abstract class GpsNavigationMessageProvider
        extends RemoteListenerHelper<IGpsNavigationMessageListener> {
    private static final String TAG = "GpsNavigationMessageProvider";

    protected GpsNavigationMessageProvider(Handler handler) {
        super(handler, TAG);
    }

    public void onNavigationMessageAvailable(final GpsNavigationMessageEvent event) {
        ListenerOperation<IGpsNavigationMessageListener> operation =
                new ListenerOperation<IGpsNavigationMessageListener>() {
                    @Override
                    public void execute(IGpsNavigationMessageListener listener)
                            throws RemoteException {
                        listener.onGpsNavigationMessageReceived(event);
                    }
                };
        foreach(operation);
    }

    public void onCapabilitiesUpdated(boolean isGpsNavigationMessageSupported) {
        setSupported(isGpsNavigationMessageSupported);
        updateResult();
    }

    public void onGpsEnabledChanged() {
        if (tryUpdateRegistrationWithService()) {
            updateResult();
        }
    }

    @Override
    protected ListenerOperation<IGpsNavigationMessageListener> getHandlerOperation(int result) {
        int status;
        switch (result) {
            case RESULT_SUCCESS:
                status = GpsNavigationMessageEvent.STATUS_READY;
                break;
            case RESULT_NOT_AVAILABLE:
            case RESULT_NOT_SUPPORTED:
            case RESULT_INTERNAL_ERROR:
                status = GpsNavigationMessageEvent.STATUS_NOT_SUPPORTED;
                break;
            case RESULT_GPS_LOCATION_DISABLED:
                status = GpsNavigationMessageEvent.STATUS_GPS_LOCATION_DISABLED;
                break;
            case RESULT_UNKNOWN:
                return null;
            default:
                Log.v(TAG, "Unhandled addListener result: " + result);
                return null;
        }
        return new StatusChangedOperation(status);
    }

    private static class StatusChangedOperation
            implements ListenerOperation<IGpsNavigationMessageListener> {
        private final int mStatus;

        public StatusChangedOperation(int status) {
            mStatus = status;
        }

        @Override
        public void execute(IGpsNavigationMessageListener listener) throws RemoteException {
            listener.onStatusChanged(mStatus);
        }
    }
}
