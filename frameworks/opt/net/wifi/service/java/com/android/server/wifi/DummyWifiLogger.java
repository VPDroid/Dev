
package com.android.server.wifi;

import android.util.Log;

import java.io.FileDescriptor;
import java.io.PrintWriter;

class DummyWifiLogger {

    public DummyWifiLogger() { }

    public synchronized void startLogging(boolean verboseEnabled) { }

    public synchronized void startPacketLog() { }

    public synchronized void stopPacketLog() { }

    public synchronized void stopLogging() { }

    public synchronized void captureBugReportData(int reason) { }

    public synchronized void captureAlertData(int errorCode, byte[] alertData) { }

    public synchronized void dump(FileDescriptor fd, PrintWriter pw, String[] args) {
        pw.println("*** firmware logging disabled, no debug data ****");
        pw.println("set config_wifi_enable_wifi_firmware_debugging to enable");
    }
}