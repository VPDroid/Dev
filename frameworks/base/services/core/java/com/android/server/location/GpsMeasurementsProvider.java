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

import android.location.GpsMeasurementsEvent;
import android.location.IGpsMeasurementsListener;
import android.os.Handler;
import android.os.RemoteException;
import android.util.Log;

/**
 * An base implementation for GPS measurements provider.
 * It abstracts out the responsibility of handling listeners, while still allowing technology
 * specific implementations to be built.
 *
 * @hide
 */
public abstract class GpsMeasurementsProvider
        extends RemoteListenerHelper<IGpsMeasurementsListener> {
    private static final String TAG = "GpsMeasurementsProvider";

    protected GpsMeasurementsProvider(Handler handler) {
        super(handler, TAG);
    }

    public void onMeasurementsAvailable(final GpsMeasurementsEvent event) {
        ListenerOperation<IGpsMeasurementsListener> operation =
                new ListenerOperation<IGpsMeasurementsListener>() {
            @Override
            public void execute(IGpsMeasurementsListener listener) throws RemoteException {
                listener.onGpsMeasurementsReceived(event);
            }
        };
        foreach(operation);
    }

    public void onCapabilitiesUpdated(boolean isGpsMeasurementsSupported) {
        setSupported(isGpsMeasurementsSupported);
        updateResult();
    }

    public void onGpsEnabledChanged() {
        if (tryUpdateRegistrationWithService()) {
            updateResult();
        }
    }

    @Override
    protected ListenerOperation<IGpsMeasurementsListener> getHandlerOperation(int result) {
        int status;
        switch (result) {
            case RESULT_SUCCESS:
                status = GpsMeasurementsEvent.STATUS_READY;
                break;
            case RESULT_NOT_AVAILABLE:
            case RESULT_NOT_SUPPORTED:
            case RESULT_INTERNAL_ERROR:
                status = GpsMeasurementsEvent.STATUS_NOT_SUPPORTED;
                break;
            case RESULT_GPS_LOCATION_DISABLED:
                status = GpsMeasurementsEvent.STATUS_GPS_LOCATION_DISABLED;
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
            implements ListenerOperation<IGpsMeasurementsListener> {
        private final int mStatus;

        public StatusChangedOperation(int status) {
            mStatus = status;
        }

        @Override
        public void execute(IGpsMeasurementsListener listener) throws RemoteException {
            listener.onStatusChanged(mStatus);
        }
    }
}
