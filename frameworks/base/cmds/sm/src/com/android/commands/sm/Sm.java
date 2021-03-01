/*
 * Copyright (C) 2015 The Android Open Source Project
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

package com.android.commands.sm;

import android.os.RemoteException;
import android.os.ServiceManager;
import android.os.SystemProperties;
import android.os.storage.DiskInfo;
import android.os.storage.IMountService;
import android.os.storage.StorageManager;
import android.os.storage.VolumeInfo;
import android.util.Log;

public final class Sm {
    private static final String TAG = "Sm";

    IMountService mSm;

    private String[] mArgs;
    private int mNextArg;
    private String mCurArgData;

    public static void main(String[] args) {
        boolean success = false;
        try {
            new Sm().run(args);
            success = true;
        } catch (Exception e) {
            if (e instanceof IllegalArgumentException) {
                showUsage();
            }
            Log.e(TAG, "Error", e);
            System.err.println("Error: " + e);
        }
        System.exit(success ? 0 : 1);
    }

    public void run(String[] args) throws Exception {
        if (args.length < 1) {
            throw new IllegalArgumentException();
        }

        mSm = IMountService.Stub.asInterface(ServiceManager.getService("mount"));
        if (mSm == null) {
            throw new RemoteException("Failed to find running mount service");
        }

        mArgs = args;
        String op = args[0];
        mNextArg = 1;

        if ("list-disks".equals(op)) {
            runListDisks();
        } else if ("list-volumes".equals(op)) {
            runListVolumes();
        } else if ("has-adoptable".equals(op)) {
            runHasAdoptable();
        } else if ("get-primary-storage-uuid".equals(op)) {
            runGetPrimaryStorageUuid();
        } else if ("set-force-adoptable".equals(op)) {
            runSetForceAdoptable();
        } else if ("partition".equals(op)) {
            runPartition();
        } else if ("mount".equals(op)) {
            runMount();
        } else if ("unmount".equals(op)) {
            runUnmount();
        } else if ("format".equals(op)) {
            runFormat();
        } else if ("benchmark".equals(op)) {
            runBenchmark();
        } else if ("forget".equals(op)) {
            runForget();
        } else {
            throw new IllegalArgumentException();
        }
    }

    public void runListDisks() throws RemoteException {
        final boolean onlyAdoptable = "adoptable".equals(nextArg());
        final DiskInfo[] disks = mSm.getDisks();
        for (DiskInfo disk : disks) {
            if (!onlyAdoptable || disk.isAdoptable()) {
                System.out.println(disk.getId());
            }
        }
    }

    public void runListVolumes() throws RemoteException {
        final String filter = nextArg();
        final int filterType;
        if ("public".equals(filter)) {
            filterType = VolumeInfo.TYPE_PUBLIC;
        } else if ("private".equals(filter)) {
            filterType = VolumeInfo.TYPE_PRIVATE;
        } else if ("emulated".equals(filter)) {
            filterType = VolumeInfo.TYPE_EMULATED;
        } else {
            filterType = -1;
        }

        final VolumeInfo[] vols = mSm.getVolumes(0);
        for (VolumeInfo vol : vols) {
            if (filterType == -1 || filterType == vol.getType()) {
                final String envState = VolumeInfo.getEnvironmentForState(vol.getState());
                System.out.println(vol.getId() + " " + envState + " " + vol.getFsUuid());
            }
        }
    }

    public void runHasAdoptable() {
        System.out.println(SystemProperties.getBoolean(StorageManager.PROP_HAS_ADOPTABLE, false));
    }

    public void runGetPrimaryStorageUuid() throws RemoteException {
        System.out.println(mSm.getPrimaryStorageUuid());
    }

    public void runSetForceAdoptable() throws RemoteException {
        final boolean forceAdoptable = Boolean.parseBoolean(nextArg());
        mSm.setDebugFlags(forceAdoptable ? StorageManager.DEBUG_FORCE_ADOPTABLE : 0,
                StorageManager.DEBUG_FORCE_ADOPTABLE);
    }

    public void runPartition() throws RemoteException {
        final String diskId = nextArg();
        final String type = nextArg();
        if ("public".equals(type)) {
            mSm.partitionPublic(diskId);
        } else if ("private".equals(type)) {
            mSm.partitionPrivate(diskId);
        } else if ("mixed".equals(type)) {
            final int ratio = Integer.parseInt(nextArg());
            mSm.partitionMixed(diskId, ratio);
        } else {
            throw new IllegalArgumentException("Unsupported partition type " + type);
        }
    }

    public void runMount() throws RemoteException {
        final String volId = nextArg();
        mSm.mount(volId);
    }

    public void runUnmount() throws RemoteException {
        final String volId = nextArg();
        mSm.unmount(volId);
    }

    public void runFormat() throws RemoteException {
        final String volId = nextArg();
        mSm.format(volId);
    }

    public void runBenchmark() throws RemoteException {
        final String volId = nextArg();
        mSm.benchmark(volId);
    }

    public void runForget() throws RemoteException{
        final String fsUuid = nextArg();
        if ("all".equals(fsUuid)) {
            mSm.forgetAllVolumes();
        } else {
            mSm.forgetVolume(fsUuid);
        }
    }

    private String nextArg() {
        if (mNextArg >= mArgs.length) {
            return null;
        }
        String arg = mArgs[mNextArg];
        mNextArg++;
        return arg;
    }

    private static int showUsage() {
        System.err.println("usage: sm list-disks [adoptable]");
        System.err.println("       sm list-volumes [public|private|emulated|all]");
        System.err.println("       sm has-adoptable");
        System.err.println("       sm get-primary-storage-uuid");
        System.err.println("       sm set-force-adoptable [true|false]");
        System.err.println("");
        System.err.println("       sm partition DISK [public|private|mixed] [ratio]");
        System.err.println("       sm mount VOLUME");
        System.err.println("       sm unmount VOLUME");
        System.err.println("       sm format VOLUME");
        System.err.println("       sm benchmark VOLUME");
        System.err.println("");
        System.err.println("       sm forget [UUID|all]");
        System.err.println("");
        return 1;
    }
}
