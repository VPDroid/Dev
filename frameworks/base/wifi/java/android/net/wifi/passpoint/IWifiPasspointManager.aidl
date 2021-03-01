/**
 * Copyright (c) 2014, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package android.net.wifi.passpoint;

import android.net.wifi.ScanResult;
import android.net.wifi.passpoint.WifiPasspointPolicy;
import android.net.wifi.passpoint.WifiPasspointCredential;
import android.os.Messenger;

/**
 * Interface that allows controlling and querying Wifi Passpoint connectivity.
 *
 * {@hide}
 */
interface IWifiPasspointManager
{
    Messenger getMessenger();

    int getPasspointState();

    List<WifiPasspointPolicy> requestCredentialMatch(in List<ScanResult> requested);

    List<WifiPasspointCredential> getCredentials();

    boolean addCredential(in WifiPasspointCredential cred);

    boolean updateCredential(in WifiPasspointCredential cred);

    boolean removeCredential(in WifiPasspointCredential cred);
}

