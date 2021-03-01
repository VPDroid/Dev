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

package com.android.server.wifi.xdja;

import android.os.Parcel;
import android.os.Parcelable;
import java.util.Arrays;
import java.nio.ByteBuffer;


/**
 * Represents a LLCP packet received in a LLCP Connectionless communication;
 */
public class WifiLazyRoamParams implements Parcelable {

    public int A_band_boost_threshold;
    public int A_band_penalty_threshold;
    public int A_band_boost_factor;
    public int A_band_penalty_factor;
    public int A_band_max_boost;
    public int lazy_roam_hysteresis;
    public int alert_roam_rssi_trigger;

    public WifiLazyRoamParams() {

    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
        dest.writeInt(A_band_boost_threshold);
        dest.writeInt(A_band_penalty_threshold);
        dest.writeInt(A_band_boost_factor);
        dest.writeInt(A_band_penalty_factor);
        dest.writeInt(A_band_max_boost);
        dest.writeInt(lazy_roam_hysteresis);
        dest.writeInt(alert_roam_rssi_trigger);
    }

    public static final Parcelable.Creator<WifiLazyRoamParams> CREATOR =
            new Parcelable.Creator<WifiLazyRoamParams>() {
        @Override
        public WifiLazyRoamParams createFromParcel(Parcel in) {
            WifiLazyRoamParams params = new WifiLazyRoamParams();
            params.A_band_boost_threshold = in.readInt();
            params.A_band_penalty_threshold = in.readInt();
            params.A_band_boost_factor = in.readInt();
            params.A_band_penalty_factor = in.readInt();
            params.A_band_max_boost = in.readInt();
            params.lazy_roam_hysteresis = in.readInt();
            params.alert_roam_rssi_trigger = in.readInt();
            return params;
        }
        @Override
        public WifiLazyRoamParams[] newArray(int size) {
            return new WifiLazyRoamParams[size];
        }
    };

    @Override
    public int hashCode() {
        final int prime = 31;
        int result = A_band_boost_threshold;
        result = prime * result + A_band_penalty_threshold;
        result = prime * result + A_band_boost_factor;
        result = prime * result + A_band_penalty_factor;
        result = prime * result + A_band_max_boost;
        result = prime * result + lazy_roam_hysteresis;
        result = prime * result + alert_roam_rssi_trigger;
        return result;
    }

    /**
     * Returns true if the specified NDEF Message contains
     * identical NDEF Records.
     */
    @Override
    public boolean equals(Object obj) {
        if (this == obj) return true;
        if (obj == null) return false;
        if (getClass() != obj.getClass()) return false;
        WifiLazyRoamParams other = (WifiLazyRoamParams) obj;
        return (A_band_boost_threshold == other.A_band_boost_threshold) && 
               (A_band_penalty_threshold == other.A_band_penalty_threshold) &&
               (A_band_boost_factor == other.A_band_boost_factor) &&
               (A_band_penalty_factor == other.A_band_penalty_factor) &&
               (A_band_max_boost == other.A_band_max_boost) &&
               (lazy_roam_hysteresis == other.lazy_roam_hysteresis) &&
               (alert_roam_rssi_trigger == other.alert_roam_rssi_trigger);
    }

    @Override
    public String toString() {
        StringBuilder sbuf = new StringBuilder();
        sbuf.append(" A_band_boost_threshold=").append(this.A_band_boost_threshold);
        sbuf.append(" A_band_penalty_threshold=").append(this.A_band_penalty_threshold);
        sbuf.append(" A_band_boost_factor=").append(this.A_band_boost_factor);
        sbuf.append(" A_band_penalty_factor=").append(this.A_band_penalty_factor);
        sbuf.append(" A_band_max_boost=").append(this.A_band_max_boost);
        sbuf.append(" lazy_roam_hysteresis=").append(this.lazy_roam_hysteresis);
        sbuf.append(" alert_roam_rssi_trigger=").append(this.alert_roam_rssi_trigger);
        return sbuf.toString();
    }

}
