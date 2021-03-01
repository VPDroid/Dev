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

package com.android.server.wifi;

import android.Manifest.permission;
import android.content.Context;
import android.net.INetworkScoreCache;
import android.net.NetworkKey;
import android.net.ScoredNetwork;
import android.net.wifi.ScanResult;
import android.net.wifi.WifiManager;
import android.util.Log;

import java.io.FileDescriptor;
import java.io.PrintWriter;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class WifiNetworkScoreCache extends INetworkScoreCache.Stub
 {

    // A Network scorer returns a score in the range [-128, +127]
    // We treat the lowest possible score as though there were no score, effectively allowing the
    // scorer to provide an RSSI threshold below which a network should not be used.
    public static int INVALID_NETWORK_SCORE = Byte.MIN_VALUE;

    private static String TAG = "WifiNetworkScoreCache";
    private boolean DBG = true;
    private final Context mContext;

    // The key is of the form "<ssid>"<bssid>
    // TODO: What about SSIDs that can't be encoded as UTF-8?
    private final Map<String, ScoredNetwork> mNetworkCache;

    public WifiNetworkScoreCache(Context context) {
        mContext = context;
        mNetworkCache = new HashMap<String, ScoredNetwork>();
    }

     @Override public final void updateScores(List<android.net.ScoredNetwork> networks) {
        if (networks == null) {
            return;
        }
        Log.e(TAG, "updateScores list size=" + networks.size());

        synchronized(mNetworkCache) {
            for (ScoredNetwork network : networks) {
                String networkKey = buildNetworkKey(network);
                if (networkKey == null) continue;
                mNetworkCache.put(networkKey, network);
            }
        }
     }

     @Override public final void clearScores() {
         synchronized (mNetworkCache) {
             mNetworkCache.clear();
         }
     }

    /**
     * Returns whether there is any score info for the given ScanResult.
     *
     * This includes null-score info, so it should only be used when determining whether to request
     * scores from the network scorer.
     */
    public boolean isScoredNetwork(ScanResult result) {
        return getScoredNetwork(result) != null;
    }

    /**
     * Returns whether there is a non-null score curve for the given ScanResult.
     *
     * A null score curve has special meaning - we should never connect to an ephemeral network if
     * the score curve is null.
     */
    public boolean hasScoreCurve(ScanResult result) {
        ScoredNetwork network = getScoredNetwork(result);
        return network != null && network.rssiCurve != null;
    }

    public int getNetworkScore(ScanResult result) {

        int score = INVALID_NETWORK_SCORE;

        ScoredNetwork network = getScoredNetwork(result);
        if (network != null && network.rssiCurve != null) {
            score = network.rssiCurve.lookupScore(result.level);
            if (DBG) {
                Log.e(TAG, "getNetworkScore found scored network " + network.networkKey
                        + " score " + Integer.toString(score)
                        + " RSSI " + result.level);
            }
        }
        return score;
    }

    public int getNetworkScore(ScanResult result, boolean isActiveNetwork) {

        int score = INVALID_NETWORK_SCORE;

        ScoredNetwork network = getScoredNetwork(result);
        if (network != null && network.rssiCurve != null) {
            score = network.rssiCurve.lookupScore(result.level, isActiveNetwork);
            if (DBG) {
                Log.e(TAG, "getNetworkScore found scored network " + network.networkKey
                        + " score " + Integer.toString(score)
                        + " RSSI " + result.level
                        + " isActiveNetwork " + isActiveNetwork);
            }
        }
        return score;
    }

    private ScoredNetwork getScoredNetwork(ScanResult result) {
        String key = buildNetworkKey(result);
        if (key == null) return null;

        //find it
        synchronized(mNetworkCache) {
            ScoredNetwork network = mNetworkCache.get(key);
            return network;
        }
    }

     private String buildNetworkKey(ScoredNetwork network) {
        if (network == null || network.networkKey == null) return null;
        if (network.networkKey.wifiKey == null) return null;
        if (network.networkKey.type == NetworkKey.TYPE_WIFI) {
            String key = network.networkKey.wifiKey.ssid;
            if (key == null) return null;
            if (network.networkKey.wifiKey.bssid != null) {
                key = key + network.networkKey.wifiKey.bssid;
            }
            return key;
        }
        return null;
    }

    private String buildNetworkKey(ScanResult result) {
        if (result == null || result.SSID == null) {
            return null;
        }
        StringBuilder key = new StringBuilder("\"");
        key.append(result.SSID);
        key.append("\"");
        if (result.BSSID != null) {
            key.append(result.BSSID);
        }
        return key.toString();
    }

    @Override protected final void dump(FileDescriptor fd, PrintWriter writer, String[] args) {
        mContext.enforceCallingOrSelfPermission(permission.DUMP, TAG);
        writer.println("WifiNetworkScoreCache");
        writer.println("  All score curves:");
        for (Map.Entry<String, ScoredNetwork> entry : mNetworkCache.entrySet()) {
            writer.println("    " + entry.getKey() + ": " + entry.getValue().rssiCurve);
        }
        writer.println("  Current network scores:");
        WifiManager wifiManager = (WifiManager) mContext.getSystemService(Context.WIFI_SERVICE);
        for (ScanResult scanResult : wifiManager.getScanResults()) {
            writer.println("    " + buildNetworkKey(scanResult) + ": " + getNetworkScore(scanResult));
        }
    }

}
