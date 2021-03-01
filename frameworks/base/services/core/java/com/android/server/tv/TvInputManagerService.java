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

package com.android.server.tv;

import static android.media.tv.TvInputManager.INPUT_STATE_CONNECTED;
import static android.media.tv.TvInputManager.INPUT_STATE_CONNECTED_STANDBY;

import android.app.ActivityManager;
import android.content.BroadcastReceiver;
import android.content.ComponentName;
import android.content.ContentProviderOperation;
import android.content.ContentProviderResult;
import android.content.ContentResolver;
import android.content.ContentUris;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.OperationApplicationException;
import android.content.ServiceConnection;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.content.pm.PackageManager.NameNotFoundException;
import android.content.pm.ResolveInfo;
import android.content.pm.ServiceInfo;
import android.graphics.Rect;
import android.hardware.hdmi.HdmiControlManager;
import android.hardware.hdmi.HdmiDeviceInfo;
import android.media.PlaybackParams;
import android.media.tv.DvbDeviceInfo;
import android.media.tv.ITvInputClient;
import android.media.tv.ITvInputHardware;
import android.media.tv.ITvInputHardwareCallback;
import android.media.tv.ITvInputManager;
import android.media.tv.ITvInputManagerCallback;
import android.media.tv.ITvInputService;
import android.media.tv.ITvInputServiceCallback;
import android.media.tv.ITvInputSession;
import android.media.tv.ITvInputSessionCallback;
import android.media.tv.TvContentRating;
import android.media.tv.TvContentRatingSystemInfo;
import android.media.tv.TvContract;
import android.media.tv.TvInputHardwareInfo;
import android.media.tv.TvInputInfo;
import android.media.tv.TvInputManager;
import android.media.tv.TvInputService;
import android.media.tv.TvStreamConfig;
import android.media.tv.TvTrackInfo;
import android.net.Uri;
import android.os.Binder;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.Message;
import android.os.ParcelFileDescriptor;
import android.os.Process;
import android.os.RemoteException;
import android.os.UserHandle;
import android.util.Slog;
import android.util.SparseArray;
import android.view.InputChannel;
import android.view.Surface;

import com.android.internal.content.PackageMonitor;
import com.android.internal.os.SomeArgs;
import com.android.internal.util.IndentingPrintWriter;
import com.android.server.IoThread;
import com.android.server.SystemService;

import org.xmlpull.v1.XmlPullParserException;

import java.io.File;
import java.io.FileDescriptor;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** This class provides a system service that manages television inputs. */
public final class TvInputManagerService extends SystemService {
    private static final boolean DEBUG = false;
    private static final String TAG = "TvInputManagerService";

    // Pattern for selecting the DVB frontend devices from the list of files in the /dev directory.
    private static final Pattern sFrontEndDevicePattern =
            Pattern.compile("^dvb([0-9]+)\\.frontend([0-9]+)$");

    private final Context mContext;
    private final TvInputHardwareManager mTvInputHardwareManager;

    // A global lock.
    private final Object mLock = new Object();

    // ID of the current user.
    private int mCurrentUserId = UserHandle.USER_OWNER;

    // A map from user id to UserState.
    private final SparseArray<UserState> mUserStates = new SparseArray<>();

    private final WatchLogHandler mWatchLogHandler;

    public TvInputManagerService(Context context) {
        super(context);

        mContext = context;
        mWatchLogHandler = new WatchLogHandler(mContext.getContentResolver(),
                IoThread.get().getLooper());
        mTvInputHardwareManager = new TvInputHardwareManager(context, new HardwareListener());

        synchronized (mLock) {
            getOrCreateUserStateLocked(mCurrentUserId);
        }
    }

    @Override
    public void onStart() {
        publishBinderService(Context.TV_INPUT_SERVICE, new BinderService());
    }

    @Override
    public void onBootPhase(int phase) {
        if (phase == SystemService.PHASE_SYSTEM_SERVICES_READY) {
            registerBroadcastReceivers();
        } else if (phase == SystemService.PHASE_THIRD_PARTY_APPS_CAN_START) {
            synchronized (mLock) {
                buildTvInputListLocked(mCurrentUserId, null);
                buildTvContentRatingSystemListLocked(mCurrentUserId);
            }
        }
        mTvInputHardwareManager.onBootPhase(phase);
    }

    private void registerBroadcastReceivers() {
        PackageMonitor monitor = new PackageMonitor() {
            private void buildTvInputList(String[] packages) {
                synchronized (mLock) {
                    buildTvInputListLocked(getChangingUserId(), packages);
                    buildTvContentRatingSystemListLocked(getChangingUserId());
                }
            }

            @Override
            public void onPackageUpdateFinished(String packageName, int uid) {
                if (DEBUG) Slog.d(TAG, "onPackageUpdateFinished(packageName=" + packageName + ")");
                // This callback is invoked when the TV input is reinstalled.
                // In this case, isReplacing() always returns true.
                buildTvInputList(new String[] { packageName });
            }

            @Override
            public void onPackagesAvailable(String[] packages) {
                if (DEBUG) {
                    Slog.d(TAG, "onPackagesAvailable(packages=" + Arrays.toString(packages) + ")");
                }
                // This callback is invoked when the media on which some packages exist become
                // available.
                if (isReplacing()) {
                    buildTvInputList(packages);
                }
            }

            @Override
            public void onPackagesUnavailable(String[] packages) {
                // This callback is invoked when the media on which some packages exist become
                // unavailable.
                if (DEBUG)  {
                    Slog.d(TAG, "onPackagesUnavailable(packages=" + Arrays.toString(packages)
                            + ")");
                }
                if (isReplacing()) {
                    buildTvInputList(packages);
                }
            }

            @Override
            public void onSomePackagesChanged() {
                // TODO: Use finer-grained methods(e.g. onPackageAdded, onPackageRemoved) to manage
                // the TV inputs.
                if (DEBUG) Slog.d(TAG, "onSomePackagesChanged()");
                if (isReplacing()) {
                    if (DEBUG) Slog.d(TAG, "Skipped building TV input list due to replacing");
                    // When the package is updated, buildTvInputListLocked is called in other
                    // methods instead.
                    return;
                }
                buildTvInputList(null);
            }

            @Override
            public boolean onPackageChanged(String packageName, int uid, String[] components) {
                // The input list needs to be updated in any cases, regardless of whether
                // it happened to the whole package or a specific component. Returning true so that
                // the update can be handled in {@link #onSomePackagesChanged}.
                return true;
            }

            @Override
            public void onPackageRemoved(String packageName, int uid) {
                synchronized (mLock) {
                    UserState userState = getOrCreateUserStateLocked(getChangingUserId());
                    if (!userState.packageSet.contains(packageName)) {
                        // Not a TV input package.
                        return;
                    }
                }

                ArrayList<ContentProviderOperation> operations = new ArrayList<>();

                String selection = TvContract.BaseTvColumns.COLUMN_PACKAGE_NAME + "=?";
                String[] selectionArgs = { packageName };

                operations.add(ContentProviderOperation.newDelete(TvContract.Channels.CONTENT_URI)
                        .withSelection(selection, selectionArgs).build());
                operations.add(ContentProviderOperation.newDelete(TvContract.Programs.CONTENT_URI)
                        .withSelection(selection, selectionArgs).build());
                operations.add(ContentProviderOperation
                        .newDelete(TvContract.WatchedPrograms.CONTENT_URI)
                        .withSelection(selection, selectionArgs).build());

                ContentProviderResult[] results = null;
                try {
                    ContentResolver cr = getContentResolverForUser(getChangingUserId());
                    results = cr.applyBatch(TvContract.AUTHORITY, operations);
                } catch (RemoteException | OperationApplicationException e) {
                    Slog.e(TAG, "error in applyBatch", e);
                }

                if (DEBUG) {
                    Slog.d(TAG, "onPackageRemoved(packageName=" + packageName + ", uid=" + uid
                            + ")");
                    Slog.d(TAG, "results=" + results);
                }
            }
        };
        monitor.register(mContext, null, UserHandle.ALL, true);

        IntentFilter intentFilter = new IntentFilter();
        intentFilter.addAction(Intent.ACTION_USER_SWITCHED);
        intentFilter.addAction(Intent.ACTION_USER_REMOVED);
        mContext.registerReceiverAsUser(new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                String action = intent.getAction();
                if (Intent.ACTION_USER_SWITCHED.equals(action)) {
                    switchUser(intent.getIntExtra(Intent.EXTRA_USER_HANDLE, 0));
                } else if (Intent.ACTION_USER_REMOVED.equals(action)) {
                    removeUser(intent.getIntExtra(Intent.EXTRA_USER_HANDLE, 0));
                }
            }
        }, UserHandle.ALL, intentFilter, null, null);
    }

    private static boolean hasHardwarePermission(PackageManager pm, ComponentName component) {
        return pm.checkPermission(android.Manifest.permission.TV_INPUT_HARDWARE,
                component.getPackageName()) == PackageManager.PERMISSION_GRANTED;
    }

    private void buildTvInputListLocked(int userId, String[] updatedPackages) {
        UserState userState = getOrCreateUserStateLocked(userId);
        userState.packageSet.clear();

        if (DEBUG) Slog.d(TAG, "buildTvInputList");
        PackageManager pm = mContext.getPackageManager();
        List<ResolveInfo> services = pm.queryIntentServicesAsUser(
                new Intent(TvInputService.SERVICE_INTERFACE),
                PackageManager.GET_SERVICES | PackageManager.GET_META_DATA,
                userId);
        List<TvInputInfo> inputList = new ArrayList<>();
        for (ResolveInfo ri : services) {
            ServiceInfo si = ri.serviceInfo;
            if (!android.Manifest.permission.BIND_TV_INPUT.equals(si.permission)) {
                Slog.w(TAG, "Skipping TV input " + si.name + ": it does not require the permission "
                        + android.Manifest.permission.BIND_TV_INPUT);
                continue;
            }

            ComponentName component = new ComponentName(si.packageName, si.name);
            if (hasHardwarePermission(pm, component)) {
                ServiceState serviceState = userState.serviceStateMap.get(component);
                if (serviceState == null) {
                    // We see this hardware TV input service for the first time; we need to
                    // prepare the ServiceState object so that we can connect to the service and
                    // let it add TvInputInfo objects to mInputList if there's any.
                    serviceState = new ServiceState(component, userId);
                    userState.serviceStateMap.put(component, serviceState);
                    updateServiceConnectionLocked(component, userId);
                } else {
                    inputList.addAll(serviceState.inputList);
                }
            } else {
                try {
                    inputList.add(TvInputInfo.createTvInputInfo(mContext, ri));
                } catch (XmlPullParserException | IOException e) {
                    Slog.e(TAG, "failed to load TV input " + si.name, e);
                    continue;
                }
            }
            userState.packageSet.add(si.packageName);
        }

        Map<String, TvInputState> inputMap = new HashMap<>();
        for (TvInputInfo info : inputList) {
            if (DEBUG) {
                Slog.d(TAG, "add " + info.getId());
            }
            TvInputState state = userState.inputMap.get(info.getId());
            if (state == null) {
                state = new TvInputState();
            }
            state.info = info;
            inputMap.put(info.getId(), state);
        }

        for (String inputId : inputMap.keySet()) {
            if (!userState.inputMap.containsKey(inputId)) {
                notifyInputAddedLocked(userState, inputId);
            } else if (updatedPackages != null) {
                // Notify the package updates
                ComponentName component = inputMap.get(inputId).info.getComponent();
                for (String updatedPackage : updatedPackages) {
                    if (component.getPackageName().equals(updatedPackage)) {
                        updateServiceConnectionLocked(component, userId);
                        notifyInputUpdatedLocked(userState, inputId);
                        break;
                    }
                }
            }
        }

        for (String inputId : userState.inputMap.keySet()) {
            if (!inputMap.containsKey(inputId)) {
                TvInputInfo info = userState.inputMap.get(inputId).info;
                ServiceState serviceState = userState.serviceStateMap.get(info.getComponent());
                if (serviceState != null) {
                    abortPendingCreateSessionRequestsLocked(serviceState, inputId, userId);
                }
                notifyInputRemovedLocked(userState, inputId);
            }
        }

        userState.inputMap.clear();
        userState.inputMap = inputMap;
    }

    private void buildTvContentRatingSystemListLocked(int userId) {
        UserState userState = getOrCreateUserStateLocked(userId);
        userState.contentRatingSystemList.clear();

        final PackageManager pm = mContext.getPackageManager();
        Intent intent = new Intent(TvInputManager.ACTION_QUERY_CONTENT_RATING_SYSTEMS);
        for (ResolveInfo resolveInfo :
                pm.queryBroadcastReceivers(intent, PackageManager.GET_META_DATA)) {
            ActivityInfo receiver = resolveInfo.activityInfo;
            Bundle metaData = receiver.metaData;
            if (metaData == null) {
                continue;
            }

            int xmlResId = metaData.getInt(TvInputManager.META_DATA_CONTENT_RATING_SYSTEMS);
            if (xmlResId == 0) {
                Slog.w(TAG, "Missing meta-data '"
                        + TvInputManager.META_DATA_CONTENT_RATING_SYSTEMS + "' on receiver "
                        + receiver.packageName + "/" + receiver.name);
                continue;
            }
            userState.contentRatingSystemList.add(
                    TvContentRatingSystemInfo.createTvContentRatingSystemInfo(xmlResId,
                            receiver.applicationInfo));
        }
    }

    private void switchUser(int userId) {
        synchronized (mLock) {
            if (mCurrentUserId == userId) {
                return;
            }
            clearSessionAndServiceStatesLocked(mUserStates.get(mCurrentUserId));

            mCurrentUserId = userId;
            getOrCreateUserStateLocked(userId);
            buildTvInputListLocked(userId, null);
            buildTvContentRatingSystemListLocked(userId);
            mWatchLogHandler.obtainMessage(WatchLogHandler.MSG_SWITCH_CONTENT_RESOLVER,
                    getContentResolverForUser(userId)).sendToTarget();
        }
    }

    private void removeUser(int userId) {
        synchronized (mLock) {
            UserState userState = mUserStates.get(userId);
            if (userState == null) {
                return;
            }
            clearSessionAndServiceStatesLocked(userState);

            // Clear everything else.
            userState.inputMap.clear();
            userState.packageSet.clear();
            userState.contentRatingSystemList.clear();
            userState.clientStateMap.clear();
            userState.callbackSet.clear();
            userState.mainSessionToken = null;

            mUserStates.remove(userId);
        }
    }

    private void clearSessionAndServiceStatesLocked(UserState userState) {
        // Release created sessions.
        for (SessionState state : userState.sessionStateMap.values()) {
            if (state.session != null) {
                try {
                    state.session.release();
                } catch (RemoteException e) {
                    Slog.e(TAG, "error in release", e);
                }
            }
        }
        userState.sessionStateMap.clear();

        // Unregister all callbacks and unbind all services.
        for (ServiceState serviceState : userState.serviceStateMap.values()) {
            if (serviceState.callback != null) {
                try {
                    serviceState.service.unregisterCallback(serviceState.callback);
                } catch (RemoteException e) {
                    Slog.e(TAG, "error in unregisterCallback", e);
                }
            }
            mContext.unbindService(serviceState.connection);
        }
        userState.serviceStateMap.clear();
    }

    private ContentResolver getContentResolverForUser(int userId) {
        UserHandle user = new UserHandle(userId);
        Context context;
        try {
            context = mContext.createPackageContextAsUser("android", 0, user);
        } catch (NameNotFoundException e) {
            Slog.e(TAG, "failed to create package contenxt as user " + user);
            context = mContext;
        }
        return context.getContentResolver();
    }

    private UserState getOrCreateUserStateLocked(int userId) {
        UserState userState = mUserStates.get(userId);
        if (userState == null) {
            userState = new UserState(mContext, userId);
            mUserStates.put(userId, userState);
        }
        return userState;
    }

    private ServiceState getServiceStateLocked(ComponentName component, int userId) {
        UserState userState = getOrCreateUserStateLocked(userId);
        ServiceState serviceState = userState.serviceStateMap.get(component);
        if (serviceState == null) {
            throw new IllegalStateException("Service state not found for " + component + " (userId="
                    + userId + ")");
        }
        return serviceState;
    }

    private SessionState getSessionStateLocked(IBinder sessionToken, int callingUid, int userId) {
        UserState userState = getOrCreateUserStateLocked(userId);
        SessionState sessionState = userState.sessionStateMap.get(sessionToken);
        if (sessionState == null) {
            throw new SessionNotFoundException("Session state not found for token " + sessionToken);
        }
        // Only the application that requested this session or the system can access it.
        if (callingUid != Process.SYSTEM_UID && callingUid != sessionState.callingUid) {
            throw new SecurityException("Illegal access to the session with token " + sessionToken
                    + " from uid " + callingUid);
        }
        return sessionState;
    }

    private ITvInputSession getSessionLocked(IBinder sessionToken, int callingUid, int userId) {
        return getSessionLocked(getSessionStateLocked(sessionToken, callingUid, userId));
    }

    private ITvInputSession getSessionLocked(SessionState sessionState) {
        ITvInputSession session = sessionState.session;
        if (session == null) {
            throw new IllegalStateException("Session not yet created for token "
                    + sessionState.sessionToken);
        }
        return session;
    }

    private int resolveCallingUserId(int callingPid, int callingUid, int requestedUserId,
            String methodName) {
        return ActivityManager.handleIncomingUser(callingPid, callingUid, requestedUserId, false,
                false, methodName, null);
    }

    private static boolean shouldMaintainConnection(ServiceState serviceState) {
        return !serviceState.sessionTokens.isEmpty() || serviceState.isHardware;
        // TODO: Find a way to maintain connection to hardware TV input service only when necessary.
    }

    private void updateServiceConnectionLocked(ComponentName component, int userId) {
        UserState userState = getOrCreateUserStateLocked(userId);
        ServiceState serviceState = userState.serviceStateMap.get(component);
        if (serviceState == null) {
            return;
        }
        if (serviceState.reconnecting) {
            if (!serviceState.sessionTokens.isEmpty()) {
                // wait until all the sessions are removed.
                return;
            }
            serviceState.reconnecting = false;
        }
        boolean maintainConnection = shouldMaintainConnection(serviceState);
        if (serviceState.service == null && maintainConnection && userId == mCurrentUserId) {
            // This means that the service is not yet connected but its state indicates that we
            // have pending requests. Then, connect the service.
            if (serviceState.bound) {
                // We have already bound to the service so we don't try to bind again until after we
                // unbind later on.
                return;
            }
            if (DEBUG) {
                Slog.d(TAG, "bindServiceAsUser(service=" + component + ", userId=" + userId + ")");
            }

            Intent i = new Intent(TvInputService.SERVICE_INTERFACE).setComponent(component);
            serviceState.bound = mContext.bindServiceAsUser(
                    i, serviceState.connection,
                    Context.BIND_AUTO_CREATE | Context.BIND_FOREGROUND_SERVICE_WHILE_AWAKE,
                    new UserHandle(userId));
        } else if (serviceState.service != null && !maintainConnection) {
            // This means that the service is already connected but its state indicates that we have
            // nothing to do with it. Then, disconnect the service.
            if (DEBUG) {
                Slog.d(TAG, "unbindService(service=" + component + ")");
            }
            mContext.unbindService(serviceState.connection);
            userState.serviceStateMap.remove(component);
        }
    }

    private void abortPendingCreateSessionRequestsLocked(ServiceState serviceState,
            String inputId, int userId) {
        // Let clients know the create session requests are failed.
        UserState userState = getOrCreateUserStateLocked(userId);
        List<SessionState> sessionsToAbort = new ArrayList<>();
        for (IBinder sessionToken : serviceState.sessionTokens) {
            SessionState sessionState = userState.sessionStateMap.get(sessionToken);
            if (sessionState.session == null && (inputId == null
                    || sessionState.info.getId().equals(inputId))) {
                sessionsToAbort.add(sessionState);
            }
        }
        for (SessionState sessionState : sessionsToAbort) {
            removeSessionStateLocked(sessionState.sessionToken, sessionState.userId);
            sendSessionTokenToClientLocked(sessionState.client,
                    sessionState.info.getId(), null, null, sessionState.seq);
        }
        updateServiceConnectionLocked(serviceState.component, userId);
    }

    private void createSessionInternalLocked(ITvInputService service, IBinder sessionToken,
            int userId) {
        UserState userState = getOrCreateUserStateLocked(userId);
        SessionState sessionState = userState.sessionStateMap.get(sessionToken);
        if (DEBUG) {
            Slog.d(TAG, "createSessionInternalLocked(inputId=" + sessionState.info.getId() + ")");
        }
        InputChannel[] channels = InputChannel.openInputChannelPair(sessionToken.toString());

        // Set up a callback to send the session token.
        ITvInputSessionCallback callback = new SessionCallback(sessionState, channels);

        // Create a session. When failed, send a null token immediately.
        try {
            service.createSession(channels[1], callback, sessionState.info.getId());
        } catch (RemoteException e) {
            Slog.e(TAG, "error in createSession", e);
            removeSessionStateLocked(sessionToken, userId);
            sendSessionTokenToClientLocked(sessionState.client, sessionState.info.getId(), null,
                    null, sessionState.seq);
        }
        channels[1].dispose();
    }

    private void sendSessionTokenToClientLocked(ITvInputClient client, String inputId,
            IBinder sessionToken, InputChannel channel, int seq) {
        try {
            client.onSessionCreated(inputId, sessionToken, channel, seq);
        } catch (RemoteException e) {
            Slog.e(TAG, "error in onSessionCreated", e);
        }
    }

    private void releaseSessionLocked(IBinder sessionToken, int callingUid, int userId) {
        SessionState sessionState = null;
        try {
            sessionState = getSessionStateLocked(sessionToken, callingUid, userId);
            if (sessionState.session != null) {
                UserState userState = getOrCreateUserStateLocked(userId);
                if (sessionToken == userState.mainSessionToken) {
                    setMainLocked(sessionToken, false, callingUid, userId);
                }
                sessionState.session.release();
            }
        } catch (RemoteException | SessionNotFoundException e) {
            Slog.e(TAG, "error in releaseSession", e);
        } finally {
            if (sessionState != null) {
                sessionState.session = null;
            }
        }
        removeSessionStateLocked(sessionToken, userId);
    }

    private void removeSessionStateLocked(IBinder sessionToken, int userId) {
        UserState userState = getOrCreateUserStateLocked(userId);
        if (sessionToken == userState.mainSessionToken) {
            if (DEBUG) {
                Slog.d(TAG, "mainSessionToken=null");
            }
            userState.mainSessionToken = null;
        }

        // Remove the session state from the global session state map of the current user.
        SessionState sessionState = userState.sessionStateMap.remove(sessionToken);

        if (sessionState == null) {
            return;
        }

        // Also remove the session token from the session token list of the current client and
        // service.
        ClientState clientState = userState.clientStateMap.get(sessionState.client.asBinder());
        if (clientState != null) {
            clientState.sessionTokens.remove(sessionToken);
            if (clientState.isEmpty()) {
                userState.clientStateMap.remove(sessionState.client.asBinder());
            }
        }

        TvInputInfo info = sessionState.info;
        if (info != null) {
            ServiceState serviceState = userState.serviceStateMap.get(info.getComponent());
            if (serviceState != null) {
                serviceState.sessionTokens.remove(sessionToken);
            }
        }
        updateServiceConnectionLocked(sessionState.info.getComponent(), userId);

        // Log the end of watch.
        SomeArgs args = SomeArgs.obtain();
        args.arg1 = sessionToken;
        args.arg2 = System.currentTimeMillis();
        mWatchLogHandler.obtainMessage(WatchLogHandler.MSG_LOG_WATCH_END, args).sendToTarget();
    }

    private void setMainLocked(IBinder sessionToken, boolean isMain, int callingUid, int userId) {
        try {
            SessionState sessionState = getSessionStateLocked(sessionToken, callingUid, userId);
            if (sessionState.hardwareSessionToken != null) {
                sessionState = getSessionStateLocked(sessionState.hardwareSessionToken,
                        Process.SYSTEM_UID, userId);
            }
            ServiceState serviceState = getServiceStateLocked(sessionState.info.getComponent(), userId);
            if (!serviceState.isHardware) {
                return;
            }
            ITvInputSession session = getSessionLocked(sessionState);
            session.setMain(isMain);
        } catch (RemoteException | SessionNotFoundException e) {
            Slog.e(TAG, "error in setMain", e);
        }
    }

    private void notifyInputAddedLocked(UserState userState, String inputId) {
        if (DEBUG) {
            Slog.d(TAG, "notifyInputAddedLocked(inputId=" + inputId + ")");
        }
        for (ITvInputManagerCallback callback : userState.callbackSet) {
            try {
                callback.onInputAdded(inputId);
            } catch (RemoteException e) {
                Slog.e(TAG, "failed to report added input to callback", e);
            }
        }
    }

    private void notifyInputRemovedLocked(UserState userState, String inputId) {
        if (DEBUG) {
            Slog.d(TAG, "notifyInputRemovedLocked(inputId=" + inputId + ")");
        }
        for (ITvInputManagerCallback callback : userState.callbackSet) {
            try {
                callback.onInputRemoved(inputId);
            } catch (RemoteException e) {
                Slog.e(TAG, "failed to report removed input to callback", e);
            }
        }
    }

    private void notifyInputUpdatedLocked(UserState userState, String inputId) {
        if (DEBUG) {
            Slog.d(TAG, "notifyInputUpdatedLocked(inputId=" + inputId + ")");
        }
        for (ITvInputManagerCallback callback : userState.callbackSet) {
            try {
                callback.onInputUpdated(inputId);
            } catch (RemoteException e) {
                Slog.e(TAG, "failed to report updated input to callback", e);
            }
        }
    }

    private void notifyInputStateChangedLocked(UserState userState, String inputId,
            int state, ITvInputManagerCallback targetCallback) {
        if (DEBUG) {
            Slog.d(TAG, "notifyInputStateChangedLocked(inputId=" + inputId
                    + ", state=" + state + ")");
        }
        if (targetCallback == null) {
            for (ITvInputManagerCallback callback : userState.callbackSet) {
                try {
                    callback.onInputStateChanged(inputId, state);
                } catch (RemoteException e) {
                    Slog.e(TAG, "failed to report state change to callback", e);
                }
            }
        } else {
            try {
                targetCallback.onInputStateChanged(inputId, state);
            } catch (RemoteException e) {
                Slog.e(TAG, "failed to report state change to callback", e);
            }
        }
    }

    private void setStateLocked(String inputId, int state, int userId) {
        UserState userState = getOrCreateUserStateLocked(userId);
        TvInputState inputState = userState.inputMap.get(inputId);
        ServiceState serviceState = userState.serviceStateMap.get(inputState.info.getComponent());
        int oldState = inputState.state;
        inputState.state = state;
        if (serviceState != null && serviceState.service == null
                && shouldMaintainConnection(serviceState)) {
            // We don't notify state change while reconnecting. It should remain disconnected.
            return;
        }
        if (oldState != state) {
            notifyInputStateChangedLocked(userState, inputId, state, null);
        }
    }

    private final class BinderService extends ITvInputManager.Stub {
        @Override
        public List<TvInputInfo> getTvInputList(int userId) {
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(),
                    Binder.getCallingUid(), userId, "getTvInputList");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    UserState userState = getOrCreateUserStateLocked(resolvedUserId);
                    List<TvInputInfo> inputList = new ArrayList<>();
                    for (TvInputState state : userState.inputMap.values()) {
                        inputList.add(state.info);
                    }
                    return inputList;
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public TvInputInfo getTvInputInfo(String inputId, int userId) {
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(),
                    Binder.getCallingUid(), userId, "getTvInputInfo");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    UserState userState = getOrCreateUserStateLocked(resolvedUserId);
                    TvInputState state = userState.inputMap.get(inputId);
                    return state == null ? null : state.info;
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public int getTvInputState(String inputId, int userId) {
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(),
                    Binder.getCallingUid(), userId, "getTvInputState");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    UserState userState = getOrCreateUserStateLocked(resolvedUserId);
                    TvInputState state = userState.inputMap.get(inputId);
                    return state == null ? INPUT_STATE_CONNECTED : state.state;
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public List<TvContentRatingSystemInfo> getTvContentRatingSystemList(int userId) {
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(),
                    Binder.getCallingUid(), userId, "getTvContentRatingSystemList");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    UserState userState = getOrCreateUserStateLocked(resolvedUserId);
                    return userState.contentRatingSystemList;
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public void registerCallback(final ITvInputManagerCallback callback, int userId) {
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(),
                    Binder.getCallingUid(), userId, "registerCallback");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    final UserState userState = getOrCreateUserStateLocked(resolvedUserId);
                    userState.callbackSet.add(callback);
                    try {
                        callback.asBinder().linkToDeath(new IBinder.DeathRecipient() {
                            @Override
                            public void binderDied() {
                                synchronized (mLock) {
                                    if (userState.callbackSet != null) {
                                        userState.callbackSet.remove(callback);
                                    }
                                }
                            }
                        }, 0);
                    } catch (RemoteException e) {
                        Slog.e(TAG, "client process has already died", e);
                    }
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public void unregisterCallback(ITvInputManagerCallback callback, int userId) {
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(),
                    Binder.getCallingUid(), userId, "unregisterCallback");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    UserState userState = getOrCreateUserStateLocked(resolvedUserId);
                    userState.callbackSet.remove(callback);
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public boolean isParentalControlsEnabled(int userId) {
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(),
                    Binder.getCallingUid(), userId, "isParentalControlsEnabled");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    UserState userState = getOrCreateUserStateLocked(resolvedUserId);
                    return userState.persistentDataStore.isParentalControlsEnabled();
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public void setParentalControlsEnabled(boolean enabled, int userId) {
            ensureParentalControlsPermission();
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(),
                    Binder.getCallingUid(), userId, "setParentalControlsEnabled");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    UserState userState = getOrCreateUserStateLocked(resolvedUserId);
                    userState.persistentDataStore.setParentalControlsEnabled(enabled);
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public boolean isRatingBlocked(String rating, int userId) {
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(),
                    Binder.getCallingUid(), userId, "isRatingBlocked");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    UserState userState = getOrCreateUserStateLocked(resolvedUserId);
                    return userState.persistentDataStore.isRatingBlocked(
                            TvContentRating.unflattenFromString(rating));
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public List<String> getBlockedRatings(int userId) {
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(),
                    Binder.getCallingUid(), userId, "getBlockedRatings");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    UserState userState = getOrCreateUserStateLocked(resolvedUserId);
                    List<String> ratings = new ArrayList<>();
                    for (TvContentRating rating
                            : userState.persistentDataStore.getBlockedRatings()) {
                        ratings.add(rating.flattenToString());
                    }
                    return ratings;
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public void addBlockedRating(String rating, int userId) {
            ensureParentalControlsPermission();
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(),
                    Binder.getCallingUid(), userId, "addBlockedRating");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    UserState userState = getOrCreateUserStateLocked(resolvedUserId);
                    userState.persistentDataStore.addBlockedRating(
                            TvContentRating.unflattenFromString(rating));
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public void removeBlockedRating(String rating, int userId) {
            ensureParentalControlsPermission();
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(),
                    Binder.getCallingUid(), userId, "removeBlockedRating");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    UserState userState = getOrCreateUserStateLocked(resolvedUserId);
                    userState.persistentDataStore.removeBlockedRating(
                            TvContentRating.unflattenFromString(rating));
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        private void ensureParentalControlsPermission() {
            if (mContext.checkCallingPermission(
                    android.Manifest.permission.MODIFY_PARENTAL_CONTROLS)
                    != PackageManager.PERMISSION_GRANTED) {
                throw new SecurityException(
                        "The caller does not have parental controls permission");
            }
        }

        @Override
        public void createSession(final ITvInputClient client, final String inputId,
                int seq, int userId) {
            final int callingUid = Binder.getCallingUid();
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(), callingUid,
                    userId, "createSession");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    UserState userState = getOrCreateUserStateLocked(resolvedUserId);
                    TvInputState inputState = userState.inputMap.get(inputId);
                    if (inputState == null) {
                        Slog.w(TAG, "Failed to find input state for inputId=" + inputId);
                        sendSessionTokenToClientLocked(client, inputId, null, null, seq);
                        return;
                    }
                    TvInputInfo info = inputState.info;
                    ServiceState serviceState = userState.serviceStateMap.get(info.getComponent());
                    if (serviceState == null) {
                        serviceState = new ServiceState(info.getComponent(), resolvedUserId);
                        userState.serviceStateMap.put(info.getComponent(), serviceState);
                    }
                    // Send a null token immediately while reconnecting.
                    if (serviceState.reconnecting) {
                        sendSessionTokenToClientLocked(client, inputId, null, null, seq);
                        return;
                    }

                    // Create a new session token and a session state.
                    IBinder sessionToken = new Binder();
                    SessionState sessionState = new SessionState(sessionToken, info, client,
                            seq, callingUid, resolvedUserId);

                    // Add them to the global session state map of the current user.
                    userState.sessionStateMap.put(sessionToken, sessionState);

                    // Also, add them to the session state map of the current service.
                    serviceState.sessionTokens.add(sessionToken);

                    if (serviceState.service != null) {
                        createSessionInternalLocked(serviceState.service, sessionToken,
                                resolvedUserId);
                    } else {
                        updateServiceConnectionLocked(info.getComponent(), resolvedUserId);
                    }
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public void releaseSession(IBinder sessionToken, int userId) {
            if (DEBUG) {
                Slog.d(TAG, "releaseSession(sessionToken=" + sessionToken + ")");
            }
            final int callingUid = Binder.getCallingUid();
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(), callingUid,
                    userId, "releaseSession");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    releaseSessionLocked(sessionToken, callingUid, resolvedUserId);
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public void setMainSession(IBinder sessionToken, int userId) {
            if (DEBUG) {
                Slog.d(TAG, "setMainSession(sessionToken=" + sessionToken + ")");
            }
            final int callingUid = Binder.getCallingUid();
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(), callingUid,
                    userId, "setMainSession");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    UserState userState = getOrCreateUserStateLocked(resolvedUserId);
                    if (userState.mainSessionToken == sessionToken) {
                        return;
                    }
                    if (DEBUG) {
                        Slog.d(TAG, "mainSessionToken=" + sessionToken);
                    }
                    IBinder oldMainSessionToken = userState.mainSessionToken;
                    userState.mainSessionToken = sessionToken;

                    // Inform the new main session first.
                    // See {@link TvInputService.Session#onSetMain}.
                    if (sessionToken != null) {
                        setMainLocked(sessionToken, true, callingUid, userId);
                    }
                    if (oldMainSessionToken != null) {
                        setMainLocked(oldMainSessionToken, false, Process.SYSTEM_UID, userId);
                    }
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public void setSurface(IBinder sessionToken, Surface surface, int userId) {
            final int callingUid = Binder.getCallingUid();
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(), callingUid,
                    userId, "setSurface");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    try {
                        SessionState sessionState = getSessionStateLocked(sessionToken, callingUid,
                                resolvedUserId);
                        if (sessionState.hardwareSessionToken == null) {
                            getSessionLocked(sessionState).setSurface(surface);
                        } else {
                            getSessionLocked(sessionState.hardwareSessionToken,
                                    Process.SYSTEM_UID, resolvedUserId).setSurface(surface);
                        }
                    } catch (RemoteException | SessionNotFoundException e) {
                        Slog.e(TAG, "error in setSurface", e);
                    }
                }
            } finally {
                if (surface != null) {
                    // surface is not used in TvInputManagerService.
                    surface.release();
                }
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public void dispatchSurfaceChanged(IBinder sessionToken, int format, int width,
                int height, int userId) {
            final int callingUid = Binder.getCallingUid();
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(), callingUid,
                    userId, "dispatchSurfaceChanged");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    try {
                        SessionState sessionState = getSessionStateLocked(sessionToken, callingUid,
                                resolvedUserId);
                        getSessionLocked(sessionState).dispatchSurfaceChanged(format, width,
                                height);
                        if (sessionState.hardwareSessionToken != null) {
                            getSessionLocked(sessionState.hardwareSessionToken, Process.SYSTEM_UID,
                                    resolvedUserId).dispatchSurfaceChanged(format, width, height);
                        }
                    } catch (RemoteException | SessionNotFoundException e) {
                        Slog.e(TAG, "error in dispatchSurfaceChanged", e);
                    }
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public void setVolume(IBinder sessionToken, float volume, int userId) {
            final float REMOTE_VOLUME_ON = 1.0f;
            final float REMOTE_VOLUME_OFF = 0f;
            final int callingUid = Binder.getCallingUid();
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(), callingUid,
                    userId, "setVolume");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    try {
                        SessionState sessionState = getSessionStateLocked(sessionToken, callingUid,
                                resolvedUserId);
                        getSessionLocked(sessionState).setVolume(volume);
                        if (sessionState.hardwareSessionToken != null) {
                            // Here, we let the hardware session know only whether volume is on or
                            // off to prevent that the volume is controlled in the both side.
                            getSessionLocked(sessionState.hardwareSessionToken,
                                    Process.SYSTEM_UID, resolvedUserId).setVolume((volume > 0.0f)
                                            ? REMOTE_VOLUME_ON : REMOTE_VOLUME_OFF);
                        }
                    } catch (RemoteException | SessionNotFoundException e) {
                        Slog.e(TAG, "error in setVolume", e);
                    }
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public void tune(IBinder sessionToken, final Uri channelUri, Bundle params, int userId) {
            final int callingUid = Binder.getCallingUid();
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(), callingUid,
                    userId, "tune");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    try {
                        getSessionLocked(sessionToken, callingUid, resolvedUserId).tune(
                                channelUri, params);
                        if (TvContract.isChannelUriForPassthroughInput(channelUri)) {
                            // Do not log the watch history for passthrough inputs.
                            return;
                        }

                        UserState userState = getOrCreateUserStateLocked(resolvedUserId);
                        SessionState sessionState = userState.sessionStateMap.get(sessionToken);

                        // Log the start of watch.
                        SomeArgs args = SomeArgs.obtain();
                        args.arg1 = sessionState.info.getComponent().getPackageName();
                        args.arg2 = System.currentTimeMillis();
                        args.arg3 = ContentUris.parseId(channelUri);
                        args.arg4 = params;
                        args.arg5 = sessionToken;
                        mWatchLogHandler.obtainMessage(WatchLogHandler.MSG_LOG_WATCH_START, args)
                                .sendToTarget();
                    } catch (RemoteException | SessionNotFoundException e) {
                        Slog.e(TAG, "error in tune", e);
                    }
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public void unblockContent(
                IBinder sessionToken, String unblockedRating, int userId) {
            final int callingUid = Binder.getCallingUid();
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(), callingUid,
                    userId, "unblockContent");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    try {
                        getSessionLocked(sessionToken, callingUid, resolvedUserId)
                                .unblockContent(unblockedRating);
                    } catch (RemoteException | SessionNotFoundException e) {
                        Slog.e(TAG, "error in unblockContent", e);
                    }
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public void setCaptionEnabled(IBinder sessionToken, boolean enabled, int userId) {
            final int callingUid = Binder.getCallingUid();
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(), callingUid,
                    userId, "setCaptionEnabled");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    try {
                        getSessionLocked(sessionToken, callingUid, resolvedUserId)
                                .setCaptionEnabled(enabled);
                    } catch (RemoteException | SessionNotFoundException e) {
                        Slog.e(TAG, "error in setCaptionEnabled", e);
                    }
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public void selectTrack(IBinder sessionToken, int type, String trackId, int userId) {
            final int callingUid = Binder.getCallingUid();
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(), callingUid,
                    userId, "selectTrack");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    try {
                        getSessionLocked(sessionToken, callingUid, resolvedUserId).selectTrack(
                                type, trackId);
                    } catch (RemoteException | SessionNotFoundException e) {
                        Slog.e(TAG, "error in selectTrack", e);
                    }
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public void sendAppPrivateCommand(IBinder sessionToken, String command, Bundle data,
                int userId) {
            final int callingUid = Binder.getCallingUid();
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(), callingUid,
                    userId, "sendAppPrivateCommand");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    try {
                        getSessionLocked(sessionToken, callingUid, resolvedUserId)
                                .appPrivateCommand(command, data);
                    } catch (RemoteException | SessionNotFoundException e) {
                        Slog.e(TAG, "error in appPrivateCommand", e);
                    }
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public void createOverlayView(IBinder sessionToken, IBinder windowToken, Rect frame,
                int userId) {
            final int callingUid = Binder.getCallingUid();
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(), callingUid,
                    userId, "createOverlayView");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    try {
                        getSessionLocked(sessionToken, callingUid, resolvedUserId)
                                .createOverlayView(windowToken, frame);
                    } catch (RemoteException | SessionNotFoundException e) {
                        Slog.e(TAG, "error in createOverlayView", e);
                    }
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public void relayoutOverlayView(IBinder sessionToken, Rect frame, int userId) {
            final int callingUid = Binder.getCallingUid();
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(), callingUid,
                    userId, "relayoutOverlayView");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    try {
                        getSessionLocked(sessionToken, callingUid, resolvedUserId)
                                .relayoutOverlayView(frame);
                    } catch (RemoteException | SessionNotFoundException e) {
                        Slog.e(TAG, "error in relayoutOverlayView", e);
                    }
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public void removeOverlayView(IBinder sessionToken, int userId) {
            final int callingUid = Binder.getCallingUid();
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(), callingUid,
                    userId, "removeOverlayView");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    try {
                        getSessionLocked(sessionToken, callingUid, resolvedUserId)
                                .removeOverlayView();
                    } catch (RemoteException | SessionNotFoundException e) {
                        Slog.e(TAG, "error in removeOverlayView", e);
                    }
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public void timeShiftPause(IBinder sessionToken, int userId) {
            final int callingUid = Binder.getCallingUid();
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(), callingUid,
                    userId, "timeShiftPause");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    try {
                        getSessionLocked(sessionToken, callingUid, resolvedUserId)
                                .timeShiftPause();
                    } catch (RemoteException | SessionNotFoundException e) {
                        Slog.e(TAG, "error in timeShiftPause", e);
                    }
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public void timeShiftResume(IBinder sessionToken, int userId) {
            final int callingUid = Binder.getCallingUid();
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(), callingUid,
                    userId, "timeShiftResume");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    try {
                        getSessionLocked(sessionToken, callingUid, resolvedUserId)
                                .timeShiftResume();
                    } catch (RemoteException | SessionNotFoundException e) {
                        Slog.e(TAG, "error in timeShiftResume", e);
                    }
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public void timeShiftSeekTo(IBinder sessionToken, long timeMs, int userId) {
            final int callingUid = Binder.getCallingUid();
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(), callingUid,
                    userId, "timeShiftSeekTo");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    try {
                        getSessionLocked(sessionToken, callingUid, resolvedUserId)
                                .timeShiftSeekTo(timeMs);
                    } catch (RemoteException | SessionNotFoundException e) {
                        Slog.e(TAG, "error in timeShiftSeekTo", e);
                    }
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public void timeShiftSetPlaybackParams(IBinder sessionToken, PlaybackParams params,
                int userId) {
            final int callingUid = Binder.getCallingUid();
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(), callingUid,
                    userId, "timeShiftSetPlaybackParams");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    try {
                        getSessionLocked(sessionToken, callingUid, resolvedUserId)
                                .timeShiftSetPlaybackParams(params);
                    } catch (RemoteException | SessionNotFoundException e) {
                        Slog.e(TAG, "error in timeShiftSetPlaybackParams", e);
                    }
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public void timeShiftEnablePositionTracking(IBinder sessionToken, boolean enable,
                int userId) {
            final int callingUid = Binder.getCallingUid();
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(), callingUid,
                    userId, "timeShiftEnablePositionTracking");
            final long identity = Binder.clearCallingIdentity();
            try {
                synchronized (mLock) {
                    try {
                        getSessionLocked(sessionToken, callingUid, resolvedUserId)
                                .timeShiftEnablePositionTracking(enable);
                    } catch (RemoteException | SessionNotFoundException e) {
                        Slog.e(TAG, "error in timeShiftEnablePositionTracking", e);
                    }
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public List<TvInputHardwareInfo> getHardwareList() throws RemoteException {
            if (mContext.checkCallingPermission(android.Manifest.permission.TV_INPUT_HARDWARE)
                    != PackageManager.PERMISSION_GRANTED) {
                return null;
            }

            final long identity = Binder.clearCallingIdentity();
            try {
                return mTvInputHardwareManager.getHardwareList();
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public ITvInputHardware acquireTvInputHardware(int deviceId,
                ITvInputHardwareCallback callback, TvInputInfo info, int userId)
                throws RemoteException {
            if (mContext.checkCallingPermission(android.Manifest.permission.TV_INPUT_HARDWARE)
                    != PackageManager.PERMISSION_GRANTED) {
                return null;
            }

            final long identity = Binder.clearCallingIdentity();
            final int callingUid = Binder.getCallingUid();
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(), callingUid,
                    userId, "acquireTvInputHardware");
            try {
                return mTvInputHardwareManager.acquireHardware(
                        deviceId, callback, info, callingUid, resolvedUserId);
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public void releaseTvInputHardware(int deviceId, ITvInputHardware hardware, int userId)
                throws RemoteException {
            if (mContext.checkCallingPermission(android.Manifest.permission.TV_INPUT_HARDWARE)
                    != PackageManager.PERMISSION_GRANTED) {
                return;
            }

            final long identity = Binder.clearCallingIdentity();
            final int callingUid = Binder.getCallingUid();
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(), callingUid,
                    userId, "releaseTvInputHardware");
            try {
                mTvInputHardwareManager.releaseHardware(
                        deviceId, hardware, callingUid, resolvedUserId);
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public List<DvbDeviceInfo> getDvbDeviceList() throws RemoteException {
            if (mContext.checkCallingPermission(android.Manifest.permission.DVB_DEVICE)
                    != PackageManager.PERMISSION_GRANTED) {
                throw new SecurityException("Requires DVB_DEVICE permission");
            }

            final long identity = Binder.clearCallingIdentity();
            try {
                ArrayList<DvbDeviceInfo> deviceInfos = new ArrayList<>();
                File devDirectory = new File("/dev");
                for (String fileName : devDirectory.list()) {
                    Matcher matcher = sFrontEndDevicePattern.matcher(fileName);
                    if (matcher.find()) {
                        int adapterId = Integer.parseInt(matcher.group(1));
                        int deviceId = Integer.parseInt(matcher.group(2));
                        deviceInfos.add(new DvbDeviceInfo(adapterId, deviceId));
                    }
                }
                return Collections.unmodifiableList(deviceInfos);
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public ParcelFileDescriptor openDvbDevice(DvbDeviceInfo info, int device)
                throws RemoteException {
            if (mContext.checkCallingPermission(android.Manifest.permission.DVB_DEVICE)
                    != PackageManager.PERMISSION_GRANTED) {
                throw new SecurityException("Requires DVB_DEVICE permission");
            }

            final long identity = Binder.clearCallingIdentity();
            try {
                String deviceFileName;
                switch (device) {
                    case TvInputManager.DVB_DEVICE_DEMUX:
                        deviceFileName = String.format("/dev/dvb%d.demux%d", info.getAdapterId(),
                                info.getDeviceId());
                        break;
                    case TvInputManager.DVB_DEVICE_DVR:
                        deviceFileName = String.format("/dev/dvb%d.dvr%d", info.getAdapterId(),
                                info.getDeviceId());
                        break;
                    case TvInputManager.DVB_DEVICE_FRONTEND:
                        deviceFileName = String.format("/dev/dvb%d.frontend%d", info.getAdapterId(),
                                info.getDeviceId());
                        break;
                    default:
                        throw new IllegalArgumentException("Invalid DVB device: " + device);
                }
                try {
                    // The DVB frontend device only needs to be opened in read/write mode, which
                    // allows performing tuning operations. The DVB demux and DVR device are enough
                    // to be opened in read only mode.
                    return ParcelFileDescriptor.open(new File(deviceFileName),
                            TvInputManager.DVB_DEVICE_FRONTEND == device
                                    ? ParcelFileDescriptor.MODE_READ_WRITE
                                    : ParcelFileDescriptor.MODE_READ_ONLY);
                } catch (FileNotFoundException e) {
                    return null;
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public List<TvStreamConfig> getAvailableTvStreamConfigList(String inputId, int userId)
                throws RemoteException {
            if (mContext.checkCallingPermission(
                    android.Manifest.permission.CAPTURE_TV_INPUT)
                    != PackageManager.PERMISSION_GRANTED) {
                throw new SecurityException("Requires CAPTURE_TV_INPUT permission");
            }

            final long identity = Binder.clearCallingIdentity();
            final int callingUid = Binder.getCallingUid();
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(), callingUid,
                    userId, "getAvailableTvStreamConfigList");
            try {
                return mTvInputHardwareManager.getAvailableTvStreamConfigList(
                        inputId, callingUid, resolvedUserId);
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public boolean captureFrame(String inputId, Surface surface, TvStreamConfig config,
                int userId)
                throws RemoteException {
            if (mContext.checkCallingPermission(
                    android.Manifest.permission.CAPTURE_TV_INPUT)
                    != PackageManager.PERMISSION_GRANTED) {
                throw new SecurityException("Requires CAPTURE_TV_INPUT permission");
            }

            final long identity = Binder.clearCallingIdentity();
            final int callingUid = Binder.getCallingUid();
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(), callingUid,
                    userId, "captureFrame");
            try {
                String hardwareInputId = null;
                synchronized (mLock) {
                    UserState userState = getOrCreateUserStateLocked(resolvedUserId);
                    if (userState.inputMap.get(inputId) == null) {
                        Slog.e(TAG, "input not found for " + inputId);
                        return false;
                    }
                    for (SessionState sessionState : userState.sessionStateMap.values()) {
                        if (sessionState.info.getId().equals(inputId)
                                && sessionState.hardwareSessionToken != null) {
                            hardwareInputId = userState.sessionStateMap.get(
                                    sessionState.hardwareSessionToken).info.getId();
                            break;
                        }
                    }
                }
                return mTvInputHardwareManager.captureFrame(
                        (hardwareInputId != null) ? hardwareInputId : inputId,
                        surface, config, callingUid, resolvedUserId);
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        public boolean isSingleSessionActive(int userId) throws RemoteException {
            final long identity = Binder.clearCallingIdentity();
            final int callingUid = Binder.getCallingUid();
            final int resolvedUserId = resolveCallingUserId(Binder.getCallingPid(), callingUid,
                    userId, "isSingleSessionActive");
            try {
                synchronized (mLock) {
                    UserState userState = getOrCreateUserStateLocked(resolvedUserId);
                    if (userState.sessionStateMap.size() == 1) {
                        return true;
                    } else if (userState.sessionStateMap.size() == 2) {
                        SessionState[] sessionStates = userState.sessionStateMap.values().toArray(
                                new SessionState[2]);
                        // Check if there is a wrapper input.
                        if (sessionStates[0].hardwareSessionToken != null
                                || sessionStates[1].hardwareSessionToken != null) {
                            return true;
                        }
                    }
                    return false;
                }
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        @Override
        @SuppressWarnings("resource")
        protected void dump(FileDescriptor fd, final PrintWriter writer, String[] args) {
            final IndentingPrintWriter pw = new IndentingPrintWriter(writer, "  ");
            if (mContext.checkCallingOrSelfPermission(android.Manifest.permission.DUMP)
                    != PackageManager.PERMISSION_GRANTED) {
                pw.println("Permission Denial: can't dump TvInputManager from pid="
                        + Binder.getCallingPid() + ", uid=" + Binder.getCallingUid());
                return;
            }

            synchronized (mLock) {
                pw.println("User Ids (Current user: " + mCurrentUserId + "):");
                pw.increaseIndent();
                for (int i = 0; i < mUserStates.size(); i++) {
                    int userId = mUserStates.keyAt(i);
                    pw.println(Integer.valueOf(userId));
                }
                pw.decreaseIndent();

                for (int i = 0; i < mUserStates.size(); i++) {
                    int userId = mUserStates.keyAt(i);
                    UserState userState = getOrCreateUserStateLocked(userId);
                    pw.println("UserState (" + userId + "):");
                    pw.increaseIndent();

                    pw.println("inputMap: inputId -> TvInputState");
                    pw.increaseIndent();
                    for (Map.Entry<String, TvInputState> entry: userState.inputMap.entrySet()) {
                        pw.println(entry.getKey() + ": " + entry.getValue());
                    }
                    pw.decreaseIndent();

                    pw.println("packageSet:");
                    pw.increaseIndent();
                    for (String packageName : userState.packageSet) {
                        pw.println(packageName);
                    }
                    pw.decreaseIndent();

                    pw.println("clientStateMap: ITvInputClient -> ClientState");
                    pw.increaseIndent();
                    for (Map.Entry<IBinder, ClientState> entry :
                            userState.clientStateMap.entrySet()) {
                        ClientState client = entry.getValue();
                        pw.println(entry.getKey() + ": " + client);

                        pw.increaseIndent();

                        pw.println("sessionTokens:");
                        pw.increaseIndent();
                        for (IBinder token : client.sessionTokens) {
                            pw.println("" + token);
                        }
                        pw.decreaseIndent();

                        pw.println("clientTokens: " + client.clientToken);
                        pw.println("userId: " + client.userId);

                        pw.decreaseIndent();
                    }
                    pw.decreaseIndent();

                    pw.println("serviceStateMap: ComponentName -> ServiceState");
                    pw.increaseIndent();
                    for (Map.Entry<ComponentName, ServiceState> entry :
                            userState.serviceStateMap.entrySet()) {
                        ServiceState service = entry.getValue();
                        pw.println(entry.getKey() + ": " + service);

                        pw.increaseIndent();

                        pw.println("sessionTokens:");
                        pw.increaseIndent();
                        for (IBinder token : service.sessionTokens) {
                            pw.println("" + token);
                        }
                        pw.decreaseIndent();

                        pw.println("service: " + service.service);
                        pw.println("callback: " + service.callback);
                        pw.println("bound: " + service.bound);
                        pw.println("reconnecting: " + service.reconnecting);

                        pw.decreaseIndent();
                    }
                    pw.decreaseIndent();

                    pw.println("sessionStateMap: ITvInputSession -> SessionState");
                    pw.increaseIndent();
                    for (Map.Entry<IBinder, SessionState> entry :
                            userState.sessionStateMap.entrySet()) {
                        SessionState session = entry.getValue();
                        pw.println(entry.getKey() + ": " + session);

                        pw.increaseIndent();
                        pw.println("info: " + session.info);
                        pw.println("client: " + session.client);
                        pw.println("seq: " + session.seq);
                        pw.println("callingUid: " + session.callingUid);
                        pw.println("userId: " + session.userId);
                        pw.println("sessionToken: " + session.sessionToken);
                        pw.println("session: " + session.session);
                        pw.println("logUri: " + session.logUri);
                        pw.println("hardwareSessionToken: " + session.hardwareSessionToken);
                        pw.decreaseIndent();
                    }
                    pw.decreaseIndent();

                    pw.println("callbackSet:");
                    pw.increaseIndent();
                    for (ITvInputManagerCallback callback : userState.callbackSet) {
                        pw.println(callback.toString());
                    }
                    pw.decreaseIndent();

                    pw.println("mainSessionToken: " + userState.mainSessionToken);
                    pw.decreaseIndent();
                }
            }
        }
    }

    private static final class UserState {
        // A mapping from the TV input id to its TvInputState.
        private Map<String, TvInputState> inputMap = new HashMap<>();

        // A set of all TV input packages.
        private final Set<String> packageSet = new HashSet<>();

        // A list of all TV content rating systems defined.
        private final List<TvContentRatingSystemInfo>
                contentRatingSystemList = new ArrayList<>();

        // A mapping from the token of a client to its state.
        private final Map<IBinder, ClientState> clientStateMap = new HashMap<>();

        // A mapping from the name of a TV input service to its state.
        private final Map<ComponentName, ServiceState> serviceStateMap = new HashMap<>();

        // A mapping from the token of a TV input session to its state.
        private final Map<IBinder, SessionState> sessionStateMap = new HashMap<>();

        // A set of callbacks.
        private final Set<ITvInputManagerCallback> callbackSet = new HashSet<>();

        // The token of a "main" TV input session.
        private IBinder mainSessionToken = null;

        // Persistent data store for all internal settings maintained by the TV input manager
        // service.
        private final PersistentDataStore persistentDataStore;

        private UserState(Context context, int userId) {
            persistentDataStore = new PersistentDataStore(context, userId);
        }
    }

    private final class ClientState implements IBinder.DeathRecipient {
        private final List<IBinder> sessionTokens = new ArrayList<>();

        private IBinder clientToken;
        private final int userId;

        ClientState(IBinder clientToken, int userId) {
            this.clientToken = clientToken;
            this.userId = userId;
        }

        public boolean isEmpty() {
            return sessionTokens.isEmpty();
        }

        @Override
        public void binderDied() {
            synchronized (mLock) {
                UserState userState = getOrCreateUserStateLocked(userId);
                // DO NOT remove the client state of clientStateMap in this method. It will be
                // removed in releaseSessionLocked().
                ClientState clientState = userState.clientStateMap.get(clientToken);
                if (clientState != null) {
                    while (clientState.sessionTokens.size() > 0) {
                        releaseSessionLocked(
                                clientState.sessionTokens.get(0), Process.SYSTEM_UID, userId);
                    }
                }
                clientToken = null;
            }
        }
    }

    private final class ServiceState {
        private final List<IBinder> sessionTokens = new ArrayList<>();
        private final ServiceConnection connection;
        private final ComponentName component;
        private final boolean isHardware;
        private final List<TvInputInfo> inputList = new ArrayList<>();

        private ITvInputService service;
        private ServiceCallback callback;
        private boolean bound;
        private boolean reconnecting;

        private ServiceState(ComponentName component, int userId) {
            this.component = component;
            this.connection = new InputServiceConnection(component, userId);
            this.isHardware = hasHardwarePermission(mContext.getPackageManager(), component);
        }
    }

    private static final class TvInputState {
        // A TvInputInfo object which represents the TV input.
        private TvInputInfo info;

        // The state of TV input. Connected by default.
        private int state = INPUT_STATE_CONNECTED;

        @Override
        public String toString() {
            return "info: " + info + "; state: " + state;
        }
    }

    private final class SessionState implements IBinder.DeathRecipient {
        private final TvInputInfo info;
        private final ITvInputClient client;
        private final int seq;
        private final int callingUid;
        private final int userId;
        private final IBinder sessionToken;
        private ITvInputSession session;
        private Uri logUri;
        // Not null if this session represents an external device connected to a hardware TV input.
        private IBinder hardwareSessionToken;

        private SessionState(IBinder sessionToken, TvInputInfo info, ITvInputClient client,
                int seq, int callingUid, int userId) {
            this.sessionToken = sessionToken;
            this.info = info;
            this.client = client;
            this.seq = seq;
            this.callingUid = callingUid;
            this.userId = userId;
        }

        @Override
        public void binderDied() {
            synchronized (mLock) {
                session = null;
                if (client != null) {
                    try {
                        client.onSessionReleased(seq);
                    } catch(RemoteException e) {
                        Slog.e(TAG, "error in onSessionReleased", e);
                    }
                }
                // If there are any other sessions based on this session, they should be released.
                UserState userState = getOrCreateUserStateLocked(userId);
                for (SessionState sessionState : userState.sessionStateMap.values()) {
                    if (sessionToken == sessionState.hardwareSessionToken) {
                        releaseSessionLocked(sessionState.sessionToken, Process.SYSTEM_UID,
                                userId);
                        try {
                            sessionState.client.onSessionReleased(sessionState.seq);
                        } catch (RemoteException e) {
                            Slog.e(TAG, "error in onSessionReleased", e);
                        }
                    }
                }
                removeSessionStateLocked(sessionToken, userId);
            }
        }
    }

    private final class InputServiceConnection implements ServiceConnection {
        private final ComponentName mComponent;
        private final int mUserId;

        private InputServiceConnection(ComponentName component, int userId) {
            mComponent = component;
            mUserId = userId;
        }

        @Override
        public void onServiceConnected(ComponentName component, IBinder service) {
            if (DEBUG) {
                Slog.d(TAG, "onServiceConnected(component=" + component + ")");
            }
            synchronized (mLock) {
                UserState userState = getOrCreateUserStateLocked(mUserId);
                ServiceState serviceState = userState.serviceStateMap.get(mComponent);
                serviceState.service = ITvInputService.Stub.asInterface(service);

                // Register a callback, if we need to.
                if (serviceState.isHardware && serviceState.callback == null) {
                    serviceState.callback = new ServiceCallback(mComponent, mUserId);
                    try {
                        serviceState.service.registerCallback(serviceState.callback);
                    } catch (RemoteException e) {
                        Slog.e(TAG, "error in registerCallback", e);
                    }
                }

                // And create sessions, if any.
                for (IBinder sessionToken : serviceState.sessionTokens) {
                    createSessionInternalLocked(serviceState.service, sessionToken, mUserId);
                }

                for (TvInputState inputState : userState.inputMap.values()) {
                    if (inputState.info.getComponent().equals(component)
                            && inputState.state != INPUT_STATE_CONNECTED) {
                        notifyInputStateChangedLocked(userState, inputState.info.getId(),
                                inputState.state, null);
                    }
                }

                if (serviceState.isHardware) {
                    List<TvInputHardwareInfo> hardwareInfoList =
                            mTvInputHardwareManager.getHardwareList();
                    for (TvInputHardwareInfo hardwareInfo : hardwareInfoList) {
                        try {
                            serviceState.service.notifyHardwareAdded(hardwareInfo);
                        } catch (RemoteException e) {
                            Slog.e(TAG, "error in notifyHardwareAdded", e);
                        }
                    }

                    List<HdmiDeviceInfo> deviceInfoList =
                            mTvInputHardwareManager.getHdmiDeviceList();
                    for (HdmiDeviceInfo deviceInfo : deviceInfoList) {
                        try {
                            serviceState.service.notifyHdmiDeviceAdded(deviceInfo);
                        } catch (RemoteException e) {
                            Slog.e(TAG, "error in notifyHdmiDeviceAdded", e);
                        }
                    }
                }
            }
        }

        @Override
        public void onServiceDisconnected(ComponentName component) {
            if (DEBUG) {
                Slog.d(TAG, "onServiceDisconnected(component=" + component + ")");
            }
            if (!mComponent.equals(component)) {
                throw new IllegalArgumentException("Mismatched ComponentName: "
                        + mComponent + " (expected), " + component + " (actual).");
            }
            synchronized (mLock) {
                UserState userState = getOrCreateUserStateLocked(mUserId);
                ServiceState serviceState = userState.serviceStateMap.get(mComponent);
                if (serviceState != null) {
                    serviceState.reconnecting = true;
                    serviceState.bound = false;
                    serviceState.service = null;
                    serviceState.callback = null;

                    abortPendingCreateSessionRequestsLocked(serviceState, null, mUserId);
                }
            }
        }
    }

    private final class ServiceCallback extends ITvInputServiceCallback.Stub {
        private final ComponentName mComponent;
        private final int mUserId;

        ServiceCallback(ComponentName component, int userId) {
            mComponent = component;
            mUserId = userId;
        }

        private void ensureHardwarePermission() {
            if (mContext.checkCallingPermission(android.Manifest.permission.TV_INPUT_HARDWARE)
                    != PackageManager.PERMISSION_GRANTED) {
                throw new SecurityException("The caller does not have hardware permission");
            }
        }

        private void ensureValidInput(TvInputInfo inputInfo) {
            if (inputInfo.getId() == null || !mComponent.equals(inputInfo.getComponent())) {
                throw new IllegalArgumentException("Invalid TvInputInfo");
            }
        }

        private void addTvInputLocked(TvInputInfo inputInfo) {
            ServiceState serviceState = getServiceStateLocked(mComponent, mUserId);
            serviceState.inputList.add(inputInfo);
            buildTvInputListLocked(mUserId, null);
        }

        @Override
        public void addHardwareTvInput(int deviceId, TvInputInfo inputInfo) {
            ensureHardwarePermission();
            ensureValidInput(inputInfo);
            synchronized (mLock) {
                mTvInputHardwareManager.addHardwareTvInput(deviceId, inputInfo);
                addTvInputLocked(inputInfo);
            }
        }

        @Override
        public void addHdmiTvInput(int id, TvInputInfo inputInfo) {
            ensureHardwarePermission();
            ensureValidInput(inputInfo);
            synchronized (mLock) {
                mTvInputHardwareManager.addHdmiTvInput(id, inputInfo);
                addTvInputLocked(inputInfo);
            }
        }

        @Override
        public void removeTvInput(String inputId) {
            ensureHardwarePermission();
            synchronized (mLock) {
                ServiceState serviceState = getServiceStateLocked(mComponent, mUserId);
                boolean removed = false;
                for (Iterator<TvInputInfo> it = serviceState.inputList.iterator();
                        it.hasNext(); ) {
                    if (it.next().getId().equals(inputId)) {
                        it.remove();
                        removed = true;
                        break;
                    }
                }
                if (removed) {
                    buildTvInputListLocked(mUserId, null);
                    mTvInputHardwareManager.removeTvInput(inputId);
                } else {
                    Slog.e(TAG, "failed to remove input " + inputId);
                }
            }
        }
    }

    private final class SessionCallback extends ITvInputSessionCallback.Stub {
        private final SessionState mSessionState;
        private final InputChannel[] mChannels;

        SessionCallback(SessionState sessionState, InputChannel[] channels) {
            mSessionState = sessionState;
            mChannels = channels;
        }

        @Override
        public void onSessionCreated(ITvInputSession session, IBinder hardwareSessionToken) {
            if (DEBUG) {
                Slog.d(TAG, "onSessionCreated(inputId=" + mSessionState.info.getId() + ")");
            }
            synchronized (mLock) {
                mSessionState.session = session;
                mSessionState.hardwareSessionToken = hardwareSessionToken;
                if (session != null && addSessionTokenToClientStateLocked(session)) {
                    sendSessionTokenToClientLocked(mSessionState.client,
                            mSessionState.info.getId(), mSessionState.sessionToken, mChannels[0],
                            mSessionState.seq);
                } else {
                    removeSessionStateLocked(mSessionState.sessionToken, mSessionState.userId);
                    sendSessionTokenToClientLocked(mSessionState.client,
                            mSessionState.info.getId(), null, null, mSessionState.seq);
                }
                mChannels[0].dispose();
            }
        }

        private boolean addSessionTokenToClientStateLocked(ITvInputSession session) {
            try {
                session.asBinder().linkToDeath(mSessionState, 0);
            } catch (RemoteException e) {
                Slog.e(TAG, "session process has already died", e);
                return false;
            }

            IBinder clientToken = mSessionState.client.asBinder();
            UserState userState = getOrCreateUserStateLocked(mSessionState.userId);
            ClientState clientState = userState.clientStateMap.get(clientToken);
            if (clientState == null) {
                clientState = new ClientState(clientToken, mSessionState.userId);
                try {
                    clientToken.linkToDeath(clientState, 0);
                } catch (RemoteException e) {
                    Slog.e(TAG, "client process has already died", e);
                    return false;
                }
                userState.clientStateMap.put(clientToken, clientState);
            }
            clientState.sessionTokens.add(mSessionState.sessionToken);
            return true;
        }

        @Override
        public void onChannelRetuned(Uri channelUri) {
            synchronized (mLock) {
                if (DEBUG) {
                    Slog.d(TAG, "onChannelRetuned(" + channelUri + ")");
                }
                if (mSessionState.session == null || mSessionState.client == null) {
                    return;
                }
                try {
                    // TODO: Consider adding this channel change in the watch log. When we do
                    // that, how we can protect the watch log from malicious tv inputs should
                    // be addressed. e.g. add a field which represents where the channel change
                    // originated from.
                    mSessionState.client.onChannelRetuned(channelUri, mSessionState.seq);
                } catch (RemoteException e) {
                    Slog.e(TAG, "error in onChannelRetuned", e);
                }
            }
        }

        @Override
        public void onTracksChanged(List<TvTrackInfo> tracks) {
            synchronized (mLock) {
                if (DEBUG) {
                    Slog.d(TAG, "onTracksChanged(" + tracks + ")");
                }
                if (mSessionState.session == null || mSessionState.client == null) {
                    return;
                }
                try {
                    mSessionState.client.onTracksChanged(tracks, mSessionState.seq);
                } catch (RemoteException e) {
                    Slog.e(TAG, "error in onTracksChanged", e);
                }
            }
        }

        @Override
        public void onTrackSelected(int type, String trackId) {
            synchronized (mLock) {
                if (DEBUG) {
                    Slog.d(TAG, "onTrackSelected(type=" + type + ", trackId=" + trackId + ")");
                }
                if (mSessionState.session == null || mSessionState.client == null) {
                    return;
                }
                try {
                    mSessionState.client.onTrackSelected(type, trackId, mSessionState.seq);
                } catch (RemoteException e) {
                    Slog.e(TAG, "error in onTrackSelected", e);
                }
            }
        }

        @Override
        public void onVideoAvailable() {
            synchronized (mLock) {
                if (DEBUG) {
                    Slog.d(TAG, "onVideoAvailable()");
                }
                if (mSessionState.session == null || mSessionState.client == null) {
                    return;
                }
                try {
                    mSessionState.client.onVideoAvailable(mSessionState.seq);
                } catch (RemoteException e) {
                    Slog.e(TAG, "error in onVideoAvailable", e);
                }
            }
        }

        @Override
        public void onVideoUnavailable(int reason) {
            synchronized (mLock) {
                if (DEBUG) {
                    Slog.d(TAG, "onVideoUnavailable(" + reason + ")");
                }
                if (mSessionState.session == null || mSessionState.client == null) {
                    return;
                }
                try {
                    mSessionState.client.onVideoUnavailable(reason, mSessionState.seq);
                } catch (RemoteException e) {
                    Slog.e(TAG, "error in onVideoUnavailable", e);
                }
            }
        }

        @Override
        public void onContentAllowed() {
            synchronized (mLock) {
                if (DEBUG) {
                    Slog.d(TAG, "onContentAllowed()");
                }
                if (mSessionState.session == null || mSessionState.client == null) {
                    return;
                }
                try {
                    mSessionState.client.onContentAllowed(mSessionState.seq);
                } catch (RemoteException e) {
                    Slog.e(TAG, "error in onContentAllowed", e);
                }
            }
        }

        @Override
        public void onContentBlocked(String rating) {
            synchronized (mLock) {
                if (DEBUG) {
                    Slog.d(TAG, "onContentBlocked()");
                }
                if (mSessionState.session == null || mSessionState.client == null) {
                    return;
                }
                try {
                    mSessionState.client.onContentBlocked(rating, mSessionState.seq);
                } catch (RemoteException e) {
                    Slog.e(TAG, "error in onContentBlocked", e);
                }
            }
        }

        @Override
        public void onLayoutSurface(int left, int top, int right, int bottom) {
            synchronized (mLock) {
                if (DEBUG) {
                    Slog.d(TAG, "onLayoutSurface (left=" + left + ", top=" + top
                            + ", right=" + right + ", bottom=" + bottom + ",)");
                }
                if (mSessionState.session == null || mSessionState.client == null) {
                    return;
                }
                try {
                    mSessionState.client.onLayoutSurface(left, top, right, bottom,
                            mSessionState.seq);
                } catch (RemoteException e) {
                    Slog.e(TAG, "error in onLayoutSurface", e);
                }
            }
        }

        @Override
        public void onSessionEvent(String eventType, Bundle eventArgs) {
            synchronized (mLock) {
                if (DEBUG) {
                    Slog.d(TAG, "onEvent(what=" + eventType + ", data=" + eventArgs + ")");
                }
                if (mSessionState.session == null || mSessionState.client == null) {
                    return;
                }
                try {
                    mSessionState.client.onSessionEvent(eventType, eventArgs, mSessionState.seq);
                } catch (RemoteException e) {
                    Slog.e(TAG, "error in onSessionEvent", e);
                }
            }
        }

        @Override
        public void onTimeShiftStatusChanged(int status) {
            synchronized (mLock) {
                if (DEBUG) {
                    Slog.d(TAG, "onTimeShiftStatusChanged()");
                }
                if (mSessionState.session == null || mSessionState.client == null) {
                    return;
                }
                try {
                    mSessionState.client.onTimeShiftStatusChanged(status, mSessionState.seq);
                } catch (RemoteException e) {
                    Slog.e(TAG, "error in onTimeShiftStatusChanged", e);
                }
            }
        }

        @Override
        public void onTimeShiftStartPositionChanged(long timeMs) {
            synchronized (mLock) {
                if (DEBUG) {
                    Slog.d(TAG, "onTimeShiftStartPositionChanged()");
                }
                if (mSessionState.session == null || mSessionState.client == null) {
                    return;
                }
                try {
                    mSessionState.client.onTimeShiftStartPositionChanged(timeMs, mSessionState.seq);
                } catch (RemoteException e) {
                    Slog.e(TAG, "error in onTimeShiftStartPositionChanged", e);
                }
            }
        }

        @Override
        public void onTimeShiftCurrentPositionChanged(long timeMs) {
            synchronized (mLock) {
                if (DEBUG) {
                    Slog.d(TAG, "onTimeShiftCurrentPositionChanged()");
                }
                if (mSessionState.session == null || mSessionState.client == null) {
                    return;
                }
                try {
                    mSessionState.client.onTimeShiftCurrentPositionChanged(timeMs,
                            mSessionState.seq);
                } catch (RemoteException e) {
                    Slog.e(TAG, "error in onTimeShiftCurrentPositionChanged", e);
                }
            }
        }
    }

    private static final class WatchLogHandler extends Handler {
        // There are only two kinds of watch events that can happen on the system:
        // 1. The current TV input session is tuned to a new channel.
        // 2. The session is released for some reason.
        // The former indicates the end of the previous log entry, if any, followed by the start of
        // a new entry. The latter indicates the end of the most recent entry for the given session.
        // Here the system supplies the database the smallest set of information only that is
        // sufficient to consolidate the log entries while minimizing database operations in the
        // system service.
        static final int MSG_LOG_WATCH_START = 1;
        static final int MSG_LOG_WATCH_END = 2;
        static final int MSG_SWITCH_CONTENT_RESOLVER = 3;

        private ContentResolver mContentResolver;

        WatchLogHandler(ContentResolver contentResolver, Looper looper) {
            super(looper);
            mContentResolver = contentResolver;
        }

        @Override
        public void handleMessage(Message msg) {
            switch (msg.what) {
                case MSG_LOG_WATCH_START: {
                    SomeArgs args = (SomeArgs) msg.obj;
                    String packageName = (String) args.arg1;
                    long watchStartTime = (long) args.arg2;
                    long channelId = (long) args.arg3;
                    Bundle tuneParams = (Bundle) args.arg4;
                    IBinder sessionToken = (IBinder) args.arg5;

                    ContentValues values = new ContentValues();
                    values.put(TvContract.WatchedPrograms.COLUMN_PACKAGE_NAME, packageName);
                    values.put(TvContract.WatchedPrograms.COLUMN_WATCH_START_TIME_UTC_MILLIS,
                            watchStartTime);
                    values.put(TvContract.WatchedPrograms.COLUMN_CHANNEL_ID, channelId);
                    if (tuneParams != null) {
                        values.put(TvContract.WatchedPrograms.COLUMN_INTERNAL_TUNE_PARAMS,
                                encodeTuneParams(tuneParams));
                    }
                    values.put(TvContract.WatchedPrograms.COLUMN_INTERNAL_SESSION_TOKEN,
                            sessionToken.toString());

                    mContentResolver.insert(TvContract.WatchedPrograms.CONTENT_URI, values);
                    args.recycle();
                    break;
                }
                case MSG_LOG_WATCH_END: {
                    SomeArgs args = (SomeArgs) msg.obj;
                    IBinder sessionToken = (IBinder) args.arg1;
                    long watchEndTime = (long) args.arg2;

                    ContentValues values = new ContentValues();
                    values.put(TvContract.WatchedPrograms.COLUMN_WATCH_END_TIME_UTC_MILLIS,
                            watchEndTime);
                    values.put(TvContract.WatchedPrograms.COLUMN_INTERNAL_SESSION_TOKEN,
                            sessionToken.toString());

                    mContentResolver.insert(TvContract.WatchedPrograms.CONTENT_URI, values);
                    args.recycle();
                    break;
                }
                case MSG_SWITCH_CONTENT_RESOLVER: {
                    mContentResolver = (ContentResolver) msg.obj;
                    break;
                }
                default: {
                    Slog.w(TAG, "unhandled message code: " + msg.what);
                    break;
                }
            }
        }

        private String encodeTuneParams(Bundle tuneParams) {
            StringBuilder builder = new StringBuilder();
            Set<String> keySet = tuneParams.keySet();
            Iterator<String> it = keySet.iterator();
            while (it.hasNext()) {
                String key = it.next();
                Object value = tuneParams.get(key);
                if (value == null) {
                    continue;
                }
                builder.append(replaceEscapeCharacters(key));
                builder.append("=");
                builder.append(replaceEscapeCharacters(value.toString()));
                if (it.hasNext()) {
                    builder.append(", ");
                }
            }
            return builder.toString();
        }

        private String replaceEscapeCharacters(String src) {
            final char ESCAPE_CHARACTER = '%';
            final String ENCODING_TARGET_CHARACTERS = "%=,";
            StringBuilder builder = new StringBuilder();
            for (char ch : src.toCharArray()) {
                if (ENCODING_TARGET_CHARACTERS.indexOf(ch) >= 0) {
                    builder.append(ESCAPE_CHARACTER);
                }
                builder.append(ch);
            }
            return builder.toString();
        }
    }

    private final class HardwareListener implements TvInputHardwareManager.Listener {
        @Override
        public void onStateChanged(String inputId, int state) {
            synchronized (mLock) {
                setStateLocked(inputId, state, mCurrentUserId);
            }
        }

        @Override
        public void onHardwareDeviceAdded(TvInputHardwareInfo info) {
            synchronized (mLock) {
                UserState userState = getOrCreateUserStateLocked(mCurrentUserId);
                // Broadcast the event to all hardware inputs.
                for (ServiceState serviceState : userState.serviceStateMap.values()) {
                    if (!serviceState.isHardware || serviceState.service == null) continue;
                    try {
                        serviceState.service.notifyHardwareAdded(info);
                    } catch (RemoteException e) {
                        Slog.e(TAG, "error in notifyHardwareAdded", e);
                    }
                }
            }
        }

        @Override
        public void onHardwareDeviceRemoved(TvInputHardwareInfo info) {
            synchronized (mLock) {
                UserState userState = getOrCreateUserStateLocked(mCurrentUserId);
                // Broadcast the event to all hardware inputs.
                for (ServiceState serviceState : userState.serviceStateMap.values()) {
                    if (!serviceState.isHardware || serviceState.service == null) continue;
                    try {
                        serviceState.service.notifyHardwareRemoved(info);
                    } catch (RemoteException e) {
                        Slog.e(TAG, "error in notifyHardwareRemoved", e);
                    }
                }
            }
        }

        @Override
        public void onHdmiDeviceAdded(HdmiDeviceInfo deviceInfo) {
            synchronized (mLock) {
                UserState userState = getOrCreateUserStateLocked(mCurrentUserId);
                // Broadcast the event to all hardware inputs.
                for (ServiceState serviceState : userState.serviceStateMap.values()) {
                    if (!serviceState.isHardware || serviceState.service == null) continue;
                    try {
                        serviceState.service.notifyHdmiDeviceAdded(deviceInfo);
                    } catch (RemoteException e) {
                        Slog.e(TAG, "error in notifyHdmiDeviceAdded", e);
                    }
                }
            }
        }

        @Override
        public void onHdmiDeviceRemoved(HdmiDeviceInfo deviceInfo) {
            synchronized (mLock) {
                UserState userState = getOrCreateUserStateLocked(mCurrentUserId);
                // Broadcast the event to all hardware inputs.
                for (ServiceState serviceState : userState.serviceStateMap.values()) {
                    if (!serviceState.isHardware || serviceState.service == null) continue;
                    try {
                        serviceState.service.notifyHdmiDeviceRemoved(deviceInfo);
                    } catch (RemoteException e) {
                        Slog.e(TAG, "error in notifyHdmiDeviceRemoved", e);
                    }
                }
            }
        }

        @Override
        public void onHdmiDeviceUpdated(String inputId, HdmiDeviceInfo deviceInfo) {
            synchronized (mLock) {
                Integer state;
                switch (deviceInfo.getDevicePowerStatus()) {
                    case HdmiControlManager.POWER_STATUS_ON:
                        state = INPUT_STATE_CONNECTED;
                        break;
                    case HdmiControlManager.POWER_STATUS_STANDBY:
                    case HdmiControlManager.POWER_STATUS_TRANSIENT_TO_ON:
                    case HdmiControlManager.POWER_STATUS_TRANSIENT_TO_STANDBY:
                        state = INPUT_STATE_CONNECTED_STANDBY;
                        break;
                    case HdmiControlManager.POWER_STATUS_UNKNOWN:
                    default:
                        state = null;
                        break;
                }
                if (state != null) {
                    setStateLocked(inputId, state, mCurrentUserId);
                }
            }
        }
    }

    private static class SessionNotFoundException extends IllegalArgumentException {
        public SessionNotFoundException(String name) {
            super(name);
        }
    }
}
