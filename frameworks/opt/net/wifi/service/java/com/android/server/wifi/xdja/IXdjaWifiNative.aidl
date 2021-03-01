/*
 * Copyright (C) 2008 The Android Open Source Project
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

import com.android.server.wifi.xdja.WifiLazyRoamParams;
import com.android.server.wifi.xdja.WifiPnoNetwork;
import com.android.server.wifi.xdja.RingBufferStatus;
import com.android.server.wifi.xdja.TdlsCapabilities;
import com.android.server.wifi.xdja.TdlsStatus;
import com.android.server.wifi.xdja.ScanSettings;
import com.android.server.wifi.xdja.ScanCapabilities;
import com.android.server.wifi.xdja.ParcelableScanData;
import com.android.server.wifi.xdja.ParcelableWifiLinkLayerStats;
import com.android.server.wifi.xdja.ParcelableHotlistSettings;
import com.android.server.wifi.xdja.ParcelableWifiChangeSettings;
import com.android.server.wifi.xdja.ParcelableRttParams;
import com.android.server.wifi.xdja.ParcelableRttCapabilities;
import com.android.server.wifi.xdja.ParcelableWifiPnoNetwork;
import com.android.server.wifi.xdja.ParcelableString;
import com.android.server.wifi.xdja.ParcelableRingBufferStatus;

/**
 * Native calls for bring up/shut down of the supplicant daemon and for
 * sending requests to the supplicant daemon
 *
 * waitForEvent() is called on the monitor thread for events. All other methods
 * must be serialized from the framework.
 *
 * {@hide}
 */
interface IXdjaWifiNative {
    boolean doLoadDriver();

    boolean doUnloadDriver();

    boolean doIsDriverLoaded();

    boolean doStartSupplicant(boolean p2pSupported);

    boolean doKillSupplicant(boolean p2pSupported);

    boolean doConnectToSupplicant();

    void doCloseSupplicantConnection();

    String doWaitForEvent();

    boolean doBooleanCommand(String command);

    int doIntCommand(String command);

    String doStringCommand(String command);

    boolean doStartHal();
    long doGetWifiHalHandle();
    long[] doGetWifiIfaceHandles();

    void doStopHal();

    void doWaitForHalEvent();

    int doGetInterfaces();

    String doGetInterfaceName(int index);

    boolean doGetScanCapabilities(int iface, out ScanCapabilities capabilities);

    boolean doStartScan(int iface, int id,in ScanSettings settings);

    boolean doStopScan(int iface, int id);

    ParcelableScanData doGetScanResults(int iface, boolean flush);

    ParcelableWifiLinkLayerStats doGetWifiLinkLayerStats(int iface);

    void doSetWifiLinkLayerStats(int iface, int enable);

    boolean doSetHotlist(int iface, int id,in ParcelableHotlistSettings settings);

    boolean doResetHotlist(int iface, int id);

    boolean doTrackSignificantWifiChange(int iface, int id, in ParcelableWifiChangeSettings settings);

    boolean doUntrackSignificantWifiChange(int iface, int id);

    int doGetSupportedFeatureSet(int iface);

    boolean doRequestRange(int iface, int id, in ParcelableRttParams params);

    boolean doCancelRangeRequest(int iface, int id, in ParcelableRttParams params);

    boolean doSetScanningMacOui(int iface, in byte[] oui);

    int[] doGetChannelsForBand(int iface, int band);

    boolean doIsGetChannelsForBandSupported();

    boolean doSetDfsFlag(int iface, boolean dfsOn);

    boolean doToggleInterface(int on);

    ParcelableRttCapabilities doGetRttCapabilities(int iface);

    boolean doSetCountryCodeHal(int iface, String CountryCode);

    boolean doEnableDisableTdls(int iface, boolean enable,String macAddr);

    TdlsStatus doGetTdlsStatus(int iface, String macAddr);

    TdlsCapabilities doGetTdlsCapabilities(int iface);

    boolean doStartLogging(int iface);

    boolean doSetLoggingEventHandler(int iface, int id);

    boolean doStartLoggingRingBuffer(int iface, int verboseLevel,
            int flags, int minIntervalSec ,int minDataSize, String ringName);

    int doGetSupportedLoggerFeatureSet(int iface);

    boolean doResetLogHandler(int iface, int id);

    String doGetDriverVersion(int iface);

    String doGetFirmwareVersion(int iface);

    ParcelableRingBufferStatus doGetRingBufferStatus(int iface);

    boolean doGetRingBufferData(int iface, String ringName);

    boolean doGetFwMemoryDump(int iface);

    boolean doSetPnoList(int iface, int id, in ParcelableWifiPnoNetwork networks);
    boolean doSetLazyRoam(int iface, int id,boolean enabled, in WifiLazyRoamParams param);

    boolean doSetBssidBlacklist(int iface, int id,in ParcelableString list);
    boolean doSetSsidWhitelist(int iface, int id,in ParcelableString list);

    int doStartSendingOffloadedPacket(int iface, int idx,
                                    in byte[] srcMac, in byte[] dstMac, in byte[] pktData, int period);
    int doStopSendingOffloadedPacket(int iface, int idx);

    int doStartRssiMonitoring(int iface, int id,byte maxRssi, byte minRssi);
    int doStopRssiMonitoring(int iface, int idx);
}
