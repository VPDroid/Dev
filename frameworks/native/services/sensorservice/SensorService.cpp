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

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <cutils/properties.h>

#include <utils/SortedVector.h>
#include <utils/KeyedVector.h>
#include <utils/threads.h>
#include <utils/Atomic.h>
#include <utils/Errors.h>
#include <utils/RefBase.h>
#include <utils/Singleton.h>
#include <utils/String16.h>

#include <binder/AppOpsManager.h>
#include <binder/BinderService.h>
#include <binder/IServiceManager.h>
#include <binder/PermissionCache.h>

#include <gui/ISensorServer.h>
#include <gui/ISensorEventConnection.h>
#include <gui/SensorEventQueue.h>

#include <hardware/sensors.h>
#include <hardware_legacy/power.h>

#include "BatteryService.h"
#include "CorrectedGyroSensor.h"
#include "GravitySensor.h"
#include "LinearAccelerationSensor.h"
#include "OrientationSensor.h"
#include "RotationVectorSensor.h"
#include "SensorFusion.h"
#include "SensorService.h"

namespace android {
// ---------------------------------------------------------------------------

/*
 * Notes:
 *
 * - what about a gyro-corrected magnetic-field sensor?
 * - run mag sensor from time to time to force calibration
 * - gravity sensor length is wrong (=> drift in linear-acc sensor)
 *
 */

const char* SensorService::WAKE_LOCK_NAME = "SensorService_wakelock";
// Permissions.
static const String16 sDump("android.permission.DUMP");

SensorService::SensorService()
    : mInitCheck(NO_INIT), mSocketBufferSize(SOCKET_BUFFER_SIZE_NON_BATCHED),
      mWakeLockAcquired(false)
{
}

void SensorService::onFirstRef()
{
    ALOGD("nuSensorService starting...");
    SensorDevice& dev(SensorDevice::getInstance());

    if (dev.initCheck() == NO_ERROR) {
        sensor_t const* list;
        ssize_t count = dev.getSensorList(&list);
        if (count > 0) {
            ssize_t orientationIndex = -1;
            bool hasGyro = false, hasAccel = false, hasMag = false;
            uint32_t virtualSensorsNeeds =
                    (1<<SENSOR_TYPE_GRAVITY) |
                    (1<<SENSOR_TYPE_LINEAR_ACCELERATION) |
                    (1<<SENSOR_TYPE_ROTATION_VECTOR);

            mLastEventSeen.setCapacity(count);
            for (ssize_t i=0 ; i<count ; i++) {
                registerSensor( new HardwareSensor(list[i]) );
                switch (list[i].type) {
                    case SENSOR_TYPE_ACCELEROMETER:
                        hasAccel = true;
                        break;
                    case SENSOR_TYPE_MAGNETIC_FIELD:
                        hasMag = true;
                        break;
                    case SENSOR_TYPE_ORIENTATION:
                        orientationIndex = i;
                        break;
                    case SENSOR_TYPE_GYROSCOPE:
                    case SENSOR_TYPE_GYROSCOPE_UNCALIBRATED:
                        hasGyro = true;
                        break;
                    case SENSOR_TYPE_GRAVITY:
                    case SENSOR_TYPE_LINEAR_ACCELERATION:
                    case SENSOR_TYPE_ROTATION_VECTOR:
                        virtualSensorsNeeds &= ~(1<<list[i].type);
                        break;
                }
            }

            // it's safe to instantiate the SensorFusion object here
            // (it wants to be instantiated after h/w sensors have been
            // registered)
            const SensorFusion& fusion(SensorFusion::getInstance());

            // build the sensor list returned to users
            mUserSensorList = mSensorList;

            if (hasGyro && hasAccel && hasMag) {
                Sensor aSensor;

                // Add Android virtual sensors if they're not already
                // available in the HAL

                aSensor = registerVirtualSensor( new RotationVectorSensor() );
                if (virtualSensorsNeeds & (1<<SENSOR_TYPE_ROTATION_VECTOR)) {
                    mUserSensorList.add(aSensor);
                }

                aSensor = registerVirtualSensor( new GravitySensor(list, count) );
                if (virtualSensorsNeeds & (1<<SENSOR_TYPE_GRAVITY)) {
                    mUserSensorList.add(aSensor);
                }

                aSensor = registerVirtualSensor( new LinearAccelerationSensor(list, count) );
                if (virtualSensorsNeeds & (1<<SENSOR_TYPE_LINEAR_ACCELERATION)) {
                    mUserSensorList.add(aSensor);
                }

                aSensor = registerVirtualSensor( new OrientationSensor() );
                if (virtualSensorsNeeds & (1<<SENSOR_TYPE_ROTATION_VECTOR)) {
                    // if we are doing our own rotation-vector, also add
                    // the orientation sensor and remove the HAL provided one.
                    mUserSensorList.replaceAt(aSensor, orientationIndex);
                }

                // virtual debugging sensors are not added to mUserSensorList
                registerVirtualSensor( new CorrectedGyroSensor(list, count) );
                registerVirtualSensor( new GyroDriftSensor() );
            }

            // debugging sensor list
            mUserSensorListDebug = mSensorList;

            // Check if the device really supports batching by looking at the FIFO event
            // counts for each sensor.
            bool batchingSupported = false;
            for (size_t i = 0; i < mSensorList.size(); ++i) {
                if (mSensorList[i].getFifoMaxEventCount() > 0) {
                    batchingSupported = true;
                    break;
                }
            }

            if (batchingSupported) {
                // Increase socket buffer size to a max of 100 KB for batching capabilities.
                mSocketBufferSize = MAX_SOCKET_BUFFER_SIZE_BATCHED;
            } else {
                mSocketBufferSize = SOCKET_BUFFER_SIZE_NON_BATCHED;
            }

            // Compare the socketBufferSize value against the system limits and limit
            // it to maxSystemSocketBufferSize if necessary.
            FILE *fp = fopen("/proc/sys/net/core/wmem_max", "r");
            char line[128];
            if (fp != NULL && fgets(line, sizeof(line), fp) != NULL) {
                line[sizeof(line) - 1] = '\0';
                size_t maxSystemSocketBufferSize;
                sscanf(line, "%zu", &maxSystemSocketBufferSize);
                if (mSocketBufferSize > maxSystemSocketBufferSize) {
                    mSocketBufferSize = maxSystemSocketBufferSize;
                }
            }
            if (fp) {
                fclose(fp);
            }

            mWakeLockAcquired = false;
            mLooper = new Looper(false);
            const size_t minBufferSize = SensorEventQueue::MAX_RECEIVE_BUFFER_EVENT_COUNT;
            mSensorEventBuffer = new sensors_event_t[minBufferSize];
            mSensorEventScratch = new sensors_event_t[minBufferSize];
            mMapFlushEventsToConnections = new SensorEventConnection const * [minBufferSize];
            mCurrentOperatingMode = NORMAL;

            mNextSensorRegIndex = 0;
            for (int i = 0; i < SENSOR_REGISTRATIONS_BUF_SIZE; ++i) {
                mLastNSensorRegistrations.push();
            }

            mInitCheck = NO_ERROR;
            mAckReceiver = new SensorEventAckReceiver(this);
            mAckReceiver->run("SensorEventAckReceiver", PRIORITY_URGENT_DISPLAY);
            run("SensorService", PRIORITY_URGENT_DISPLAY);
        }
    }
}

Sensor SensorService::registerSensor(SensorInterface* s)
{
    sensors_event_t event;
    memset(&event, 0, sizeof(event));

    const Sensor sensor(s->getSensor());
    // add to the sensor list (returned to clients)
    mSensorList.add(sensor);
    // add to our handle->SensorInterface mapping
    mSensorMap.add(sensor.getHandle(), s);
    // create an entry in the mLastEventSeen array
    mLastEventSeen.add(sensor.getHandle(), NULL);

    return sensor;
}

Sensor SensorService::registerVirtualSensor(SensorInterface* s)
{
    Sensor sensor = registerSensor(s);
    mVirtualSensorList.add( s );
    return sensor;
}

SensorService::~SensorService()
{
    for (size_t i=0 ; i<mSensorMap.size() ; i++)
        delete mSensorMap.valueAt(i);
}

status_t SensorService::dump(int fd, const Vector<String16>& args)
{
    String8 result;
    if (!PermissionCache::checkCallingPermission(sDump)) {
        result.appendFormat("Permission Denial: "
                "can't dump SensorService from pid=%d, uid=%d\n",
                IPCThreadState::self()->getCallingPid(),
                IPCThreadState::self()->getCallingUid());
    } else {
        if (args.size() > 2) {
           return INVALID_OPERATION;
        }
        Mutex::Autolock _l(mLock);
        SensorDevice& dev(SensorDevice::getInstance());
        if (args.size() == 2 && args[0] == String16("restrict")) {
            // If already in restricted mode. Ignore.
            if (mCurrentOperatingMode == RESTRICTED) {
                return status_t(NO_ERROR);
            }
            // If in any mode other than normal, ignore.
            if (mCurrentOperatingMode != NORMAL) {
                return INVALID_OPERATION;
            }
            mCurrentOperatingMode = RESTRICTED;
            dev.disableAllSensors();
            // Clear all pending flush connections for all active sensors. If one of the active
            // connections has called flush() and the underlying sensor has been disabled before a
            // flush complete event is returned, we need to remove the connection from this queue.
            for (size_t i=0 ; i< mActiveSensors.size(); ++i) {
                mActiveSensors.valueAt(i)->clearAllPendingFlushConnections();
            }
            mWhiteListedPackage.setTo(String8(args[1]));
            return status_t(NO_ERROR);
        } else if (args.size() == 1 && args[0] == String16("enable")) {
            // If currently in restricted mode, reset back to NORMAL mode else ignore.
            if (mCurrentOperatingMode == RESTRICTED) {
                mCurrentOperatingMode = NORMAL;
                dev.enableAllSensors();
            }
            if (mCurrentOperatingMode == DATA_INJECTION) {
               resetToNormalModeLocked();
            }
            mWhiteListedPackage.clear();
            return status_t(NO_ERROR);
        } else if (args.size() == 2 && args[0] == String16("data_injection")) {
            if (mCurrentOperatingMode == NORMAL) {
                dev.disableAllSensors();
                status_t err = dev.setMode(DATA_INJECTION);
                if (err == NO_ERROR) {
                    mCurrentOperatingMode = DATA_INJECTION;
                } else {
                    // Re-enable sensors.
                    dev.enableAllSensors();
                }
                mWhiteListedPackage.setTo(String8(args[1]));
                return NO_ERROR;
            } else if (mCurrentOperatingMode == DATA_INJECTION) {
                // Already in DATA_INJECTION mode. Treat this as a no_op.
                return NO_ERROR;
            } else {
                // Transition to data injection mode supported only from NORMAL mode.
                return INVALID_OPERATION;
            }
        } else if (mSensorList.size() == 0) {
            result.append("No Sensors on the device\n");
        } else {
            // Default dump the sensor list and debugging information.
            result.append("Sensor List:\n");
            for (size_t i=0 ; i<mSensorList.size() ; i++) {
                const Sensor& s(mSensorList[i]);
                result.appendFormat(
                        "%-15s| %-10s| version=%d |%-20s| 0x%08x | \"%s\" | type=%d |",
                        s.getName().string(),
                        s.getVendor().string(),
                        s.getVersion(),
                        s.getStringType().string(),
                        s.getHandle(),
                        s.getRequiredPermission().string(),
                        s.getType());

                const int reportingMode = s.getReportingMode();
                if (reportingMode == AREPORTING_MODE_CONTINUOUS) {
                    result.append(" continuous | ");
                } else if (reportingMode == AREPORTING_MODE_ON_CHANGE) {
                    result.append(" on-change | ");
                } else if (reportingMode == AREPORTING_MODE_ONE_SHOT) {
                    result.append(" one-shot | ");
                } else {
                    result.append(" special-trigger | ");
                }

                if (s.getMaxDelay() > 0) {
                    result.appendFormat("minRate=%.2fHz | ", 1e6f / s.getMaxDelay());
                } else {
                    result.appendFormat("maxDelay=%dus |", s.getMaxDelay());
                }

                if (s.getMinDelay() > 0) {
                    result.appendFormat("maxRate=%.2fHz | ", 1e6f / s.getMinDelay());
                } else {
                    result.appendFormat("minDelay=%dus |", s.getMinDelay());
                }

                if (s.getFifoMaxEventCount() > 0) {
                    result.appendFormat("FifoMax=%d events | ",
                            s.getFifoMaxEventCount());
                } else {
                    result.append("no batching | ");
                }

                if (s.isWakeUpSensor()) {
                    result.appendFormat("wakeUp | ");
                } else {
                    result.appendFormat("non-wakeUp | ");
                }

                int bufIndex = mLastEventSeen.indexOfKey(s.getHandle());
                if (bufIndex >= 0) {
                    const CircularBuffer* buf = mLastEventSeen.valueAt(bufIndex);
                    if (buf != NULL && s.getRequiredPermission().isEmpty()) {
                        buf->printBuffer(result);
                    } else {
                        result.append("last=<> \n");
                    }
                }
                result.append("\n");
            }
            SensorFusion::getInstance().dump(result);
            SensorDevice::getInstance().dump(result);

            result.append("Active sensors:\n");
            for (size_t i=0 ; i<mActiveSensors.size() ; i++) {
                int handle = mActiveSensors.keyAt(i);
                result.appendFormat("%s (handle=0x%08x, connections=%zu)\n",
                        getSensorName(handle).string(),
                        handle,
                        mActiveSensors.valueAt(i)->getNumConnections());
            }

            result.appendFormat("Socket Buffer size = %d events\n",
                                mSocketBufferSize/sizeof(sensors_event_t));
            result.appendFormat("WakeLock Status: %s \n", mWakeLockAcquired ? "acquired" :
                    "not held");
            result.appendFormat("Mode :");
            switch(mCurrentOperatingMode) {
               case NORMAL:
                   result.appendFormat(" NORMAL\n");
                   break;
               case RESTRICTED:
                   result.appendFormat(" RESTRICTED : %s\n", mWhiteListedPackage.string());
                   break;
               case DATA_INJECTION:
                   result.appendFormat(" DATA_INJECTION : %s\n", mWhiteListedPackage.string());
            }
            result.appendFormat("%zd active connections\n", mActiveConnections.size());

            for (size_t i=0 ; i < mActiveConnections.size() ; i++) {
                sp<SensorEventConnection> connection(mActiveConnections[i].promote());
                if (connection != 0) {
                    result.appendFormat("Connection Number: %zu \n", i);
                    connection->dump(result);
                }
            }

            result.appendFormat("Previous Registrations:\n");
            // Log in the reverse chronological order.
            int currentIndex = (mNextSensorRegIndex - 1 + SENSOR_REGISTRATIONS_BUF_SIZE) %
                SENSOR_REGISTRATIONS_BUF_SIZE;
            const int startIndex = currentIndex;
            do {
                const SensorRegistrationInfo& reg_info = mLastNSensorRegistrations[currentIndex];
                if (SensorRegistrationInfo::isSentinel(reg_info)) {
                    // Ignore sentinel, proceed to next item.
                    currentIndex = (currentIndex - 1 + SENSOR_REGISTRATIONS_BUF_SIZE) %
                        SENSOR_REGISTRATIONS_BUF_SIZE;
                    continue;
                }
                if (reg_info.mActivated) {
                   result.appendFormat("%02d:%02d:%02d activated package=%s handle=0x%08x "
                           "samplingRate=%dus maxReportLatency=%dus\n",
                           reg_info.mHour, reg_info.mMin, reg_info.mSec,
                           reg_info.mPackageName.string(), reg_info.mSensorHandle,
                           reg_info.mSamplingRateUs, reg_info.mMaxReportLatencyUs);
                } else {
                   result.appendFormat("%02d:%02d:%02d de-activated package=%s handle=0x%08x\n",
                           reg_info.mHour, reg_info.mMin, reg_info.mSec,
                           reg_info.mPackageName.string(), reg_info.mSensorHandle);
                }
                currentIndex = (currentIndex - 1 + SENSOR_REGISTRATIONS_BUF_SIZE) %
                        SENSOR_REGISTRATIONS_BUF_SIZE;
            } while(startIndex != currentIndex);
        }
    }
    write(fd, result.string(), result.size());
    return NO_ERROR;
}

void SensorService::cleanupAutoDisabledSensorLocked(const sp<SensorEventConnection>& connection,
        sensors_event_t const* buffer, const int count) {
    for (int i=0 ; i<count ; i++) {
        int handle = buffer[i].sensor;
        if (buffer[i].type == SENSOR_TYPE_META_DATA) {
            handle = buffer[i].meta_data.sensor;
        }
        if (connection->hasSensor(handle)) {
            SensorInterface* sensor = mSensorMap.valueFor(handle);
            // If this buffer has an event from a one_shot sensor and this connection is registered
            // for this particular one_shot sensor, try cleaning up the connection.
            if (sensor != NULL &&
                sensor->getSensor().getReportingMode() == AREPORTING_MODE_ONE_SHOT) {
                sensor->autoDisable(connection.get(), handle);
                cleanupWithoutDisableLocked(connection, handle);
            }

        }
   }
}

bool SensorService::threadLoop()
{
    ALOGD("nuSensorService thread starting...");

    // each virtual sensor could generate an event per "real" event, that's why we need
    // to size numEventMax much smaller than MAX_RECEIVE_BUFFER_EVENT_COUNT.
    // in practice, this is too aggressive, but guaranteed to be enough.
    const size_t minBufferSize = SensorEventQueue::MAX_RECEIVE_BUFFER_EVENT_COUNT;
    const size_t numEventMax = minBufferSize / (1 + mVirtualSensorList.size());

    SensorDevice& device(SensorDevice::getInstance());
    const size_t vcount = mVirtualSensorList.size();

    const int halVersion = device.getHalDeviceVersion();
    do {
        ssize_t count = device.poll(mSensorEventBuffer, numEventMax);
        if (count < 0) {
            ALOGE("sensor poll failed (%s)", strerror(-count));
            break;
        }

        // Reset sensors_event_t.flags to zero for all events in the buffer.
        for (int i = 0; i < count; i++) {
             mSensorEventBuffer[i].flags = 0;
        }

        // Make a copy of the connection vector as some connections may be removed during the
        // course of this loop (especially when one-shot sensor events are present in the
        // sensor_event buffer). Promote all connections to StrongPointers before the lock is
        // acquired. If the destructor of the sp gets called when the lock is acquired, it may
        // result in a deadlock as ~SensorEventConnection() needs to acquire mLock again for
        // cleanup. So copy all the strongPointers to a vector before the lock is acquired.
        SortedVector< sp<SensorEventConnection> > activeConnections;
        populateActiveConnections(&activeConnections);
        Mutex::Autolock _l(mLock);
        // Poll has returned. Hold a wakelock if one of the events is from a wake up sensor. The
        // rest of this loop is under a critical section protected by mLock. Acquiring a wakeLock,
        // sending events to clients (incrementing SensorEventConnection::mWakeLockRefCount) should
        // not be interleaved with decrementing SensorEventConnection::mWakeLockRefCount and
        // releasing the wakelock.
        bool bufferHasWakeUpEvent = false;
        for (int i = 0; i < count; i++) {
            if (isWakeUpSensorEvent(mSensorEventBuffer[i])) {
                bufferHasWakeUpEvent = true;
                break;
            }
        }

        if (bufferHasWakeUpEvent && !mWakeLockAcquired) {
            setWakeLockAcquiredLocked(true);
        }
        recordLastValueLocked(mSensorEventBuffer, count);

        // handle virtual sensors
        if (count && vcount) {
            sensors_event_t const * const event = mSensorEventBuffer;
            const size_t activeVirtualSensorCount = mActiveVirtualSensors.size();
            if (activeVirtualSensorCount) {
                size_t k = 0;
                SensorFusion& fusion(SensorFusion::getInstance());
                if (fusion.isEnabled()) {
                    for (size_t i=0 ; i<size_t(count) ; i++) {
                        fusion.process(event[i]);
                    }
                }
                for (size_t i=0 ; i<size_t(count) && k<minBufferSize ; i++) {
                    for (size_t j=0 ; j<activeVirtualSensorCount ; j++) {
                        if (count + k >= minBufferSize) {
                            ALOGE("buffer too small to hold all events: "
                                    "count=%zd, k=%zu, size=%zu",
                                    count, k, minBufferSize);
                            break;
                        }
                        sensors_event_t out;
                        SensorInterface* si = mActiveVirtualSensors.valueAt(j);
                        if (si->process(&out, event[i])) {
                            mSensorEventBuffer[count + k] = out;
                            k++;
                        }
                    }
                }
                if (k) {
                    // record the last synthesized values
                    recordLastValueLocked(&mSensorEventBuffer[count], k);
                    count += k;
                    // sort the buffer by time-stamps
                    sortEventBuffer(mSensorEventBuffer, count);
                }
            }
        }

        // handle backward compatibility for RotationVector sensor
        if (halVersion < SENSORS_DEVICE_API_VERSION_1_0) {
            for (int i = 0; i < count; i++) {
                if (mSensorEventBuffer[i].type == SENSOR_TYPE_ROTATION_VECTOR) {
                    // All the 4 components of the quaternion should be available
                    // No heading accuracy. Set it to -1
                    mSensorEventBuffer[i].data[4] = -1;
                }
            }
        }

        // Map flush_complete_events in the buffer to SensorEventConnections which called
        // flush on the hardware sensor. mapFlushEventsToConnections[i] will be the
        // SensorEventConnection mapped to the corresponding flush_complete_event in
        // mSensorEventBuffer[i] if such a mapping exists (NULL otherwise).
        for (int i = 0; i < count; ++i) {
            mMapFlushEventsToConnections[i] = NULL;
            if (mSensorEventBuffer[i].type == SENSOR_TYPE_META_DATA) {
                const int sensor_handle = mSensorEventBuffer[i].meta_data.sensor;
                SensorRecord* rec = mActiveSensors.valueFor(sensor_handle);
                if (rec != NULL) {
                    mMapFlushEventsToConnections[i] = rec->getFirstPendingFlushConnection();
                    rec->removeFirstPendingFlushConnection();
                }
            }
        }

        // Send our events to clients. Check the state of wake lock for each client and release the
        // lock if none of the clients need it.
        bool needsWakeLock = false;
        size_t numConnections = activeConnections.size();
        for (size_t i=0 ; i < numConnections; ++i) {
            if (activeConnections[i] != 0) {
                activeConnections[i]->sendEvents(mSensorEventBuffer, count, mSensorEventScratch,
                        mMapFlushEventsToConnections);
                needsWakeLock |= activeConnections[i]->needsWakeLock();
                // If the connection has one-shot sensors, it may be cleaned up after first trigger.
                // Early check for one-shot sensors.
                if (activeConnections[i]->hasOneShotSensors()) {
                    cleanupAutoDisabledSensorLocked(activeConnections[i], mSensorEventBuffer,
                            count);
                }
            }
        }

        if (mWakeLockAcquired && !needsWakeLock) {
            setWakeLockAcquiredLocked(false);
        }
    } while (!Thread::exitPending());

    ALOGW("Exiting SensorService::threadLoop => aborting...");
    abort();
    return false;
}

sp<Looper> SensorService::getLooper() const {
    return mLooper;
}

void SensorService::resetAllWakeLockRefCounts() {
    SortedVector< sp<SensorEventConnection> > activeConnections;
    populateActiveConnections(&activeConnections);
    {
        Mutex::Autolock _l(mLock);
        for (size_t i=0 ; i < activeConnections.size(); ++i) {
            if (activeConnections[i] != 0) {
                activeConnections[i]->resetWakeLockRefCount();
            }
        }
        setWakeLockAcquiredLocked(false);
    }
}

void SensorService::setWakeLockAcquiredLocked(bool acquire) {
    if (acquire) {
        if (!mWakeLockAcquired) {
            acquire_wake_lock(PARTIAL_WAKE_LOCK, WAKE_LOCK_NAME);
            mWakeLockAcquired = true;
        }
        mLooper->wake();
    } else {
        if (mWakeLockAcquired) {
            release_wake_lock(WAKE_LOCK_NAME);
            mWakeLockAcquired = false;
        }
    }
}

bool SensorService::isWakeLockAcquired() {
    Mutex::Autolock _l(mLock);
    return mWakeLockAcquired;
}

bool SensorService::SensorEventAckReceiver::threadLoop() {
    ALOGD("new thread SensorEventAckReceiver");
    sp<Looper> looper = mService->getLooper();
    do {
        bool wakeLockAcquired = mService->isWakeLockAcquired();
        int timeout = -1;
        if (wakeLockAcquired) timeout = 5000;
        int ret = looper->pollOnce(timeout);
        if (ret == ALOOPER_POLL_TIMEOUT) {
           mService->resetAllWakeLockRefCounts();
        }
    } while(!Thread::exitPending());
    return false;
}

void SensorService::recordLastValueLocked(
        const sensors_event_t* buffer, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (buffer[i].type != SENSOR_TYPE_META_DATA) {
            CircularBuffer* &circular_buf = mLastEventSeen.editValueFor(buffer[i].sensor);
            if (circular_buf == NULL) {
                circular_buf = new CircularBuffer(buffer[i].type);
            }
            circular_buf->addEvent(buffer[i]);
        }
    }
}

void SensorService::sortEventBuffer(sensors_event_t* buffer, size_t count)
{
    struct compar {
        static int cmp(void const* lhs, void const* rhs) {
            sensors_event_t const* l = static_cast<sensors_event_t const*>(lhs);
            sensors_event_t const* r = static_cast<sensors_event_t const*>(rhs);
            return l->timestamp - r->timestamp;
        }
    };
    qsort(buffer, count, sizeof(sensors_event_t), compar::cmp);
}

String8 SensorService::getSensorName(int handle) const {
    size_t count = mUserSensorList.size();
    for (size_t i=0 ; i<count ; i++) {
        const Sensor& sensor(mUserSensorList[i]);
        if (sensor.getHandle() == handle) {
            return sensor.getName();
        }
    }
    String8 result("unknown");
    return result;
}

bool SensorService::isVirtualSensor(int handle) const {
    SensorInterface* sensor = mSensorMap.valueFor(handle);
    return sensor->isVirtual();
}

bool SensorService::isWakeUpSensorEvent(const sensors_event_t& event) const {
    int handle = event.sensor;
    if (event.type == SENSOR_TYPE_META_DATA) {
        handle = event.meta_data.sensor;
    }
    SensorInterface* sensor = mSensorMap.valueFor(handle);
    return sensor != NULL && sensor->getSensor().isWakeUpSensor();
}

SensorService::SensorRecord * SensorService::getSensorRecord(int handle) {
     return mActiveSensors.valueFor(handle);
}

Vector<Sensor> SensorService::getSensorList(const String16& opPackageName)
{
    char value[PROPERTY_VALUE_MAX];
    property_get("debug.sensors", value, "0");
    const Vector<Sensor>& initialSensorList = (atoi(value)) ?
            mUserSensorListDebug : mUserSensorList;
    Vector<Sensor> accessibleSensorList;
    for (size_t i = 0; i < initialSensorList.size(); i++) {
        Sensor sensor = initialSensorList[i];
        if (canAccessSensor(sensor, "getSensorList", opPackageName)) {
            accessibleSensorList.add(sensor);
        } else {
            ALOGI("Skipped sensor %s because it requires permission %s and app op %d",
                  sensor.getName().string(),
                  sensor.getRequiredPermission().string(),
                  sensor.getRequiredAppOp());
        }
    }
    return accessibleSensorList;
}

sp<ISensorEventConnection> SensorService::createSensorEventConnection(const String8& packageName,
        int requestedMode, const String16& opPackageName) {
    // Only 2 modes supported for a SensorEventConnection ... NORMAL and DATA_INJECTION.
    if (requestedMode != NORMAL && requestedMode != DATA_INJECTION) {
        return NULL;
    }

    Mutex::Autolock _l(mLock);
    // To create a client in DATA_INJECTION mode to inject data, SensorService should already be
    // operating in DI mode.
    if (requestedMode == DATA_INJECTION) {
        if (mCurrentOperatingMode != DATA_INJECTION) return NULL;
        if (!isWhiteListedPackage(packageName)) return NULL;
    }

    uid_t uid = IPCThreadState::self()->getCallingUid();
    sp<SensorEventConnection> result(new SensorEventConnection(this, uid, packageName,
            requestedMode == DATA_INJECTION, opPackageName));
    if (requestedMode == DATA_INJECTION) {
        if (mActiveConnections.indexOf(result) < 0) {
            mActiveConnections.add(result);
        }
        // Add the associated file descriptor to the Looper for polling whenever there is data to
        // be injected.
        result->updateLooperRegistration(mLooper);
    }
    return result;
}

int SensorService::isDataInjectionEnabled() {
    Mutex::Autolock _l(mLock);
    return (mCurrentOperatingMode == DATA_INJECTION);
}

status_t SensorService::resetToNormalMode() {
    Mutex::Autolock _l(mLock);
    return resetToNormalModeLocked();
}

status_t SensorService::resetToNormalModeLocked() {
    SensorDevice& dev(SensorDevice::getInstance());
    dev.enableAllSensors();
    status_t err = dev.setMode(NORMAL);
    mCurrentOperatingMode = NORMAL;
    return err;
}

void SensorService::cleanupConnection(SensorEventConnection* c)
{
    Mutex::Autolock _l(mLock);
    const wp<SensorEventConnection> connection(c);
    size_t size = mActiveSensors.size();
    ALOGD_IF(DEBUG_CONNECTIONS, "%zu active sensors", size);
    for (size_t i=0 ; i<size ; ) {
        int handle = mActiveSensors.keyAt(i);
        if (c->hasSensor(handle)) {
            ALOGD_IF(DEBUG_CONNECTIONS, "%zu: disabling handle=0x%08x", i, handle);
            SensorInterface* sensor = mSensorMap.valueFor( handle );
            ALOGE_IF(!sensor, "mSensorMap[handle=0x%08x] is null!", handle);
            if (sensor) {
                sensor->activate(c, false);
            }
            c->removeSensor(handle);
        }
        SensorRecord* rec = mActiveSensors.valueAt(i);
        ALOGE_IF(!rec, "mActiveSensors[%zu] is null (handle=0x%08x)!", i, handle);
        ALOGD_IF(DEBUG_CONNECTIONS,
                "removing connection %p for sensor[%zu].handle=0x%08x",
                c, i, handle);

        if (rec && rec->removeConnection(connection)) {
            ALOGD_IF(DEBUG_CONNECTIONS, "... and it was the last connection");
            mActiveSensors.removeItemsAt(i, 1);
            mActiveVirtualSensors.removeItem(handle);
            delete rec;
            size--;
        } else {
            i++;
        }
    }
    c->updateLooperRegistration(mLooper);
    mActiveConnections.remove(connection);
    BatteryService::cleanup(c->getUid());
    if (c->needsWakeLock()) {
        checkWakeLockStateLocked();
    }
}

Sensor SensorService::getSensorFromHandle(int handle) const {
    return mSensorMap.valueFor(handle)->getSensor();
}

status_t SensorService::enable(const sp<SensorEventConnection>& connection,
        int handle, nsecs_t samplingPeriodNs, nsecs_t maxBatchReportLatencyNs, int reservedFlags,
        const String16& opPackageName)
{
    if (mInitCheck != NO_ERROR)
        return mInitCheck;

    SensorInterface* sensor = mSensorMap.valueFor(handle);
    if (sensor == NULL) {
        return BAD_VALUE;
    }

    if (!canAccessSensor(sensor->getSensor(), "Tried enabling", opPackageName)) {
        return BAD_VALUE;
    }

    Mutex::Autolock _l(mLock);
    if ((mCurrentOperatingMode == RESTRICTED || mCurrentOperatingMode == DATA_INJECTION)
           && !isWhiteListedPackage(connection->getPackageName())) {
        return INVALID_OPERATION;
    }

    SensorRecord* rec = mActiveSensors.valueFor(handle);
    if (rec == 0) {
        rec = new SensorRecord(connection);
        mActiveSensors.add(handle, rec);
        if (sensor->isVirtual()) {
            mActiveVirtualSensors.add(handle, sensor);
        }
    } else {
        if (rec->addConnection(connection)) {
            // this sensor is already activated, but we are adding a connection that uses it.
            // Immediately send down the last known value of the requested sensor if it's not a
            // "continuous" sensor.
            if (sensor->getSensor().getReportingMode() == AREPORTING_MODE_ON_CHANGE) {
                // NOTE: The wake_up flag of this event may get set to
                // WAKE_UP_SENSOR_EVENT_NEEDS_ACK if this is a wake_up event.
                CircularBuffer *circular_buf = mLastEventSeen.valueFor(handle);
                if (circular_buf) {
                    sensors_event_t event;
                    memset(&event, 0, sizeof(event));
                    // It is unlikely that this buffer is empty as the sensor is already active.
                    // One possible corner case may be two applications activating an on-change
                    // sensor at the same time.
                    if(circular_buf->populateLastEvent(&event)) {
                        event.sensor = handle;
                        if (event.version == sizeof(sensors_event_t)) {
                            if (isWakeUpSensorEvent(event) && !mWakeLockAcquired) {
                                setWakeLockAcquiredLocked(true);
                            }
                            connection->sendEvents(&event, 1, NULL);
                            if (!connection->needsWakeLock() && mWakeLockAcquired) {
                                checkWakeLockStateLocked();
                            }
                        }
                    }
                }
            }
        }
    }

    if (connection->addSensor(handle)) {
        BatteryService::enableSensor(connection->getUid(), handle);
        // the sensor was added (which means it wasn't already there)
        // so, see if this connection becomes active
        if (mActiveConnections.indexOf(connection) < 0) {
            mActiveConnections.add(connection);
        }
    } else {
        ALOGW("sensor %08x already enabled in connection %p (ignoring)",
            handle, connection.get());
    }

    nsecs_t minDelayNs = sensor->getSensor().getMinDelayNs();
    if (samplingPeriodNs < minDelayNs) {
        samplingPeriodNs = minDelayNs;
    }

    ALOGD_IF(DEBUG_CONNECTIONS, "Calling batch handle==%d flags=%d"
                                "rate=%" PRId64 " timeout== %" PRId64"",
             handle, reservedFlags, samplingPeriodNs, maxBatchReportLatencyNs);

    status_t err = sensor->batch(connection.get(), handle, 0, samplingPeriodNs,
                                 maxBatchReportLatencyNs);

    // Call flush() before calling activate() on the sensor. Wait for a first
    // flush complete event before sending events on this connection. Ignore
    // one-shot sensors which don't support flush(). Ignore on-change sensors
    // to maintain the on-change logic (any on-change events except the initial
    // one should be trigger by a change in value). Also if this sensor isn't
    // already active, don't call flush().
    if (err == NO_ERROR &&
            sensor->getSensor().getReportingMode() != AREPORTING_MODE_ONE_SHOT &&
            sensor->getSensor().getReportingMode() != AREPORTING_MODE_ON_CHANGE &&
            rec->getNumConnections() > 1) {
        connection->setFirstFlushPending(handle, true);
        status_t err_flush = sensor->flush(connection.get(), handle);
        // Flush may return error if the underlying h/w sensor uses an older HAL.
        if (err_flush == NO_ERROR) {
            rec->addPendingFlushConnection(connection.get());
        } else {
            connection->setFirstFlushPending(handle, false);
        }
    }

    if (err == NO_ERROR) {
        ALOGD_IF(DEBUG_CONNECTIONS, "Calling activate on %d", handle);
        err = sensor->activate(connection.get(), true);
    }

    if (err == NO_ERROR) {
        connection->updateLooperRegistration(mLooper);
        SensorRegistrationInfo &reg_info =
            mLastNSensorRegistrations.editItemAt(mNextSensorRegIndex);
        reg_info.mSensorHandle = handle;
        reg_info.mSamplingRateUs = samplingPeriodNs/1000;
        reg_info.mMaxReportLatencyUs = maxBatchReportLatencyNs/1000;
        reg_info.mActivated = true;
        reg_info.mPackageName = connection->getPackageName();
        time_t rawtime = time(NULL);
        struct tm * timeinfo = localtime(&rawtime);
        reg_info.mHour = timeinfo->tm_hour;
        reg_info.mMin = timeinfo->tm_min;
        reg_info.mSec = timeinfo->tm_sec;
        mNextSensorRegIndex = (mNextSensorRegIndex + 1) % SENSOR_REGISTRATIONS_BUF_SIZE;
    }

    if (err != NO_ERROR) {
        // batch/activate has failed, reset our state.
        cleanupWithoutDisableLocked(connection, handle);
    }
    return err;
}

status_t SensorService::disable(const sp<SensorEventConnection>& connection,
        int handle)
{
    if (mInitCheck != NO_ERROR)
        return mInitCheck;

    Mutex::Autolock _l(mLock);
    status_t err = cleanupWithoutDisableLocked(connection, handle);
    if (err == NO_ERROR) {
        SensorInterface* sensor = mSensorMap.valueFor(handle);
        err = sensor ? sensor->activate(connection.get(), false) : status_t(BAD_VALUE);

    }
    if (err == NO_ERROR) {
        SensorRegistrationInfo &reg_info =
            mLastNSensorRegistrations.editItemAt(mNextSensorRegIndex);
        reg_info.mActivated = false;
        reg_info.mPackageName= connection->getPackageName();
        reg_info.mSensorHandle = handle;
        time_t rawtime = time(NULL);
        struct tm * timeinfo = localtime(&rawtime);
        reg_info.mHour = timeinfo->tm_hour;
        reg_info.mMin = timeinfo->tm_min;
        reg_info.mSec = timeinfo->tm_sec;
        mNextSensorRegIndex = (mNextSensorRegIndex + 1) % SENSOR_REGISTRATIONS_BUF_SIZE;
    }
    return err;
}

status_t SensorService::cleanupWithoutDisable(
        const sp<SensorEventConnection>& connection, int handle) {
    Mutex::Autolock _l(mLock);
    return cleanupWithoutDisableLocked(connection, handle);
}

status_t SensorService::cleanupWithoutDisableLocked(
        const sp<SensorEventConnection>& connection, int handle) {
    SensorRecord* rec = mActiveSensors.valueFor(handle);
    if (rec) {
        // see if this connection becomes inactive
        if (connection->removeSensor(handle)) {
            BatteryService::disableSensor(connection->getUid(), handle);
        }
        if (connection->hasAnySensor() == false) {
            connection->updateLooperRegistration(mLooper);
            mActiveConnections.remove(connection);
        }
        // see if this sensor becomes inactive
        if (rec->removeConnection(connection)) {
            mActiveSensors.removeItem(handle);
            mActiveVirtualSensors.removeItem(handle);
            delete rec;
        }
        return NO_ERROR;
    }
    return BAD_VALUE;
}

status_t SensorService::setEventRate(const sp<SensorEventConnection>& connection,
        int handle, nsecs_t ns, const String16& opPackageName)
{
    if (mInitCheck != NO_ERROR)
        return mInitCheck;

    SensorInterface* sensor = mSensorMap.valueFor(handle);
    if (!sensor)
        return BAD_VALUE;

    if (!canAccessSensor(sensor->getSensor(), "Tried configuring", opPackageName)) {
        return BAD_VALUE;
    }

    if (ns < 0)
        return BAD_VALUE;

    nsecs_t minDelayNs = sensor->getSensor().getMinDelayNs();
    if (ns < minDelayNs) {
        ns = minDelayNs;
    }

    return sensor->setDelay(connection.get(), handle, ns);
}

status_t SensorService::flushSensor(const sp<SensorEventConnection>& connection,
        const String16& opPackageName) {
    if (mInitCheck != NO_ERROR) return mInitCheck;
    SensorDevice& dev(SensorDevice::getInstance());
    const int halVersion = dev.getHalDeviceVersion();
    status_t err(NO_ERROR);
    Mutex::Autolock _l(mLock);
    // Loop through all sensors for this connection and call flush on each of them.
    for (size_t i = 0; i < connection->mSensorInfo.size(); ++i) {
        const int handle = connection->mSensorInfo.keyAt(i);
        SensorInterface* sensor = mSensorMap.valueFor(handle);
        if (sensor->getSensor().getReportingMode() == AREPORTING_MODE_ONE_SHOT) {
            ALOGE("flush called on a one-shot sensor");
            err = INVALID_OPERATION;
            continue;
        }
        if (halVersion <= SENSORS_DEVICE_API_VERSION_1_0 || isVirtualSensor(handle)) {
            // For older devices just increment pending flush count which will send a trivial
            // flush complete event.
            connection->incrementPendingFlushCount(handle);
        } else {
            if (!canAccessSensor(sensor->getSensor(), "Tried flushing", opPackageName)) {
                err = INVALID_OPERATION;
                continue;
            }
            status_t err_flush = sensor->flush(connection.get(), handle);
            if (err_flush == NO_ERROR) {
                SensorRecord* rec = mActiveSensors.valueFor(handle);
                if (rec != NULL) rec->addPendingFlushConnection(connection);
            }
            err = (err_flush != NO_ERROR) ? err_flush : err;
        }
    }
    return err;
}

bool SensorService::canAccessSensor(const Sensor& sensor, const char* operation,
        const String16& opPackageName) {
    const String8& requiredPermission = sensor.getRequiredPermission();

    if (requiredPermission.length() <= 0) {
        return true;
    }

    bool hasPermission = false;

    // Runtime permissions can't use the cache as they may change.
    if (sensor.isRequiredPermissionRuntime()) {
        hasPermission = checkPermission(String16(requiredPermission),
                IPCThreadState::self()->getCallingPid(), IPCThreadState::self()->getCallingUid());
    } else {
        hasPermission = PermissionCache::checkCallingPermission(String16(requiredPermission));
    }

    if (!hasPermission) {
        ALOGE("%s a sensor (%s) without holding its required permission: %s",
                operation, sensor.getName().string(), sensor.getRequiredPermission().string());
        return false;
    }

    const int32_t opCode = sensor.getRequiredAppOp();
    if (opCode >= 0) {
        AppOpsManager appOps;
        if (appOps.noteOp(opCode, IPCThreadState::self()->getCallingUid(), opPackageName)
                        != AppOpsManager::MODE_ALLOWED) {
            ALOGE("%s a sensor (%s) without enabled required app op: %D",
                    operation, sensor.getName().string(), opCode);
            return false;
        }
    }

    return true;
}

void SensorService::checkWakeLockState() {
    Mutex::Autolock _l(mLock);
    checkWakeLockStateLocked();
}

void SensorService::checkWakeLockStateLocked() {
    if (!mWakeLockAcquired) {
        return;
    }
    bool releaseLock = true;
    for (size_t i=0 ; i<mActiveConnections.size() ; i++) {
        sp<SensorEventConnection> connection(mActiveConnections[i].promote());
        if (connection != 0) {
            if (connection->needsWakeLock()) {
                releaseLock = false;
                break;
            }
        }
    }
    if (releaseLock) {
        setWakeLockAcquiredLocked(false);
    }
}

void SensorService::sendEventsFromCache(const sp<SensorEventConnection>& connection) {
    Mutex::Autolock _l(mLock);
    connection->writeToSocketFromCache();
    if (connection->needsWakeLock()) {
        setWakeLockAcquiredLocked(true);
    }
}

void SensorService::populateActiveConnections(
        SortedVector< sp<SensorEventConnection> >* activeConnections) {
    Mutex::Autolock _l(mLock);
    for (size_t i=0 ; i < mActiveConnections.size(); ++i) {
        sp<SensorEventConnection> connection(mActiveConnections[i].promote());
        if (connection != 0) {
            activeConnections->add(connection);
        }
    }
}

bool SensorService::isWhiteListedPackage(const String8& packageName) {
    return (packageName.contains(mWhiteListedPackage.string()));
}

int SensorService::getNumEventsForSensorType(int sensor_event_type) {
    switch (sensor_event_type) {
        case SENSOR_TYPE_ROTATION_VECTOR:
        case SENSOR_TYPE_GEOMAGNETIC_ROTATION_VECTOR:
            return 5;

        case SENSOR_TYPE_MAGNETIC_FIELD_UNCALIBRATED:
        case SENSOR_TYPE_GYROSCOPE_UNCALIBRATED:
            return 6;

        case SENSOR_TYPE_GAME_ROTATION_VECTOR:
            return 4;

        case SENSOR_TYPE_SIGNIFICANT_MOTION:
        case SENSOR_TYPE_STEP_DETECTOR:
        case SENSOR_TYPE_STEP_COUNTER:
            return 1;

         default:
            return 3;
    }
}

// ---------------------------------------------------------------------------
SensorService::SensorRecord::SensorRecord(
        const sp<SensorEventConnection>& connection)
{
    mConnections.add(connection);
}

bool SensorService::SensorRecord::addConnection(
        const sp<SensorEventConnection>& connection)
{
    if (mConnections.indexOf(connection) < 0) {
        mConnections.add(connection);
        return true;
    }
    return false;
}

bool SensorService::SensorRecord::removeConnection(
        const wp<SensorEventConnection>& connection)
{
    ssize_t index = mConnections.indexOf(connection);
    if (index >= 0) {
        mConnections.removeItemsAt(index, 1);
    }
    // Remove this connections from the queue of flush() calls made on this sensor.
    for (Vector< wp<SensorEventConnection> >::iterator it =
            mPendingFlushConnections.begin(); it != mPendingFlushConnections.end();) {

        if (it->unsafe_get() == connection.unsafe_get()) {
            it = mPendingFlushConnections.erase(it);
        } else {
            ++it;
        }
    }
    return mConnections.size() ? false : true;
}

void SensorService::SensorRecord::addPendingFlushConnection(
        const sp<SensorEventConnection>& connection) {
    mPendingFlushConnections.add(connection);
}

void SensorService::SensorRecord::removeFirstPendingFlushConnection() {
    if (mPendingFlushConnections.size() > 0) {
        mPendingFlushConnections.removeAt(0);
    }
}

SensorService::SensorEventConnection *
SensorService::SensorRecord::getFirstPendingFlushConnection() {
   if (mPendingFlushConnections.size() > 0) {
        return mPendingFlushConnections[0].unsafe_get();
    }
    return NULL;
}

void SensorService::SensorRecord::clearAllPendingFlushConnections() {
    mPendingFlushConnections.clear();
}


// ---------------------------------------------------------------------------
SensorService::TrimmedSensorEvent::TrimmedSensorEvent(int sensorType) {
    mTimestamp = -1;
    const int numData = SensorService::getNumEventsForSensorType(sensorType);
    if (sensorType == SENSOR_TYPE_STEP_COUNTER) {
        mStepCounter = 0;
    } else {
        mData = new float[numData];
        for (int i = 0; i < numData; ++i) {
            mData[i] = -1.0;
        }
    }
    mHour = mMin = mSec = INT32_MIN;
}

bool SensorService::TrimmedSensorEvent::isSentinel(const TrimmedSensorEvent& event) {
    return (event.mHour == INT32_MIN && event.mMin == INT32_MIN && event.mSec == INT32_MIN);
}
// --------------------------------------------------------------------------
SensorService::CircularBuffer::CircularBuffer(int sensor_event_type) {
    mNextInd = 0;
    mBufSize = CIRCULAR_BUF_SIZE;
    if (sensor_event_type == SENSOR_TYPE_STEP_COUNTER ||
            sensor_event_type == SENSOR_TYPE_SIGNIFICANT_MOTION ||
            sensor_event_type == SENSOR_TYPE_ACCELEROMETER) {
        mBufSize = CIRCULAR_BUF_SIZE * 5;
    }
    mTrimmedSensorEventArr = new TrimmedSensorEvent *[mBufSize];
    mSensorType = sensor_event_type;
    for (int i = 0; i < mBufSize; ++i) {
        mTrimmedSensorEventArr[i] = new TrimmedSensorEvent(mSensorType);
    }
}

void SensorService::CircularBuffer::addEvent(const sensors_event_t& sensor_event) {
    TrimmedSensorEvent *curr_event = mTrimmedSensorEventArr[mNextInd];
    curr_event->mTimestamp = sensor_event.timestamp;
    if (mSensorType == SENSOR_TYPE_STEP_COUNTER) {
        curr_event->mStepCounter = sensor_event.u64.step_counter;
    } else {
        memcpy(curr_event->mData, sensor_event.data,
                 sizeof(float) * SensorService::getNumEventsForSensorType(mSensorType));
    }
    time_t rawtime = time(NULL);
    struct tm * timeinfo = localtime(&rawtime);
    curr_event->mHour = timeinfo->tm_hour;
    curr_event->mMin = timeinfo->tm_min;
    curr_event->mSec = timeinfo->tm_sec;
    mNextInd = (mNextInd + 1) % mBufSize;
}

void SensorService::CircularBuffer::printBuffer(String8& result) const {
    const int numData = SensorService::getNumEventsForSensorType(mSensorType);
    int i = mNextInd, eventNum = 1;
    result.appendFormat("last %d events = < ", mBufSize);
    do {
        if (TrimmedSensorEvent::isSentinel(*mTrimmedSensorEventArr[i])) {
            // Sentinel, ignore.
            i = (i + 1) % mBufSize;
            continue;
        }
        result.appendFormat("%d) ", eventNum++);
        if (mSensorType == SENSOR_TYPE_STEP_COUNTER) {
            result.appendFormat("%llu,", mTrimmedSensorEventArr[i]->mStepCounter);
        } else {
            for (int j = 0; j < numData; ++j) {
                result.appendFormat("%5.1f,", mTrimmedSensorEventArr[i]->mData[j]);
            }
        }
        result.appendFormat("%lld %02d:%02d:%02d ", mTrimmedSensorEventArr[i]->mTimestamp,
                mTrimmedSensorEventArr[i]->mHour, mTrimmedSensorEventArr[i]->mMin,
                mTrimmedSensorEventArr[i]->mSec);
        i = (i + 1) % mBufSize;
    } while (i != mNextInd);
    result.appendFormat(">\n");
}

bool SensorService::CircularBuffer::populateLastEvent(sensors_event_t *event) {
    int lastEventInd = (mNextInd - 1 + mBufSize) % mBufSize;
    // Check if the buffer is empty.
    if (TrimmedSensorEvent::isSentinel(*mTrimmedSensorEventArr[lastEventInd])) {
        return false;
    }
    event->version = sizeof(sensors_event_t);
    event->type = mSensorType;
    event->timestamp = mTrimmedSensorEventArr[lastEventInd]->mTimestamp;
    if (mSensorType == SENSOR_TYPE_STEP_COUNTER) {
          event->u64.step_counter = mTrimmedSensorEventArr[lastEventInd]->mStepCounter;
    } else {
        memcpy(event->data, mTrimmedSensorEventArr[lastEventInd]->mData,
                 sizeof(float) * SensorService::getNumEventsForSensorType(mSensorType));
    }
    return true;
}

SensorService::CircularBuffer::~CircularBuffer() {
    for (int i = 0; i < mBufSize; ++i) {
        delete mTrimmedSensorEventArr[i];
    }
    delete [] mTrimmedSensorEventArr;
}

// ---------------------------------------------------------------------------

SensorService::SensorEventConnection::SensorEventConnection(
        const sp<SensorService>& service, uid_t uid, String8 packageName, bool isDataInjectionMode,
        const String16& opPackageName)
    : mService(service), mUid(uid), mWakeLockRefCount(0), mHasLooperCallbacks(false),
      mDead(false), mDataInjectionMode(isDataInjectionMode), mEventCache(NULL),
      mCacheSize(0), mMaxCacheSize(0), mPackageName(packageName), mOpPackageName(opPackageName) {
    mChannel = new BitTube(mService->mSocketBufferSize);
#if DEBUG_CONNECTIONS
    mEventsReceived = mEventsSentFromCache = mEventsSent = 0;
    mTotalAcksNeeded = mTotalAcksReceived = 0;
#endif
}

SensorService::SensorEventConnection::~SensorEventConnection() {
    ALOGD_IF(DEBUG_CONNECTIONS, "~SensorEventConnection(%p)", this);
    mService->cleanupConnection(this);
    if (mEventCache != NULL) {
        delete mEventCache;
    }
}

void SensorService::SensorEventConnection::onFirstRef() {
    LooperCallback::onFirstRef();
}

bool SensorService::SensorEventConnection::needsWakeLock() {
    Mutex::Autolock _l(mConnectionLock);
    return !mDead && mWakeLockRefCount > 0;
}

void SensorService::SensorEventConnection::resetWakeLockRefCount() {
    Mutex::Autolock _l(mConnectionLock);
    mWakeLockRefCount = 0;
}

void SensorService::SensorEventConnection::dump(String8& result) {
    Mutex::Autolock _l(mConnectionLock);
    result.appendFormat("\tOperating Mode: %s\n",mDataInjectionMode ? "DATA_INJECTION" : "NORMAL");
    result.appendFormat("\t %s | WakeLockRefCount %d | uid %d | cache size %d | "
            "max cache size %d\n", mPackageName.string(), mWakeLockRefCount, mUid, mCacheSize,
            mMaxCacheSize);
    for (size_t i = 0; i < mSensorInfo.size(); ++i) {
        const FlushInfo& flushInfo = mSensorInfo.valueAt(i);
        result.appendFormat("\t %s 0x%08x | status: %s | pending flush events %d \n",
                            mService->getSensorName(mSensorInfo.keyAt(i)).string(),
                            mSensorInfo.keyAt(i),
                            flushInfo.mFirstFlushPending ? "First flush pending" :
                                                           "active",
                            flushInfo.mPendingFlushEventsToSend);
    }
#if DEBUG_CONNECTIONS
    result.appendFormat("\t events recvd: %d | sent %d | cache %d | dropped %d |"
            " total_acks_needed %d | total_acks_recvd %d\n",
            mEventsReceived,
            mEventsSent,
            mEventsSentFromCache,
            mEventsReceived - (mEventsSentFromCache + mEventsSent + mCacheSize),
            mTotalAcksNeeded,
            mTotalAcksReceived);
#endif
}

bool SensorService::SensorEventConnection::addSensor(int32_t handle) {
    Mutex::Autolock _l(mConnectionLock);
    if (!canAccessSensor(mService->getSensorFromHandle(handle),
            "Tried adding", mOpPackageName)) {
        return false;
    }
    if (mSensorInfo.indexOfKey(handle) < 0) {
        mSensorInfo.add(handle, FlushInfo());
        return true;
    }
    return false;
}

bool SensorService::SensorEventConnection::removeSensor(int32_t handle) {
    Mutex::Autolock _l(mConnectionLock);
    if (mSensorInfo.removeItem(handle) >= 0) {
        return true;
    }
    return false;
}

bool SensorService::SensorEventConnection::hasSensor(int32_t handle) const {
    Mutex::Autolock _l(mConnectionLock);
    return mSensorInfo.indexOfKey(handle) >= 0;
}

bool SensorService::SensorEventConnection::hasAnySensor() const {
    Mutex::Autolock _l(mConnectionLock);
    return mSensorInfo.size() ? true : false;
}

bool SensorService::SensorEventConnection::hasOneShotSensors() const {
    Mutex::Autolock _l(mConnectionLock);
    for (size_t i = 0; i < mSensorInfo.size(); ++i) {
        const int handle = mSensorInfo.keyAt(i);
        if (mService->getSensorFromHandle(handle).getReportingMode() == AREPORTING_MODE_ONE_SHOT) {
            return true;
        }
    }
    return false;
}

String8 SensorService::SensorEventConnection::getPackageName() const {
    return mPackageName;
}

void SensorService::SensorEventConnection::setFirstFlushPending(int32_t handle,
                                bool value) {
    Mutex::Autolock _l(mConnectionLock);
    ssize_t index = mSensorInfo.indexOfKey(handle);
    if (index >= 0) {
        FlushInfo& flushInfo = mSensorInfo.editValueAt(index);
        flushInfo.mFirstFlushPending = value;
    }
}

void SensorService::SensorEventConnection::updateLooperRegistration(const sp<Looper>& looper) {
    Mutex::Autolock _l(mConnectionLock);
    updateLooperRegistrationLocked(looper);
}

void SensorService::SensorEventConnection::updateLooperRegistrationLocked(
        const sp<Looper>& looper) {
    bool isConnectionActive = (mSensorInfo.size() > 0 && !mDataInjectionMode) ||
                              mDataInjectionMode;
    // If all sensors are unregistered OR Looper has encountered an error, we
    // can remove the Fd from the Looper if it has been previously added.
    if (!isConnectionActive || mDead) {
        if (mHasLooperCallbacks) {
            ALOGD_IF(DEBUG_CONNECTIONS, "%p removeFd fd=%d", this, mChannel->getSendFd());
            looper->removeFd(mChannel->getSendFd());
            mHasLooperCallbacks = false;
        }
        return;
    }

    int looper_flags = 0;
    if (mCacheSize > 0) looper_flags |= ALOOPER_EVENT_OUTPUT;
    if (mDataInjectionMode) looper_flags |= ALOOPER_EVENT_INPUT;
    for (size_t i = 0; i < mSensorInfo.size(); ++i) {
        const int handle = mSensorInfo.keyAt(i);
        if (mService->getSensorFromHandle(handle).isWakeUpSensor()) {
            looper_flags |= ALOOPER_EVENT_INPUT;
            break;
        }
    }
    // If flags is still set to zero, we don't need to add this fd to the Looper, if
    // the fd has already been added, remove it. This is likely to happen when ALL the
    // events stored in the cache have been sent to the corresponding app.
    if (looper_flags == 0) {
        if (mHasLooperCallbacks) {
            ALOGD_IF(DEBUG_CONNECTIONS, "removeFd fd=%d", mChannel->getSendFd());
            looper->removeFd(mChannel->getSendFd());
            mHasLooperCallbacks = false;
        }
        return;
    }
    // Add the file descriptor to the Looper for receiving acknowledegments if the app has
    // registered for wake-up sensors OR for sending events in the cache.
    int ret = looper->addFd(mChannel->getSendFd(), 0, looper_flags, this, NULL);
    if (ret == 1) {
        ALOGD_IF(DEBUG_CONNECTIONS, "%p addFd fd=%d", this, mChannel->getSendFd());
        mHasLooperCallbacks = true;
    } else {
        ALOGE("Looper::addFd failed ret=%d fd=%d", ret, mChannel->getSendFd());
    }
}

void SensorService::SensorEventConnection::incrementPendingFlushCount(int32_t handle) {
    Mutex::Autolock _l(mConnectionLock);
    ssize_t index = mSensorInfo.indexOfKey(handle);
    if (index >= 0) {
        FlushInfo& flushInfo = mSensorInfo.editValueAt(index);
        flushInfo.mPendingFlushEventsToSend++;
    }
}

status_t SensorService::SensorEventConnection::sendEvents(
        sensors_event_t const* buffer, size_t numEvents,
        sensors_event_t* scratch,
        SensorEventConnection const * const * mapFlushEventsToConnections) {
    // filter out events not for this connection
    int count = 0;
    Mutex::Autolock _l(mConnectionLock);
    if (scratch) {
        size_t i=0;
        while (i<numEvents) {
            int32_t sensor_handle = buffer[i].sensor;
            if (buffer[i].type == SENSOR_TYPE_META_DATA) {
                ALOGD_IF(DEBUG_CONNECTIONS, "flush complete event sensor==%d ",
                        buffer[i].meta_data.sensor);
                // Setting sensor_handle to the correct sensor to ensure the sensor events per
                // connection are filtered correctly.  buffer[i].sensor is zero for meta_data
                // events.
                sensor_handle = buffer[i].meta_data.sensor;
            }
            ssize_t index = mSensorInfo.indexOfKey(sensor_handle);
            // Check if this connection has registered for this sensor. If not continue to the
            // next sensor_event.
            if (index < 0) {
                ++i;
                continue;
            }

            FlushInfo& flushInfo = mSensorInfo.editValueAt(index);
            // Check if there is a pending flush_complete event for this sensor on this connection.
            if (buffer[i].type == SENSOR_TYPE_META_DATA && flushInfo.mFirstFlushPending == true &&
                    this == mapFlushEventsToConnections[i]) {
                flushInfo.mFirstFlushPending = false;
                ALOGD_IF(DEBUG_CONNECTIONS, "First flush event for sensor==%d ",
                        buffer[i].meta_data.sensor);
                ++i;
                continue;
            }

            // If there is a pending flush complete event for this sensor on this connection,
            // ignore the event and proceed to the next.
            if (flushInfo.mFirstFlushPending) {
                ++i;
                continue;
            }

            do {
                // Keep copying events into the scratch buffer as long as they are regular
                // sensor_events are from the same sensor_handle OR they are flush_complete_events
                // from the same sensor_handle AND the current connection is mapped to the
                // corresponding flush_complete_event.
                if (buffer[i].type == SENSOR_TYPE_META_DATA) {
                    if (this == mapFlushEventsToConnections[i]) {
                        scratch[count++] = buffer[i];
                    }
                    ++i;
                } else {
                    // Regular sensor event, just copy it to the scratch buffer.
                    scratch[count++] = buffer[i++];
                }
            } while ((i<numEvents) && ((buffer[i].sensor == sensor_handle &&
                                        buffer[i].type != SENSOR_TYPE_META_DATA) ||
                                       (buffer[i].type == SENSOR_TYPE_META_DATA  &&
                                        buffer[i].meta_data.sensor == sensor_handle)));
        }
    } else {
        scratch = const_cast<sensors_event_t *>(buffer);
        count = numEvents;
    }

    sendPendingFlushEventsLocked();
    // Early return if there are no events for this connection.
    if (count == 0) {
        return status_t(NO_ERROR);
    }

#if DEBUG_CONNECTIONS
     mEventsReceived += count;
#endif
    if (mCacheSize != 0) {
        // There are some events in the cache which need to be sent first. Copy this buffer to
        // the end of cache.
        if (mCacheSize + count <= mMaxCacheSize) {
            memcpy(&mEventCache[mCacheSize], scratch, count * sizeof(sensors_event_t));
            mCacheSize += count;
        } else {
            // Check if any new sensors have registered on this connection which may have increased
            // the max cache size that is desired.
            if (mCacheSize + count < computeMaxCacheSizeLocked()) {
                reAllocateCacheLocked(scratch, count);
                return status_t(NO_ERROR);
            }
            // Some events need to be dropped.
            int remaningCacheSize = mMaxCacheSize - mCacheSize;
            if (remaningCacheSize != 0) {
                memcpy(&mEventCache[mCacheSize], scratch,
                                                remaningCacheSize * sizeof(sensors_event_t));
            }
            int numEventsDropped = count - remaningCacheSize;
            countFlushCompleteEventsLocked(mEventCache, numEventsDropped);
            // Drop the first "numEventsDropped" in the cache.
            memmove(mEventCache, &mEventCache[numEventsDropped],
                    (mCacheSize - numEventsDropped) * sizeof(sensors_event_t));

            // Copy the remainingEvents in scratch buffer to the end of cache.
            memcpy(&mEventCache[mCacheSize - numEventsDropped], scratch + remaningCacheSize,
                                            numEventsDropped * sizeof(sensors_event_t));
        }
        return status_t(NO_ERROR);
    }

    int index_wake_up_event = findWakeUpSensorEventLocked(scratch, count);
    if (index_wake_up_event >= 0) {
        scratch[index_wake_up_event].flags |= WAKE_UP_SENSOR_EVENT_NEEDS_ACK;
        ++mWakeLockRefCount;
#if DEBUG_CONNECTIONS
        ++mTotalAcksNeeded;
#endif
    }

    // NOTE: ASensorEvent and sensors_event_t are the same type.
    ssize_t size = SensorEventQueue::write(mChannel,
                                    reinterpret_cast<ASensorEvent const*>(scratch), count);
    if (size < 0) {
        // Write error, copy events to local cache.
        if (index_wake_up_event >= 0) {
            // If there was a wake_up sensor_event, reset the flag.
            scratch[index_wake_up_event].flags &= ~WAKE_UP_SENSOR_EVENT_NEEDS_ACK;
            if (mWakeLockRefCount > 0) {
                --mWakeLockRefCount;
            }
#if DEBUG_CONNECTIONS
            --mTotalAcksNeeded;
#endif
        }
        if (mEventCache == NULL) {
            mMaxCacheSize = computeMaxCacheSizeLocked();
            mEventCache = new sensors_event_t[mMaxCacheSize];
            mCacheSize = 0;
        }
        memcpy(&mEventCache[mCacheSize], scratch, count * sizeof(sensors_event_t));
        mCacheSize += count;

        // Add this file descriptor to the looper to get a callback when this fd is available for
        // writing.
        updateLooperRegistrationLocked(mService->getLooper());
        return size;
    }

#if DEBUG_CONNECTIONS
    if (size > 0) {
        mEventsSent += count;
    }
#endif

    return size < 0 ? status_t(size) : status_t(NO_ERROR);
}

void SensorService::SensorEventConnection::reAllocateCacheLocked(sensors_event_t const* scratch,
                                                                 int count) {
    sensors_event_t *eventCache_new;
    const int new_cache_size = computeMaxCacheSizeLocked();
    // Allocate new cache, copy over events from the old cache & scratch, free up memory.
    eventCache_new = new sensors_event_t[new_cache_size];
    memcpy(eventCache_new, mEventCache, mCacheSize * sizeof(sensors_event_t));
    memcpy(&eventCache_new[mCacheSize], scratch, count * sizeof(sensors_event_t));

    ALOGD_IF(DEBUG_CONNECTIONS, "reAllocateCacheLocked maxCacheSize=%d %d", mMaxCacheSize,
            new_cache_size);

    delete mEventCache;
    mEventCache = eventCache_new;
    mCacheSize += count;
    mMaxCacheSize = new_cache_size;
}

void SensorService::SensorEventConnection::sendPendingFlushEventsLocked() {
    ASensorEvent flushCompleteEvent;
    memset(&flushCompleteEvent, 0, sizeof(flushCompleteEvent));
    flushCompleteEvent.type = SENSOR_TYPE_META_DATA;
    // Loop through all the sensors for this connection and check if there are any pending
    // flush complete events to be sent.
    for (size_t i = 0; i < mSensorInfo.size(); ++i) {
        FlushInfo& flushInfo = mSensorInfo.editValueAt(i);
        while (flushInfo.mPendingFlushEventsToSend > 0) {
            const int sensor_handle = mSensorInfo.keyAt(i);
            flushCompleteEvent.meta_data.sensor = sensor_handle;
            bool wakeUpSensor = mService->getSensorFromHandle(sensor_handle).isWakeUpSensor();
            if (wakeUpSensor) {
               ++mWakeLockRefCount;
               flushCompleteEvent.flags |= WAKE_UP_SENSOR_EVENT_NEEDS_ACK;
            }
            ssize_t size = SensorEventQueue::write(mChannel, &flushCompleteEvent, 1);
            if (size < 0) {
                if (wakeUpSensor) --mWakeLockRefCount;
                return;
            }
            ALOGD_IF(DEBUG_CONNECTIONS, "sent dropped flush complete event==%d ",
                    flushCompleteEvent.meta_data.sensor);
            flushInfo.mPendingFlushEventsToSend--;
        }
    }
}

void SensorService::SensorEventConnection::writeToSocketFromCache() {
    // At a time write at most half the size of the receiver buffer in SensorEventQueue OR
    // half the size of the socket buffer allocated in BitTube whichever is smaller.
    const int maxWriteSize = helpers::min(SensorEventQueue::MAX_RECEIVE_BUFFER_EVENT_COUNT/2,
            int(mService->mSocketBufferSize/(sizeof(sensors_event_t)*2)));
    Mutex::Autolock _l(mConnectionLock);
    // Send pending flush complete events (if any)
    sendPendingFlushEventsLocked();
    for (int numEventsSent = 0; numEventsSent < mCacheSize;) {
        const int numEventsToWrite = helpers::min(mCacheSize - numEventsSent, maxWriteSize);
        int index_wake_up_event =
                  findWakeUpSensorEventLocked(mEventCache + numEventsSent, numEventsToWrite);
        if (index_wake_up_event >= 0) {
            mEventCache[index_wake_up_event + numEventsSent].flags |=
                    WAKE_UP_SENSOR_EVENT_NEEDS_ACK;
            ++mWakeLockRefCount;
#if DEBUG_CONNECTIONS
            ++mTotalAcksNeeded;
#endif
        }

        ssize_t size = SensorEventQueue::write(mChannel,
                          reinterpret_cast<ASensorEvent const*>(mEventCache + numEventsSent),
                          numEventsToWrite);
        if (size < 0) {
            if (index_wake_up_event >= 0) {
                // If there was a wake_up sensor_event, reset the flag.
                mEventCache[index_wake_up_event + numEventsSent].flags  &=
                        ~WAKE_UP_SENSOR_EVENT_NEEDS_ACK;
                if (mWakeLockRefCount > 0) {
                    --mWakeLockRefCount;
                }
#if DEBUG_CONNECTIONS
                --mTotalAcksNeeded;
#endif
            }
            memmove(mEventCache, &mEventCache[numEventsSent],
                                 (mCacheSize - numEventsSent) * sizeof(sensors_event_t));
            ALOGD_IF(DEBUG_CONNECTIONS, "wrote %d events from cache size==%d ",
                    numEventsSent, mCacheSize);
            mCacheSize -= numEventsSent;
            return;
        }
        numEventsSent += numEventsToWrite;
#if DEBUG_CONNECTIONS
        mEventsSentFromCache += numEventsToWrite;
#endif
    }
    ALOGD_IF(DEBUG_CONNECTIONS, "wrote all events from cache size=%d ", mCacheSize);
    // All events from the cache have been sent. Reset cache size to zero.
    mCacheSize = 0;
    // There are no more events in the cache. We don't need to poll for write on the fd.
    // Update Looper registration.
    updateLooperRegistrationLocked(mService->getLooper());
}

void SensorService::SensorEventConnection::countFlushCompleteEventsLocked(
                sensors_event_t const* scratch, const int numEventsDropped) {
    ALOGD_IF(DEBUG_CONNECTIONS, "dropping %d events ", numEventsDropped);
    // Count flushComplete events in the events that are about to the dropped. These will be sent
    // separately before the next batch of events.
    for (int j = 0; j < numEventsDropped; ++j) {
        if (scratch[j].type == SENSOR_TYPE_META_DATA) {
            FlushInfo& flushInfo = mSensorInfo.editValueFor(scratch[j].meta_data.sensor);
            flushInfo.mPendingFlushEventsToSend++;
            ALOGD_IF(DEBUG_CONNECTIONS, "increment pendingFlushCount %d",
                     flushInfo.mPendingFlushEventsToSend);
        }
    }
    return;
}

int SensorService::SensorEventConnection::findWakeUpSensorEventLocked(
                       sensors_event_t const* scratch, const int count) {
    for (int i = 0; i < count; ++i) {
        if (mService->isWakeUpSensorEvent(scratch[i])) {
            return i;
        }
    }
    return -1;
}

sp<BitTube> SensorService::SensorEventConnection::getSensorChannel() const
{
    return mChannel;
}

status_t SensorService::SensorEventConnection::enableDisable(
        int handle, bool enabled, nsecs_t samplingPeriodNs, nsecs_t maxBatchReportLatencyNs,
        int reservedFlags)
{
    status_t err;
    if (enabled) {
        err = mService->enable(this, handle, samplingPeriodNs, maxBatchReportLatencyNs,
                               reservedFlags, mOpPackageName);

    } else {
        err = mService->disable(this, handle);
    }
    return err;
}

status_t SensorService::SensorEventConnection::setEventRate(
        int handle, nsecs_t samplingPeriodNs)
{
    return mService->setEventRate(this, handle, samplingPeriodNs, mOpPackageName);
}

status_t  SensorService::SensorEventConnection::flush() {
    return  mService->flushSensor(this, mOpPackageName);
}

int SensorService::SensorEventConnection::handleEvent(int fd, int events, void* /*data*/) {
    if (events & ALOOPER_EVENT_HANGUP || events & ALOOPER_EVENT_ERROR) {
        {
            // If the Looper encounters some error, set the flag mDead, reset mWakeLockRefCount,
            // and remove the fd from Looper. Call checkWakeLockState to know if SensorService
            // can release the wake-lock.
            ALOGD_IF(DEBUG_CONNECTIONS, "%p Looper error %d", this, fd);
            Mutex::Autolock _l(mConnectionLock);
            mDead = true;
            mWakeLockRefCount = 0;
            updateLooperRegistrationLocked(mService->getLooper());
        }
        mService->checkWakeLockState();
        if (mDataInjectionMode) {
            // If the Looper has encountered some error in data injection mode, reset SensorService
            // back to normal mode.
            mService->resetToNormalMode();
            mDataInjectionMode = false;
        }
        return 1;
    }

    if (events & ALOOPER_EVENT_INPUT) {
        unsigned char buf[sizeof(sensors_event_t)];
        ssize_t numBytesRead = ::recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
        {
           Mutex::Autolock _l(mConnectionLock);
           if (numBytesRead == sizeof(sensors_event_t)) {
               if (!mDataInjectionMode) {
                   ALOGE("Data injected in normal mode, dropping event"
                         "package=%s uid=%d", mPackageName.string(), mUid);
                   // Unregister call backs.
                   return 0;
               }
               SensorDevice& dev(SensorDevice::getInstance());
               sensors_event_t sensor_event;
               memset(&sensor_event, 0, sizeof(sensor_event));
               memcpy(&sensor_event, buf, sizeof(sensors_event_t));
               Sensor sensor = mService->getSensorFromHandle(sensor_event.sensor);
               sensor_event.type = sensor.getType();
               dev.injectSensorData(&sensor_event);
#if DEBUG_CONNECTIONS
               ++mEventsReceived;
#endif
           } else if (numBytesRead == sizeof(uint32_t)) {
               uint32_t numAcks = 0;
               memcpy(&numAcks, buf, numBytesRead);
               // Sanity check to ensure  there are no read errors in recv, numAcks is always
               // within the range and not zero. If any of the above don't hold reset
               // mWakeLockRefCount to zero.
               if (numAcks > 0 && numAcks < mWakeLockRefCount) {
                   mWakeLockRefCount -= numAcks;
               } else {
                   mWakeLockRefCount = 0;
               }
#if DEBUG_CONNECTIONS
               mTotalAcksReceived += numAcks;
#endif
           } else {
               // Read error, reset wakelock refcount.
               mWakeLockRefCount = 0;
           }
        }
        // Check if wakelock can be released by sensorservice. mConnectionLock needs to be released
        // here as checkWakeLockState() will need it.
        if (mWakeLockRefCount == 0) {
            mService->checkWakeLockState();
        }
        // continue getting callbacks.
        return 1;
    }

    if (events & ALOOPER_EVENT_OUTPUT) {
        // send sensor data that is stored in mEventCache for this connection.
        mService->sendEventsFromCache(this);
    }
    return 1;
}

int SensorService::SensorEventConnection::computeMaxCacheSizeLocked() const {
    size_t fifoWakeUpSensors = 0;
    size_t fifoNonWakeUpSensors = 0;
    for (size_t i = 0; i < mSensorInfo.size(); ++i) {
        const Sensor& sensor = mService->getSensorFromHandle(mSensorInfo.keyAt(i));
        if (sensor.getFifoReservedEventCount() == sensor.getFifoMaxEventCount()) {
            // Each sensor has a reserved fifo. Sum up the fifo sizes for all wake up sensors and
            // non wake_up sensors.
            if (sensor.isWakeUpSensor()) {
                fifoWakeUpSensors += sensor.getFifoReservedEventCount();
            } else {
                fifoNonWakeUpSensors += sensor.getFifoReservedEventCount();
            }
        } else {
            // Shared fifo. Compute the max of the fifo sizes for wake_up and non_wake up sensors.
            if (sensor.isWakeUpSensor()) {
                fifoWakeUpSensors = fifoWakeUpSensors > sensor.getFifoMaxEventCount() ?
                                          fifoWakeUpSensors : sensor.getFifoMaxEventCount();

            } else {
                fifoNonWakeUpSensors = fifoNonWakeUpSensors > sensor.getFifoMaxEventCount() ?
                                          fifoNonWakeUpSensors : sensor.getFifoMaxEventCount();

            }
        }
   }
   if (fifoWakeUpSensors + fifoNonWakeUpSensors == 0) {
       // It is extremely unlikely that there is a write failure in non batch mode. Return a cache
       // size that is equal to that of the batch mode.
       // ALOGW("Write failure in non-batch mode");
       return MAX_SOCKET_BUFFER_SIZE_BATCHED/sizeof(sensors_event_t);
   }
   return fifoWakeUpSensors + fifoNonWakeUpSensors;
}

// ---------------------------------------------------------------------------
}; // namespace android

