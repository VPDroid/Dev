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

import android.Manifest;
import android.app.ActivityManager;
import android.app.AppOpsManager;
import android.bluetooth.BluetoothAdapter;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.content.pm.UserInfo;
import android.database.ContentObserver;
import android.net.ConnectivityManager;
import android.net.DhcpInfo;
import android.net.DhcpResults;
import android.net.Network;
import android.net.NetworkScorerAppManager;
import android.net.NetworkUtils;
import android.net.Uri;
import android.net.wifi.BatchedScanResult;
import android.net.wifi.BatchedScanSettings;
import android.net.wifi.IWifiManager;
import android.net.wifi.ScanResult;
import android.net.wifi.ScanSettings;
import android.net.wifi.WifiActivityEnergyInfo;
import android.net.wifi.WifiChannel;
import android.net.wifi.WifiConfiguration;
import android.net.wifi.WifiConnectionStatistics;
import android.net.wifi.WifiEnterpriseConfig;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiLinkLayerStats;
import android.net.wifi.WifiManager;
import android.os.AsyncTask;
import android.os.Binder;
import android.os.Build;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.Message;
import android.os.Messenger;
import android.os.PowerManager;
import android.os.RemoteException;
import android.os.SystemClock;
import android.os.SystemProperties;
import android.os.UserHandle;
import android.os.UserManager;
import android.os.WorkSource;
import android.provider.Settings;
import android.text.TextUtils;
import android.util.Log;
import android.util.Slog;

import com.android.internal.R;
import com.android.internal.app.IBatteryStats;
import com.android.internal.telephony.IccCardConstants;
import com.android.internal.telephony.PhoneConstants;
import com.android.internal.telephony.TelephonyIntents;
import com.android.internal.util.AsyncChannel;
import com.android.server.am.BatteryStatsService;
import com.android.server.wifi.configparse.ConfigBuilder;

import org.xml.sax.SAXException;

import java.io.BufferedReader;
import java.io.FileDescriptor;
import java.io.FileNotFoundException;
import java.io.FileReader;
import java.io.IOException;
import java.io.PrintWriter;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.security.GeneralSecurityException;
import java.security.KeyStore;
import java.security.cert.CertPath;
import java.security.cert.CertPathValidator;
import java.security.cert.CertPathValidatorException;
import java.security.cert.CertificateFactory;
import java.security.cert.PKIXParameters;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

import static com.android.server.wifi.WifiController.CMD_AIRPLANE_TOGGLED;
import static com.android.server.wifi.WifiController.CMD_BATTERY_CHANGED;
import static com.android.server.wifi.WifiController.CMD_EMERGENCY_CALL_STATE_CHANGED;
import static com.android.server.wifi.WifiController.CMD_EMERGENCY_MODE_CHANGED;
import static com.android.server.wifi.WifiController.CMD_LOCKS_CHANGED;
import static com.android.server.wifi.WifiController.CMD_SCAN_ALWAYS_MODE_CHANGED;
import static com.android.server.wifi.WifiController.CMD_SCREEN_OFF;
import static com.android.server.wifi.WifiController.CMD_SCREEN_ON;
import static com.android.server.wifi.WifiController.CMD_SET_AP;
import static com.android.server.wifi.WifiController.CMD_USER_PRESENT;
import static com.android.server.wifi.WifiController.CMD_WIFI_TOGGLED;
/**
 * WifiService handles remote WiFi operation requests by implementing
 * the IWifiManager interface.
 *
 * @hide
 */
public final class WifiServiceImpl extends IWifiManager.Stub {
    private static final String TAG = "WifiService";
    private static final boolean DBG = true;
    private static final boolean VDBG = false;

    final WifiStateMachine mWifiStateMachine;

    private final Context mContext;

    final LockList mLocks = new LockList();
    // some wifi lock statistics
    private int mFullHighPerfLocksAcquired;
    private int mFullHighPerfLocksReleased;
    private int mFullLocksAcquired;
    private int mFullLocksReleased;
    private int mScanLocksAcquired;
    private int mScanLocksReleased;

    private final List<Multicaster> mMulticasters =
            new ArrayList<Multicaster>();
    private int mMulticastEnabled;
    private int mMulticastDisabled;

    private final IBatteryStats mBatteryStats;
    private final PowerManager mPowerManager;
    private final AppOpsManager mAppOps;
    private final UserManager mUserManager;

    private String mInterfaceName;

    // Debug counter tracking scan requests sent by WifiManager
    private int scanRequestCounter = 0;

    /* Tracks the open wi-fi network notification */
    private WifiNotificationController mNotificationController;
    /* Polls traffic stats and notifies clients */
    private WifiTrafficPoller mTrafficPoller;
    /* Tracks the persisted states for wi-fi & airplane mode */
    final WifiSettingsStore mSettingsStore;

    /**
     * Asynchronous channel to WifiStateMachine
     */
    private AsyncChannel mWifiStateMachineChannel;

    /**
     * Handles client connections
     */
    private class ClientHandler extends Handler {

        ClientHandler(android.os.Looper looper) {
            super(looper);
        }

        @Override
        public void handleMessage(Message msg) {
            switch (msg.what) {
                case AsyncChannel.CMD_CHANNEL_HALF_CONNECTED: {
                    if (msg.arg1 == AsyncChannel.STATUS_SUCCESSFUL) {
                        if (DBG) Slog.d(TAG, "New client listening to asynchronous messages");
                        // We track the clients by the Messenger
                        // since it is expected to be always available
                        mTrafficPoller.addClient(msg.replyTo);
                    } else {
                        Slog.e(TAG, "Client connection failure, error=" + msg.arg1);
                    }
                    break;
                }
                case AsyncChannel.CMD_CHANNEL_DISCONNECTED: {
                    if (msg.arg1 == AsyncChannel.STATUS_SEND_UNSUCCESSFUL) {
                        if (DBG) Slog.d(TAG, "Send failed, client connection lost");
                    } else {
                        if (DBG) Slog.d(TAG, "Client connection lost with reason: " + msg.arg1);
                    }
                    mTrafficPoller.removeClient(msg.replyTo);
                    break;
                }
                case AsyncChannel.CMD_CHANNEL_FULL_CONNECTION: {
                    AsyncChannel ac = new AsyncChannel();
                    ac.connect(mContext, this, msg.replyTo);
                    break;
                }
                /* Client commands are forwarded to state machine */
                case WifiManager.CONNECT_NETWORK:
                case WifiManager.SAVE_NETWORK: {
                    WifiConfiguration config = (WifiConfiguration) msg.obj;
                    int networkId = msg.arg1;
                    if (msg.what == WifiManager.SAVE_NETWORK) {
                        Slog.e("WiFiServiceImpl ", "SAVE"
                                + " nid=" + Integer.toString(networkId)
                                + " uid=" + msg.sendingUid
                                + " name="
                                + mContext.getPackageManager().getNameForUid(msg.sendingUid));
                    }
                    if (msg.what == WifiManager.CONNECT_NETWORK) {
                        Slog.e("WiFiServiceImpl ", "CONNECT "
                                + " nid=" + Integer.toString(networkId)
                                + " uid=" + msg.sendingUid
                                + " name="
                                + mContext.getPackageManager().getNameForUid(msg.sendingUid));
                    }

                    if (config != null && isValid(config)) {
                        if (DBG) Slog.d(TAG, "Connect with config" + config);
                        mWifiStateMachine.sendMessage(Message.obtain(msg));
                    } else if (config == null
                            && networkId != WifiConfiguration.INVALID_NETWORK_ID) {
                        if (DBG) Slog.d(TAG, "Connect with networkId" + networkId);
                        mWifiStateMachine.sendMessage(Message.obtain(msg));
                    } else {
                        Slog.e(TAG, "ClientHandler.handleMessage ignoring invalid msg=" + msg);
                        if (msg.what == WifiManager.CONNECT_NETWORK) {
                            replyFailed(msg, WifiManager.CONNECT_NETWORK_FAILED,
                                    WifiManager.INVALID_ARGS);
                        } else {
                            replyFailed(msg, WifiManager.SAVE_NETWORK_FAILED,
                                    WifiManager.INVALID_ARGS);
                        }
                    }
                    break;
                }
                case WifiManager.FORGET_NETWORK:
                    if (isOwner(msg.sendingUid)) {
                        mWifiStateMachine.sendMessage(Message.obtain(msg));
                    } else {
                        Slog.e(TAG, "Forget is not authorized for user");
                        replyFailed(msg, WifiManager.FORGET_NETWORK_FAILED,
                                WifiManager.NOT_AUTHORIZED);
                    }
                    break;
                case WifiManager.START_WPS:
                case WifiManager.CANCEL_WPS:
                case WifiManager.DISABLE_NETWORK:
                case WifiManager.RSSI_PKTCNT_FETCH: {
                    mWifiStateMachine.sendMessage(Message.obtain(msg));
                    break;
                }
                default: {
                    Slog.d(TAG, "ClientHandler.handleMessage ignoring msg=" + msg);
                    break;
                }
            }
        }

        private void replyFailed(Message msg, int what, int why) {
            Message reply = msg.obtain();
            reply.what = what;
            reply.arg1 = why;
            try {
                msg.replyTo.send(reply);
            } catch (RemoteException e) {
                // There's not much we can do if reply can't be sent!
            }
        }
    }
    private ClientHandler mClientHandler;

    /**
     * Handles interaction with WifiStateMachine
     */
    private class WifiStateMachineHandler extends Handler {
        private AsyncChannel mWsmChannel;

        WifiStateMachineHandler(android.os.Looper looper) {
            super(looper);
            mWsmChannel = new AsyncChannel();
            mWsmChannel.connect(mContext, this, mWifiStateMachine.getHandler());
        }

        @Override
        public void handleMessage(Message msg) {
            switch (msg.what) {
                case AsyncChannel.CMD_CHANNEL_HALF_CONNECTED: {
                    if (msg.arg1 == AsyncChannel.STATUS_SUCCESSFUL) {
                        mWifiStateMachineChannel = mWsmChannel;
                    } else {
                        Slog.e(TAG, "WifiStateMachine connection failure, error=" + msg.arg1);
                        mWifiStateMachineChannel = null;
                    }
                    break;
                }
                case AsyncChannel.CMD_CHANNEL_DISCONNECTED: {
                    Slog.e(TAG, "WifiStateMachine channel lost, msg.arg1 =" + msg.arg1);
                    mWifiStateMachineChannel = null;
                    //Re-establish connection to state machine
                    mWsmChannel.connect(mContext, this, mWifiStateMachine.getHandler());
                    break;
                }
                default: {
                    Slog.d(TAG, "WifiStateMachineHandler.handleMessage ignoring msg=" + msg);
                    break;
                }
            }
        }
    }

    WifiStateMachineHandler mWifiStateMachineHandler;

    private WifiWatchdogStateMachine mWifiWatchdogStateMachine;

    private WifiController mWifiController;

    public WifiServiceImpl(Context context) {
        mContext = context;

        mInterfaceName =  SystemProperties.get("wifi.interface", "wlan0");

        mTrafficPoller = new WifiTrafficPoller(mContext, mInterfaceName);
        mWifiStateMachine = new WifiStateMachine(mContext, mInterfaceName, mTrafficPoller);
        mWifiStateMachine.enableRssiPolling(true);
        mBatteryStats = BatteryStatsService.getService();
        mPowerManager = context.getSystemService(PowerManager.class);
        mAppOps = (AppOpsManager)context.getSystemService(Context.APP_OPS_SERVICE);
        mUserManager = UserManager.get(mContext);

        mNotificationController = new WifiNotificationController(mContext, mWifiStateMachine);
        mSettingsStore = new WifiSettingsStore(mContext);

        HandlerThread wifiThread = new HandlerThread("WifiService");
        wifiThread.start();
        mClientHandler = new ClientHandler(wifiThread.getLooper());
        mWifiStateMachineHandler = new WifiStateMachineHandler(wifiThread.getLooper());
        mWifiController = new WifiController(mContext, this, wifiThread.getLooper());
    }


    /**
     * Check if Wi-Fi needs to be enabled and start
     * if needed
     *
     * This function is used only at boot time
     */
    public void checkAndStartWifi() {
        /* Check if wi-fi needs to be enabled */
        boolean wifiEnabled = mSettingsStore.isWifiToggleEnabled();
        Slog.i(TAG, "WifiService starting up with Wi-Fi " +
                (wifiEnabled ? "enabled" : "disabled"));

        registerForScanModeChange();
        mContext.registerReceiver(
                new BroadcastReceiver() {
                    @Override
                    public void onReceive(Context context, Intent intent) {
                        if (mSettingsStore.handleAirplaneModeToggled()) {
                            mWifiController.sendMessage(CMD_AIRPLANE_TOGGLED);
                        }
                        if (mSettingsStore.isAirplaneModeOn()) {
                            Log.d(TAG, "resetting country code because Airplane mode is ON");
                            mWifiStateMachine.resetCountryCode();
                        }
                    }
                },
                new IntentFilter(Intent.ACTION_AIRPLANE_MODE_CHANGED));

        mContext.registerReceiver(
                new BroadcastReceiver() {
                    @Override
                    public void onReceive(Context context, Intent intent) {
                        String state = intent.getStringExtra(IccCardConstants.INTENT_KEY_ICC_STATE);
                        if (state.equals(IccCardConstants.INTENT_VALUE_ICC_ABSENT)) {
                            Log.d(TAG, "resetting networks because SIM was removed");
                            mWifiStateMachine.resetSimAuthNetworks();
                            Log.d(TAG, "resetting country code because SIM is removed");
                            mWifiStateMachine.resetCountryCode();
                        }
                    }
                },
                new IntentFilter(TelephonyIntents.ACTION_SIM_STATE_CHANGED));

        // Adding optimizations of only receiving broadcasts when wifi is enabled
        // can result in race conditions when apps toggle wifi in the background
        // without active user involvement. Always receive broadcasts.
        registerForBroadcasts();
        registerForPackageOrUserRemoval();
        mInIdleMode = mPowerManager.isDeviceIdleMode();

        mWifiController.start();

        // If we are already disabled (could be due to airplane mode), avoid changing persist
        // state here
        if (wifiEnabled) setWifiEnabled(wifiEnabled);

        mWifiWatchdogStateMachine = WifiWatchdogStateMachine.
               makeWifiWatchdogStateMachine(mContext, mWifiStateMachine.getMessenger());
    }

    /**
     * see {@link android.net.wifi.WifiManager#pingSupplicant()}
     * @return {@code true} if the operation succeeds, {@code false} otherwise
     */
    public boolean pingSupplicant() {
        enforceAccessPermission();
        if (mWifiStateMachineChannel != null) {
            return mWifiStateMachine.syncPingSupplicant(mWifiStateMachineChannel);
        } else {
            Slog.e(TAG, "mWifiStateMachineChannel is not initialized");
            return false;
        }
    }

    /**
     * see {@link android.net.wifi.WifiManager#getChannelList}
     */
    public List<WifiChannel> getChannelList() {
        enforceAccessPermission();
        if (mWifiStateMachineChannel != null) {
            return mWifiStateMachine.syncGetChannelList(mWifiStateMachineChannel);
        } else {
            Slog.e(TAG, "mWifiStateMachineChannel is not initialized");
            return null;
        }
    }

    // Start a location scan.
    // L release: A location scan is implemented as a normal scan and avoids scanning DFS channels
    // Deprecated: Will soon remove implementation
    public void startLocationRestrictedScan(WorkSource workSource) {
        enforceChangePermission();
        enforceLocationHardwarePermission();
        List<WifiChannel> channels = getChannelList();
        if (channels == null) {
            Slog.e(TAG, "startLocationRestrictedScan cant get channels");
            return;
        }
        ScanSettings settings = new ScanSettings();
        for (WifiChannel channel : channels) {
            if (!channel.isDFS) {
                settings.channelSet.add(channel);
            }
        }
        if (workSource == null) {
            // Make sure we always have a workSource indicating the origin of the scan
            // hence if there is none, pick an internal WifiStateMachine one
            workSource = new WorkSource(WifiStateMachine.DFS_RESTRICTED_SCAN_REQUEST);
        }
        startScan(settings, workSource);
    }

    /**
     * see {@link android.net.wifi.WifiManager#startScan}
     * and {@link android.net.wifi.WifiManager#startCustomizedScan}
     *
     * @param settings If null, use default parameter, i.e. full scan.
     * @param workSource If null, all blame is given to the calling uid.
     */
    public void startScan(ScanSettings settings, WorkSource workSource) {
        enforceChangePermission();
        synchronized (this) {
            if (mInIdleMode) {
                // Need to send an immediate scan result broadcast in case the
                // caller is waiting for a result ..

                // clear calling identity to send broadcast
                long callingIdentity = Binder.clearCallingIdentity();
                try {
                    mWifiStateMachine.sendScanResultsAvailableBroadcast(/* scanSucceeded = */ false);
                } finally {
                    // restore calling identity
                    Binder.restoreCallingIdentity(callingIdentity);
                }
                mScanPending = true;
                return;
            }
        }
        if (settings != null) {
            settings = new ScanSettings(settings);
            if (!settings.isValid()) {
                Slog.e(TAG, "invalid scan setting");
                return;
            }
        }
        if (workSource != null) {
            enforceWorkSourcePermission();
            // WifiManager currently doesn't use names, so need to clear names out of the
            // supplied WorkSource to allow future WorkSource combining.
            workSource.clearNames();
        }
        mWifiStateMachine.startScan(Binder.getCallingUid(), scanRequestCounter++,
                settings, workSource);
    }

    public boolean isBatchedScanSupported() {
        return false;
    }

    public void pollBatchedScan() { }

    public String getWpsNfcConfigurationToken(int netId) {
        enforceConnectivityInternalPermission();
        return mWifiStateMachine.syncGetWpsNfcConfigurationToken(netId);
    }

    /**
     * see {@link android.net.wifi.WifiManager#requestBatchedScan()}
     */
    public boolean requestBatchedScan(BatchedScanSettings requested, IBinder binder,
            WorkSource workSource) {
        return false;
    }

    public List<BatchedScanResult> getBatchedScanResults(String callingPackage) {
        return null;
    }

    public void stopBatchedScan(BatchedScanSettings settings) { }

    boolean mInIdleMode;
    boolean mScanPending;

    void handleIdleModeChanged() {
        boolean doScan = false;
        synchronized (this) {
            boolean idle = mPowerManager.isDeviceIdleMode();
            if (mInIdleMode != idle) {
                mInIdleMode = idle;
                if (!idle) {
                    if (mScanPending) {
                        mScanPending = false;
                        doScan = true;
                    }
                }
            }
        }
        if (doScan) {
            // Someone requested a scan while we were idle; do a full scan now.
            startScan(null, null);
        }
    }

    private void enforceAccessPermission() {
        //mContext.enforceCallingOrSelfPermission(android.Manifest.permission.ACCESS_WIFI_STATE,
        //        "WifiService");
    }

    private void enforceChangePermission() {
        //mContext.enforceCallingOrSelfPermission(android.Manifest.permission.CHANGE_WIFI_STATE,
        //        "WifiService");
    }

    private void enforceLocationHardwarePermission() {
        //mContext.enforceCallingOrSelfPermission(Manifest.permission.LOCATION_HARDWARE,
        //        "LocationHardware");
    }

    private void enforceReadCredentialPermission() {
        //mContext.enforceCallingOrSelfPermission(android.Manifest.permission.READ_WIFI_CREDENTIAL,
        //                                        "WifiService");
    }

    private void enforceWorkSourcePermission() {
        //mContext.enforceCallingPermission(android.Manifest.permission.UPDATE_DEVICE_STATS,
        //        "WifiService");

    }

    private void enforceMulticastChangePermission() {
        //mContext.enforceCallingOrSelfPermission(
        //        android.Manifest.permission.CHANGE_WIFI_MULTICAST_STATE,
        //        "WifiService");
    }

    private void enforceConnectivityInternalPermission() {
        //mContext.enforceCallingOrSelfPermission(
        //        android.Manifest.permission.CONNECTIVITY_INTERNAL,
        //        "ConnectivityService");
    }

    /**
     * see {@link android.net.wifi.WifiManager#setWifiEnabled(boolean)}
     * @param enable {@code true} to enable, {@code false} to disable.
     * @return {@code true} if the enable/disable operation was
     *         started or is already in the queue.
     */
    public synchronized boolean setWifiEnabled(boolean enable) {
        enforceChangePermission();
        Slog.d(TAG, "setWifiEnabled: " + enable + " pid=" + Binder.getCallingPid()
                    + ", uid=" + Binder.getCallingUid());
        if (DBG) {
            Slog.e(TAG, "Invoking mWifiStateMachine.setWifiEnabled\n");
        }

        /*
        * Caller might not have WRITE_SECURE_SETTINGS,
        * only CHANGE_WIFI_STATE is enforced
        */

        long ident = Binder.clearCallingIdentity();
        try {
            if (! mSettingsStore.handleWifiToggled(enable)) {
                // Nothing to do if wifi cannot be toggled
                return true;
            }
        } finally {
            Binder.restoreCallingIdentity(ident);
        }

        mWifiController.sendMessage(CMD_WIFI_TOGGLED);
        return true;
    }

    /**
     * see {@link WifiManager#getWifiState()}
     * @return One of {@link WifiManager#WIFI_STATE_DISABLED},
     *         {@link WifiManager#WIFI_STATE_DISABLING},
     *         {@link WifiManager#WIFI_STATE_ENABLED},
     *         {@link WifiManager#WIFI_STATE_ENABLING},
     *         {@link WifiManager#WIFI_STATE_UNKNOWN}
     */
    public int getWifiEnabledState() {
        enforceAccessPermission();
        return mWifiStateMachine.syncGetWifiState();
    }

    /**
     * see {@link android.net.wifi.WifiManager#setWifiApEnabled(WifiConfiguration, boolean)}
     * @param wifiConfig SSID, security and channel details as
     *        part of WifiConfiguration
     * @param enabled true to enable and false to disable
     */
    public void setWifiApEnabled(WifiConfiguration wifiConfig, boolean enabled) {
        enforceChangePermission();
        ConnectivityManager.enforceTetherChangePermission(mContext);
        if (mUserManager.hasUserRestriction(UserManager.DISALLOW_CONFIG_TETHERING)) {
            throw new SecurityException("DISALLOW_CONFIG_TETHERING is enabled for this user.");
        }
        // null wifiConfig is a meaningful input for CMD_SET_AP
        if (wifiConfig == null || isValid(wifiConfig)) {
            mWifiController.obtainMessage(CMD_SET_AP, enabled ? 1 : 0, 0, wifiConfig).sendToTarget();
        } else {
            Slog.e(TAG, "Invalid WifiConfiguration");
        }
    }

    /**
     * see {@link WifiManager#getWifiApState()}
     * @return One of {@link WifiManager#WIFI_AP_STATE_DISABLED},
     *         {@link WifiManager#WIFI_AP_STATE_DISABLING},
     *         {@link WifiManager#WIFI_AP_STATE_ENABLED},
     *         {@link WifiManager#WIFI_AP_STATE_ENABLING},
     *         {@link WifiManager#WIFI_AP_STATE_FAILED}
     */
    public int getWifiApEnabledState() {
        enforceAccessPermission();
        return mWifiStateMachine.syncGetWifiApState();
    }

    /**
     * see {@link WifiManager#getWifiApConfiguration()}
     * @return soft access point configuration
     */
    public WifiConfiguration getWifiApConfiguration() {
        enforceAccessPermission();
        return mWifiStateMachine.syncGetWifiApConfiguration();
    }

    /**
     * see {@link WifiManager#buildWifiConfig()}
     * @return a WifiConfiguration.
     */
    public WifiConfiguration buildWifiConfig(String uriString, String mimeType, byte[] data) {
        if (mimeType.equals(ConfigBuilder.WifiConfigType)) {
            try {
                return ConfigBuilder.buildConfig(uriString, data, mContext);
            }
            catch (IOException | GeneralSecurityException | SAXException e) {
                Log.e(TAG, "Failed to parse wi-fi configuration: " + e);
            }
        }
        else {
            Log.i(TAG, "Unknown wi-fi config type: " + mimeType);
        }
        return null;
    }

    /**
     * see {@link WifiManager#setWifiApConfiguration(WifiConfiguration)}
     * @param wifiConfig WifiConfiguration details for soft access point
     */
    public void setWifiApConfiguration(WifiConfiguration wifiConfig) {
        enforceChangePermission();
        if (wifiConfig == null)
            return;
        if (isValid(wifiConfig)) {
            mWifiStateMachine.setWifiApConfiguration(wifiConfig);
        } else {
            Slog.e(TAG, "Invalid WifiConfiguration");
        }
    }

    /**
     * @param enable {@code true} to enable, {@code false} to disable.
     * @return {@code true} if the enable/disable operation was
     *         started or is already in the queue.
     */
    public boolean isScanAlwaysAvailable() {
        enforceAccessPermission();
        return mSettingsStore.isScanAlwaysAvailable();
    }

    /**
     * see {@link android.net.wifi.WifiManager#disconnect()}
     */
    public void disconnect() {
        enforceChangePermission();
        mWifiStateMachine.disconnectCommand();
    }

    /**
     * see {@link android.net.wifi.WifiManager#reconnect()}
     */
    public void reconnect() {
        enforceChangePermission();
        mWifiStateMachine.reconnectCommand();
    }

    /**
     * see {@link android.net.wifi.WifiManager#reassociate()}
     */
    public void reassociate() {
        enforceChangePermission();
        mWifiStateMachine.reassociateCommand();
    }

    /**
     * see {@link android.net.wifi.WifiManager#getSupportedFeatures}
     */
    public int getSupportedFeatures() {
        enforceAccessPermission();
        if (mWifiStateMachineChannel != null) {
            return mWifiStateMachine.syncGetSupportedFeatures(mWifiStateMachineChannel);
        } else {
            Slog.e(TAG, "mWifiStateMachineChannel is not initialized");
            return 0;
        }
    }

    /**
     * see {@link android.net.wifi.WifiManager#getControllerActivityEnergyInfo(int)}
     */
    public WifiActivityEnergyInfo reportActivityInfo() {
        enforceAccessPermission();
        WifiLinkLayerStats stats;
        WifiActivityEnergyInfo energyInfo = null;
        if (mWifiStateMachineChannel != null) {
            stats = mWifiStateMachine.syncGetLinkLayerStats(mWifiStateMachineChannel);
            if (stats != null) {
                final long rxIdleCurrent = mContext.getResources().getInteger(
                        com.android.internal.R.integer.config_wifi_idle_receive_cur_ma);
                final long rxCurrent = mContext.getResources().getInteger(
                        com.android.internal.R.integer.config_wifi_active_rx_cur_ma);
                final long txCurrent = mContext.getResources().getInteger(
                        com.android.internal.R.integer.config_wifi_tx_cur_ma);
                final double voltage = mContext.getResources().getInteger(
                        com.android.internal.R.integer.config_wifi_operating_voltage_mv)
                        / 1000.0;

                final long rxIdleTime = stats.on_time - stats.tx_time - stats.rx_time;
                final long energyUsed = (long)((stats.tx_time * txCurrent +
                        stats.rx_time * rxCurrent +
                        rxIdleTime * rxIdleCurrent) * voltage);
                if (VDBG || rxIdleTime < 0 || stats.on_time < 0 || stats.tx_time < 0 ||
                        stats.rx_time < 0 || energyUsed < 0) {
                    StringBuilder sb = new StringBuilder();
                    sb.append(" rxIdleCur=" + rxIdleCurrent);
                    sb.append(" rxCur=" + rxCurrent);
                    sb.append(" txCur=" + txCurrent);
                    sb.append(" voltage=" + voltage);
                    sb.append(" on_time=" + stats.on_time);
                    sb.append(" tx_time=" + stats.tx_time);
                    sb.append(" rx_time=" + stats.rx_time);
                    sb.append(" rxIdleTime=" + rxIdleTime);
                    sb.append(" energy=" + energyUsed);
                    Log.e(TAG, " reportActivityInfo: " + sb.toString());
                }

                // Convert the LinkLayerStats into EnergyActivity
                energyInfo = new WifiActivityEnergyInfo(SystemClock.elapsedRealtime(),
                        WifiActivityEnergyInfo.STACK_STATE_STATE_IDLE, stats.tx_time,
                        stats.rx_time, rxIdleTime, energyUsed);
            }
            return energyInfo;
        } else {
            Slog.e(TAG, "mWifiStateMachineChannel is not initialized");
            return null;
        }
    }

    /**
     * see {@link android.net.wifi.WifiManager#getConfiguredNetworks()}
     * @return the list of configured networks
     */
    public List<WifiConfiguration> getConfiguredNetworks() {
        enforceAccessPermission();
        if (mWifiStateMachineChannel != null) {
            return mWifiStateMachine.syncGetConfiguredNetworks(Binder.getCallingUid(),
                    mWifiStateMachineChannel);
        } else {
            Slog.e(TAG, "mWifiStateMachineChannel is not initialized");
            return null;
        }
    }

    /**
     * see {@link android.net.wifi.WifiManager#getPrivilegedConfiguredNetworks()}
     * @return the list of configured networks with real preSharedKey
     */
    public List<WifiConfiguration> getPrivilegedConfiguredNetworks() {
        enforceReadCredentialPermission();
        enforceAccessPermission();
        if (mWifiStateMachineChannel != null) {
            return mWifiStateMachine.syncGetPrivilegedConfiguredNetwork(mWifiStateMachineChannel);
        } else {
            Slog.e(TAG, "mWifiStateMachineChannel is not initialized");
            return null;
        }
    }

    /**
     * Returns a WifiConfiguration matching this ScanResult
     * @param scanResult scanResult that represents the BSSID
     * @return {@link WifiConfiguration} that matches this BSSID or null
     */
    public WifiConfiguration getMatchingWifiConfig(ScanResult scanResult) {
        enforceAccessPermission();
        return mWifiStateMachine.syncGetMatchingWifiConfig(scanResult, mWifiStateMachineChannel);
    }


    /**
     * see {@link android.net.wifi.WifiManager#addOrUpdateNetwork(WifiConfiguration)}
     * @return the supplicant-assigned identifier for the new or updated
     * network if the operation succeeds, or {@code -1} if it fails
     */
    public int addOrUpdateNetwork(WifiConfiguration config) {
        enforceChangePermission();
        if (isValid(config) && isValidPasspoint(config)) {

            WifiEnterpriseConfig enterpriseConfig = config.enterpriseConfig;

            if (config.isPasspoint() &&
                    (enterpriseConfig.getEapMethod() == WifiEnterpriseConfig.Eap.TLS ||
                enterpriseConfig.getEapMethod() == WifiEnterpriseConfig.Eap.TTLS)) {
                try {
                    verifyCert(enterpriseConfig.getCaCertificate());
                } catch (CertPathValidatorException cpve) {
                    Slog.e(TAG, "CA Cert " +
                            enterpriseConfig.getCaCertificate().getSubjectX500Principal() +
                            " untrusted: " + cpve.getMessage());
                    return -1;
                } catch (GeneralSecurityException | IOException e) {
                    Slog.e(TAG, "Failed to verify certificate" +
                            enterpriseConfig.getCaCertificate().getSubjectX500Principal() +
                            ": " + e);
                    return -1;
                }
            }

            //TODO: pass the Uid the WifiStateMachine as a message parameter
            Slog.i("addOrUpdateNetwork", " uid = " + Integer.toString(Binder.getCallingUid())
                    + " SSID " + config.SSID
                    + " nid=" + Integer.toString(config.networkId));
            if (config.networkId == WifiConfiguration.INVALID_NETWORK_ID) {
                config.creatorUid = Binder.getCallingUid();
            } else {
                config.lastUpdateUid = Binder.getCallingUid();
            }
            if (mWifiStateMachineChannel != null) {
                return mWifiStateMachine.syncAddOrUpdateNetwork(mWifiStateMachineChannel, config);
            } else {
                Slog.e(TAG, "mWifiStateMachineChannel is not initialized");
                return -1;
            }
        } else {
            Slog.e(TAG, "bad network configuration");
            return -1;
        }
    }

    public static void verifyCert(X509Certificate caCert)
            throws GeneralSecurityException, IOException {
        CertificateFactory factory = CertificateFactory.getInstance("X.509");
        CertPathValidator validator =
                CertPathValidator.getInstance(CertPathValidator.getDefaultType());
        CertPath path = factory.generateCertPath(
                Arrays.asList(caCert));
        KeyStore ks = KeyStore.getInstance("AndroidCAStore");
        ks.load(null, null);
        PKIXParameters params = new PKIXParameters(ks);
        params.setRevocationEnabled(false);
        validator.validate(path, params);
    }

    /**
     * See {@link android.net.wifi.WifiManager#removeNetwork(int)}
     * @param netId the integer that identifies the network configuration
     * to the supplicant
     * @return {@code true} if the operation succeeded
     */
    public boolean removeNetwork(int netId) {
        enforceChangePermission();

        if (!isOwner(Binder.getCallingUid())) {
            Slog.e(TAG, "Remove is not authorized for user");
            return false;
        }

        if (mWifiStateMachineChannel != null) {
            return mWifiStateMachine.syncRemoveNetwork(mWifiStateMachineChannel, netId);
        } else {
            Slog.e(TAG, "mWifiStateMachineChannel is not initialized");
            return false;
        }
    }

    /**
     * See {@link android.net.wifi.WifiManager#enableNetwork(int, boolean)}
     * @param netId the integer that identifies the network configuration
     * to the supplicant
     * @param disableOthers if true, disable all other networks.
     * @return {@code true} if the operation succeeded
     */
    public boolean enableNetwork(int netId, boolean disableOthers) {
        enforceChangePermission();
        if (mWifiStateMachineChannel != null) {
            return mWifiStateMachine.syncEnableNetwork(mWifiStateMachineChannel, netId,
                    disableOthers);
        } else {
            Slog.e(TAG, "mWifiStateMachineChannel is not initialized");
            return false;
        }
    }

    /**
     * See {@link android.net.wifi.WifiManager#disableNetwork(int)}
     * @param netId the integer that identifies the network configuration
     * to the supplicant
     * @return {@code true} if the operation succeeded
     */
    public boolean disableNetwork(int netId) {
        enforceChangePermission();
        if (mWifiStateMachineChannel != null) {
            return mWifiStateMachine.syncDisableNetwork(mWifiStateMachineChannel, netId);
        } else {
            Slog.e(TAG, "mWifiStateMachineChannel is not initialized");
            return false;
        }
    }

    /**
     * See {@link android.net.wifi.WifiManager#getConnectionInfo()}
     * @return the Wi-Fi information, contained in {@link WifiInfo}.
     */
    public WifiInfo getConnectionInfo() {
        enforceAccessPermission();
        /*
         * Make sure we have the latest information, by sending
         * a status request to the supplicant.
         */
        return mWifiStateMachine.syncRequestConnectionInfo();
    }

    /**
     * Return the results of the most recent access point scan, in the form of
     * a list of {@link ScanResult} objects.
     * @return the list of results
     */
    public List<ScanResult> getScanResults(String callingPackage) {
        enforceAccessPermission();
        int userId = UserHandle.getCallingUserId();
        int uid = Binder.getCallingUid();
        boolean canReadPeerMacAddresses = checkPeersMacAddress();
        boolean isActiveNetworkScorer =
                NetworkScorerAppManager.isCallerActiveScorer(mContext, uid);
        boolean hasInteractUsersFull = checkInteractAcrossUsersFull();
        long ident = Binder.clearCallingIdentity();
        try {
            if (!canReadPeerMacAddresses && !isActiveNetworkScorer
                    && !isLocationEnabled(callingPackage)) {
                return new ArrayList<ScanResult>();
            }
            if (!canReadPeerMacAddresses && !isActiveNetworkScorer
                    && !checkCallerCanAccessScanResults(callingPackage, uid)) {
                return new ArrayList<ScanResult>();
            }
            if (mAppOps.noteOp(AppOpsManager.OP_WIFI_SCAN, uid, callingPackage)
                    != AppOpsManager.MODE_ALLOWED) {
                return new ArrayList<ScanResult>();
            }
            if (!isCurrentProfile(userId) && !hasInteractUsersFull) {
                return new ArrayList<ScanResult>();
            }
            return mWifiStateMachine.syncGetScanResultsList();
        } finally {
            Binder.restoreCallingIdentity(ident);
        }
    }

    private boolean isLocationEnabled(String callingPackage) {
        boolean legacyForegroundApp = !isMApp(mContext, callingPackage)
                && isForegroundApp(callingPackage);
        return legacyForegroundApp || Settings.Secure.getInt(mContext.getContentResolver(),
                Settings.Secure.LOCATION_MODE, Settings.Secure.LOCATION_MODE_OFF)
                != Settings.Secure.LOCATION_MODE_OFF;
    }

    /**
     * Returns true if the caller holds INTERACT_ACROSS_USERS_FULL.
     */
    private boolean checkInteractAcrossUsersFull() {
        return mContext.checkCallingOrSelfPermission(
                android.Manifest.permission.INTERACT_ACROSS_USERS_FULL)
                == PackageManager.PERMISSION_GRANTED;
    }

    /**
     * Returns true if the caller holds PEERS_MAC_ADDRESS.
     */
    private boolean checkPeersMacAddress() {
        return mContext.checkCallingOrSelfPermission(
                android.Manifest.permission.PEERS_MAC_ADDRESS) == PackageManager.PERMISSION_GRANTED;
    }

    /**
     * Returns true if the calling user is the current one or a profile of the
     * current user..
     */
    private boolean isCurrentProfile(int userId) {
        int currentUser = ActivityManager.getCurrentUser();
        if (userId == currentUser) {
            return true;
        }
        List<UserInfo> profiles = mUserManager.getProfiles(currentUser);
        for (UserInfo user : profiles) {
            if (userId == user.id) {
                return true;
            }
        }
        return false;
    }

    /**
     * Returns true if uid is an application running under the owner or a profile of the owner.
     *
     * Note: Should not be called if identity is cleared.
     */
    private boolean isOwner(int uid) {
        long ident = Binder.clearCallingIdentity();
        int userId = UserHandle.getUserId(uid);
        try {
            int ownerUser = UserHandle.USER_OWNER;
            if (userId == ownerUser) {
                return true;
            }
            List<UserInfo> profiles = mUserManager.getProfiles(ownerUser);
            for (UserInfo profile : profiles) {
                if (userId == profile.id) {
                    return true;
                }
            }
            return false;
        }
        finally {
            Binder.restoreCallingIdentity(ident);
        }
    }


    /**
     * Tell the supplicant to persist the current list of configured networks.
     * @return {@code true} if the operation succeeded
     *
     * TODO: deprecate this
     */
    public boolean saveConfiguration() {
        boolean result = true;
        enforceChangePermission();
        if (mWifiStateMachineChannel != null) {
            return mWifiStateMachine.syncSaveConfig(mWifiStateMachineChannel);
        } else {
            Slog.e(TAG, "mWifiStateMachineChannel is not initialized");
            return false;
        }
    }

    /**
     * Set the country code
     * @param countryCode ISO 3166 country code.
     * @param persist {@code true} if the setting should be remembered.
     *
     * The persist behavior exists so that wifi can fall back to the last
     * persisted country code on a restart, when the locale information is
     * not available from telephony.
     */
    public void setCountryCode(String countryCode, boolean persist) {
        Slog.i(TAG, "WifiService trying to set country code to " + countryCode +
                " with persist set to " + persist);
        enforceConnectivityInternalPermission();
        final long token = Binder.clearCallingIdentity();
        try {
            mWifiStateMachine.setCountryCode(countryCode, persist);
        } finally {
            Binder.restoreCallingIdentity(token);
        }
    }

     /**
     * Get the country code
     * @return ISO 3166 country code.
     */
    public String getCountryCode() {
        enforceConnectivityInternalPermission();
        String country = mWifiStateMachine.getCurrentCountryCode();
        return country;
    }
    /**
     * Set the operational frequency band
     * @param band One of
     *     {@link WifiManager#WIFI_FREQUENCY_BAND_AUTO},
     *     {@link WifiManager#WIFI_FREQUENCY_BAND_5GHZ},
     *     {@link WifiManager#WIFI_FREQUENCY_BAND_2GHZ},
     * @param persist {@code true} if the setting should be remembered.
     *
     */
    public void setFrequencyBand(int band, boolean persist) {
        enforceChangePermission();
        if (!isDualBandSupported()) return;
        Slog.i(TAG, "WifiService trying to set frequency band to " + band +
                " with persist set to " + persist);
        final long token = Binder.clearCallingIdentity();
        try {
            mWifiStateMachine.setFrequencyBand(band, persist);
        } finally {
            Binder.restoreCallingIdentity(token);
        }
    }


    /**
     * Get the operational frequency band
     */
    public int getFrequencyBand() {
        enforceAccessPermission();
        return mWifiStateMachine.getFrequencyBand();
    }

    public boolean isDualBandSupported() {
        //TODO: Should move towards adding a driver API that checks at runtime
        return mContext.getResources().getBoolean(
                com.android.internal.R.bool.config_wifi_dual_band_support);
    }

    /**
     * Return the DHCP-assigned addresses from the last successful DHCP request,
     * if any.
     * @return the DHCP information
     * @deprecated
     */
    public DhcpInfo getDhcpInfo() {
        enforceAccessPermission();
        DhcpResults dhcpResults = mWifiStateMachine.syncGetDhcpResults();

        DhcpInfo info = new DhcpInfo();

        if (dhcpResults.ipAddress != null &&
                dhcpResults.ipAddress.getAddress() instanceof Inet4Address) {
            info.ipAddress = NetworkUtils.inetAddressToInt((Inet4Address) dhcpResults.ipAddress.getAddress());
        }

        if (dhcpResults.gateway != null) {
            info.gateway = NetworkUtils.inetAddressToInt((Inet4Address) dhcpResults.gateway);
        }

        int dnsFound = 0;
        for (InetAddress dns : dhcpResults.dnsServers) {
            if (dns instanceof Inet4Address) {
                if (dnsFound == 0) {
                    info.dns1 = NetworkUtils.inetAddressToInt((Inet4Address)dns);
                } else {
                    info.dns2 = NetworkUtils.inetAddressToInt((Inet4Address)dns);
                }
                if (++dnsFound > 1) break;
            }
        }
        InetAddress serverAddress = dhcpResults.serverAddress;
        if (serverAddress instanceof Inet4Address) {
            info.serverAddress = NetworkUtils.inetAddressToInt((Inet4Address)serverAddress);
        }
        info.leaseDuration = dhcpResults.leaseDuration;

        return info;
    }

    /**
     * see {@link android.net.wifi.WifiManager#startWifi}
     *
     */
    public void startWifi() {
        enforceConnectivityInternalPermission();
        /* TODO: may be add permissions for access only to connectivity service
         * TODO: if a start issued, keep wifi alive until a stop issued irrespective
         * of WifiLock & device idle status unless wifi enabled status is toggled
         */

        mWifiStateMachine.setDriverStart(true);
        mWifiStateMachine.reconnectCommand();
    }

    /**
     * see {@link android.net.wifi.WifiManager#stopWifi}
     *
     */
    public void stopWifi() {
        enforceConnectivityInternalPermission();
        /*
         * TODO: if a stop is issued, wifi is brought up only by startWifi
         * unless wifi enabled status is toggled
         */
        mWifiStateMachine.setDriverStart(false);
    }

    /**
     * see {@link android.net.wifi.WifiManager#addToBlacklist}
     *
     */
    public void addToBlacklist(String bssid) {
        enforceChangePermission();

        mWifiStateMachine.addToBlacklist(bssid);
    }

    /**
     * see {@link android.net.wifi.WifiManager#clearBlacklist}
     *
     */
    public void clearBlacklist() {
        enforceChangePermission();

        mWifiStateMachine.clearBlacklist();
    }

    /**
     * enable TDLS for the local NIC to remote NIC
     * The APPs don't know the remote MAC address to identify NIC though,
     * so we need to do additional work to find it from remote IP address
     */

    class TdlsTaskParams {
        public String remoteIpAddress;
        public boolean enable;
    }

    class TdlsTask extends AsyncTask<TdlsTaskParams, Integer, Integer> {
        @Override
        protected Integer doInBackground(TdlsTaskParams... params) {

            // Retrieve parameters for the call
            TdlsTaskParams param = params[0];
            String remoteIpAddress = param.remoteIpAddress.trim();
            boolean enable = param.enable;

            // Get MAC address of Remote IP
            String macAddress = null;

            BufferedReader reader = null;

            try {
                reader = new BufferedReader(new FileReader("/proc/net/arp"));

                // Skip over the line bearing colum titles
                String line = reader.readLine();

                while ((line = reader.readLine()) != null) {
                    String[] tokens = line.split("[ ]+");
                    if (tokens.length < 6) {
                        continue;
                    }

                    // ARP column format is
                    // Address HWType HWAddress Flags Mask IFace
                    String ip = tokens[0];
                    String mac = tokens[3];

                    if (remoteIpAddress.equals(ip)) {
                        macAddress = mac;
                        break;
                    }
                }

                if (macAddress == null) {
                    Slog.w(TAG, "Did not find remoteAddress {" + remoteIpAddress + "} in " +
                            "/proc/net/arp");
                } else {
                    enableTdlsWithMacAddress(macAddress, enable);
                }

            } catch (FileNotFoundException e) {
                Slog.e(TAG, "Could not open /proc/net/arp to lookup mac address");
            } catch (IOException e) {
                Slog.e(TAG, "Could not read /proc/net/arp to lookup mac address");
            } finally {
                try {
                    if (reader != null) {
                        reader.close();
                    }
                }
                catch (IOException e) {
                    // Do nothing
                }
            }

            return 0;
        }
    }

    public void enableTdls(String remoteAddress, boolean enable) {
        if (remoteAddress == null) {
          throw new IllegalArgumentException("remoteAddress cannot be null");
        }

        TdlsTaskParams params = new TdlsTaskParams();
        params.remoteIpAddress = remoteAddress;
        params.enable = enable;
        new TdlsTask().execute(params);
    }


    public void enableTdlsWithMacAddress(String remoteMacAddress, boolean enable) {
        if (remoteMacAddress == null) {
          throw new IllegalArgumentException("remoteMacAddress cannot be null");
        }

        mWifiStateMachine.enableTdls(remoteMacAddress, enable);
    }

    /**
     * Get a reference to handler. This is used by a client to establish
     * an AsyncChannel communication with WifiService
     */
    public Messenger getWifiServiceMessenger() {
        enforceAccessPermission();
        enforceChangePermission();
        return new Messenger(mClientHandler);
    }

    /**
     * Disable an ephemeral network, i.e. network that is created thru a WiFi Scorer
     */
    public void disableEphemeralNetwork(String SSID) {
        enforceAccessPermission();
        enforceChangePermission();
        mWifiStateMachine.disableEphemeralNetwork(SSID);
    }

    /**
     * Get the IP and proxy configuration file
     */
    public String getConfigFile() {
        enforceAccessPermission();
        return mWifiStateMachine.getConfigFile();
    }

    private final BroadcastReceiver mReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            if (action.equals(Intent.ACTION_SCREEN_ON)) {
                mWifiController.sendMessage(CMD_SCREEN_ON);
            } else if (action.equals(Intent.ACTION_USER_PRESENT)) {
                mWifiController.sendMessage(CMD_USER_PRESENT);
            } else if (action.equals(Intent.ACTION_SCREEN_OFF)) {
                mWifiController.sendMessage(CMD_SCREEN_OFF);
            } else if (action.equals(Intent.ACTION_BATTERY_CHANGED)) {
                int pluggedType = intent.getIntExtra("plugged", 0);
                mWifiController.sendMessage(CMD_BATTERY_CHANGED, pluggedType, 0, null);
            } else if (action.equals(BluetoothAdapter.ACTION_CONNECTION_STATE_CHANGED)) {
                int state = intent.getIntExtra(BluetoothAdapter.EXTRA_CONNECTION_STATE,
                        BluetoothAdapter.STATE_DISCONNECTED);
                mWifiStateMachine.sendBluetoothAdapterStateChange(state);
            } else if (action.equals(TelephonyIntents.ACTION_EMERGENCY_CALLBACK_MODE_CHANGED)) {
                boolean emergencyMode = intent.getBooleanExtra("phoneinECMState", false);
                mWifiController.sendMessage(CMD_EMERGENCY_MODE_CHANGED, emergencyMode ? 1 : 0, 0);
            } else if (action.equals(TelephonyIntents.ACTION_EMERGENCY_CALL_STATE_CHANGED)) {
                boolean inCall = intent.getBooleanExtra(PhoneConstants.PHONE_IN_EMERGENCY_CALL, false);
                mWifiController.sendMessage(CMD_EMERGENCY_CALL_STATE_CHANGED, inCall ? 1 : 0, 0);
            } else if (action.equals(PowerManager.ACTION_DEVICE_IDLE_MODE_CHANGED)) {
                handleIdleModeChanged();
            }
        }
    };

    /**
     * Observes settings changes to scan always mode.
     */
    private void registerForScanModeChange() {
        ContentObserver contentObserver = new ContentObserver(null) {
            @Override
            public void onChange(boolean selfChange) {
                mSettingsStore.handleWifiScanAlwaysAvailableToggled();
                mWifiController.sendMessage(CMD_SCAN_ALWAYS_MODE_CHANGED);
            }
        };

        mContext.getContentResolver().registerContentObserver(
                Settings.Global.getUriFor(Settings.Global.WIFI_SCAN_ALWAYS_AVAILABLE),
                false, contentObserver);
    }

    private void registerForBroadcasts() {
        IntentFilter intentFilter = new IntentFilter();
        intentFilter.addAction(Intent.ACTION_SCREEN_ON);
        intentFilter.addAction(Intent.ACTION_USER_PRESENT);
        intentFilter.addAction(Intent.ACTION_SCREEN_OFF);
        intentFilter.addAction(Intent.ACTION_BATTERY_CHANGED);
        intentFilter.addAction(WifiManager.NETWORK_STATE_CHANGED_ACTION);
        intentFilter.addAction(BluetoothAdapter.ACTION_CONNECTION_STATE_CHANGED);
        intentFilter.addAction(TelephonyIntents.ACTION_EMERGENCY_CALLBACK_MODE_CHANGED);
        intentFilter.addAction(PowerManager.ACTION_DEVICE_IDLE_MODE_CHANGED);

        boolean trackEmergencyCallState = mContext.getResources().getBoolean(
                com.android.internal.R.bool.config_wifi_turn_off_during_emergency_call);
        if (trackEmergencyCallState) {
            intentFilter.addAction(TelephonyIntents.ACTION_EMERGENCY_CALL_STATE_CHANGED);
        }

        mContext.registerReceiver(mReceiver, intentFilter);
    }

    private void registerForPackageOrUserRemoval() {
        IntentFilter intentFilter = new IntentFilter();
        intentFilter.addAction(Intent.ACTION_PACKAGE_REMOVED);
        intentFilter.addAction(Intent.ACTION_USER_REMOVED);
        mContext.registerReceiverAsUser(new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                switch (intent.getAction()) {
                    case Intent.ACTION_PACKAGE_REMOVED: {
                        if (intent.getBooleanExtra(Intent.EXTRA_REPLACING, false)) {
                            return;
                        }
                        int uid = intent.getIntExtra(Intent.EXTRA_UID, -1);
                        Uri uri = intent.getData();
                        if (uid == -1 || uri == null) {
                            return;
                        }
                        String pkgName = uri.getSchemeSpecificPart();
                        mWifiStateMachine.removeAppConfigs(pkgName, uid);
                        break;
                    }
                    case Intent.ACTION_USER_REMOVED: {
                        int userHandle = intent.getIntExtra(Intent.EXTRA_USER_HANDLE, 0);
                        mWifiStateMachine.removeUserConfigs(userHandle);
                        break;
                    }
                }
            }
        }, UserHandle.ALL, intentFilter, null, null);
    }

    @Override
    protected void dump(FileDescriptor fd, PrintWriter pw, String[] args) {
        if (mContext.checkCallingOrSelfPermission(android.Manifest.permission.DUMP)
                != PackageManager.PERMISSION_GRANTED) {
            pw.println("Permission Denial: can't dump WifiService from from pid="
                    + Binder.getCallingPid()
                    + ", uid=" + Binder.getCallingUid());
            return;
        }
        pw.println("Wi-Fi is " + mWifiStateMachine.syncGetWifiStateByName());
        pw.println("Stay-awake conditions: " +
                Settings.Global.getInt(mContext.getContentResolver(),
                                       Settings.Global.STAY_ON_WHILE_PLUGGED_IN, 0));
        pw.println("mMulticastEnabled " + mMulticastEnabled);
        pw.println("mMulticastDisabled " + mMulticastDisabled);
        pw.println("mInIdleMode " + mInIdleMode);
        pw.println("mScanPending " + mScanPending);
        mWifiController.dump(fd, pw, args);
        mSettingsStore.dump(fd, pw, args);
        mNotificationController.dump(fd, pw, args);
        mTrafficPoller.dump(fd, pw, args);

        pw.println("Latest scan results:");
        List<ScanResult> scanResults = mWifiStateMachine.syncGetScanResultsList();
        long nowMs = System.currentTimeMillis();
        if (scanResults != null && scanResults.size() != 0) {
            pw.println("    BSSID              Frequency  RSSI    Age      SSID " +
                    "                                Flags");
            for (ScanResult r : scanResults) {
                long ageSec = 0;
                long ageMilli = 0;
                if (nowMs > r.seen && r.seen > 0) {
                    ageSec = (nowMs - r.seen) / 1000;
                    ageMilli = (nowMs - r.seen) % 1000;
                }
                String candidate = " ";
                if (r.isAutoJoinCandidate > 0) candidate = "+";
                pw.printf("  %17s  %9d  %5d  %3d.%03d%s   %-32s  %s\n",
                                         r.BSSID,
                                         r.frequency,
                                         r.level,
                                         ageSec, ageMilli,
                                         candidate,
                                         r.SSID == null ? "" : r.SSID,
                                         r.capabilities);
            }
        }
        pw.println();
        pw.println("Locks acquired: " + mFullLocksAcquired + " full, " +
                mFullHighPerfLocksAcquired + " full high perf, " +
                mScanLocksAcquired + " scan");
        pw.println("Locks released: " + mFullLocksReleased + " full, " +
                mFullHighPerfLocksReleased + " full high perf, " +
                mScanLocksReleased + " scan");
        pw.println();
        pw.println("Locks held:");
        mLocks.dump(pw);

        pw.println("Multicast Locks held:");
        for (Multicaster l : mMulticasters) {
            pw.print("    ");
            pw.println(l);
        }

        mWifiWatchdogStateMachine.dump(fd, pw, args);
        pw.println();
        mWifiStateMachine.dump(fd, pw, args);
        pw.println();
    }

    private class WifiLock extends DeathRecipient {
        WifiLock(int lockMode, String tag, IBinder binder, WorkSource ws) {
            super(lockMode, tag, binder, ws);
        }

        public void binderDied() {
            synchronized (mLocks) {
                releaseWifiLockLocked(mBinder);
            }
        }

        public String toString() {
            return "WifiLock{" + mTag + " type=" + mMode + " binder=" + mBinder + "}";
        }
    }

    class LockList {
        private List<WifiLock> mList;

        private LockList() {
            mList = new ArrayList<WifiLock>();
        }

        synchronized boolean hasLocks() {
            return !mList.isEmpty();
        }

        synchronized int getStrongestLockMode() {
            if (mList.isEmpty()) {
                return WifiManager.WIFI_MODE_FULL;
            }

            if (mFullHighPerfLocksAcquired > mFullHighPerfLocksReleased) {
                return WifiManager.WIFI_MODE_FULL_HIGH_PERF;
            }

            if (mFullLocksAcquired > mFullLocksReleased) {
                return WifiManager.WIFI_MODE_FULL;
            }

            return WifiManager.WIFI_MODE_SCAN_ONLY;
        }

        synchronized void updateWorkSource(WorkSource ws) {
            for (int i = 0; i < mLocks.mList.size(); i++) {
                ws.add(mLocks.mList.get(i).mWorkSource);
            }
        }

        private void addLock(WifiLock lock) {
            if (findLockByBinder(lock.mBinder) < 0) {
                mList.add(lock);
            }
        }

        private WifiLock removeLock(IBinder binder) {
            int index = findLockByBinder(binder);
            if (index >= 0) {
                WifiLock ret = mList.remove(index);
                ret.unlinkDeathRecipient();
                return ret;
            } else {
                return null;
            }
        }

        private int findLockByBinder(IBinder binder) {
            int size = mList.size();
            for (int i = size - 1; i >= 0; i--) {
                if (mList.get(i).mBinder == binder)
                    return i;
            }
            return -1;
        }

        private void dump(PrintWriter pw) {
            for (WifiLock l : mList) {
                pw.print("    ");
                pw.println(l);
            }
        }
    }

    void enforceWakeSourcePermission(int uid, int pid) {
        if (uid == android.os.Process.myUid()) {
            return;
        }
        mContext.enforcePermission(android.Manifest.permission.UPDATE_DEVICE_STATS,
                pid, uid, null);
    }

    public boolean acquireWifiLock(IBinder binder, int lockMode, String tag, WorkSource ws) {
        mContext.enforceCallingOrSelfPermission(android.Manifest.permission.WAKE_LOCK, null);
        if (lockMode != WifiManager.WIFI_MODE_FULL &&
                lockMode != WifiManager.WIFI_MODE_SCAN_ONLY &&
                lockMode != WifiManager.WIFI_MODE_FULL_HIGH_PERF) {
            Slog.e(TAG, "Illegal argument, lockMode= " + lockMode);
            if (DBG) throw new IllegalArgumentException("lockMode=" + lockMode);
            return false;
        }
        if (ws != null && ws.size() == 0) {
            ws = null;
        }
        if (ws != null) {
            enforceWakeSourcePermission(Binder.getCallingUid(), Binder.getCallingPid());
        }
        if (ws == null) {
            ws = new WorkSource(Binder.getCallingUid());
        }
        WifiLock wifiLock = new WifiLock(lockMode, tag, binder, ws);
        synchronized (mLocks) {
            return acquireWifiLockLocked(wifiLock);
        }
    }

    private void noteAcquireWifiLock(WifiLock wifiLock) throws RemoteException {
        switch(wifiLock.mMode) {
            case WifiManager.WIFI_MODE_FULL:
            case WifiManager.WIFI_MODE_FULL_HIGH_PERF:
            case WifiManager.WIFI_MODE_SCAN_ONLY:
                mBatteryStats.noteFullWifiLockAcquiredFromSource(wifiLock.mWorkSource);
                break;
        }
    }

    private void noteReleaseWifiLock(WifiLock wifiLock) throws RemoteException {
        switch(wifiLock.mMode) {
            case WifiManager.WIFI_MODE_FULL:
            case WifiManager.WIFI_MODE_FULL_HIGH_PERF:
            case WifiManager.WIFI_MODE_SCAN_ONLY:
                mBatteryStats.noteFullWifiLockReleasedFromSource(wifiLock.mWorkSource);
                break;
        }
    }

    private boolean acquireWifiLockLocked(WifiLock wifiLock) {
        if (DBG) Slog.d(TAG, "acquireWifiLockLocked: " + wifiLock);

        mLocks.addLock(wifiLock);

        long ident = Binder.clearCallingIdentity();
        try {
            noteAcquireWifiLock(wifiLock);
            switch(wifiLock.mMode) {
            case WifiManager.WIFI_MODE_FULL:
                ++mFullLocksAcquired;
                break;
            case WifiManager.WIFI_MODE_FULL_HIGH_PERF:
                ++mFullHighPerfLocksAcquired;
                break;

            case WifiManager.WIFI_MODE_SCAN_ONLY:
                ++mScanLocksAcquired;
                break;
            }
            mWifiController.sendMessage(CMD_LOCKS_CHANGED);
            return true;
        } catch (RemoteException e) {
            return false;
        } finally {
            Binder.restoreCallingIdentity(ident);
        }
    }

    public void updateWifiLockWorkSource(IBinder lock, WorkSource ws) {
        int uid = Binder.getCallingUid();
        int pid = Binder.getCallingPid();
        if (ws != null && ws.size() == 0) {
            ws = null;
        }
        if (ws != null) {
            enforceWakeSourcePermission(uid, pid);
        }
        long ident = Binder.clearCallingIdentity();
        try {
            synchronized (mLocks) {
                int index = mLocks.findLockByBinder(lock);
                if (index < 0) {
                    throw new IllegalArgumentException("Wifi lock not active");
                }
                WifiLock wl = mLocks.mList.get(index);
                noteReleaseWifiLock(wl);
                wl.mWorkSource = ws != null ? new WorkSource(ws) : new WorkSource(uid);
                noteAcquireWifiLock(wl);
            }
        } catch (RemoteException e) {
        } finally {
            Binder.restoreCallingIdentity(ident);
        }
    }

    public boolean releaseWifiLock(IBinder lock) {
        mContext.enforceCallingOrSelfPermission(android.Manifest.permission.WAKE_LOCK, null);
        synchronized (mLocks) {
            return releaseWifiLockLocked(lock);
        }
    }

    private boolean releaseWifiLockLocked(IBinder lock) {
        boolean hadLock;

        WifiLock wifiLock = mLocks.removeLock(lock);

        if (DBG) Slog.d(TAG, "releaseWifiLockLocked: " + wifiLock);

        hadLock = (wifiLock != null);

        long ident = Binder.clearCallingIdentity();
        try {
            if (hadLock) {
                noteReleaseWifiLock(wifiLock);
                switch(wifiLock.mMode) {
                    case WifiManager.WIFI_MODE_FULL:
                        ++mFullLocksReleased;
                        break;
                    case WifiManager.WIFI_MODE_FULL_HIGH_PERF:
                        ++mFullHighPerfLocksReleased;
                        break;
                    case WifiManager.WIFI_MODE_SCAN_ONLY:
                        ++mScanLocksReleased;
                        break;
                }
                mWifiController.sendMessage(CMD_LOCKS_CHANGED);
            }
        } catch (RemoteException e) {
        } finally {
            Binder.restoreCallingIdentity(ident);
        }

        return hadLock;
    }

    private abstract class DeathRecipient
            implements IBinder.DeathRecipient {
        String mTag;
        int mMode;
        IBinder mBinder;
        WorkSource mWorkSource;

        DeathRecipient(int mode, String tag, IBinder binder, WorkSource ws) {
            super();
            mTag = tag;
            mMode = mode;
            mBinder = binder;
            mWorkSource = ws;
            try {
                mBinder.linkToDeath(this, 0);
            } catch (RemoteException e) {
                binderDied();
            }
        }

        void unlinkDeathRecipient() {
            mBinder.unlinkToDeath(this, 0);
        }
    }

    private class Multicaster extends DeathRecipient {
        Multicaster(String tag, IBinder binder) {
            super(Binder.getCallingUid(), tag, binder, null);
        }

        public void binderDied() {
            Slog.e(TAG, "Multicaster binderDied");
            synchronized (mMulticasters) {
                int i = mMulticasters.indexOf(this);
                if (i != -1) {
                    removeMulticasterLocked(i, mMode);
                }
            }
        }

        public String toString() {
            return "Multicaster{" + mTag + " binder=" + mBinder + "}";
        }

        public int getUid() {
            return mMode;
        }
    }

    public void initializeMulticastFiltering() {
        enforceMulticastChangePermission();

        synchronized (mMulticasters) {
            // if anybody had requested filters be off, leave off
            if (mMulticasters.size() != 0) {
                return;
            } else {
                mWifiStateMachine.startFilteringMulticastV4Packets();
            }
        }
    }

    public void acquireMulticastLock(IBinder binder, String tag) {
        enforceMulticastChangePermission();

        synchronized (mMulticasters) {
            mMulticastEnabled++;
            mMulticasters.add(new Multicaster(tag, binder));
            // Note that we could call stopFilteringMulticastV4Packets only when
            // our new size == 1 (first call), but this function won't
            // be called often and by making the stopPacket call each
            // time we're less fragile and self-healing.
            mWifiStateMachine.stopFilteringMulticastV4Packets();
        }

        int uid = Binder.getCallingUid();
        final long ident = Binder.clearCallingIdentity();
        try {
            mBatteryStats.noteWifiMulticastEnabled(uid);
        } catch (RemoteException e) {
        } finally {
            Binder.restoreCallingIdentity(ident);
        }
    }

    public void releaseMulticastLock() {
        enforceMulticastChangePermission();

        int uid = Binder.getCallingUid();
        synchronized (mMulticasters) {
            mMulticastDisabled++;
            int size = mMulticasters.size();
            for (int i = size - 1; i >= 0; i--) {
                Multicaster m = mMulticasters.get(i);
                if ((m != null) && (m.getUid() == uid)) {
                    removeMulticasterLocked(i, uid);
                }
            }
        }
    }

    private void removeMulticasterLocked(int i, int uid)
    {
        Multicaster removed = mMulticasters.remove(i);

        if (removed != null) {
            removed.unlinkDeathRecipient();
        }
        if (mMulticasters.size() == 0) {
            mWifiStateMachine.startFilteringMulticastV4Packets();
        }

        final long ident = Binder.clearCallingIdentity();
        try {
            mBatteryStats.noteWifiMulticastDisabled(uid);
        } catch (RemoteException e) {
        } finally {
            Binder.restoreCallingIdentity(ident);
        }
    }

    public boolean isMulticastEnabled() {
        enforceAccessPermission();

        synchronized (mMulticasters) {
            return (mMulticasters.size() > 0);
        }
    }

    public WifiMonitor getWifiMonitor() {
        return mWifiStateMachine.getWifiMonitor();
    }

    public void enableVerboseLogging(int verbose) {
        enforceAccessPermission();
        mWifiStateMachine.enableVerboseLogging(verbose);
    }

    public int getVerboseLoggingLevel() {
        enforceAccessPermission();
        return mWifiStateMachine.getVerboseLoggingLevel();
    }

    public void enableAggressiveHandover(int enabled) {
        enforceAccessPermission();
        mWifiStateMachine.enableAggressiveHandover(enabled);
    }

    public int getAggressiveHandover() {
        enforceAccessPermission();
        return mWifiStateMachine.getAggressiveHandover();
    }

    public void setAllowScansWithTraffic(int enabled) {
        enforceAccessPermission();
        mWifiStateMachine.setAllowScansWithTraffic(enabled);
    }

    public int getAllowScansWithTraffic() {
        enforceAccessPermission();
        return mWifiStateMachine.getAllowScansWithTraffic();
    }

    public boolean enableAutoJoinWhenAssociated(boolean enabled) {
        enforceChangePermission();
        return mWifiStateMachine.enableAutoJoinWhenAssociated(enabled);
    }

    public boolean getEnableAutoJoinWhenAssociated() {
        enforceAccessPermission();
        return mWifiStateMachine.getEnableAutoJoinWhenAssociated();
    }
    public void setHalBasedAutojoinOffload(int enabled) {
        enforceConnectivityInternalPermission();
        mWifiStateMachine.setHalBasedAutojoinOffload(enabled);
    }

    public int getHalBasedAutojoinOffload() {
        enforceAccessPermission();
        return mWifiStateMachine.getHalBasedAutojoinOffload();
    }

    /* Return the Wifi Connection statistics object */
    public WifiConnectionStatistics getConnectionStatistics() {
        enforceAccessPermission();
        enforceReadCredentialPermission();
        if (mWifiStateMachineChannel != null) {
            return mWifiStateMachine.syncGetConnectionStatistics(mWifiStateMachineChannel);
        } else {
            Slog.e(TAG, "mWifiStateMachineChannel is not initialized");
            return null;
        }
    }

    public void factoryReset() {
        enforceConnectivityInternalPermission();

        if (mUserManager.hasUserRestriction(UserManager.DISALLOW_NETWORK_RESET)) {
            return;
        }

        if (!mUserManager.hasUserRestriction(UserManager.DISALLOW_CONFIG_TETHERING)) {
            // Turn mobile hotspot off
            setWifiApEnabled(null, false);
        }

        if (!mUserManager.hasUserRestriction(UserManager.DISALLOW_CONFIG_WIFI)) {
            // Enable wifi
            setWifiEnabled(true);
            // Delete all Wifi SSIDs
            List<WifiConfiguration> networks = getConfiguredNetworks();
            if (networks != null) {
                for (WifiConfiguration config : networks) {
                    removeNetwork(config.networkId);
                }
                saveConfiguration();
            }
        }
    }

    /* private methods */
    static boolean logAndReturnFalse(String s) {
        Log.d(TAG, s);
        return false;
    }

    public static boolean isValid(WifiConfiguration config) {
        String validity = checkValidity(config);
        return validity == null || logAndReturnFalse(validity);
    }

    public static boolean isValidPasspoint(WifiConfiguration config) {
        String validity = checkPasspointValidity(config);
        return validity == null || logAndReturnFalse(validity);
    }

    public static String checkValidity(WifiConfiguration config) {
        if (config.allowedKeyManagement == null)
            return "allowed kmgmt";

        if (config.allowedKeyManagement.cardinality() > 1) {
            if (config.allowedKeyManagement.cardinality() != 2) {
                return "cardinality != 2";
            }
            if (!config.allowedKeyManagement.get(WifiConfiguration.KeyMgmt.WPA_EAP)) {
                return "not WPA_EAP";
            }
            if ((!config.allowedKeyManagement.get(WifiConfiguration.KeyMgmt.IEEE8021X))
                    && (!config.allowedKeyManagement.get(WifiConfiguration.KeyMgmt.WPA_PSK))) {
                return "not PSK or 8021X";
            }
        }
        return null;
    }

    public static String checkPasspointValidity(WifiConfiguration config) {
        if (!TextUtils.isEmpty(config.FQDN)) {
            /* this is passpoint configuration; it must not have an SSID */
            if (!TextUtils.isEmpty(config.SSID)) {
                return "SSID not expected for Passpoint: '" + config.SSID +
                        "' FQDN " + toHexString(config.FQDN);
            }
            /* this is passpoint configuration; it must have a providerFriendlyName */
            if (TextUtils.isEmpty(config.providerFriendlyName)) {
                return "no provider friendly name";
            }
            WifiEnterpriseConfig enterpriseConfig = config.enterpriseConfig;
            /* this is passpoint configuration; it must have enterprise config */
            if (enterpriseConfig == null
                    || enterpriseConfig.getEapMethod() == WifiEnterpriseConfig.Eap.NONE ) {
                return "no enterprise config";
            }
            if ((enterpriseConfig.getEapMethod() == WifiEnterpriseConfig.Eap.TLS ||
                    enterpriseConfig.getEapMethod() == WifiEnterpriseConfig.Eap.TTLS ||
                    enterpriseConfig.getEapMethod() == WifiEnterpriseConfig.Eap.PEAP) &&
                    enterpriseConfig.getCaCertificate() == null) {
                return "no CA certificate";
            }
        }
        return null;
    }

    public Network getCurrentNetwork() {
        enforceAccessPermission();
        return mWifiStateMachine.getCurrentNetwork();
    }

    public void reCreateVMNetworkAgent(int index) {
        mWifiStateMachine.reCreateVMNetworkAgent(index);
    }

    public static String toHexString(String s) {
        if (s == null) {
            return "null";
        }
        StringBuilder sb = new StringBuilder();
        sb.append('\'').append(s).append('\'');
        for (int n = 0; n < s.length(); n++) {
            sb.append(String.format(" %02x", s.charAt(n) & 0xffff));
        }
        return sb.toString();
    }

    /**
     * Checks that calling process has android.Manifest.permission.ACCESS_COARSE_LOCATION or
     * android.Manifest.permission.ACCESS_FINE_LOCATION and a corresponding app op is allowed
     */
    private boolean checkCallerCanAccessScanResults(String callingPackage, int uid) {
        if (ActivityManager.checkUidPermission(Manifest.permission.ACCESS_FINE_LOCATION, uid)
                == PackageManager.PERMISSION_GRANTED
                && checkAppOppAllowed(AppOpsManager.OP_FINE_LOCATION, callingPackage, uid)) {
            return true;
        }

        if (ActivityManager.checkUidPermission(Manifest.permission.ACCESS_COARSE_LOCATION, uid)
                == PackageManager.PERMISSION_GRANTED
                && checkAppOppAllowed(AppOpsManager.OP_COARSE_LOCATION, callingPackage, uid)) {
            return true;
        }
        boolean apiLevel23App = isMApp(mContext, callingPackage);
        // Pre-M apps running in the foreground should continue getting scan results
        if (!apiLevel23App && isForegroundApp(callingPackage)) {
            return true;
        }
        Log.e(TAG, "Permission denial: Need ACCESS_COARSE_LOCATION or ACCESS_FINE_LOCATION "
                + "permission to get scan results");
        return false;
    }

    private boolean checkAppOppAllowed(int op, String callingPackage, int uid) {
        return mAppOps.noteOp(op, uid, callingPackage) == AppOpsManager.MODE_ALLOWED;
    }

    private static boolean isMApp(Context context, String pkgName) {
        try {
            return context.getPackageManager().getApplicationInfo(pkgName, 0)
                    .targetSdkVersion >= Build.VERSION_CODES.M;
        } catch (PackageManager.NameNotFoundException e) {
            // In case of exception, assume M app (more strict checking)
        }
        return true;
    }

    /**
     * Return true if the specified package name is a foreground app.
     *
     * @param pkgName application package name.
     */
    private boolean isForegroundApp(String pkgName) {
        ActivityManager am = (ActivityManager)mContext.getSystemService(Context.ACTIVITY_SERVICE);
        List<ActivityManager.RunningTaskInfo> tasks = am.getRunningTasks(1);
        return !tasks.isEmpty() && pkgName.equals(tasks.get(0).topActivity.getPackageName());
    }

}
