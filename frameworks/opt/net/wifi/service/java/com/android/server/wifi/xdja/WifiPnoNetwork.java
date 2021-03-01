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

import android.net.wifi.WifiConfiguration;


/**
 * Represents a LLCP packet received in a LLCP Connectionless communication;
 */
public class WifiPnoNetwork implements Parcelable {

    public String SSID;
    public int rssi_threshold;
    public int flags;
    public int auth;
    public String configKey; // kept for reference

    public WifiPnoNetwork(WifiConfiguration config, int threshold) {
        if (config.SSID == null) {
            this.SSID = "";
            this.flags = 1;
        } else {
            this.SSID = config.SSID;
        }
        this.rssi_threshold = threshold;
        if (config.allowedKeyManagement.get(WifiConfiguration.KeyMgmt.WPA_PSK)) {
            auth |= 2;
        } else if (config.allowedKeyManagement.get(WifiConfiguration.KeyMgmt.WPA_EAP) ||
                config.allowedKeyManagement.get(WifiConfiguration.KeyMgmt.IEEE8021X)) {
            auth |= 4;
        } else if (config.wepKeys[0] != null) {
            auth |= 1;
        } else {
            auth |= 1;
        }
//            auth = 0;
        flags |= 6; //A and G
        configKey = config.configKey();
    }

    public WifiPnoNetwork() {

    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
        dest.writeString(SSID);
        dest.writeInt(rssi_threshold);
        dest.writeInt(flags);
        dest.writeInt(auth);
        dest.writeString(configKey);
    }

    public static final Parcelable.Creator<WifiPnoNetwork> CREATOR =
            new Parcelable.Creator<WifiPnoNetwork>() {
        @Override
        public WifiPnoNetwork createFromParcel(Parcel in) {
            WifiPnoNetwork params = new WifiPnoNetwork();
            params.SSID = in.readString();
            params.rssi_threshold = in.readInt();
            params.flags = in.readInt();
            params.auth = in.readInt();
            params.configKey = in.readString();
            return params;
        }
        @Override
        public WifiPnoNetwork[] newArray(int size) {
            return new WifiPnoNetwork[size];
        }
    };

    @Override
    public int hashCode() {
        final int prime = 31;
        int result = SSID.hashCode();
        result = prime * result + rssi_threshold;
        result = prime * result + flags;
        result = prime * result + auth;
        result = prime * result + configKey.hashCode();
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
        WifiPnoNetwork other = (WifiPnoNetwork) obj;
        return (SSID.equals(other.SSID)) && 
               (rssi_threshold == other.rssi_threshold) &&
               (flags == other.flags) &&
               (auth == other.auth) &&
               (configKey.equals(other.configKey));
    }

    @Override
    public String toString() {
        StringBuilder sbuf = new StringBuilder();
        sbuf.append(this.SSID);
        sbuf.append(" flags=").append(this.flags);
        sbuf.append(" rssi=").append(this.rssi_threshold);
        sbuf.append(" auth=").append(this.auth);
        return sbuf.toString();
    }

}
