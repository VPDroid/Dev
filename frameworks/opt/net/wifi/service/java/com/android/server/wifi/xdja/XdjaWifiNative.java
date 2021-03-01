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
import android.net.wifi.WifiScanner;
import android.net.wifi.RttManager;
import android.net.wifi.WifiLinkLayerStats;
import android.os.RemoteException;
import android.os.Binder;
import android.util.Log;

/**
 * Native calls for bring up/shut down of the supplicant daemon and for
 * sending requests to the supplicant daemon
 *
 * waitForEvent() is called on the monitor thread for events. All other methods
 * must be serialized from the framework.
 *
 * {@hide}
 */
public class XdjaWifiNative extends IXdjaWifiNative.Stub {

    static {
        /* Native functions are defined in libwifi-service.so */
        System.loadLibrary("wifi-service");
        registerNatives();
    }

    private boolean DBG = false;

    private final String TAG = "XdjaWifiNative";

    private static native int registerNatives();

    private int mloadDriverMode = 0;
    private native static boolean loadDriver();
    @Override
    public synchronized boolean doLoadDriver() throws RemoteException {
        if(DBG)
            Log.d(TAG, "doLoadDriver SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));
        //1
        boolean needload = mloadDriverMode == 0;

        //2
        mloadDriverMode |= Binder.getCallingSystemMode()+1;
        Log.d(TAG, "doLoadDriver SystemMode: " + Binder.getCallingSystemMode() + " mloadDriverMode:" + mloadDriverMode);

        //3
        if(needload)
            return loadDriver();
        else
            return true;
    }

    private native static boolean unloadDriver();
    @Override
    public synchronized boolean doUnloadDriver() throws RemoteException {
        if(DBG)
            Log.d(TAG, "doUnloadDriver SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));
        //1
        mloadDriverMode &= ~(Binder.getCallingSystemMode()+1);
        Log.d(TAG, "doUnloadDriver SystemMode: " + Binder.getCallingSystemMode() + " mloadDriverMode:" + mloadDriverMode);

        //2
        if(mloadDriverMode == 0)
            return unloadDriver();
        else
            return true;
    }

    private native static boolean isDriverLoaded();
    @Override
    public  boolean doIsDriverLoaded() throws RemoteException {
        if(DBG)
            Log.d(TAG, "doIsDriverLoaded SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));
        return isDriverLoaded();
    }

    private int mstartSupplicant = 0;
    private native static boolean startSupplicant(boolean p2pSupported);
    @Override
    public synchronized boolean doStartSupplicant(boolean p2pSupported) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doStartSupplicant SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        //1
        boolean needStart = mstartSupplicant == 0;

        //2
        mstartSupplicant |= (Binder.getCallingSystemMode()+1);
        Log.d(TAG, "doStartSupplicant SystemMode: " + Binder.getCallingSystemMode() + " mstartSupplicant:" + mstartSupplicant);       

        //3
        if(needStart)
            return startSupplicant(p2pSupported);
        else
            return true;
    }

    private native static boolean killSupplicant(boolean p2pSupported);
    @Override
    public synchronized boolean doKillSupplicant(boolean p2pSupported) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doKillSupplicant SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));
        //1
        mstartSupplicant &= ~(Binder.getCallingSystemMode()+1);
        Log.d(TAG, "doKillSupplicant SystemMode: " + Binder.getCallingSystemMode() + " mstartSupplicant:" + mstartSupplicant);

        //2
        if(mstartSupplicant == 0)
            return killSupplicant(p2pSupported);
        else
            return true;
    }

    private int mconnectToSupplicant = 0;
    private native boolean connectToSupplicantNative();
    @Override
    public boolean doConnectToSupplicant() throws RemoteException {
        if(DBG)
            Log.d(TAG, "doConnectToSupplicant SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        //1
        boolean needConnect = mconnectToSupplicant == 0;

        //2
        mconnectToSupplicant |= (Binder.getCallingSystemMode()+1);
        Log.d(TAG, "doConnectToSupplicant SystemMode: " + Binder.getCallingSystemMode() + " mconnectToSupplicant:" + mconnectToSupplicant);       

        //3
        if(needConnect)
            return connectToSupplicantNative();
        else
            return true;
    }

    private native void closeSupplicantConnectionNative();
    @Override
    public void doCloseSupplicantConnection() throws RemoteException {
        if(DBG)
            Log.d(TAG, "doCloseSupplicantConnection SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));
        //1
        mconnectToSupplicant &= ~(Binder.getCallingSystemMode()+1);
        Log.d(TAG, "doCloseSupplicantConnection SystemMode: " + Binder.getCallingSystemMode() + " mconnectToSupplicant:" + mconnectToSupplicant);

        //2
        if(mconnectToSupplicant == 0)
            closeSupplicantConnectionNative();
    }

    /**
     * Wait for the supplicant to send an event, returning the event string.
     * @return the event string sent by the supplicant.
     */
    private native String waitForEventNative();
    @Override
    public String doWaitForEvent() throws RemoteException {
        if(DBG)
            Log.d(TAG, "doWaitForEvent SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return waitForEventNative();
    }

    private native boolean doBooleanCommandNative(String command);
    @Override
    public boolean doBooleanCommand(String command) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doBooleanCommand SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return doBooleanCommandNative(command);
    }

    private native int doIntCommandNative(String command);
    @Override
    public  int doIntCommand(String command) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doIntCommand SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return doIntCommandNative(command);
    }

    private native String doStringCommandNative(String command);
    @Override
    public  String doStringCommand(String command) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doStringCommand SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return doStringCommandNative(command);
    }

    private static long sWifiHalHandle = 0;             /* used by JNI to save wifi_handle */
    private static long[] sWifiIfaceHandles = null;     /* used by JNI to save interface handles */
    private int mstartHal = 0;
    private static native boolean startHalNative();
    @Override
    public synchronized boolean doStartHal() throws RemoteException {
        if(DBG)
            Log.d(TAG, "doStartHal SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));
        //1
        boolean needstart = mstartHal == 0;

        //2
        mstartHal |= (Binder.getCallingSystemMode()+1);
        Log.d(TAG, "doStartHal SystemMode: " + Binder.getCallingSystemMode() + " mstartHal:" + mstartHal);       

        //3
        if(needstart)
            return startHalNative();
        else
            return true;
    }

    private static native void stopHalNative();
    @Override
    public synchronized void doStopHal() throws RemoteException {
        if(DBG)
            Log.d(TAG, "doStopHal SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));
        //1
        mstartHal &= ~(Binder.getCallingSystemMode()+1);
        Log.d(TAG, "doStopHal SystemMode: " + Binder.getCallingSystemMode() + " mstartHal:" + mstartHal);

        //2
        if(mstartHal == 0){
            stopHalNative();
            sWifiHalHandle = 0;
            sWifiIfaceHandles = null;
        }
    }

    @Override
    public  long doGetWifiHalHandle() throws RemoteException {
        if(DBG)
            Log.d(TAG, "doGetWifiHalHandle SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return sWifiHalHandle;
    }

    @Override
    public  long[] doGetWifiIfaceHandles() throws RemoteException {
        if(DBG)
            Log.d(TAG, "doGetWifiIfaceHandles SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return sWifiIfaceHandles;
    }

    private static native void waitForHalEventNative();
    @Override
    public  void doWaitForHalEvent() throws RemoteException {
        if(DBG)
            Log.d(TAG, "doWaitForHalEvent SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        waitForHalEventNative();
    }

    private static native int getInterfacesNative();
    @Override
    public  int doGetInterfaces() throws RemoteException {
        if(DBG)
            Log.d(TAG, "doGetInterfaces SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return getInterfacesNative();
    }

    private static native String getInterfaceNameNative(int index);
    @Override
    public  String doGetInterfaceName(int index) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doGetInterfaceName SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return getInterfaceNameNative(index);
    }

    private static native boolean getScanCapabilitiesNative(int iface, ScanCapabilities capabilities);
    @Override
    public  boolean doGetScanCapabilities(int iface, ScanCapabilities capabilities) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doGetScanCapabilities SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return getScanCapabilitiesNative(iface,capabilities);
    }   

    private static native boolean startScanNative(int iface, int id, ScanSettings settings);
    @Override
    public  boolean doStartScan(int iface, int id, ScanSettings settings) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doStartScan SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return startScanNative(iface,id,settings);
    }   

    private static native boolean stopScanNative(int iface, int id);
    @Override
    public  boolean doStopScan(int iface, int id) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doStopScan SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return stopScanNative(iface,id);
    }

    private static native WifiScanner.ScanData[] getScanResultsNative(int iface, boolean flush);
    @Override
    public  ParcelableScanData doGetScanResults(int iface, boolean flush) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doGetScanResults SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return new ParcelableScanData(getScanResultsNative(iface,flush));
    }

    private static native WifiLinkLayerStats getWifiLinkLayerStatsNative(int iface);
    @Override
    public  ParcelableWifiLinkLayerStats doGetWifiLinkLayerStats(int iface) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doGetWifiLinkLayerStats SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return new ParcelableWifiLinkLayerStats(getWifiLinkLayerStatsNative(iface));
    }

    private static native void setWifiLinkLayerStatsNative(int iface, int enable);
    @Override
    public  void doSetWifiLinkLayerStats(int iface, int enable) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doSetWifiLinkLayerStats SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        setWifiLinkLayerStatsNative(iface,enable);
    }

    private native static boolean setHotlistNative(int iface, int id,WifiScanner.HotlistSettings settings);
    @Override
    public  boolean doSetHotlist(int iface, int id,ParcelableHotlistSettings settings) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doSetHotlist SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return setHotlistNative(iface,id,settings!=null?settings.getResult():null);
    }

    private native static boolean resetHotlistNative(int iface, int id);
    @Override
    public  boolean doResetHotlist(int iface, int id) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doResetHotlist SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return resetHotlistNative(iface,id);
    }

    private static native boolean trackSignificantWifiChangeNative(int iface, int id, WifiScanner.WifiChangeSettings settings);
    @Override
    public  boolean doTrackSignificantWifiChange(int iface, int id, ParcelableWifiChangeSettings settings) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doTrackSignificantWifiChange SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return trackSignificantWifiChangeNative(iface,id,settings!=null?settings.getResult():null);
    }

    private static native boolean untrackSignificantWifiChangeNative(int iface, int id);
    @Override
    public  boolean doUntrackSignificantWifiChange(int iface, int id) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doUntrackSignificantWifiChange SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return untrackSignificantWifiChangeNative(iface,id);
    }

    private static native int getSupportedFeatureSetNative(int iface);
    @Override
    public  int doGetSupportedFeatureSet(int iface) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doGetSupportedFeatureSet SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return getSupportedFeatureSetNative(iface);
    }

    private static native boolean requestRangeNative(int iface, int id, RttManager.RttParams[] params);
    @Override
    public  boolean doRequestRange(int iface, int id, ParcelableRttParams params) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doRequestRange SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return requestRangeNative(iface,id,params!=null?params.getResults():null);
    }

    private static native boolean cancelRangeRequestNative(int iface, int id, RttManager.RttParams[] params);
    @Override
    public  boolean doCancelRangeRequest(int iface, int id, ParcelableRttParams params) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doCancelRangeRequest SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return cancelRangeRequestNative(iface,id,params!=null?params.getResults():null);
    }

    private static native boolean setScanningMacOuiNative(int iface, byte[] oui);
    @Override
    public  boolean doSetScanningMacOui(int iface, byte[] oui) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doSetScanningMacOui SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return setScanningMacOuiNative(iface,oui);
    }

    private static native int[] getChannelsForBandNative(int iface, int band);
    @Override
    public  int[] doGetChannelsForBand(int iface, int band) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doGetChannelsForBand SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return getChannelsForBandNative(iface,band);
    }

    private static native boolean isGetChannelsForBandSupportedNative();
    @Override
    public  boolean doIsGetChannelsForBandSupported() throws RemoteException {
        if(DBG)
            Log.d(TAG, "doIsGetChannelsForBandSupported SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return isGetChannelsForBandSupportedNative();
    }

    private static native boolean setDfsFlagNative(int iface, boolean dfsOn);
    @Override
    public  boolean doSetDfsFlag(int iface, boolean dfsOn) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doSetDfsFlag SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return setDfsFlagNative(iface,dfsOn);
    }

    private static native boolean toggleInterfaceNative(int on);
    @Override
    public  boolean doToggleInterface(int on) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doToggleInterface SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return toggleInterfaceNative(on);
    }

    private static native RttManager.RttCapabilities getRttCapabilitiesNative(int iface);
    @Override
    public  ParcelableRttCapabilities doGetRttCapabilities(int iface) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doGetRttCapabilities SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return new ParcelableRttCapabilities(getRttCapabilitiesNative(iface));
    }

    private static native boolean setCountryCodeHalNative(int iface, String CountryCode);
    @Override
    public  boolean doSetCountryCodeHal(int iface, String CountryCode) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doSetCountryCodeHal SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return setCountryCodeHalNative(iface,CountryCode);
    }

    private static native boolean enableDisableTdlsNative(int iface, boolean enable,String macAddr);
    @Override
    public  boolean doEnableDisableTdls(int iface, boolean enable,String macAddr) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doEnableDisableTdls SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return enableDisableTdlsNative(iface,enable,macAddr);
    }

    private static native TdlsStatus getTdlsStatusNative(int iface, String macAddr);
    @Override
    public  TdlsStatus doGetTdlsStatus(int iface, String macAddr) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doGetTdlsStatus SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return getTdlsStatusNative(iface,macAddr);
    }

    private static native TdlsCapabilities getTdlsCapabilitiesNative(int iface);
    @Override
    public  TdlsCapabilities doGetTdlsCapabilities(int iface) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doGetTdlsCapabilities SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return getTdlsCapabilitiesNative(iface);
    }

    public static native boolean startLogging(int iface);
    @Override
    public  boolean doStartLogging(int iface) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doStartLogging SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return startLogging(iface);
    }

    private static native boolean setLoggingEventHandlerNative(int iface, int id);
    @Override
    public  boolean doSetLoggingEventHandler(int iface, int id) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doSetLoggingEventHandler SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return setLoggingEventHandlerNative(iface,id);
    }

    private static native boolean startLoggingRingBufferNative(int iface, int verboseLevel,
            int flags, int minIntervalSec ,int minDataSize, String ringName);
    @Override
    public  boolean doStartLoggingRingBuffer(int iface, int verboseLevel,
            int flags, int minIntervalSec ,int minDataSize, String ringName) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doStartLoggingRingBuffer SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return startLoggingRingBufferNative(iface,verboseLevel,flags,minIntervalSec,minDataSize,ringName);
    }    

    private static native int getSupportedLoggerFeatureSetNative(int iface);
    @Override
    public  int doGetSupportedLoggerFeatureSet(int iface) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doGetSupportedLoggerFeatureSet SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return getSupportedLoggerFeatureSetNative(iface);
    }

    private static native boolean resetLogHandlerNative(int iface, int id);
    @Override
    public  boolean doResetLogHandler(int iface, int id) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doResetLogHandler SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return resetLogHandlerNative(iface,id);
    }

    private static native String getDriverVersionNative(int iface);
    @Override
    public  String doGetDriverVersion(int iface) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doGetDriverVersion SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return getDriverVersionNative(iface);
    }

    private static native String getFirmwareVersionNative(int iface);
    @Override
    public  String doGetFirmwareVersion(int iface) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doGetFirmwareVersion SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return getFirmwareVersionNative(iface);
    }

    private static native RingBufferStatus[] getRingBufferStatusNative(int iface);
    @Override
    public  ParcelableRingBufferStatus doGetRingBufferStatus(int iface) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doGetRingBufferStatus SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return new ParcelableRingBufferStatus(getRingBufferStatusNative(iface));
    }

    private static native boolean getRingBufferDataNative(int iface, String ringName);
    @Override
    public  boolean doGetRingBufferData(int iface, String ringName) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doGetRingBufferData SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return getRingBufferDataNative(iface,ringName);
    }

    private static native boolean getFwMemoryDumpNative(int iface);
    @Override
    public  boolean doGetFwMemoryDump(int iface) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doGetFwMemoryDump SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return getFwMemoryDumpNative(iface);
    }

    private native static boolean setPnoListNative(int iface, int id, WifiPnoNetwork list[]);
    @Override
    public  boolean doSetPnoList(int iface, int id, ParcelableWifiPnoNetwork networks) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doSetPnoList SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return setPnoListNative(iface,id,networks!=null?networks.getResults():null);
    }

    private native static boolean setLazyRoamNative(int iface, int id,
                                              boolean enabled, WifiLazyRoamParams param);
    @Override
    public  boolean doSetLazyRoam(int iface, int id,
                                              boolean enabled, WifiLazyRoamParams param) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doSetLazyRoam SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return setLazyRoamNative(iface,id,enabled,param);
    }

    private native static boolean setBssidBlacklistNative(int iface, int id,
                                              String list[]);
    @Override
    public  boolean doSetBssidBlacklist(int iface, int id,
                                              ParcelableString list) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doSetBssidBlacklist SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return setBssidBlacklistNative(iface,id,list!=null?list.getResults():null);
    }

    private native static boolean setSsidWhitelistNative(int iface, int id, String list[]);
    @Override
    public  boolean doSetSsidWhitelist(int iface, int id, ParcelableString list) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doSetSsidWhitelist SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return setSsidWhitelistNative(iface,id,list!=null?list.getResults():null);
    }

    private native static int startSendingOffloadedPacketNative(int iface, int idx,
                                    byte[] srcMac, byte[] dstMac, byte[] pktData, int period);
    @Override
    public   int doStartSendingOffloadedPacket(int iface, int idx,
                                    byte[] srcMac, byte[] dstMac, byte[] pktData, int period) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doStartSendingOffloadedPacket SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return startSendingOffloadedPacketNative(iface,idx,srcMac,dstMac,pktData,period);
    }

    private native static int stopSendingOffloadedPacketNative(int iface, int idx);
    @Override
    public   int doStopSendingOffloadedPacket(int iface, int idx) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doStopSendingOffloadedPacket SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return stopSendingOffloadedPacketNative(iface,idx);
    }

    private native static int startRssiMonitoringNative(int iface, int id,
                                        byte maxRssi, byte minRssi);
    @Override
    public   int doStartRssiMonitoring(int iface, int id,
                                        byte maxRssi, byte minRssi) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doStartRssiMonitoring SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return startRssiMonitoringNative(iface,id,maxRssi,minRssi);
    }

    private native static int stopRssiMonitoringNative(int iface, int idx);
    @Override
    public   int doStopRssiMonitoring(int iface, int idx) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doStopRssiMonitoring SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return stopRssiMonitoringNative(iface,idx);
    }
}
