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

package android.net.xdja;

import java.io.FileDescriptor;
import java.net.InetAddress;
import java.net.Inet4Address;
import java.net.Inet6Address;
import java.net.SocketException;
import java.net.UnknownHostException;
import java.util.Collection;
import java.util.Locale;

import android.os.Parcel;
import android.util.Log;
import android.util.Pair;

import android.os.ParcelFileDescriptor;
import android.net.DhcpResults;
import android.net.xdja.IXdjaNetworkUtils;
import android.os.RemoteException;
import android.os.Binder;
import android.util.Log;

/**
 * Native methods for managing network interfaces.
 *
 * {@hide}
 */
public class XdjaNetworkUtils extends IXdjaNetworkUtils.Stub {

    private boolean DBG = false;

    private final String TAG = "XdjaNetworkUtils";
    /**
     * Reset IPv6 or IPv4 sockets that are connected via the named interface.
     *
     * @param interfaceName is the interface to reset
     * @param mask {@see #RESET_IPV4_ADDRESSES} and {@see #RESET_IPV6_ADDRESSES}
     */
    public native static int resetConnections(String interfaceName, int mask);
    @Override
    public synchronized int doResetConnections(String interfaceName, int mask) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doResetConnections SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return resetConnections(interfaceName,mask);
    }

    /**
     * Start the DHCP client daemon, in order to have it request addresses
     * for the named interface.  This returns {@code true} if the DHCPv4 daemon
     * starts, {@code false} otherwise.  This call blocks until such time as a
     * result is available or the default discovery timeout has been reached.
     * Callers should check {@link #getDhcpResults} to determine whether DHCP
     * succeeded or failed, and if it succeeded, to fetch the {@link DhcpResults}.
     * @param interfaceName the name of the interface to configure
     * @return {@code true} for success, {@code false} for failure
     */
    public native static boolean startDhcp(String interfaceName);
    @Override
    public synchronized boolean doStartDhcp(String interfaceName) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doStartDhcp SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return startDhcp(interfaceName);
    }

    /**
     * Initiate renewal on the DHCP client daemon for the named interface.  This
     * returns {@code true} if the DHCPv4 daemon has been notified, {@code false}
     * otherwise.  This call blocks until such time as a result is available or
     * the default renew timeout has been reached.  Callers should check
     * {@link #getDhcpResults} to determine whether DHCP succeeded or failed,
     * and if it succeeded, to fetch the {@link DhcpResults}.
     * @param interfaceName the name of the interface to configure
     * @return {@code true} for success, {@code false} for failure
     */
    public native static boolean startDhcpRenew(String interfaceName);
    @Override
    public synchronized boolean doStartDhcpRenew(String interfaceName) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doStartDhcpRenew SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return startDhcpRenew(interfaceName);
    }

    /**
     * Fetch results from the DHCP client daemon. This call returns {@code true} if
     * if there are results available to be read, {@code false} otherwise.
     * @param interfaceName the name of the interface to configure
     * @param dhcpResults if the request succeeds, this object is filled in with
     * the IP address information.
     * @return {@code true} for success, {@code false} for failure
     */
    public native static boolean getDhcpResults(String interfaceName, DhcpResults dhcpResults);
    @Override
    public synchronized boolean doGetDhcpResults(String interfaceName, DhcpResults dhcpResults) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doGetDhcpResults SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return getDhcpResults(interfaceName,dhcpResults);
    }

    /**
     * Shut down the DHCP client daemon.
     * @param interfaceName the name of the interface for which the daemon
     * should be stopped
     * @return {@code true} for success, {@code false} for failure
     */
    public native static boolean stopDhcp(String interfaceName);
    @Override
    public synchronized boolean doStopDhcp(String interfaceName) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doStopDhcp SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return stopDhcp(interfaceName);
    }

    /**
     * Release the current DHCP lease.
     * @param interfaceName the name of the interface for which the lease should
     * be released
     * @return {@code true} for success, {@code false} for failure
     */
    public native static boolean releaseDhcpLease(String interfaceName);
    @Override
    public synchronized boolean doReleaseDhcpLease(String interfaceName) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doReleaseDhcpLease SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return releaseDhcpLease(interfaceName);
    }

    /**
     * Return the last DHCP-related error message that was recorded.
     * <p/>NOTE: This string is not localized, but currently it is only
     * used in logging.
     * @return the most recent error message, if any
     */
    public native static String getDhcpError();
    @Override
    public synchronized String doGetDhcpError() throws RemoteException {
        if(DBG)
            Log.d(TAG, "doGetDhcpError SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return getDhcpError();
    }

    /**
     * Attaches a socket filter that accepts DHCP packets to the given socket.
     */
    public native static void attachDhcpFilter(FileDescriptor fd) throws SocketException;
    @Override
    public synchronized void doAttachDhcpFilter(ParcelFileDescriptor fd) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doAttachDhcpFilter SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        try {
            attachDhcpFilter(fd!=null?fd.getFileDescriptor():null);
        } catch (SocketException e) {
            throw new RemoteException("attachDhcpFilter RemoteException");
        }
    }

    /**
     * Binds the current process to the network designated by {@code netId}.  All sockets created
     * in the future (and not explicitly bound via a bound {@link SocketFactory} (see
     * {@link Network#getSocketFactory}) will be bound to this network.  Note that if this
     * {@code Network} ever disconnects all sockets created in this way will cease to work.  This
     * is by design so an application doesn't accidentally use sockets it thinks are still bound to
     * a particular {@code Network}.  Passing NETID_UNSET clears the binding.
     */
    public native static boolean bindProcessToNetwork(int netId);
    @Override
    public synchronized boolean doBindProcessToNetwork(int netId) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doBindProcessToNetwork SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return bindProcessToNetwork(netId);
    }

    /**
     * Return the netId last passed to {@link #bindProcessToNetwork}, or NETID_UNSET if
     * {@link #unbindProcessToNetwork} has been called since {@link #bindProcessToNetwork}.
     */
    public native static int getBoundNetworkForProcess();
    @Override
    public synchronized int doGetBoundNetworkForProcess() throws RemoteException {
        if(DBG)
            Log.d(TAG, "doGetBoundNetworkForProcess SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return getBoundNetworkForProcess();
    }

    /**
     * Binds host resolutions performed by this process to the network designated by {@code netId}.
     * {@link #bindProcessToNetwork} takes precedence over this setting.  Passing NETID_UNSET clears
     * the binding.
     *
     * @deprecated This is strictly for legacy usage to support startUsingNetworkFeature().
     */
    public native static boolean bindProcessToNetworkForHostResolution(int netId);
    @Override
    public synchronized boolean doBindProcessToNetworkForHostResolution(int netId) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doBindProcessToNetworkForHostResolution SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return bindProcessToNetworkForHostResolution(netId);
    }

    /**
     * Explicitly binds {@code socketfd} to the network designated by {@code netId}.  This
     * overrides any binding via {@link #bindProcessToNetwork}.
     * @return 0 on success or negative errno on failure.
     */
    public native static int bindSocketToNetwork(int socketfd, int netId);
    @Override
    public synchronized int doBindSocketToNetwork(int socketfd, int netId) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doBindSocketToNetwork SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return bindSocketToNetwork(socketfd,netId);
    }

    /**
     * Protect {@code socketfd} from VPN connections.  After protecting, data sent through
     * this socket will go directly to the underlying network, so its traffic will not be
     * forwarded through the VPN.
     */
    public native static boolean protectFromVpn(int socketfd);
    @Override
    public synchronized boolean doProtectFromVpn(int socketfd) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doProtectFromVpn SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return protectFromVpn(socketfd);
    }

    /**
     * Determine if {@code uid} can access network designated by {@code netId}.
     * @return {@code true} if {@code uid} can access network, {@code false} otherwise.
     */
    public native static boolean queryUserAccess(int uid, int netId);
    @Override
    public synchronized boolean doQueryUserAccess(int uid, int netId) throws RemoteException {
        if(DBG)
            Log.d(TAG, "doQueryUserAccess SystemMode: " + Binder.getCallingSystemMode() + " Throwable:" + Log.getStackTraceString(new Throwable()));

        return queryUserAccess(uid,netId);
    }

}
