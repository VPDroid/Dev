/*
 * Copyright (C) 2007 The Android Open Source Project
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

#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include <stdint.h>
#include <sys/types.h>
#include <errno.h>
#include <math.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <stdatomic.h>

#include <EGL/egl.h>

#include <cutils/log.h>
#include <cutils/properties.h>

#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <binder/MemoryHeapBase.h>
#include <binder/PermissionCache.h>

#include <ui/DisplayInfo.h>
#include <ui/DisplayStatInfo.h>

#include <gui/BitTube.h>
#include <gui/BufferQueue.h>
#include <gui/GuiConfig.h>
#include <gui/IDisplayEventConnection.h>
#include <gui/Surface.h>
#include <gui/GraphicBufferAlloc.h>

#include <ui/GraphicBufferAllocator.h>
#include <ui/PixelFormat.h>
#include <ui/UiConfig.h>

#include <utils/misc.h>
#include <utils/String8.h>
#include <utils/String16.h>
#include <utils/StopWatch.h>
#include <utils/Trace.h>

#include <private/android_filesystem_config.h>
#include <private/gui/SyncFeatures.h>

#include "Client.h"
#include "clz.h"
#include "Colorizer.h"
#include "DdmConnection.h"
#include "DisplayDevice.h"
#include "DispSync.h"
#include "EventControlThread.h"
#include "EventThread.h"
#include "Layer.h"
#include "LayerDim.h"
#include "SurfaceFlinger.h"

#include "DisplayHardware/FramebufferSurface.h"
#include "DisplayHardware/HWComposer.h"
#include "DisplayHardware/VirtualDisplaySurface.h"

#include "Effects/Daltonizer.h"

#include "RenderEngine/RenderEngine.h"
#include <cutils/compiler.h>

#include <utils/CallStack.h>

#define DISPLAY_COUNT       1

/*
 * DEBUG_SCREENSHOTS: set to true to check that screenshots are not all
 * black pixels.
 */
#define DEBUG_SCREENSHOTS   false

EGLAPI const char* eglQueryStringImplementationANDROID(EGLDisplay dpy, EGLint name);

namespace android {

// This is the phase offset in nanoseconds of the software vsync event
// relative to the vsync event reported by HWComposer.  The software vsync
// event is when SurfaceFlinger and Choreographer-based applications run each
// frame.
//
// This phase offset allows adjustment of the minimum latency from application
// wake-up (by Choregographer) time to the time at which the resulting window
// image is displayed.  This value may be either positive (after the HW vsync)
// or negative (before the HW vsync).  Setting it to 0 will result in a
// minimum latency of two vsync periods because the app and SurfaceFlinger
// will run just after the HW vsync.  Setting it to a positive number will
// result in the minimum latency being:
//
//     (2 * VSYNC_PERIOD - (vsyncPhaseOffsetNs % VSYNC_PERIOD))
//
// Note that reducing this latency makes it more likely for the applications
// to not have their window content image ready in time.  When this happens
// the latency will end up being an additional vsync period, and animations
// will hiccup.  Therefore, this latency should be tuned somewhat
// conservatively (or at least with awareness of the trade-off being made).
static const int64_t vsyncPhaseOffsetNs = VSYNC_EVENT_PHASE_OFFSET_NS;

// This is the phase offset at which SurfaceFlinger's composition runs.
static const int64_t sfVsyncPhaseOffsetNs = SF_VSYNC_EVENT_PHASE_OFFSET_NS;

// ---------------------------------------------------------------------------

const String16 sHardwareTest("android.permission.HARDWARE_TEST");
const String16 sAccessSurfaceFlinger("android.permission.ACCESS_SURFACE_FLINGER");
const String16 sReadFramebuffer("android.permission.READ_FRAME_BUFFER");
const String16 sDump("android.permission.DUMP");

// ---------------------------------------------------------------------------

SurfaceFlinger::SurfaceFlinger()
    :   BnSurfaceComposer(),
        mTransactionFlags(0),
        mTransactionPending(false),
        mAnimTransactionPending(false),
        mLayersRemoved(false),
        mRepaintEverything(0),
        mRenderEngine(NULL),
        mBootTime(systemTime()),
        mVisibleRegionsDirty(false),
        mHwWorkListDirty(false),
        mAnimCompositionPending(false),
        mDebugRegion(0),
        mDebugDDMS(0),
        mDebugDisableHWC(0),
        mDebugDisableTransformHint(0),
        mDebugInSwapBuffers(0),
        mLastSwapBufferTime(0),
        mDebugInTransaction(0),
        mLastTransactionTime(0),
        mBootFinished(false),
        mForceFullDamage(false),
        mPrimaryHWVsyncEnabled(false),
        mHWVsyncAvailable(false),
        mDaltonize(false),
        mHasColorMatrix(false),
        mHasPoweredOff(false),
        mFrameBuckets(),
        mTotalTime(0),
        mLastSwapTime(0)
{
    ALOGI("SurfaceFlinger is starting");

    // debugging stuff...
    char value[PROPERTY_VALUE_MAX];

    property_get("ro.bq.gpu_to_cpu_unsupported", value, "0");
    mGpuToCpuSupported = !atoi(value);

    property_get("debug.sf.drop_missed_frames", value, "0");
    mDropMissedFrames = atoi(value);

    property_get("debug.sf.showupdates", value, "0");
    mDebugRegion = atoi(value);

    property_get("debug.sf.ddms", value, "0");
    mDebugDDMS = atoi(value);
    if (mDebugDDMS) {
        if (!startDdmConnection()) {
            // start failed, and DDMS debugging not enabled
            mDebugDDMS = 0;
        }
    }
    ALOGI_IF(mDebugRegion, "showupdates enabled");
    ALOGI_IF(mDebugDDMS, "DDMS debugging enabled");
}

void SurfaceFlinger::onFirstRef()
{
    mEventQueue.init(this);
}

SurfaceFlinger::~SurfaceFlinger()
{
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglTerminate(display);
}

void SurfaceFlinger::binderDied(const wp<IBinder>& /* who */)
{
    // the window manager died on us. prepare its eulogy.

    // restore initial conditions (default device unblank, etc)
    initializeDisplays();

    // restart the boot-animation
    startBootAnim();
}

sp<ISurfaceComposerClient> SurfaceFlinger::createConnection()
{
    sp<ISurfaceComposerClient> bclient;
    sp<Client> client(new Client(this));
    status_t err = client->initCheck();
    if (err == NO_ERROR) {
        bclient = client;
    }
    return bclient;
}

sp<IBinder> SurfaceFlinger::createDisplay(const String8& displayName,
        bool secure)
{
    class DisplayToken : public BBinder {
        sp<SurfaceFlinger> flinger;
        virtual ~DisplayToken() {
             // no more references, this display must be terminated
             Mutex::Autolock _l(flinger->mStateLock);
             flinger->mCurrentState.displays.removeItem(this);
             flinger->setTransactionFlags(eDisplayTransactionNeeded);
         }
     public:
        DisplayToken(const sp<SurfaceFlinger>& flinger)
            : flinger(flinger) {
        }
    };

    sp<BBinder> token = new DisplayToken(this);

    Mutex::Autolock _l(mStateLock);
    DisplayDeviceState info(DisplayDevice::DISPLAY_VIRTUAL);
    info.displayName = displayName;
    info.isSecure = secure;
    mCurrentState.displays.add(token, info);

    return token;
}

void SurfaceFlinger::destroyDisplay(const sp<IBinder>& display) {
    Mutex::Autolock _l(mStateLock);

    ssize_t idx = mCurrentState.displays.indexOfKey(display);
    if (idx < 0) {
        ALOGW("destroyDisplay: invalid display token");
        return;
    }

    const DisplayDeviceState& info(mCurrentState.displays.valueAt(idx));
    if (!info.isVirtualDisplay()) {
        ALOGE("destroyDisplay called for non-virtual display");
        return;
    }

    mCurrentState.displays.removeItemsAt(idx);
    setTransactionFlags(eDisplayTransactionNeeded);
}

void SurfaceFlinger::createBuiltinDisplayLocked(DisplayDevice::DisplayType type) {
    ALOGW_IF(mBuiltinDisplays[type],
            "Overwriting display token for display type %d", type);
    mBuiltinDisplays[type] = new BBinder();
    DisplayDeviceState info(type);
    // All non-virtual displays are currently considered secure.
    info.isSecure = true;
    mCurrentState.displays.add(mBuiltinDisplays[type], info);
}

sp<IBinder> SurfaceFlinger::getBuiltInDisplay(int32_t id) {
    if (uint32_t(id) >= DisplayDevice::NUM_BUILTIN_DISPLAY_TYPES) {
        ALOGE("getDefaultDisplay: id=%d is not a valid default display id", id);
        return NULL;
    }
    return mBuiltinDisplays[id];
}

sp<IGraphicBufferAlloc> SurfaceFlinger::createGraphicBufferAlloc()
{
    sp<GraphicBufferAlloc> gba(new GraphicBufferAlloc());
    return gba;
}

void SurfaceFlinger::bootFinished()
{
    const nsecs_t now = systemTime();
    const nsecs_t duration = now - mBootTime;
    ALOGI("Boot is finished (%ld ms)", long(ns2ms(duration)) );
    mBootFinished = true;

    // wait patiently for the window manager death
    const String16 name("window");
    sp<IBinder> window(defaultServiceManager()->getService(name));
    if (window != 0) {
        window->linkToDeath(static_cast<IBinder::DeathRecipient*>(this));
    }

    // stop boot animation
    // formerly we would just kill the process, but we now ask it to exit so it
    // can choose where to stop the animation.
    property_set("service.bootanim.exit", "1");
}

void SurfaceFlinger::deleteTextureAsync(uint32_t texture) {
    class MessageDestroyGLTexture : public MessageBase {
        RenderEngine& engine;
        uint32_t texture;
    public:
        MessageDestroyGLTexture(RenderEngine& engine, uint32_t texture)
            : engine(engine), texture(texture) {
        }
        virtual bool handler() {
            engine.deleteTextures(1, &texture);
            return true;
        }
    };
    postMessageAsync(new MessageDestroyGLTexture(getRenderEngine(), texture));
}

class DispSyncSource : public VSyncSource, private DispSync::Callback {
public:
    DispSyncSource(DispSync* dispSync, nsecs_t phaseOffset, bool traceVsync,
        const char* label) :
            mValue(0),
            mTraceVsync(traceVsync),
            mVsyncOnLabel(String8::format("VsyncOn-%s", label)),
            mVsyncEventLabel(String8::format("VSYNC-%s", label)),
            mDispSync(dispSync),
            mCallbackMutex(),
            mCallback(),
            mVsyncMutex(),
            mPhaseOffset(phaseOffset),
            mEnabled(false) {}

    virtual ~DispSyncSource() {}

    virtual void setVSyncEnabled(bool enable) {
        Mutex::Autolock lock(mVsyncMutex);
        if (enable) {
            status_t err = mDispSync->addEventListener(mPhaseOffset,
                    static_cast<DispSync::Callback*>(this));
            if (err != NO_ERROR) {
                ALOGE("error registering vsync callback: %s (%d)",
                        strerror(-err), err);
            }
            //ATRACE_INT(mVsyncOnLabel.string(), 1);
        } else {
            status_t err = mDispSync->removeEventListener(
                    static_cast<DispSync::Callback*>(this));
            if (err != NO_ERROR) {
                ALOGE("error unregistering vsync callback: %s (%d)",
                        strerror(-err), err);
            }
            //ATRACE_INT(mVsyncOnLabel.string(), 0);
        }
        mEnabled = enable;
    }

    virtual void setCallback(const sp<VSyncSource::Callback>& callback) {
        Mutex::Autolock lock(mCallbackMutex);
        mCallback = callback;
    }

    virtual void setPhaseOffset(nsecs_t phaseOffset) {
        Mutex::Autolock lock(mVsyncMutex);

        // Normalize phaseOffset to [0, period)
        auto period = mDispSync->getPeriod();
        phaseOffset %= period;
        if (phaseOffset < 0) {
            // If we're here, then phaseOffset is in (-period, 0). After this
            // operation, it will be in (0, period)
            phaseOffset += period;
        }
        mPhaseOffset = phaseOffset;

        // If we're not enabled, we don't need to mess with the listeners
        if (!mEnabled) {
            return;
        }

        // Remove the listener with the old offset
        status_t err = mDispSync->removeEventListener(
                static_cast<DispSync::Callback*>(this));
        if (err != NO_ERROR) {
            ALOGE("error unregistering vsync callback: %s (%d)",
                    strerror(-err), err);
        }

        // Add a listener with the new offset
        err = mDispSync->addEventListener(mPhaseOffset,
                static_cast<DispSync::Callback*>(this));
        if (err != NO_ERROR) {
            ALOGE("error registering vsync callback: %s (%d)",
                    strerror(-err), err);
        }
    }

private:
    virtual void onDispSyncEvent(nsecs_t when) {
        sp<VSyncSource::Callback> callback;
        {
            Mutex::Autolock lock(mCallbackMutex);
            callback = mCallback;

            if (mTraceVsync) {
                mValue = (mValue + 1) % 2;
                ATRACE_INT(mVsyncEventLabel.string(), mValue);
            }
        }

        if (callback != NULL) {
            callback->onVSyncEvent(when);
        }
    }

    int mValue;

    const bool mTraceVsync;
    const String8 mVsyncOnLabel;
    const String8 mVsyncEventLabel;

    DispSync* mDispSync;

    Mutex mCallbackMutex; // Protects the following
    sp<VSyncSource::Callback> mCallback;

    Mutex mVsyncMutex; // Protects the following
    nsecs_t mPhaseOffset;
    bool mEnabled;
};

void SurfaceFlinger::init() {
    ALOGI(  "SurfaceFlinger's main thread ready to run. "
            "Initializing graphics H/W...");

    Mutex::Autolock _l(mStateLock);

    // initialize EGL for the default display
    mEGLDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(mEGLDisplay, NULL, NULL);

    // start the EventThread
    sp<VSyncSource> vsyncSrc = new DispSyncSource(&mPrimaryDispSync,
            vsyncPhaseOffsetNs, true, "app");
    mEventThread = new EventThread(vsyncSrc);
    sp<VSyncSource> sfVsyncSrc = new DispSyncSource(&mPrimaryDispSync,
            sfVsyncPhaseOffsetNs, true, "sf");
    mSFEventThread = new EventThread(sfVsyncSrc);
    mEventQueue.setEventThread(mSFEventThread);

    // Initialize the H/W composer object.  There may or may not be an
    // actual hardware composer underneath.
    mHwc = new HWComposer(this,
            *static_cast<HWComposer::EventHandler *>(this));

    // get a RenderEngine for the given display / config (can't fail)
    mRenderEngine = RenderEngine::create(mEGLDisplay, mHwc->getVisualID());

    // retrieve the EGL context that was selected/created
    mEGLContext = mRenderEngine->getEGLContext();

    LOG_ALWAYS_FATAL_IF(mEGLContext == EGL_NO_CONTEXT,
            "couldn't create EGLContext");

    // initialize our non-virtual displays
    for (size_t i=0 ; i<DisplayDevice::NUM_BUILTIN_DISPLAY_TYPES ; i++) {
        DisplayDevice::DisplayType type((DisplayDevice::DisplayType)i);
        // set-up the displays that are already connected
        if (mHwc->isConnected(i) || type==DisplayDevice::DISPLAY_PRIMARY) {
            // All non-virtual displays are currently considered secure.
            bool isSecure = true;
            createBuiltinDisplayLocked(type);
            wp<IBinder> token = mBuiltinDisplays[i];

            sp<IGraphicBufferProducer> producer;
            sp<IGraphicBufferConsumer> consumer;
            BufferQueue::createBufferQueue(&producer, &consumer,
                    new GraphicBufferAlloc());

            sp<FramebufferSurface> fbs = new FramebufferSurface(*mHwc, i,
                    consumer);
            int32_t hwcId = allocateHwcDisplayId(type);
            sp<DisplayDevice> hw = new DisplayDevice(this,
                    type, hwcId, mHwc->getFormat(hwcId), isSecure, token,
                    fbs, producer,
                    mRenderEngine->getEGLConfig());
            if (i > DisplayDevice::DISPLAY_PRIMARY) {
                // FIXME: currently we don't get blank/unblank requests
                // for displays other than the main display, so we always
                // assume a connected display is unblanked.
                ALOGD("marking display %zu as acquired/unblanked", i);

                hw->setPowerMode(HWC_POWER_MODE_NORMAL);
            }
            mDisplays.add(token, hw);
        }
    }

    // make the GLContext current so that we can create textures when creating Layers
    // (which may happens before we render something)
    getDefaultDisplayDevice()->makeCurrent(mEGLDisplay, mEGLContext);

    mEventControlThread = new EventControlThread(this);
    mEventControlThread->run("EventControl", PRIORITY_URGENT_DISPLAY);

    // set a fake vsync period if there is no HWComposer
    if (mHwc->initCheck() != NO_ERROR) {
        mPrimaryDispSync.setPeriod(16666667);
    }

    // initialize our drawing state
    mDrawingState = mCurrentState;

    // set initial conditions (e.g. unblank default device)
    initializeDisplays();

    // start boot animation
    startBootAnim();
}

int32_t SurfaceFlinger::allocateHwcDisplayId(DisplayDevice::DisplayType type) {
    return (uint32_t(type) < DisplayDevice::NUM_BUILTIN_DISPLAY_TYPES) ?
            type : mHwc->allocateDisplayId();
}

void SurfaceFlinger::startBootAnim() {
    // start boot animation
    property_set("service.bootanim.exit", "0");
    property_set("ctl.start", "bootanim");
}

size_t SurfaceFlinger::getMaxTextureSize() const {
    return mRenderEngine->getMaxTextureSize();
}

size_t SurfaceFlinger::getMaxViewportDims() const {
    return mRenderEngine->getMaxViewportDims();
}

// ----------------------------------------------------------------------------

bool SurfaceFlinger::authenticateSurfaceTexture(
        const sp<IGraphicBufferProducer>& bufferProducer) const {
    Mutex::Autolock _l(mStateLock);
    sp<IBinder> surfaceTextureBinder(IInterface::asBinder(bufferProducer));
    return mGraphicBufferProducerList.indexOf(surfaceTextureBinder) >= 0;
}

status_t SurfaceFlinger::getDisplayConfigs(const sp<IBinder>& display,
        Vector<DisplayInfo>* configs) {
    if ((configs == NULL) || (display.get() == NULL)) {
        return BAD_VALUE;
    }

    if (!display.get())
        return NAME_NOT_FOUND;

    int32_t type = NAME_NOT_FOUND;
    for (int i=0 ; i<DisplayDevice::NUM_BUILTIN_DISPLAY_TYPES ; i++) {
        if (display == mBuiltinDisplays[i]) {
            type = i;
            break;
        }
    }

    if (type < 0) {
        return type;
    }

    // TODO: Not sure if display density should handled by SF any longer
    class Density {
        static int getDensityFromProperty(char const* propName) {
            char property[PROPERTY_VALUE_MAX];
            int density = 0;
            if (property_get(propName, property, NULL) > 0) {
                density = atoi(property);
            }
            return density;
        }
    public:
        static int getEmuDensity() {
            return getDensityFromProperty("qemu.sf.lcd_density"); }
        static int getBuildDensity()  {
            return getDensityFromProperty("ro.sf.lcd_density"); }
    };

    configs->clear();

    const Vector<HWComposer::DisplayConfig>& hwConfigs =
            getHwComposer().getConfigs(type);
    for (size_t c = 0; c < hwConfigs.size(); ++c) {
        const HWComposer::DisplayConfig& hwConfig = hwConfigs[c];
        DisplayInfo info = DisplayInfo();

        float xdpi = hwConfig.xdpi;
        float ydpi = hwConfig.ydpi;

        if (type == DisplayDevice::DISPLAY_PRIMARY) {
            // The density of the device is provided by a build property
            float density = Density::getBuildDensity() / 160.0f;
            if (density == 0) {
                // the build doesn't provide a density -- this is wrong!
                // use xdpi instead
                ALOGE("ro.sf.lcd_density must be defined as a build property");
                density = xdpi / 160.0f;
            }
            if (Density::getEmuDensity()) {
                // if "qemu.sf.lcd_density" is specified, it overrides everything
                xdpi = ydpi = density = Density::getEmuDensity();
                density /= 160.0f;
            }
            info.density = density;

            // TODO: this needs to go away (currently needed only by webkit)
            sp<const DisplayDevice> hw(getDefaultDisplayDevice());
            info.orientation = hw->getOrientation();
        } else {
            // TODO: where should this value come from?
            static const int TV_DENSITY = 213;
            info.density = TV_DENSITY / 160.0f;
            info.orientation = 0;
        }

        info.w = hwConfig.width;
        info.h = hwConfig.height;
        info.xdpi = xdpi;
        info.ydpi = ydpi;
        info.fps = float(1e9 / hwConfig.refresh);
        info.appVsyncOffset = VSYNC_EVENT_PHASE_OFFSET_NS;
        info.colorTransform = hwConfig.colorTransform;

        // This is how far in advance a buffer must be queued for
        // presentation at a given time.  If you want a buffer to appear
        // on the screen at time N, you must submit the buffer before
        // (N - presentationDeadline).
        //
        // Normally it's one full refresh period (to give SF a chance to
        // latch the buffer), but this can be reduced by configuring a
        // DispSync offset.  Any additional delays introduced by the hardware
        // composer or panel must be accounted for here.
        //
        // We add an additional 1ms to allow for processing time and
        // differences between the ideal and actual refresh rate.
        info.presentationDeadline =
                hwConfig.refresh - SF_VSYNC_EVENT_PHASE_OFFSET_NS + 1000000;

        // All non-virtual displays are currently considered secure.
        info.secure = true;

        configs->push_back(info);
    }

    return NO_ERROR;
}

status_t SurfaceFlinger::getDisplayStats(const sp<IBinder>& /* display */,
        DisplayStatInfo* stats) {
    if (stats == NULL) {
        return BAD_VALUE;
    }

    // FIXME for now we always return stats for the primary display
    memset(stats, 0, sizeof(*stats));
    stats->vsyncTime   = mPrimaryDispSync.computeNextRefresh(0);
    stats->vsyncPeriod = mPrimaryDispSync.getPeriod();
    return NO_ERROR;
}

int SurfaceFlinger::getActiveConfig(const sp<IBinder>& display) {
    sp<DisplayDevice> device(getDisplayDevice(display));
    if (device != NULL) {
        return device->getActiveConfig();
    }
    return BAD_VALUE;
}

void SurfaceFlinger::setActiveConfigInternal(const sp<DisplayDevice>& hw, int mode) {
    ALOGD("Set active config mode=%d, type=%d flinger=%p", mode, hw->getDisplayType(),
          this);
    int32_t type = hw->getDisplayType();
    int currentMode = hw->getActiveConfig();

    if (mode == currentMode) {
        ALOGD("Screen type=%d is already mode=%d", hw->getDisplayType(), mode);
        return;
    }

    if (type >= DisplayDevice::NUM_BUILTIN_DISPLAY_TYPES) {
        ALOGW("Trying to set config for virtual display");
        return;
    }

    hw->setActiveConfig(mode);
    getHwComposer().setActiveConfig(type, mode);
}

status_t SurfaceFlinger::setActiveConfig(const sp<IBinder>& display, int mode) {
    class MessageSetActiveConfig: public MessageBase {
        SurfaceFlinger& mFlinger;
        sp<IBinder> mDisplay;
        int mMode;
    public:
        MessageSetActiveConfig(SurfaceFlinger& flinger, const sp<IBinder>& disp,
                               int mode) :
            mFlinger(flinger), mDisplay(disp) { mMode = mode; }
        virtual bool handler() {
            Vector<DisplayInfo> configs;
            mFlinger.getDisplayConfigs(mDisplay, &configs);
            if (mMode < 0 || mMode >= static_cast<int>(configs.size())) {
                ALOGE("Attempt to set active config = %d for display with %zu configs",
                        mMode, configs.size());
            }
            sp<DisplayDevice> hw(mFlinger.getDisplayDevice(mDisplay));
            if (hw == NULL) {
                ALOGE("Attempt to set active config = %d for null display %p",
                        mMode, mDisplay.get());
            } else if (hw->getDisplayType() >= DisplayDevice::DISPLAY_VIRTUAL) {
                ALOGW("Attempt to set active config = %d for virtual display",
                        mMode);
            } else {
                mFlinger.setActiveConfigInternal(hw, mMode);
            }
            return true;
        }
    };
    sp<MessageBase> msg = new MessageSetActiveConfig(*this, display, mode);
    postMessageSync(msg);
    return NO_ERROR;
}

status_t SurfaceFlinger::clearAnimationFrameStats() {
    Mutex::Autolock _l(mStateLock);
    mAnimFrameTracker.clearStats();
    return NO_ERROR;
}

status_t SurfaceFlinger::getAnimationFrameStats(FrameStats* outStats) const {
    Mutex::Autolock _l(mStateLock);
    mAnimFrameTracker.getStats(outStats);
    return NO_ERROR;
}

// ----------------------------------------------------------------------------

sp<IDisplayEventConnection> SurfaceFlinger::createDisplayEventConnection() {
    return mEventThread->createEventConnection();
}

// ----------------------------------------------------------------------------

void SurfaceFlinger::waitForEvent() {
    mEventQueue.waitMessage();
}

void SurfaceFlinger::signalTransaction() {
    mEventQueue.invalidate();
}

void SurfaceFlinger::signalLayerUpdate() {
    mEventQueue.invalidate();
}

void SurfaceFlinger::signalRefresh() {
    mEventQueue.refresh();
}

status_t SurfaceFlinger::postMessageAsync(const sp<MessageBase>& msg,
        nsecs_t reltime, uint32_t /* flags */) {
    return mEventQueue.postMessage(msg, reltime);
}

status_t SurfaceFlinger::postMessageSync(const sp<MessageBase>& msg,
        nsecs_t reltime, uint32_t /* flags */) {
    status_t res = mEventQueue.postMessage(msg, reltime);
    if (res == NO_ERROR) {
        msg->wait();
    }
    return res;
}

void SurfaceFlinger::run() {
    do {
        waitForEvent();
    } while (true);
}

void SurfaceFlinger::enableHardwareVsync() {
    Mutex::Autolock _l(mHWVsyncLock);
    if (!mPrimaryHWVsyncEnabled && mHWVsyncAvailable) {
        mPrimaryDispSync.beginResync();
        //eventControl(HWC_DISPLAY_PRIMARY, SurfaceFlinger::EVENT_VSYNC, true);
        mEventControlThread->setVsyncEnabled(true);
        mPrimaryHWVsyncEnabled = true;
    }
}

void SurfaceFlinger::resyncToHardwareVsync(bool makeAvailable) {
    Mutex::Autolock _l(mHWVsyncLock);

    if (makeAvailable) {
        mHWVsyncAvailable = true;
    } else if (!mHWVsyncAvailable) {
        ALOGE("resyncToHardwareVsync called when HW vsync unavailable");
        return;
    }

    const nsecs_t period =
            getHwComposer().getRefreshPeriod(HWC_DISPLAY_PRIMARY);

    mPrimaryDispSync.reset();
    mPrimaryDispSync.setPeriod(period);

    if (!mPrimaryHWVsyncEnabled) {
        mPrimaryDispSync.beginResync();
        //eventControl(HWC_DISPLAY_PRIMARY, SurfaceFlinger::EVENT_VSYNC, true);
        mEventControlThread->setVsyncEnabled(true);
        mPrimaryHWVsyncEnabled = true;
    }
}

void SurfaceFlinger::disableHardwareVsync(bool makeUnavailable) {
    Mutex::Autolock _l(mHWVsyncLock);
    if (mPrimaryHWVsyncEnabled) {
        //eventControl(HWC_DISPLAY_PRIMARY, SurfaceFlinger::EVENT_VSYNC, false);
        mEventControlThread->setVsyncEnabled(false);
        mPrimaryDispSync.endResync();
        mPrimaryHWVsyncEnabled = false;
    }
    if (makeUnavailable) {
        mHWVsyncAvailable = false;
    }
}

void SurfaceFlinger::onVSyncReceived(int type, nsecs_t timestamp) {
    bool needsHwVsync = false;

    { // Scope for the lock
        Mutex::Autolock _l(mHWVsyncLock);
        if (type == 0 && mPrimaryHWVsyncEnabled) {
            needsHwVsync = mPrimaryDispSync.addResyncSample(timestamp);
        }
    }

    if (needsHwVsync) {
        enableHardwareVsync();
    } else {
        disableHardwareVsync(false);
    }
}

void SurfaceFlinger::onHotplugReceived(int type, bool connected) {
    if (mEventThread == NULL) {
        // This is a temporary workaround for b/7145521.  A non-null pointer
        // does not mean EventThread has finished initializing, so this
        // is not a correct fix.
        ALOGW("WARNING: EventThread not started, ignoring hotplug");
        return;
    }

    if (uint32_t(type) < DisplayDevice::NUM_BUILTIN_DISPLAY_TYPES) {
        Mutex::Autolock _l(mStateLock);
        if (connected) {
            createBuiltinDisplayLocked((DisplayDevice::DisplayType)type);
        } else {
            mCurrentState.displays.removeItem(mBuiltinDisplays[type]);
            mBuiltinDisplays[type].clear();
        }
        setTransactionFlags(eDisplayTransactionNeeded);

        // Defer EventThread notification until SF has updated mDisplays.
    }
}

void SurfaceFlinger::eventControl(int disp, int event, int enabled) {
    ATRACE_CALL();
    getHwComposer().eventControl(disp, event, enabled);
}

void SurfaceFlinger::onMessageReceived(int32_t what) {
    ATRACE_CALL();
    switch (what) {
        case MessageQueue::TRANSACTION: {
            handleMessageTransaction();
            break;
        }
        case MessageQueue::INVALIDATE: {
            bool refreshNeeded = handleMessageTransaction();
            refreshNeeded |= handleMessageInvalidate();
            refreshNeeded |= mRepaintEverything;
            if (refreshNeeded) {
                // Signal a refresh if a transaction modified the window state,
                // a new buffer was latched, or if HWC has requested a full
                // repaint
                signalRefresh();
            }
            break;
        }
        case MessageQueue::REFRESH: {
            handleMessageRefresh();
            break;
        }
    }
}

bool SurfaceFlinger::handleMessageTransaction() {
    uint32_t transactionFlags = peekTransactionFlags(eTransactionMask);
    if (transactionFlags) {
        handleTransaction(transactionFlags);
        return true;
    }
    return false;
}

bool SurfaceFlinger::handleMessageInvalidate() {
    ATRACE_CALL();
    return handlePageFlip();
}

void SurfaceFlinger::handleMessageRefresh() {
    ATRACE_CALL();

    static nsecs_t previousExpectedPresent = 0;
    nsecs_t expectedPresent = mPrimaryDispSync.computeNextRefresh(0);
    static bool previousFrameMissed = false;
    bool frameMissed = (expectedPresent == previousExpectedPresent);
    if (frameMissed != previousFrameMissed) {
        ATRACE_INT("FrameMissed", static_cast<int>(frameMissed));
    }
    previousFrameMissed = frameMissed;

    if (CC_UNLIKELY(mDropMissedFrames && frameMissed)) {
        // Latch buffers, but don't send anything to HWC, then signal another
        // wakeup for the next vsync
        preComposition();
        repaintEverything();
    } else {
        preComposition();
        rebuildLayerStacks();
        setUpHWComposer();
        doDebugFlashRegions();
        doComposition();
        postComposition();
    }

    previousExpectedPresent = mPrimaryDispSync.computeNextRefresh(0);
}

void SurfaceFlinger::doDebugFlashRegions()
{
    // is debugging enabled
    if (CC_LIKELY(!mDebugRegion))
        return;

    const bool repaintEverything = mRepaintEverything;
    for (size_t dpy=0 ; dpy<mDisplays.size() ; dpy++) {
        const sp<DisplayDevice>& hw(mDisplays[dpy]);
        if (hw->isDisplayOn()) {
            // transform the dirty region into this screen's coordinate space
            const Region dirtyRegion(hw->getDirtyRegion(repaintEverything));
            if (!dirtyRegion.isEmpty()) {
                // redraw the whole screen
                doComposeSurfaces(hw, Region(hw->bounds()));

                // and draw the dirty region
                const int32_t height = hw->getHeight();
                RenderEngine& engine(getRenderEngine());
                engine.fillRegionWithColor(dirtyRegion, height, 1, 0, 1, 1);

                hw->compositionComplete();
                hw->swapBuffers(getHwComposer());
            }
        }
    }

    postFramebuffer();

    if (mDebugRegion > 1) {
        usleep(mDebugRegion * 1000);
    }

    HWComposer& hwc(getHwComposer());
    if (hwc.initCheck() == NO_ERROR) {
        status_t err = hwc.prepare();
        ALOGE_IF(err, "HWComposer::prepare failed (%s)", strerror(-err));
    }
}

void SurfaceFlinger::preComposition()
{
    bool needExtraInvalidate = false;
    const LayerVector& layers(mDrawingState.layersSortedByZ);
    const size_t count = layers.size();
    for (size_t i=0 ; i<count ; i++) {
        if (layers[i]->onPreComposition()) {
            needExtraInvalidate = true;
        }
    }
    if (needExtraInvalidate) {
        signalLayerUpdate();
    }
}

void SurfaceFlinger::postComposition()
{
    const LayerVector& layers(mDrawingState.layersSortedByZ);
    const size_t count = layers.size();
    for (size_t i=0 ; i<count ; i++) {
        layers[i]->onPostComposition();
    }

    const HWComposer& hwc = getHwComposer();
    sp<Fence> presentFence = hwc.getDisplayFence(HWC_DISPLAY_PRIMARY);

    if (presentFence->isValid()) {
        if (mPrimaryDispSync.addPresentFence(presentFence)) {
            enableHardwareVsync();
        } else {
            disableHardwareVsync(false);
        }
    }

    const sp<const DisplayDevice> hw(getDefaultDisplayDevice());
    if (kIgnorePresentFences) {
        if (hw->isDisplayOn()) {
            enableHardwareVsync();
        }
    }

    if (mAnimCompositionPending) {
        mAnimCompositionPending = false;

        if (presentFence->isValid()) {
            mAnimFrameTracker.setActualPresentFence(presentFence);
        } else {
            // The HWC doesn't support present fences, so use the refresh
            // timestamp instead.
            nsecs_t presentTime = hwc.getRefreshTimestamp(HWC_DISPLAY_PRIMARY);
            mAnimFrameTracker.setActualPresentTime(presentTime);
        }
        mAnimFrameTracker.advanceFrame();
    }

    if (hw->getPowerMode() == HWC_POWER_MODE_OFF) {
        return;
    }

    nsecs_t currentTime = systemTime();
    if (mHasPoweredOff) {
        mHasPoweredOff = false;
    } else {
        nsecs_t period = mPrimaryDispSync.getPeriod();
        nsecs_t elapsedTime = currentTime - mLastSwapTime;
        size_t numPeriods = static_cast<size_t>(elapsedTime / period);
        if (numPeriods < NUM_BUCKETS - 1) {
            mFrameBuckets[numPeriods] += elapsedTime;
        } else {
            mFrameBuckets[NUM_BUCKETS - 1] += elapsedTime;
        }
        mTotalTime += elapsedTime;
    }
    mLastSwapTime = currentTime;
}

void SurfaceFlinger::rebuildLayerStacks() {
    // rebuild the visible layer list per screen
    if (CC_UNLIKELY(mVisibleRegionsDirty)) {
        ATRACE_CALL();
        mVisibleRegionsDirty = false;
        invalidateHwcGeometry();

        const LayerVector& layers(mDrawingState.layersSortedByZ);
        for (size_t dpy=0 ; dpy<mDisplays.size() ; dpy++) {
            Region opaqueRegion;
            Region dirtyRegion;
            Vector< sp<Layer> > layersSortedByZ;
            const sp<DisplayDevice>& hw(mDisplays[dpy]);
            const Transform& tr(hw->getTransform());
            const Rect bounds(hw->getBounds());
            if (hw->isDisplayOn()) {
                SurfaceFlinger::computeVisibleRegions(layers,
                        hw, dirtyRegion, opaqueRegion);

                const size_t count = layers.size();
                for (size_t i=0 ; i<count ; i++) {
                    const sp<Layer>& layer(layers[i]);
                    const Layer::State& s(layer->getDrawingState());
                    if (layer->getSystemName() == hw->getActiveSystemName()&&
                        s.layerStack == hw->getLayerStack()) {
                        Region drawRegion(tr.transform(
                                layer->visibleNonTransparentRegion));
                        drawRegion.andSelf(bounds);
                        if (!drawRegion.isEmpty()) {
                            layersSortedByZ.add(layer);
                        }
                    }
                }
            }
            hw->setVisibleLayersSortedByZ(layersSortedByZ);
            hw->undefinedRegion.set(bounds);
            hw->undefinedRegion.subtractSelf(tr.transform(opaqueRegion));
            hw->dirtyRegion.orSelf(dirtyRegion);
        }
    }
}

void SurfaceFlinger::setUpHWComposer() {
    for (size_t dpy=0 ; dpy<mDisplays.size() ; dpy++) {
        bool dirty = !mDisplays[dpy]->getDirtyRegion(false).isEmpty();
        bool empty = mDisplays[dpy]->getVisibleLayersSortedByZ().size() == 0;
        bool wasEmpty = !mDisplays[dpy]->lastCompositionHadVisibleLayers;

        // If nothing has changed (!dirty), don't recompose.
        // If something changed, but we don't currently have any visible layers,
        //   and didn't when we last did a composition, then skip it this time.
        // The second rule does two things:
        // - When all layers are removed from a display, we'll emit one black
        //   frame, then nothing more until we get new layers.
        // - When a display is created with a private layer stack, we won't
        //   emit any black frames until a layer is added to the layer stack.
        bool mustRecompose = dirty && !(empty && wasEmpty);

        ALOGV_IF(mDisplays[dpy]->getDisplayType() == DisplayDevice::DISPLAY_VIRTUAL,
                "dpy[%zu]: %s composition (%sdirty %sempty %swasEmpty)", dpy,
                mustRecompose ? "doing" : "skipping",
                dirty ? "+" : "-",
                empty ? "+" : "-",
                wasEmpty ? "+" : "-");

        mDisplays[dpy]->beginFrame(mustRecompose);

        if (mustRecompose) {
            mDisplays[dpy]->lastCompositionHadVisibleLayers = !empty;
        }
    }

    HWComposer& hwc(getHwComposer());
    if (hwc.initCheck() == NO_ERROR) {
        // build the h/w work list
        if (CC_UNLIKELY(mHwWorkListDirty)) {
            mHwWorkListDirty = false;
            for (size_t dpy=0 ; dpy<mDisplays.size() ; dpy++) {
                sp<const DisplayDevice> hw(mDisplays[dpy]);
                const int32_t id = hw->getHwcDisplayId();
                if (id >= 0) {
                    const Vector< sp<Layer> >& currentLayers(
                        hw->getVisibleLayersSortedByZ());
                    const size_t count = currentLayers.size();
                    if (hwc.createWorkList(id, count) == NO_ERROR) {
                        HWComposer::LayerListIterator cur = hwc.begin(id);
                        const HWComposer::LayerListIterator end = hwc.end(id);
                        for (size_t i=0 ; cur!=end && i<count ; ++i, ++cur) {
                            const sp<Layer>& layer(currentLayers[i]);
                            layer->setGeometry(hw, *cur);
                            if (mDebugDisableHWC || mDebugRegion || mDaltonize || mHasColorMatrix) {
                                cur->setSkip(true);
                            }
                        }
                    }
                }
            }
        }

        // set the per-frame data
        for (size_t dpy=0 ; dpy<mDisplays.size() ; dpy++) {
            sp<const DisplayDevice> hw(mDisplays[dpy]);
            const int32_t id = hw->getHwcDisplayId();
            if (id >= 0) {
                const Vector< sp<Layer> >& currentLayers(
                    hw->getVisibleLayersSortedByZ());
                const size_t count = currentLayers.size();
                HWComposer::LayerListIterator cur = hwc.begin(id);
                const HWComposer::LayerListIterator end = hwc.end(id);
                for (size_t i=0 ; cur!=end && i<count ; ++i, ++cur) {
                    /*
                     * update the per-frame h/w composer data for each layer
                     * and build the transparent region of the FB
                     */
                    const sp<Layer>& layer(currentLayers[i]);
                    layer->setPerFrameData(hw, *cur);
                }
            }
        }

        // If possible, attempt to use the cursor overlay on each display.
        for (size_t dpy=0 ; dpy<mDisplays.size() ; dpy++) {
            sp<const DisplayDevice> hw(mDisplays[dpy]);
            const int32_t id = hw->getHwcDisplayId();
            if (id >= 0) {
                const Vector< sp<Layer> >& currentLayers(
                    hw->getVisibleLayersSortedByZ());
                const size_t count = currentLayers.size();
                HWComposer::LayerListIterator cur = hwc.begin(id);
                const HWComposer::LayerListIterator end = hwc.end(id);
                for (size_t i=0 ; cur!=end && i<count ; ++i, ++cur) {
                    const sp<Layer>& layer(currentLayers[i]);
                    if (layer->isPotentialCursor()) {
                        cur->setIsCursorLayerHint();
                        break;
                    }
                }
            }
        }

        status_t err = hwc.prepare();
        ALOGE_IF(err, "HWComposer::prepare failed (%s)", strerror(-err));

        for (size_t dpy=0 ; dpy<mDisplays.size() ; dpy++) {
            sp<const DisplayDevice> hw(mDisplays[dpy]);
            hw->prepareFrame(hwc);
        }
    }
}

void SurfaceFlinger::doComposition() {
    ATRACE_CALL();
    const bool repaintEverything = android_atomic_and(0, &mRepaintEverything);
    for (size_t dpy=0 ; dpy<mDisplays.size() ; dpy++) {
        const sp<DisplayDevice>& hw(mDisplays[dpy]);
        if (hw->isDisplayOn()) {
            // transform the dirty region into this screen's coordinate space
            const Region dirtyRegion(hw->getDirtyRegion(repaintEverything));

            // repaint the framebuffer (if needed)
            doDisplayComposition(hw, dirtyRegion);

            hw->dirtyRegion.clear();
            hw->flip(hw->swapRegion);
            hw->swapRegion.clear();
        }
        // inform the h/w that we're done compositing
        hw->compositionComplete();
    }

    postFramebuffer();
}

void SurfaceFlinger::postFramebuffer()
{
    ATRACE_CALL();

    const nsecs_t now = systemTime();
    mDebugInSwapBuffers = now;

    HWComposer& hwc(getHwComposer());
    if (hwc.initCheck() == NO_ERROR) {
        if (!hwc.supportsFramebufferTarget()) {
            // EGL spec says:
            //   "surface must be bound to the calling thread's current context,
            //    for the current rendering API."
            getDefaultDisplayDevice()->makeCurrent(mEGLDisplay, mEGLContext);
        }
        
        hwc.commit();
    }

    // make the default display current because the VirtualDisplayDevice code cannot
    // deal with dequeueBuffer() being called outside of the composition loop; however
    // the code below can call glFlush() which is allowed (and does in some case) call
    // dequeueBuffer().
    getDefaultDisplayDevice()->makeCurrent(mEGLDisplay, mEGLContext);

    for (size_t dpy=0 ; dpy<mDisplays.size() ; dpy++) {
        sp<const DisplayDevice> hw(mDisplays[dpy]);
        const Vector< sp<Layer> >& currentLayers(hw->getVisibleLayersSortedByZ());
        hw->onSwapBuffersCompleted(hwc);
        const size_t count = currentLayers.size();
        int32_t id = hw->getHwcDisplayId();
        if (id >=0 && hwc.initCheck() == NO_ERROR) {
            HWComposer::LayerListIterator cur = hwc.begin(id);
            const HWComposer::LayerListIterator end = hwc.end(id);
            for (size_t i = 0; cur != end && i < count; ++i, ++cur) {
                currentLayers[i]->onLayerDisplayed(hw, &*cur);
            }
        } else {
            for (size_t i = 0; i < count; i++) {
                currentLayers[i]->onLayerDisplayed(hw, NULL);
            }
        }
    }

    mLastSwapBufferTime = systemTime() - now;
    mDebugInSwapBuffers = 0;

    uint32_t flipCount = getDefaultDisplayDevice()->getPageFlipCount();
    if (flipCount % LOG_FRAME_STATS_PERIOD == 0) {
        logFrameStats();
    }
}

void SurfaceFlinger::handleTransaction(uint32_t transactionFlags)
{
    ATRACE_CALL();

    // here we keep a copy of the drawing state (that is the state that's
    // going to be overwritten by handleTransactionLocked()) outside of
    // mStateLock so that the side-effects of the State assignment
    // don't happen with mStateLock held (which can cause deadlocks).
    State drawingState(mDrawingState);

    Mutex::Autolock _l(mStateLock);
    const nsecs_t now = systemTime();
    mDebugInTransaction = now;

    // Here we're guaranteed that some transaction flags are set
    // so we can call handleTransactionLocked() unconditionally.
    // We call getTransactionFlags(), which will also clear the flags,
    // with mStateLock held to guarantee that mCurrentState won't change
    // until the transaction is committed.

    transactionFlags = getTransactionFlags(eTransactionMask);
    handleTransactionLocked(transactionFlags);

    mLastTransactionTime = systemTime() - now;
    mDebugInTransaction = 0;
    invalidateHwcGeometry();
    // here the transaction has been committed
}

void SurfaceFlinger::handleTransactionLocked(uint32_t transactionFlags)
{
    const LayerVector& currentLayers(mCurrentState.layersSortedByZ);
    const size_t count = currentLayers.size();

    /*
     * Traversal of the children
     * (perform the transaction for each of them if needed)
     */

    if (transactionFlags & eTraversalNeeded) {
        for (size_t i=0 ; i<count ; i++) {
            const sp<Layer>& layer(currentLayers[i]);
            uint32_t trFlags = layer->getTransactionFlags(eTransactionNeeded);
            if (!trFlags) continue;

            const uint32_t flags = layer->doTransaction(0);
            if (flags & Layer::eVisibleRegion)
                mVisibleRegionsDirty = true;
        }
    }

    /*
     * Perform display own transactions if needed
     */

    if (transactionFlags & eDisplayTransactionNeeded) {
        // here we take advantage of Vector's copy-on-write semantics to
        // improve performance by skipping the transaction entirely when
        // know that the lists are identical
        const KeyedVector<  wp<IBinder>, DisplayDeviceState>& curr(mCurrentState.displays);
        const KeyedVector<  wp<IBinder>, DisplayDeviceState>& draw(mDrawingState.displays);
        if (!curr.isIdenticalTo(draw)) {
            mVisibleRegionsDirty = true;
            const size_t cc = curr.size();
                  size_t dc = draw.size();

            // find the displays that were removed
            // (ie: in drawing state but not in current state)
            // also handle displays that changed
            // (ie: displays that are in both lists)
            for (size_t i=0 ; i<dc ; i++) {
                const ssize_t j = curr.indexOfKey(draw.keyAt(i));
                if (j < 0) {
                    // in drawing state but not in current state
                    if (!draw[i].isMainDisplay()) {
                        // Call makeCurrent() on the primary display so we can
                        // be sure that nothing associated with this display
                        // is current.
                        const sp<const DisplayDevice> defaultDisplay(getDefaultDisplayDevice());
                        defaultDisplay->makeCurrent(mEGLDisplay, mEGLContext);
                        sp<DisplayDevice> hw(getDisplayDevice(draw.keyAt(i)));
                        if (hw != NULL)
                            hw->disconnect(getHwComposer());
                        if (draw[i].type < DisplayDevice::NUM_BUILTIN_DISPLAY_TYPES)
                            mEventThread->onHotplugReceived(draw[i].type, false);
                        mDisplays.removeItem(draw.keyAt(i));
                    } else {
                        ALOGW("trying to remove the main display");
                    }
                } else {
                    // this display is in both lists. see if something changed.
                    const DisplayDeviceState& state(curr[j]);
                    const wp<IBinder>& display(curr.keyAt(j));
                    const sp<IBinder> state_binder = IInterface::asBinder(state.surface);
                    const sp<IBinder> draw_binder = IInterface::asBinder(draw[i].surface);
                    if (state_binder != draw_binder) {
                        // changing the surface is like destroying and
                        // recreating the DisplayDevice, so we just remove it
                        // from the drawing state, so that it get re-added
                        // below.
                        sp<DisplayDevice> hw(getDisplayDevice(display));
                        if (hw != NULL)
                            hw->disconnect(getHwComposer());
                        mDisplays.removeItem(display);
                        mDrawingState.displays.removeItemsAt(i);
                        dc--; i--;
                        // at this point we must loop to the next item
                        continue;
                    }

                    const sp<DisplayDevice> disp(getDisplayDevice(display));
                    if (disp != NULL) {
                        if (state.layerStack != draw[i].layerStack) {
                            disp->setLayerStack(state.layerStack);
                        }
                        if ((state.orientation != draw[i].orientation)
                                || (state.viewport != draw[i].viewport)
                                || (state.frame != draw[i].frame))
                        {
                            disp->setProjection(state.orientation,
                                    state.viewport, state.frame);
                        }
                        if (state.width != draw[i].width || state.height != draw[i].height) {
                            disp->setDisplaySize(state.width, state.height);
                        }
                    }
                }
            }

            // find displays that were added
            // (ie: in current state but not in drawing state)
            for (size_t i=0 ; i<cc ; i++) {
                if (draw.indexOfKey(curr.keyAt(i)) < 0) {
                    const DisplayDeviceState& state(curr[i]);

                    sp<DisplaySurface> dispSurface;
                    sp<IGraphicBufferProducer> producer;
                    sp<IGraphicBufferProducer> bqProducer;
                    sp<IGraphicBufferConsumer> bqConsumer;
                    BufferQueue::createBufferQueue(&bqProducer, &bqConsumer,
                            new GraphicBufferAlloc());

                    int32_t hwcDisplayId = -1;
                    if (state.isVirtualDisplay()) {
                        // Virtual displays without a surface are dormant:
                        // they have external state (layer stack, projection,
                        // etc.) but no internal state (i.e. a DisplayDevice).
                        if (state.surface != NULL) {

                            int width = 0;
                            int status = state.surface->query(
                                    NATIVE_WINDOW_WIDTH, &width);
                            ALOGE_IF(status != NO_ERROR,
                                    "Unable to query width (%d)", status);
                            int height = 0;
                            status = state.surface->query(
                                    NATIVE_WINDOW_HEIGHT, &height);
                            ALOGE_IF(status != NO_ERROR,
                                    "Unable to query height (%d)", status);
                            if (MAX_VIRTUAL_DISPLAY_DIMENSION == 0 ||
                                    (width <= MAX_VIRTUAL_DISPLAY_DIMENSION &&
                                     height <= MAX_VIRTUAL_DISPLAY_DIMENSION)) {
                                hwcDisplayId = allocateHwcDisplayId(state.type);
                            }

                            sp<VirtualDisplaySurface> vds = new VirtualDisplaySurface(
                                    *mHwc, hwcDisplayId, state.surface,
                                    bqProducer, bqConsumer, state.displayName);

                            dispSurface = vds;
                            producer = vds;
                        }
                    } else {
                        ALOGE_IF(state.surface!=NULL,
                                "adding a supported display, but rendering "
                                "surface is provided (%p), ignoring it",
                                state.surface.get());
                        hwcDisplayId = allocateHwcDisplayId(state.type);
                        // for supported (by hwc) displays we provide our
                        // own rendering surface
                        dispSurface = new FramebufferSurface(*mHwc, state.type,
                                bqConsumer);
                        producer = bqProducer;
                    }

                    const wp<IBinder>& display(curr.keyAt(i));
                    if (dispSurface != NULL) {
                        sp<DisplayDevice> hw = new DisplayDevice(this,
                                state.type, hwcDisplayId,
                                mHwc->getFormat(hwcDisplayId), state.isSecure,
                                display, dispSurface, producer,
                                mRenderEngine->getEGLConfig());
                        hw->setLayerStack(state.layerStack);
                        hw->setProjection(state.orientation,
                                state.viewport, state.frame);
                        hw->setDisplayName(state.displayName);
                        mDisplays.add(display, hw);
                        if (state.isVirtualDisplay()) {
                            if (hwcDisplayId >= 0) {
                                mHwc->setVirtualDisplayProperties(hwcDisplayId,
                                        hw->getWidth(), hw->getHeight(),
                                        hw->getFormat());
                            }
                        } else {
                            mEventThread->onHotplugReceived(state.type, true);
                        }
                    }
                }
            }
        }
    }

    if (transactionFlags & (eTraversalNeeded|eDisplayTransactionNeeded)) {
        // The transform hint might have changed for some layers
        // (either because a display has changed, or because a layer
        // as changed).
        //
        // Walk through all the layers in currentLayers,
        // and update their transform hint.
        //
        // If a layer is visible only on a single display, then that
        // display is used to calculate the hint, otherwise we use the
        // default display.
        //
        // NOTE: we do this here, rather than in rebuildLayerStacks() so that
        // the hint is set before we acquire a buffer from the surface texture.
        //
        // NOTE: layer transactions have taken place already, so we use their
        // drawing state. However, SurfaceFlinger's own transaction has not
        // happened yet, so we must use the current state layer list
        // (soon to become the drawing state list).
        //
        sp<const DisplayDevice> disp;
        uint32_t currentlayerStack = 0;
        for (size_t i=0; i<count; i++) {
            // NOTE: we rely on the fact that layers are sorted by
            // layerStack first (so we don't have to traverse the list
            // of displays for every layer).
            const sp<Layer>& layer(currentLayers[i]);
            uint32_t layerStack = layer->getDrawingState().layerStack;
            if (i==0 || currentlayerStack != layerStack) {
                currentlayerStack = layerStack;
                // figure out if this layerstack is mirrored
                // (more than one display) if so, pick the default display,
                // if not, pick the only display it's on.
                disp.clear();
                for (size_t dpy=0 ; dpy<mDisplays.size() ; dpy++) {
                    sp<const DisplayDevice> hw(mDisplays[dpy]);
                    if (hw->getLayerStack() == currentlayerStack) {
                        if (disp == NULL) {
                            disp = hw;
                        } else {
                            disp = NULL;
                            break;
                        }
                    }
                }
            }
            if (disp == NULL) {
                // NOTE: TEMPORARY FIX ONLY. Real fix should cause layers to
                // redraw after transform hint changes. See bug 8508397.

                // could be null when this layer is using a layerStack
                // that is not visible on any display. Also can occur at
                // screen off/on times.
                disp = getDefaultDisplayDevice();
            }
            layer->updateTransformHint(disp);
        }
    }


    /*
     * Perform our own transaction if needed
     */

    const LayerVector& layers(mDrawingState.layersSortedByZ);
    if (currentLayers.size() > layers.size()) {
        // layers have been added
        mVisibleRegionsDirty = true;
    }

    // some layers might have been removed, so
    // we need to update the regions they're exposing.
    if (mLayersRemoved) {
        mLayersRemoved = false;
        mVisibleRegionsDirty = true;
        const size_t count = layers.size();
        for (size_t i=0 ; i<count ; i++) {
            const sp<Layer>& layer(layers[i]);
            if (currentLayers.indexOf(layer) < 0) {
                // this layer is not visible anymore
                // TODO: we could traverse the tree from front to back and
                //       compute the actual visible region
                // TODO: we could cache the transformed region
                const Layer::State& s(layer->getDrawingState());
                Region visibleReg = s.transform.transform(
                        Region(Rect(s.active.w, s.active.h)));
                invalidateLayerStack(s.layerStack, visibleReg);
            }
        }
    }

    commitTransaction();

    updateCursorAsync();
}

void SurfaceFlinger::updateCursorAsync()
{
    HWComposer& hwc(getHwComposer());
    for (size_t dpy=0 ; dpy<mDisplays.size() ; dpy++) {
        sp<const DisplayDevice> hw(mDisplays[dpy]);
        const int32_t id = hw->getHwcDisplayId();
        if (id < 0) {
            continue;
        }
        const Vector< sp<Layer> >& currentLayers(
            hw->getVisibleLayersSortedByZ());
        const size_t count = currentLayers.size();
        HWComposer::LayerListIterator cur = hwc.begin(id);
        const HWComposer::LayerListIterator end = hwc.end(id);
        for (size_t i=0 ; cur!=end && i<count ; ++i, ++cur) {
            if (cur->getCompositionType() != HWC_CURSOR_OVERLAY) {
                continue;
            }
            const sp<Layer>& layer(currentLayers[i]);
            Rect cursorPos = layer->getPosition(hw);
            hwc.setCursorPositionAsync(id, cursorPos);
            break;
        }
    }
}

void SurfaceFlinger::commitTransaction()
{
    if (!mLayersPendingRemoval.isEmpty()) {
        // Notify removed layers now that they can't be drawn from
        for (size_t i = 0; i < mLayersPendingRemoval.size(); i++) {
            mLayersPendingRemoval[i]->onRemoved();
        }
        mLayersPendingRemoval.clear();
    }

    // If this transaction is part of a window animation then the next frame
    // we composite should be considered an animation as well.
    mAnimCompositionPending = mAnimTransactionPending;

    mDrawingState = mCurrentState;
    mTransactionPending = false;
    mAnimTransactionPending = false;
    mTransactionCV.broadcast();
}

void SurfaceFlinger::computeVisibleRegions(
        const LayerVector& currentLayers, const sp<const DisplayDevice>& hw,
        Region& outDirtyRegion, Region& outOpaqueRegion)
{
    ATRACE_CALL();

    Region aboveOpaqueLayers;
    Region aboveCoveredLayers;
    Region dirty;

    outDirtyRegion.clear();

    size_t i = currentLayers.size();
    while (i--) {
        const sp<Layer>& layer = currentLayers[i];

        // start with the whole surface at its current location
        const Layer::State& s(layer->getDrawingState());

        // only consider the layers on the given layer stack
        if (s.layerStack !=  hw->getLayerStack())
            continue;

        if (layer->getSystemName() != hw->getActiveSystemName())
            continue;

        /*
         * opaqueRegion: area of a surface that is fully opaque.
         */
        Region opaqueRegion;

        /*
         * visibleRegion: area of a surface that is visible on screen
         * and not fully transparent. This is essentially the layer's
         * footprint minus the opaque regions above it.
         * Areas covered by a translucent surface are considered visible.
         */
        Region visibleRegion;

        /*
         * coveredRegion: area of a surface that is covered by all
         * visible regions above it (which includes the translucent areas).
         */
        Region coveredRegion;

        /*
         * transparentRegion: area of a surface that is hinted to be completely
         * transparent. This is only used to tell when the layer has no visible
         * non-transparent regions and can be removed from the layer list. It
         * does not affect the visibleRegion of this layer or any layers
         * beneath it. The hint may not be correct if apps don't respect the
         * SurfaceView restrictions (which, sadly, some don't).
         */
        Region transparentRegion;


        // handle hidden surfaces by setting the visible region to empty
        if (CC_LIKELY(layer->isVisible())) {
            const bool translucent = !layer->isOpaque(s);
            Rect bounds(s.transform.transform(layer->computeBounds()));
            visibleRegion.set(bounds);
            if (!visibleRegion.isEmpty()) {
                // Remove the transparent area from the visible region
                if (translucent) {
                    const Transform tr(s.transform);
                    if (tr.transformed()) {
                        if (tr.preserveRects()) {
                            // transform the transparent region
                            transparentRegion = tr.transform(s.activeTransparentRegion);
                        } else {
                            // transformation too complex, can't do the
                            // transparent region optimization.
                            transparentRegion.clear();
                        }
                    } else {
                        transparentRegion = s.activeTransparentRegion;
                    }
                }

                // compute the opaque region
                const int32_t layerOrientation = s.transform.getOrientation();
                if (s.alpha==255 && !translucent &&
                        ((layerOrientation & Transform::ROT_INVALID) == false)) {
                    // the opaque region is the layer's footprint
                    opaqueRegion = visibleRegion;
                }
            }
        }

        // Clip the covered region to the visible region
        coveredRegion = aboveCoveredLayers.intersect(visibleRegion);

        // Update aboveCoveredLayers for next (lower) layer
        aboveCoveredLayers.orSelf(visibleRegion);

        // subtract the opaque region covered by the layers above us
        visibleRegion.subtractSelf(aboveOpaqueLayers);

        // compute this layer's dirty region
        if (layer->contentDirty) {
            // we need to invalidate the whole region
            dirty = visibleRegion;
            // as well, as the old visible region
            dirty.orSelf(layer->visibleRegion);
            layer->contentDirty = false;
        } else {
            /* compute the exposed region:
             *   the exposed region consists of two components:
             *   1) what's VISIBLE now and was COVERED before
             *   2) what's EXPOSED now less what was EXPOSED before
             *
             * note that (1) is conservative, we start with the whole
             * visible region but only keep what used to be covered by
             * something -- which mean it may have been exposed.
             *
             * (2) handles areas that were not covered by anything but got
             * exposed because of a resize.
             */
            const Region newExposed = visibleRegion - coveredRegion;
            const Region oldVisibleRegion = layer->visibleRegion;
            const Region oldCoveredRegion = layer->coveredRegion;
            const Region oldExposed = oldVisibleRegion - oldCoveredRegion;
            dirty = (visibleRegion&oldCoveredRegion) | (newExposed-oldExposed);
        }
        dirty.subtractSelf(aboveOpaqueLayers);

        // accumulate to the screen dirty region
        outDirtyRegion.orSelf(dirty);

        // Update aboveOpaqueLayers for next (lower) layer
        aboveOpaqueLayers.orSelf(opaqueRegion);

        // Store the visible region in screen space
        layer->setVisibleRegion(visibleRegion);
        layer->setCoveredRegion(coveredRegion);
        layer->setVisibleNonTransparentRegion(
                visibleRegion.subtract(transparentRegion));
    }

    outOpaqueRegion = aboveOpaqueLayers;
}

void SurfaceFlinger::invalidateLayerStack(uint32_t layerStack,
        const Region& dirty) {
    for (size_t dpy=0 ; dpy<mDisplays.size() ; dpy++) {
        const sp<DisplayDevice>& hw(mDisplays[dpy]);
        if (hw->getLayerStack() == layerStack) {
            hw->dirtyRegion.orSelf(dirty);
        }
    }
}

bool SurfaceFlinger::handlePageFlip()
{
    Region dirtyRegion;

    bool visibleRegions = false;
    const LayerVector& layers(mDrawingState.layersSortedByZ);
    bool frameQueued = false;

    // Store the set of layers that need updates. This set must not change as
    // buffers are being latched, as this could result in a deadlock.
    // Example: Two producers share the same command stream and:
    // 1.) Layer 0 is latched
    // 2.) Layer 0 gets a new frame
    // 2.) Layer 1 gets a new frame
    // 3.) Layer 1 is latched.
    // Display is now waiting on Layer 1's frame, which is behind layer 0's
    // second frame. But layer 0's second frame could be waiting on display.
    Vector<Layer*> layersWithQueuedFrames;
    for (size_t i = 0, count = layers.size(); i<count ; i++) {
        const sp<Layer>& layer(layers[i]);
        if (layer->hasQueuedFrame()) {
            frameQueued = true;
            if (layer->shouldPresentNow(mPrimaryDispSync)) {
                layersWithQueuedFrames.push_back(layer.get());
            } else {
                layer->useEmptyDamage();
            }
        } else {
            layer->useEmptyDamage();
        }
    }
    for (size_t i = 0, count = layersWithQueuedFrames.size() ; i<count ; i++) {
        Layer* layer = layersWithQueuedFrames[i];
        const Region dirty(layer->latchBuffer(visibleRegions));
        layer->useSurfaceDamage();
        const Layer::State& s(layer->getDrawingState());
        invalidateLayerStack(s.layerStack, dirty);
    }

    mVisibleRegionsDirty |= visibleRegions;

    // If we will need to wake up at some time in the future to deal with a
    // queued frame that shouldn't be displayed during this vsync period, wake
    // up during the next vsync period to check again.
    if (frameQueued && layersWithQueuedFrames.empty()) {
        signalLayerUpdate();
    }

    // Only continue with the refresh if there is actually new work to do
    return !layersWithQueuedFrames.empty();
}

void SurfaceFlinger::invalidateHwcGeometry()
{
    mHwWorkListDirty = true;
}


void SurfaceFlinger::doDisplayComposition(const sp<const DisplayDevice>& hw,
        const Region& inDirtyRegion)
{
    // We only need to actually compose the display if:
    // 1) It is being handled by hardware composer, which may need this to
    //    keep its virtual display state machine in sync, or
    // 2) There is work to be done (the dirty region isn't empty)
    bool isHwcDisplay = hw->getHwcDisplayId() >= 0;
    if (!isHwcDisplay && inDirtyRegion.isEmpty()) {
        return;
    }

    Region dirtyRegion(inDirtyRegion);

    // compute the invalid region
    hw->swapRegion.orSelf(dirtyRegion);

    uint32_t flags = hw->getFlags();
    if (flags & DisplayDevice::SWAP_RECTANGLE) {
        // we can redraw only what's dirty, but since SWAP_RECTANGLE only
        // takes a rectangle, we must make sure to update that whole
        // rectangle in that case
        dirtyRegion.set(hw->swapRegion.bounds());
    } else {
        if (flags & DisplayDevice::PARTIAL_UPDATES) {
            // We need to redraw the rectangle that will be updated
            // (pushed to the framebuffer).
            // This is needed because PARTIAL_UPDATES only takes one
            // rectangle instead of a region (see DisplayDevice::flip())
            dirtyRegion.set(hw->swapRegion.bounds());
        } else {
            // we need to redraw everything (the whole screen)
            dirtyRegion.set(hw->bounds());
            hw->swapRegion = dirtyRegion;
        }
    }

    if (CC_LIKELY(!mDaltonize && !mHasColorMatrix)) {
        if (!doComposeSurfaces(hw, dirtyRegion)) return;
    } else {
        RenderEngine& engine(getRenderEngine());
        mat4 colorMatrix = mColorMatrix;
        if (mDaltonize) {
            colorMatrix = colorMatrix * mDaltonizer();
        }
        mat4 oldMatrix = engine.setupColorTransform(colorMatrix);
        doComposeSurfaces(hw, dirtyRegion);
        engine.setupColorTransform(oldMatrix);
    }

    // update the swap region and clear the dirty region
    hw->swapRegion.orSelf(dirtyRegion);

    // swap buffers (presentation)
    hw->swapBuffers(getHwComposer());
}

bool SurfaceFlinger::doComposeSurfaces(const sp<const DisplayDevice>& hw, const Region& dirty)
{
    RenderEngine& engine(getRenderEngine());
    const int32_t id = hw->getHwcDisplayId();
    HWComposer& hwc(getHwComposer());
    HWComposer::LayerListIterator cur = hwc.begin(id);
    const HWComposer::LayerListIterator end = hwc.end(id);

    bool hasGlesComposition = hwc.hasGlesComposition(id);
    if (hasGlesComposition) {
        if (!hw->makeCurrent(mEGLDisplay, mEGLContext)) {
            ALOGW("DisplayDevice::makeCurrent failed. Aborting surface composition for display %s",
                  hw->getDisplayName().string());
            eglMakeCurrent(mEGLDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if(!getDefaultDisplayDevice()->makeCurrent(mEGLDisplay, mEGLContext)) {
              ALOGE("DisplayDevice::makeCurrent on default display failed. Aborting.");
            }
            return false;
        }

        // Never touch the framebuffer if we don't have any framebuffer layers
        const bool hasHwcComposition = hwc.hasHwcComposition(id);
        if (hasHwcComposition) {
            // when using overlays, we assume a fully transparent framebuffer
            // NOTE: we could reduce how much we need to clear, for instance
            // remove where there are opaque FB layers. however, on some
            // GPUs doing a "clean slate" clear might be more efficient.
            // We'll revisit later if needed.
            engine.clearWithColor(0, 0, 0, 0);
        } else {
            // we start with the whole screen area
            const Region bounds(hw->getBounds());

            // we remove the scissor part
            // we're left with the letterbox region
            // (common case is that letterbox ends-up being empty)
            const Region letterbox(bounds.subtract(hw->getScissor()));

            // compute the area to clear
            Region region(hw->undefinedRegion.merge(letterbox));

            // but limit it to the dirty region
            region.andSelf(dirty);

            // screen is already cleared here
            if (!region.isEmpty()) {
                // can happen with SurfaceView
                drawWormhole(hw, region);
            }
        }

        if (hw->getDisplayType() != DisplayDevice::DISPLAY_PRIMARY) {
            // just to be on the safe side, we don't set the
            // scissor on the main display. It should never be needed
            // anyways (though in theory it could since the API allows it).
            const Rect& bounds(hw->getBounds());
            const Rect& scissor(hw->getScissor());
            if (scissor != bounds) {
                // scissor doesn't match the screen's dimensions, so we
                // need to clear everything outside of it and enable
                // the GL scissor so we don't draw anything where we shouldn't

                // enable scissor for this frame
                const uint32_t height = hw->getHeight();
                engine.setScissor(scissor.left, height - scissor.bottom,
                        scissor.getWidth(), scissor.getHeight());
            }
        }
    }

    /*
     * and then, render the layers targeted at the framebuffer
     */

    const Vector< sp<Layer> >& layers(hw->getVisibleLayersSortedByZ());
    const size_t count = layers.size();
    const Transform& tr = hw->getTransform();
    if (cur != end) {
        // we're using h/w composer
        for (size_t i=0 ; i<count && cur!=end ; ++i, ++cur) {
            const sp<Layer>& layer(layers[i]);
            const Region clip(dirty.intersect(tr.transform(layer->visibleRegion)));
            if (!clip.isEmpty()) {
                switch (cur->getCompositionType()) {
                    case HWC_CURSOR_OVERLAY:
                    case HWC_OVERLAY: {
                        const Layer::State& state(layer->getDrawingState());
                        if ((cur->getHints() & HWC_HINT_CLEAR_FB)
                                && i
                                && layer->isOpaque(state) && (state.alpha == 0xFF)
                                && hasGlesComposition) {
                            // never clear the very first layer since we're
                            // guaranteed the FB is already cleared
                            layer->clearWithOpenGL(hw, clip);
                        }
                        break;
                    }
                    case HWC_FRAMEBUFFER: {
                        layer->draw(hw, clip);
                        break;
                    }
                    case HWC_FRAMEBUFFER_TARGET: {
                        // this should not happen as the iterator shouldn't
                        // let us get there.
                        ALOGW("HWC_FRAMEBUFFER_TARGET found in hwc list (index=%zu)", i);
                        break;
                    }
                }
            }
            layer->setAcquireFence(hw, *cur);
        }
    } else {
        // we're not using h/w composer
        for (size_t i=0 ; i<count ; ++i) {
            const sp<Layer>& layer(layers[i]);
            const Region clip(dirty.intersect(
                    tr.transform(layer->visibleRegion)));
            if (!clip.isEmpty()) {
                layer->draw(hw, clip);
            }
        }
    }

    // disable scissor at the end of the frame
    engine.disableScissor();
    return true;
}

void SurfaceFlinger::drawWormhole(const sp<const DisplayDevice>& hw, const Region& region) const {
    const int32_t height = hw->getHeight();
    RenderEngine& engine(getRenderEngine());
    engine.fillRegionWithColor(region, height, 0, 0, 0, 0);
}

status_t SurfaceFlinger::addClientLayer(const sp<Client>& client,
        const sp<IBinder>& handle,
        const sp<IGraphicBufferProducer>& gbc,
        const sp<Layer>& lbc)
{
    // add this layer to the current state list
    {
        Mutex::Autolock _l(mStateLock);
        if (mCurrentState.layersSortedByZ.size() >= MAX_LAYERS) {
            return NO_MEMORY;
        }
        mCurrentState.layersSortedByZ.add(lbc);
        mGraphicBufferProducerList.add(IInterface::asBinder(gbc));
    }

    // attach this layer to the client
    client->attachLayer(handle, lbc);

    return NO_ERROR;
}

status_t SurfaceFlinger::removeLayer(const sp<Layer>& layer) {
    Mutex::Autolock _l(mStateLock);
    ssize_t index = mCurrentState.layersSortedByZ.remove(layer);
    if (index >= 0) {
        mLayersPendingRemoval.push(layer);
        mLayersRemoved = true;
        setTransactionFlags(eTransactionNeeded);
        return NO_ERROR;
    }
    return status_t(index);
}

uint32_t SurfaceFlinger::peekTransactionFlags(uint32_t /* flags */) {
    return android_atomic_release_load(&mTransactionFlags);
}

uint32_t SurfaceFlinger::getTransactionFlags(uint32_t flags) {
    return android_atomic_and(~flags, &mTransactionFlags) & flags;
}

uint32_t SurfaceFlinger::setTransactionFlags(uint32_t flags) {
    uint32_t old = android_atomic_or(flags, &mTransactionFlags);
    if ((old & flags)==0) { // wake the server up
        signalTransaction();
    }
    return old;
}

void SurfaceFlinger::setTransactionState(
        const Vector<ComposerState>& state,
        const Vector<DisplayState>& displays,
        uint32_t flags)
{
    ATRACE_CALL();
    Mutex::Autolock _l(mStateLock);
    uint32_t transactionFlags = 0;

    if (flags & eAnimation) {
        // For window updates that are part of an animation we must wait for
        // previous animation "frames" to be handled.
        while (mAnimTransactionPending) {
            status_t err = mTransactionCV.waitRelative(mStateLock, s2ns(5));
            if (CC_UNLIKELY(err != NO_ERROR)) {
                // just in case something goes wrong in SF, return to the
                // caller after a few seconds.
                ALOGW_IF(err == TIMED_OUT, "setTransactionState timed out "
                        "waiting for previous animation frame");
                mAnimTransactionPending = false;
                break;
            }
        }
    }

    size_t count = displays.size();
    for (size_t i=0 ; i<count ; i++) {
        const DisplayState& s(displays[i]);
        transactionFlags |= setDisplayStateLocked(s);
    }

    count = state.size();
    for (size_t i=0 ; i<count ; i++) {
        const ComposerState& s(state[i]);
        // Here we need to check that the interface we're given is indeed
        // one of our own. A malicious client could give us a NULL
        // IInterface, or one of its own or even one of our own but a
        // different type. All these situations would cause us to crash.
        //
        // NOTE: it would be better to use RTTI as we could directly check
        // that we have a Client*. however, RTTI is disabled in Android.
        if (s.client != NULL) {
            sp<IBinder> binder = IInterface::asBinder(s.client);
            if (binder != NULL) {
                String16 desc(binder->getInterfaceDescriptor());
                if (desc == ISurfaceComposerClient::descriptor) {
                    sp<Client> client( static_cast<Client *>(s.client.get()) );
                    transactionFlags |= setClientStateLocked(client, s.state);
                }
            }
        }
    }

    if (transactionFlags) {
        // this triggers the transaction
        setTransactionFlags(transactionFlags);

        // if this is a synchronous transaction, wait for it to take effect
        // before returning.
        if (flags & eSynchronous) {
            mTransactionPending = true;
        }
        if (flags & eAnimation) {
            mAnimTransactionPending = true;
        }
        while (mTransactionPending) {
            status_t err = mTransactionCV.waitRelative(mStateLock, s2ns(5));
            if (CC_UNLIKELY(err != NO_ERROR)) {
                // just in case something goes wrong in SF, return to the
                // called after a few seconds.
                ALOGW_IF(err == TIMED_OUT, "setTransactionState timed out!");
                mTransactionPending = false;
                break;
            }
        }
    }
}

uint32_t SurfaceFlinger::setDisplayStateLocked(const DisplayState& s)
{
    ssize_t dpyIdx = mCurrentState.displays.indexOfKey(s.token);
    if (dpyIdx < 0)
        return 0;

    uint32_t flags = 0;
    DisplayDeviceState& disp(mCurrentState.displays.editValueAt(dpyIdx));
    if (disp.isValid()) {
        const uint32_t what = s.what;
        if (what & DisplayState::eSurfaceChanged) {
            if (IInterface::asBinder(disp.surface) != IInterface::asBinder(s.surface)) {
                disp.surface = s.surface;
                flags |= eDisplayTransactionNeeded;
            }
        }
        if (what & DisplayState::eLayerStackChanged) {
            if (disp.layerStack != s.layerStack) {
                disp.layerStack = s.layerStack;
                flags |= eDisplayTransactionNeeded;
            }
        }
        if (what & DisplayState::eDisplayProjectionChanged) {
            if (disp.orientation != s.orientation) {
                disp.orientation = s.orientation;
                flags |= eDisplayTransactionNeeded;
            }
            if (disp.frame != s.frame) {
                disp.frame = s.frame;
                flags |= eDisplayTransactionNeeded;
            }
            if (disp.viewport != s.viewport) {
                disp.viewport = s.viewport;
                flags |= eDisplayTransactionNeeded;
            }
        }
        if (what & DisplayState::eDisplaySizeChanged) {
            if (disp.width != s.width) {
                disp.width = s.width;
                flags |= eDisplayTransactionNeeded;
            }
            if (disp.height != s.height) {
                disp.height = s.height;
                flags |= eDisplayTransactionNeeded;
            }
        }
    }
    return flags;
}

uint32_t SurfaceFlinger::setClientStateLocked(
        const sp<Client>& client,
        const layer_state_t& s)
{
    uint32_t flags = 0;
    sp<Layer> layer(client->getLayerUser(s.surface));
    if (layer != 0) {
        const uint32_t what = s.what;
        if (what & layer_state_t::ePositionChanged) {
            if (layer->setPosition(s.x, s.y))
                flags |= eTraversalNeeded;
        }
        if (what & layer_state_t::eLayerChanged) {
            // NOTE: index needs to be calculated before we update the state
            ssize_t idx = mCurrentState.layersSortedByZ.indexOf(layer);
            if (layer->setLayer(s.z)) {
                mCurrentState.layersSortedByZ.removeAt(idx);
                mCurrentState.layersSortedByZ.add(layer);
                // we need traversal (state changed)
                // AND transaction (list changed)
                flags |= eTransactionNeeded|eTraversalNeeded;
            }
        }
        if (what & layer_state_t::eSizeChanged) {
            if (layer->setSize(s.w, s.h)) {
                flags |= eTraversalNeeded;
            }
        }
        if (what & layer_state_t::eAlphaChanged) {
            if (layer->setAlpha(uint8_t(255.0f*s.alpha+0.5f)))
                flags |= eTraversalNeeded;
        }
        if (what & layer_state_t::eMatrixChanged) {
            if (layer->setMatrix(s.matrix))
                flags |= eTraversalNeeded;
        }
        if (what & layer_state_t::eTransparentRegionChanged) {
            if (layer->setTransparentRegionHint(s.transparentRegion))
                flags |= eTraversalNeeded;
        }
        if (what & layer_state_t::eFlagsChanged) {
            if (layer->setFlags(s.flags, s.mask))
                flags |= eTraversalNeeded;
        }
        if (what & layer_state_t::eCropChanged) {
            if (layer->setCrop(s.crop))
                flags |= eTraversalNeeded;
        }
        if (what & layer_state_t::eLayerStackChanged) {
            // NOTE: index needs to be calculated before we update the state
            ssize_t idx = mCurrentState.layersSortedByZ.indexOf(layer);
            if (layer->setLayerStack(s.layerStack)) {
                mCurrentState.layersSortedByZ.removeAt(idx);
                mCurrentState.layersSortedByZ.add(layer);
                // we need traversal (state changed)
                // AND transaction (list changed)
                flags |= eTransactionNeeded|eTraversalNeeded;
            }
        }
    }
    return flags;
}

status_t SurfaceFlinger::createLayer(
        const String8& name,
        const String8& systemname,
        const sp<Client>& client,
        uint32_t w, uint32_t h, PixelFormat format, uint32_t flags,
        sp<IBinder>* handle, sp<IGraphicBufferProducer>* gbp)
{
    //ALOGD("createLayer for (%d x %d), name=%s", w, h, name.string());
    if (int32_t(w|h) < 0) {
        ALOGE("createLayer() failed, w or h is negative (w=%d, h=%d)",
                int(w), int(h));
        return BAD_VALUE;
    }

    status_t result = NO_ERROR;

    sp<Layer> layer;

    switch (flags & ISurfaceComposerClient::eFXSurfaceMask) {
        case ISurfaceComposerClient::eFXSurfaceNormal:
            result = createNormalLayer(client,
                    name, systemname, w, h, flags, format,
                    handle, gbp, &layer);
            break;
        case ISurfaceComposerClient::eFXSurfaceDim:
            result = createDimLayer(client,
                    name, systemname, w, h, flags,
                    handle, gbp, &layer);
            break;
        default:
            result = BAD_VALUE;
            break;
    }

    if (result != NO_ERROR) {
        return result;
    }

    result = addClientLayer(client, *handle, *gbp, layer);
    if (result != NO_ERROR) {
        return result;
    }

    setTransactionFlags(eTransactionNeeded);
    return result;
}

status_t SurfaceFlinger::createNormalLayer(const sp<Client>& client,
        const String8& name,  const String8& systemname, uint32_t w, uint32_t h, uint32_t flags, PixelFormat& format,
        sp<IBinder>* handle, sp<IGraphicBufferProducer>* gbp, sp<Layer>* outLayer)
{
    // initialize the surfaces
    switch (format) {
    case PIXEL_FORMAT_TRANSPARENT:
    case PIXEL_FORMAT_TRANSLUCENT:
        format = PIXEL_FORMAT_RGBA_8888;
        break;
    case PIXEL_FORMAT_OPAQUE:
        format = PIXEL_FORMAT_RGBX_8888;
        break;
    }

    *outLayer = new Layer(this, client, name, systemname, w, h, flags);
    status_t err = (*outLayer)->setBuffers(w, h, format, flags);
    if (err == NO_ERROR) {
        *handle = (*outLayer)->getHandle();
        *gbp = (*outLayer)->getProducer();
    }

    ALOGE_IF(err, "createNormalLayer() failed (%s)", strerror(-err));
    return err;
}

status_t SurfaceFlinger::createDimLayer(const sp<Client>& client,
        const String8& name, const String8& systemname, uint32_t w, uint32_t h, uint32_t flags,
        sp<IBinder>* handle, sp<IGraphicBufferProducer>* gbp, sp<Layer>* outLayer)
{
    *outLayer = new LayerDim(this, client, name, systemname, w, h, flags);
    *handle = (*outLayer)->getHandle();
    *gbp = (*outLayer)->getProducer();
    return NO_ERROR;
}

status_t SurfaceFlinger::onLayerRemoved(const sp<Client>& client, const sp<IBinder>& handle)
{
    // called by the window manager when it wants to remove a Layer
    status_t err = NO_ERROR;
    sp<Layer> l(client->getLayerUser(handle));
    if (l != NULL) {
        err = removeLayer(l);
        ALOGE_IF(err<0 && err != NAME_NOT_FOUND,
                "error removing layer=%p (%s)", l.get(), strerror(-err));
    }
    return err;
}

status_t SurfaceFlinger::onLayerDestroyed(const wp<Layer>& layer)
{
    // called by ~LayerCleaner() when all references to the IBinder (handle)
    // are gone
    status_t err = NO_ERROR;
    sp<Layer> l(layer.promote());
    if (l != NULL) {
        err = removeLayer(l);
        ALOGE_IF(err<0 && err != NAME_NOT_FOUND,
                "error removing layer=%p (%s)", l.get(), strerror(-err));
    }
    return err;
}

// ---------------------------------------------------------------------------

void SurfaceFlinger::onInitializeDisplays() {
    // reset screen orientation and use primary layer stack
    Vector<ComposerState> state;
    Vector<DisplayState> displays;
    DisplayState d;
    d.what = DisplayState::eDisplayProjectionChanged |
             DisplayState::eLayerStackChanged;
    d.token = mBuiltinDisplays[DisplayDevice::DISPLAY_PRIMARY];
    d.layerStack = 0;
    d.orientation = DisplayState::eOrientationDefault;
    d.frame.makeInvalid();
    d.viewport.makeInvalid();
    d.width = 0;
    d.height = 0;
    displays.add(d);
    setTransactionState(state, displays, 0);

    setPowerModeInternal(getDisplayDevice(d.token), HWC_POWER_MODE_NORMAL);

    const nsecs_t period =
            getHwComposer().getRefreshPeriod(HWC_DISPLAY_PRIMARY);
    mAnimFrameTracker.setDisplayRefreshPeriod(period);
}

void SurfaceFlinger::initializeDisplays() {
    class MessageScreenInitialized : public MessageBase {
        SurfaceFlinger* flinger;
    public:
        MessageScreenInitialized(SurfaceFlinger* flinger) : flinger(flinger) { }
        virtual bool handler() {
            flinger->onInitializeDisplays();
            return true;
        }
    };
    sp<MessageBase> msg = new MessageScreenInitialized(this);
    postMessageAsync(msg);  // we may be called from main thread, use async message
}

void SurfaceFlinger::setPowerModeInternal(const sp<DisplayDevice>& hw,
        int mode) {
    ALOGD("Set power mode=%d, type=%d flinger=%p", mode, hw->getDisplayType(),
            this);
    int32_t type = hw->getDisplayType();
    int currentMode = hw->getPowerMode();

    if (mode == currentMode) {
        ALOGD("Screen type=%d is already mode=%d", hw->getDisplayType(), mode);
        return;
    }

    hw->setPowerMode(mode);
    if (type >= DisplayDevice::NUM_BUILTIN_DISPLAY_TYPES) {
        ALOGW("Trying to set power mode for virtual display");
        return;
    }

    if (currentMode == HWC_POWER_MODE_OFF) {
        getHwComposer().setPowerMode(type, mode);
        if (type == DisplayDevice::DISPLAY_PRIMARY) {
            // FIXME: eventthread only knows about the main display right now
            mEventThread->onScreenAcquired();
            resyncToHardwareVsync(true);
        }

        mVisibleRegionsDirty = true;
        mHasPoweredOff = true;
        repaintEverything();
    } else if (mode == HWC_POWER_MODE_OFF) {
        if (type == DisplayDevice::DISPLAY_PRIMARY) {
            disableHardwareVsync(true); // also cancels any in-progress resync

            // FIXME: eventthread only knows about the main display right now
            mEventThread->onScreenReleased();
        }

        getHwComposer().setPowerMode(type, mode);
        mVisibleRegionsDirty = true;
        // from this point on, SF will stop drawing on this display
    } else {
        getHwComposer().setPowerMode(type, mode);
    }
}

void SurfaceFlinger::setPowerMode(const sp<IBinder>& display, int mode) {
    class MessageSetPowerMode: public MessageBase {
        SurfaceFlinger& mFlinger;
        sp<IBinder> mDisplay;
        int mMode;
    public:
        MessageSetPowerMode(SurfaceFlinger& flinger,
                const sp<IBinder>& disp, int mode) : mFlinger(flinger),
                    mDisplay(disp) { mMode = mode; }
        virtual bool handler() {
            sp<DisplayDevice> hw(mFlinger.getDisplayDevice(mDisplay));
            if (hw == NULL) {
                ALOGE("Attempt to set power mode = %d for null display %p",
                        mMode, mDisplay.get());
            } else if (hw->getDisplayType() >= DisplayDevice::DISPLAY_VIRTUAL) {
                ALOGW("Attempt to set power mode = %d for virtual display",
                        mMode);
            } else {
                mFlinger.setPowerModeInternal(hw, mMode);
            }
            return true;
        }
    };
    sp<MessageBase> msg = new MessageSetPowerMode(*this, display, mode);
    postMessageSync(msg);
}

// ---------------------------------------------------------------------------

status_t SurfaceFlinger::dump(int fd, const Vector<String16>& args)
{
    String8 result;

    IPCThreadState* ipc = IPCThreadState::self();
    const int pid = ipc->getCallingPid();
    const int uid = ipc->getCallingUid();
    if ((uid != AID_SHELL) &&
            !PermissionCache::checkPermission(sDump, pid, uid)) {
        result.appendFormat("Permission Denial: "
                "can't dump SurfaceFlinger from pid=%d, uid=%d\n", pid, uid);
    } else {
        // Try to get the main lock, but give up after one second
        // (this would indicate SF is stuck, but we want to be able to
        // print something in dumpsys).
        status_t err = mStateLock.timedLock(s2ns(1));
        bool locked = (err == NO_ERROR);
        if (!locked) {
            result.appendFormat(
                    "SurfaceFlinger appears to be unresponsive (%s [%d]), "
                    "dumping anyways (no locks held)\n", strerror(-err), err);
        }

        bool dumpAll = true;
        size_t index = 0;
        size_t numArgs = args.size();
        if (numArgs) {
            if ((index < numArgs) &&
                    (args[index] == String16("--list"))) {
                index++;
                listLayersLocked(args, index, result);
                dumpAll = false;
            }

            if ((index < numArgs) &&
                    (args[index] == String16("--latency"))) {
                index++;
                dumpStatsLocked(args, index, result);
                dumpAll = false;
            }

            if ((index < numArgs) &&
                    (args[index] == String16("--latency-clear"))) {
                index++;
                clearStatsLocked(args, index, result);
                dumpAll = false;
            }

            if ((index < numArgs) &&
                    (args[index] == String16("--dispsync"))) {
                index++;
                mPrimaryDispSync.dump(result);
                dumpAll = false;
            }

            if ((index < numArgs) &&
                    (args[index] == String16("--static-screen"))) {
                index++;
                dumpStaticScreenStats(result);
                dumpAll = false;
            }
        }

        if (dumpAll) {
            dumpAllLocked(args, index, result);
        }

        if (locked) {
            mStateLock.unlock();
        }
    }
    write(fd, result.string(), result.size());
    return NO_ERROR;
}

void SurfaceFlinger::listLayersLocked(const Vector<String16>& /* args */,
        size_t& /* index */, String8& result) const
{
    const LayerVector& currentLayers = mCurrentState.layersSortedByZ;
    const size_t count = currentLayers.size();
    for (size_t i=0 ; i<count ; i++) {
        const sp<Layer>& layer(currentLayers[i]);
        result.appendFormat("%s\n", layer->getName().string());
    }
}

void SurfaceFlinger::dumpStatsLocked(const Vector<String16>& args, size_t& index,
        String8& result) const
{
    String8 name;
    if (index < args.size()) {
        name = String8(args[index]);
        index++;
    }

    const nsecs_t period =
            getHwComposer().getRefreshPeriod(HWC_DISPLAY_PRIMARY);
    result.appendFormat("%" PRId64 "\n", period);

    if (name.isEmpty()) {
        mAnimFrameTracker.dumpStats(result);
    } else {
        const LayerVector& currentLayers = mCurrentState.layersSortedByZ;
        const size_t count = currentLayers.size();
        for (size_t i=0 ; i<count ; i++) {
            const sp<Layer>& layer(currentLayers[i]);
            if (name == layer->getName()) {
                layer->dumpFrameStats(result);
            }
        }
    }
}

void SurfaceFlinger::clearStatsLocked(const Vector<String16>& args, size_t& index,
        String8& /* result */)
{
    String8 name;
    if (index < args.size()) {
        name = String8(args[index]);
        index++;
    }

    const LayerVector& currentLayers = mCurrentState.layersSortedByZ;
    const size_t count = currentLayers.size();
    for (size_t i=0 ; i<count ; i++) {
        const sp<Layer>& layer(currentLayers[i]);
        if (name.isEmpty() || (name == layer->getName())) {
            layer->clearFrameStats();
        }
    }

    mAnimFrameTracker.clearStats();
}

// This should only be called from the main thread.  Otherwise it would need
// the lock and should use mCurrentState rather than mDrawingState.
void SurfaceFlinger::logFrameStats() {
    const LayerVector& drawingLayers = mDrawingState.layersSortedByZ;
    const size_t count = drawingLayers.size();
    for (size_t i=0 ; i<count ; i++) {
        const sp<Layer>& layer(drawingLayers[i]);
        layer->logFrameStats();
    }

    mAnimFrameTracker.logAndResetStats(String8("<win-anim>"));
}

/*static*/ void SurfaceFlinger::appendSfConfigString(String8& result)
{
    static const char* config =
            " [sf"
#ifdef HAS_CONTEXT_PRIORITY
            " HAS_CONTEXT_PRIORITY"
#endif
#ifdef NEVER_DEFAULT_TO_ASYNC_MODE
            " NEVER_DEFAULT_TO_ASYNC_MODE"
#endif
#ifdef TARGET_DISABLE_TRIPLE_BUFFERING
            " TARGET_DISABLE_TRIPLE_BUFFERING"
#endif
            "]";
    result.append(config);
}

void SurfaceFlinger::dumpStaticScreenStats(String8& result) const
{
    result.appendFormat("Static screen stats:\n");
    for (size_t b = 0; b < NUM_BUCKETS - 1; ++b) {
        float bucketTimeSec = mFrameBuckets[b] / 1e9;
        float percent = 100.0f *
                static_cast<float>(mFrameBuckets[b]) / mTotalTime;
        result.appendFormat("  < %zd frames: %.3f s (%.1f%%)\n",
                b + 1, bucketTimeSec, percent);
    }
    float bucketTimeSec = mFrameBuckets[NUM_BUCKETS - 1] / 1e9;
    float percent = 100.0f *
            static_cast<float>(mFrameBuckets[NUM_BUCKETS - 1]) / mTotalTime;
    result.appendFormat("  %zd+ frames: %.3f s (%.1f%%)\n",
            NUM_BUCKETS - 1, bucketTimeSec, percent);
}

void SurfaceFlinger::dumpAllLocked(const Vector<String16>& args, size_t& index,
        String8& result) const
{
    bool colorize = false;
    if (index < args.size()
            && (args[index] == String16("--color"))) {
        colorize = true;
        index++;
    }

    Colorizer colorizer(colorize);

    // figure out if we're stuck somewhere
    const nsecs_t now = systemTime();
    const nsecs_t inSwapBuffers(mDebugInSwapBuffers);
    const nsecs_t inTransaction(mDebugInTransaction);
    nsecs_t inSwapBuffersDuration = (inSwapBuffers) ? now-inSwapBuffers : 0;
    nsecs_t inTransactionDuration = (inTransaction) ? now-inTransaction : 0;

    /*
     * Dump library configuration.
     */

    colorizer.bold(result);
    result.append("Build configuration:");
    colorizer.reset(result);
    appendSfConfigString(result);
    appendUiConfigString(result);
    appendGuiConfigString(result);
    result.append("\n");

    colorizer.bold(result);
    result.append("Sync configuration: ");
    colorizer.reset(result);
    result.append(SyncFeatures::getInstance().toString());
    result.append("\n");

    colorizer.bold(result);
    result.append("DispSync configuration: ");
    colorizer.reset(result);
    result.appendFormat("app phase %" PRId64 " ns, sf phase %" PRId64 " ns, "
            "present offset %d ns (refresh %" PRId64 " ns)",
        vsyncPhaseOffsetNs, sfVsyncPhaseOffsetNs, PRESENT_TIME_OFFSET_FROM_VSYNC_NS,
        mHwc->getRefreshPeriod(HWC_DISPLAY_PRIMARY));
    result.append("\n");

    // Dump static screen stats
    result.append("\n");
    dumpStaticScreenStats(result);
    result.append("\n");

    /*
     * Dump the visible layer list
     */
    const LayerVector& currentLayers = mCurrentState.layersSortedByZ;
    const size_t count = currentLayers.size();
    colorizer.bold(result);
    result.appendFormat("Visible layers (count = %zu)\n", count);
    colorizer.reset(result);
    for (size_t i=0 ; i<count ; i++) {
        const sp<Layer>& layer(currentLayers[i]);
        layer->dump(result, colorizer);
    }

    /*
     * Dump Display state
     */

    colorizer.bold(result);
    result.appendFormat("Displays (%zu entries)\n", mDisplays.size());
    colorizer.reset(result);
    for (size_t dpy=0 ; dpy<mDisplays.size() ; dpy++) {
        const sp<const DisplayDevice>& hw(mDisplays[dpy]);
        hw->dump(result);
    }

    /*
     * Dump SurfaceFlinger global state
     */

    colorizer.bold(result);
    result.append("SurfaceFlinger global state:\n");
    colorizer.reset(result);

    HWComposer& hwc(getHwComposer());
    sp<const DisplayDevice> hw(getDefaultDisplayDevice());

    colorizer.bold(result);
    result.appendFormat("EGL implementation : %s\n",
            eglQueryStringImplementationANDROID(mEGLDisplay, EGL_VERSION));
    colorizer.reset(result);
    result.appendFormat("%s\n",
            eglQueryStringImplementationANDROID(mEGLDisplay, EGL_EXTENSIONS));

    mRenderEngine->dump(result);

    hw->undefinedRegion.dump(result, "undefinedRegion");
    result.appendFormat("  orientation=%d, isDisplayOn=%d\n",
            hw->getOrientation(), hw->isDisplayOn());
    result.appendFormat(
            "  last eglSwapBuffers() time: %f us\n"
            "  last transaction time     : %f us\n"
            "  transaction-flags         : %08x\n"
            "  refresh-rate              : %f fps\n"
            "  x-dpi                     : %f\n"
            "  y-dpi                     : %f\n"
            "  gpu_to_cpu_unsupported    : %d\n"
            ,
            mLastSwapBufferTime/1000.0,
            mLastTransactionTime/1000.0,
            mTransactionFlags,
            1e9 / hwc.getRefreshPeriod(HWC_DISPLAY_PRIMARY),
            hwc.getDpiX(HWC_DISPLAY_PRIMARY),
            hwc.getDpiY(HWC_DISPLAY_PRIMARY),
            !mGpuToCpuSupported);

    result.appendFormat("  eglSwapBuffers time: %f us\n",
            inSwapBuffersDuration/1000.0);

    result.appendFormat("  transaction time: %f us\n",
            inTransactionDuration/1000.0);

    /*
     * VSYNC state
     */
    mEventThread->dump(result);

    /*
     * Dump HWComposer state
     */
    colorizer.bold(result);
    result.append("h/w composer state:\n");
    colorizer.reset(result);
    result.appendFormat("  h/w composer %s and %s\n",
            hwc.initCheck()==NO_ERROR ? "present" : "not present",
                    (mDebugDisableHWC || mDebugRegion || mDaltonize
                            || mHasColorMatrix) ? "disabled" : "enabled");
    hwc.dump(result);

    /*
     * Dump gralloc state
     */
    const GraphicBufferAllocator& alloc(GraphicBufferAllocator::get());
    alloc.dump(result);
}

const Vector< sp<Layer> >&
SurfaceFlinger::getLayerSortedByZForHwcDisplay(int id) {
    // Note: mStateLock is held here
    wp<IBinder> dpy;
    for (size_t i=0 ; i<mDisplays.size() ; i++) {
        if (mDisplays.valueAt(i)->getHwcDisplayId() == id) {
            dpy = mDisplays.keyAt(i);
            break;
        }
    }
    if (dpy == NULL) {
        ALOGE("getLayerSortedByZForHwcDisplay: invalid hwc display id %d", id);
        // Just use the primary display so we have something to return
        dpy = getBuiltInDisplay(DisplayDevice::DISPLAY_PRIMARY);
    }
    return getDisplayDevice(dpy)->getVisibleLayersSortedByZ();
}

bool SurfaceFlinger::startDdmConnection()
{
    void* libddmconnection_dso =
            dlopen("libsurfaceflinger_ddmconnection.so", RTLD_NOW);
    if (!libddmconnection_dso) {
        return false;
    }
    void (*DdmConnection_start)(const char* name);
    DdmConnection_start =
            (decltype(DdmConnection_start))dlsym(libddmconnection_dso, "DdmConnection_start");
    if (!DdmConnection_start) {
        dlclose(libddmconnection_dso);
        return false;
    }
    (*DdmConnection_start)(getServiceName());
    return true;
}

status_t SurfaceFlinger::onTransact(
    uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags)
{
    switch (code) {
        case CREATE_CONNECTION:
        case CREATE_DISPLAY:
        case SET_TRANSACTION_STATE:
        case BOOT_FINISHED:
        case CLEAR_ANIMATION_FRAME_STATS:
        case GET_ANIMATION_FRAME_STATS:
        case SET_POWER_MODE:
        {
            // codes that require permission check
            IPCThreadState* ipc = IPCThreadState::self();
            const int pid = ipc->getCallingPid();
            const int uid = ipc->getCallingUid();
            if ((uid != AID_GRAPHICS && uid != AID_SYSTEM) &&
                    !PermissionCache::checkPermission(sAccessSurfaceFlinger, pid, uid)) {
                ALOGE("Permission Denial: "
                        "can't access SurfaceFlinger pid=%d, uid=%d", pid, uid);
                return PERMISSION_DENIED;
            }
            break;
        }
        case CAPTURE_SCREEN:
        {
            // codes that require permission check
            IPCThreadState* ipc = IPCThreadState::self();
            const int pid = ipc->getCallingPid();
            const int uid = ipc->getCallingUid();
            if ((uid != AID_GRAPHICS) &&
                    !PermissionCache::checkPermission(sReadFramebuffer, pid, uid)) {
                ALOGE("Permission Denial: "
                        "can't read framebuffer pid=%d, uid=%d", pid, uid);
                return PERMISSION_DENIED;
            }
            break;
        }
    }

    status_t err = BnSurfaceComposer::onTransact(code, data, reply, flags);
    if (err == UNKNOWN_TRANSACTION || err == PERMISSION_DENIED) {
        CHECK_INTERFACE(ISurfaceComposer, data, reply);
        if (CC_UNLIKELY(!PermissionCache::checkCallingPermission(sHardwareTest))) {
            IPCThreadState* ipc = IPCThreadState::self();
            const int pid = ipc->getCallingPid();
            const int uid = ipc->getCallingUid();
            ALOGE("Permission Denial: "
                    "can't access SurfaceFlinger pid=%d, uid=%d", pid, uid);
            return PERMISSION_DENIED;
        }
        int n;
        switch (code) {
            case 1000: // SHOW_CPU, NOT SUPPORTED ANYMORE
            case 1001: // SHOW_FPS, NOT SUPPORTED ANYMORE
                return NO_ERROR;
            case 1002:  // SHOW_UPDATES
                n = data.readInt32();
                mDebugRegion = n ? n : (mDebugRegion ? 0 : 1);
                invalidateHwcGeometry();
                repaintEverything();
                return NO_ERROR;
            case 1004:{ // repaint everything
                repaintEverything();
                return NO_ERROR;
            }
            case 1005:{ // force transaction
                setTransactionFlags(
                        eTransactionNeeded|
                        eDisplayTransactionNeeded|
                        eTraversalNeeded);
                return NO_ERROR;
            }
            case 1006:{ // send empty update
                signalRefresh();
                return NO_ERROR;
            }
            case 1008:  // toggle use of hw composer
                n = data.readInt32();
                mDebugDisableHWC = n ? 1 : 0;
                invalidateHwcGeometry();
                repaintEverything();
                return NO_ERROR;
            case 1009:  // toggle use of transform hint
                n = data.readInt32();
                mDebugDisableTransformHint = n ? 1 : 0;
                invalidateHwcGeometry();
                repaintEverything();
                return NO_ERROR;
            case 1010:  // interrogate.
                reply->writeInt32(0);
                reply->writeInt32(0);
                reply->writeInt32(mDebugRegion);
                reply->writeInt32(0);
                reply->writeInt32(mDebugDisableHWC);
                return NO_ERROR;
            case 1013: {
                Mutex::Autolock _l(mStateLock);
                sp<const DisplayDevice> hw(getDefaultDisplayDevice());
                reply->writeInt32(hw->getPageFlipCount());
                return NO_ERROR;
            }
            case 1014: {
                // daltonize
                n = data.readInt32();
                switch (n % 10) {
                    case 1: mDaltonizer.setType(Daltonizer::protanomaly);   break;
                    case 2: mDaltonizer.setType(Daltonizer::deuteranomaly); break;
                    case 3: mDaltonizer.setType(Daltonizer::tritanomaly);   break;
                }
                if (n >= 10) {
                    mDaltonizer.setMode(Daltonizer::correction);
                } else {
                    mDaltonizer.setMode(Daltonizer::simulation);
                }
                mDaltonize = n > 0;
                invalidateHwcGeometry();
                repaintEverything();
                return NO_ERROR;
            }
            case 1015: {
                // apply a color matrix
                n = data.readInt32();
                mHasColorMatrix = n ? 1 : 0;
                if (n) {
                    // color matrix is sent as mat3 matrix followed by vec3
                    // offset, then packed into a mat4 where the last row is
                    // the offset and extra values are 0
                    for (size_t i = 0 ; i < 4; i++) {
                      for (size_t j = 0; j < 4; j++) {
                          mColorMatrix[i][j] = data.readFloat();
                      }
                    }
                } else {
                    mColorMatrix = mat4();
                }
                invalidateHwcGeometry();
                repaintEverything();
                return NO_ERROR;
            }
            // This is an experimental interface
            // Needs to be shifted to proper binder interface when we productize
            case 1016: {
                n = data.readInt32();
                mPrimaryDispSync.setRefreshSkipCount(n);
                return NO_ERROR;
            }
            case 1017: {
                n = data.readInt32();
                mForceFullDamage = static_cast<bool>(n);
                return NO_ERROR;
            }
            case 1018: { // Modify Choreographer's phase offset
                n = data.readInt32();
                mEventThread->setPhaseOffset(static_cast<nsecs_t>(n));
                return NO_ERROR;
            }
            case 1019: { // Modify SurfaceFlinger's phase offset
                n = data.readInt32();
                mSFEventThread->setPhaseOffset(static_cast<nsecs_t>(n));
                return NO_ERROR;
            }
        }
    }
    return err;
}

void SurfaceFlinger::repaintEverything() {
    android_atomic_or(1, &mRepaintEverything);
    signalTransaction();
}

// ---------------------------------------------------------------------------
// Capture screen into an IGraphiBufferProducer
// ---------------------------------------------------------------------------

/* The code below is here to handle b/8734824
 *
 * We create a IGraphicBufferProducer wrapper that forwards all calls
 * from the surfaceflinger thread to the calling binder thread, where they
 * are executed. This allows the calling thread in the calling process to be
 * reused and not depend on having "enough" binder threads to handle the
 * requests.
 */
class GraphicProducerWrapper : public BBinder, public MessageHandler {
    /* Parts of GraphicProducerWrapper are run on two different threads,
     * communicating by sending messages via Looper but also by shared member
     * data. Coherence maintenance is subtle and in places implicit (ugh).
     *
     * Don't rely on Looper's sendMessage/handleMessage providing
     * release/acquire semantics for any data not actually in the Message.
     * Data going from surfaceflinger to binder threads needs to be
     * synchronized explicitly.
     *
     * Barrier open/wait do provide release/acquire semantics. This provides
     * implicit synchronization for data coming back from binder to
     * surfaceflinger threads.
     */

    sp<IGraphicBufferProducer> impl;
    sp<Looper> looper;
    status_t result;
    bool exitPending;
    bool exitRequested;
    Barrier barrier;
    uint32_t code;
    Parcel const* data;
    Parcel* reply;

    enum {
        MSG_API_CALL,
        MSG_EXIT
    };

    /*
     * Called on surfaceflinger thread. This is called by our "fake"
     * BpGraphicBufferProducer. We package the data and reply Parcel and
     * forward them to the binder thread.
     */
    virtual status_t transact(uint32_t code,
            const Parcel& data, Parcel* reply, uint32_t /* flags */) {
        this->code = code;
        this->data = &data;
        this->reply = reply;
        if (exitPending) {
            // if we've exited, we run the message synchronously right here.
            // note (JH): as far as I can tell from looking at the code, this
            // never actually happens. if it does, i'm not sure if it happens
            // on the surfaceflinger or binder thread.
            handleMessage(Message(MSG_API_CALL));
        } else {
            barrier.close();
            // Prevent stores to this->{code, data, reply} from being
            // reordered later than the construction of Message.
            atomic_thread_fence(memory_order_release);
            looper->sendMessage(this, Message(MSG_API_CALL));
            barrier.wait();
        }
        return result;
    }

    /*
     * here we run on the binder thread. All we've got to do is
     * call the real BpGraphicBufferProducer.
     */
    virtual void handleMessage(const Message& message) {
        int what = message.what;
        // Prevent reads below from happening before the read from Message
        atomic_thread_fence(memory_order_acquire);
        if (what == MSG_API_CALL) {
            result = IInterface::asBinder(impl)->transact(code, data[0], reply);
            barrier.open();
        } else if (what == MSG_EXIT) {
            exitRequested = true;
        }
    }

public:
    GraphicProducerWrapper(const sp<IGraphicBufferProducer>& impl)
    :   impl(impl),
        looper(new Looper(true)),
        exitPending(false),
        exitRequested(false)
    {}

    // Binder thread
    status_t waitForResponse() {
        do {
            looper->pollOnce(-1);
        } while (!exitRequested);
        return result;
    }

    // Client thread
    void exit(status_t result) {
        this->result = result;
        exitPending = true;
        // Ensure this->result is visible to the binder thread before it
        // handles the message.
        atomic_thread_fence(memory_order_release);
        looper->sendMessage(this, Message(MSG_EXIT));
    }
};


status_t SurfaceFlinger::captureScreen(const sp<IBinder>& display,
        const sp<IGraphicBufferProducer>& producer,
        Rect sourceCrop, uint32_t reqWidth, uint32_t reqHeight,
        uint32_t minLayerZ, uint32_t maxLayerZ,
        bool useIdentityTransform, ISurfaceComposer::Rotation rotation) {

    if (CC_UNLIKELY(display == 0))
        return BAD_VALUE;

    if (CC_UNLIKELY(producer == 0))
        return BAD_VALUE;

    // if we have secure windows on this display, never allow the screen capture
    // unless the producer interface is local (i.e.: we can take a screenshot for
    // ourselves).
    if (!IInterface::asBinder(producer)->localBinder()) {
        Mutex::Autolock _l(mStateLock);
        sp<const DisplayDevice> hw(getDisplayDevice(display));
        if (hw->getSecureLayerVisible()) {
            ALOGW("FB is protected: PERMISSION_DENIED");
            return PERMISSION_DENIED;
        }
    }

    // Convert to surfaceflinger's internal rotation type.
    Transform::orientation_flags rotationFlags;
    switch (rotation) {
        case ISurfaceComposer::eRotateNone:
            rotationFlags = Transform::ROT_0;
            break;
        case ISurfaceComposer::eRotate90:
            rotationFlags = Transform::ROT_90;
            break;
        case ISurfaceComposer::eRotate180:
            rotationFlags = Transform::ROT_180;
            break;
        case ISurfaceComposer::eRotate270:
            rotationFlags = Transform::ROT_270;
            break;
        default:
            rotationFlags = Transform::ROT_0;
            ALOGE("Invalid rotation passed to captureScreen(): %d\n", rotation);
            break;
    }

    class MessageCaptureScreen : public MessageBase {
        SurfaceFlinger* flinger;
        sp<IBinder> display;
        sp<IGraphicBufferProducer> producer;
        Rect sourceCrop;
        uint32_t reqWidth, reqHeight;
        uint32_t minLayerZ,maxLayerZ;
        bool useIdentityTransform;
        Transform::orientation_flags rotation;
        status_t result;
    public:
        MessageCaptureScreen(SurfaceFlinger* flinger,
                const sp<IBinder>& display,
                const sp<IGraphicBufferProducer>& producer,
                Rect sourceCrop, uint32_t reqWidth, uint32_t reqHeight,
                uint32_t minLayerZ, uint32_t maxLayerZ,
                bool useIdentityTransform, Transform::orientation_flags rotation)
            : flinger(flinger), display(display), producer(producer),
              sourceCrop(sourceCrop), reqWidth(reqWidth), reqHeight(reqHeight),
              minLayerZ(minLayerZ), maxLayerZ(maxLayerZ),
              useIdentityTransform(useIdentityTransform),
              rotation(rotation),
              result(PERMISSION_DENIED)
        {
        }
        status_t getResult() const {
            return result;
        }
        virtual bool handler() {
            Mutex::Autolock _l(flinger->mStateLock);
            sp<const DisplayDevice> hw(flinger->getDisplayDevice(display));
            result = flinger->captureScreenImplLocked(hw, producer,
                    sourceCrop, reqWidth, reqHeight, minLayerZ, maxLayerZ,
                    useIdentityTransform, rotation);
            static_cast<GraphicProducerWrapper*>(IInterface::asBinder(producer).get())->exit(result);
            return true;
        }
    };

    // make sure to process transactions before screenshots -- a transaction
    // might already be pending but scheduled for VSYNC; this guarantees we
    // will handle it before the screenshot. When VSYNC finally arrives
    // the scheduled transaction will be a no-op. If no transactions are
    // scheduled at this time, this will end-up being a no-op as well.
    mEventQueue.invalidateTransactionNow();

    // this creates a "fake" BBinder which will serve as a "fake" remote
    // binder to receive the marshaled calls and forward them to the
    // real remote (a BpGraphicBufferProducer)
    sp<GraphicProducerWrapper> wrapper = new GraphicProducerWrapper(producer);

    // the asInterface() call below creates our "fake" BpGraphicBufferProducer
    // which does the marshaling work forwards to our "fake remote" above.
    sp<MessageBase> msg = new MessageCaptureScreen(this,
            display, IGraphicBufferProducer::asInterface( wrapper ),
            sourceCrop, reqWidth, reqHeight, minLayerZ, maxLayerZ,
            useIdentityTransform, rotationFlags);

    status_t res = postMessageAsync(msg);
    if (res == NO_ERROR) {
        res = wrapper->waitForResponse();
    }
    return res;
}


void SurfaceFlinger::renderScreenImplLocked(
        const sp<const DisplayDevice>& hw,
        Rect sourceCrop, uint32_t reqWidth, uint32_t reqHeight,
        uint32_t minLayerZ, uint32_t maxLayerZ,
        bool yswap, bool useIdentityTransform, Transform::orientation_flags rotation)
{
    ATRACE_CALL();
    RenderEngine& engine(getRenderEngine());

    // get screen geometry
    const int32_t hw_w = hw->getWidth();
    const int32_t hw_h = hw->getHeight();
    const bool filtering = static_cast<int32_t>(reqWidth) != hw_w ||
                           static_cast<int32_t>(reqHeight) != hw_h;

    // if a default or invalid sourceCrop is passed in, set reasonable values
    if (sourceCrop.width() == 0 || sourceCrop.height() == 0 ||
            !sourceCrop.isValid()) {
        sourceCrop.setLeftTop(Point(0, 0));
        sourceCrop.setRightBottom(Point(hw_w, hw_h));
    }

    // ensure that sourceCrop is inside screen
    if (sourceCrop.left < 0) {
        ALOGE("Invalid crop rect: l = %d (< 0)", sourceCrop.left);
    }
    if (sourceCrop.right > hw_w) {
        ALOGE("Invalid crop rect: r = %d (> %d)", sourceCrop.right, hw_w);
    }
    if (sourceCrop.top < 0) {
        ALOGE("Invalid crop rect: t = %d (< 0)", sourceCrop.top);
    }
    if (sourceCrop.bottom > hw_h) {
        ALOGE("Invalid crop rect: b = %d (> %d)", sourceCrop.bottom, hw_h);
    }

    // make sure to clear all GL error flags
    engine.checkErrors();

    // set-up our viewport
    engine.setViewportAndProjection(
        reqWidth, reqHeight, sourceCrop, hw_h, yswap, rotation);
    engine.disableTexturing();

    // redraw the screen entirely...
    engine.clearWithColor(0, 0, 0, 1);

    const LayerVector& layers( mDrawingState.layersSortedByZ );
    const size_t count = layers.size();
    for (size_t i=0 ; i<count ; ++i) {
        const sp<Layer>& layer(layers[i]);
        const Layer::State& state(layer->getDrawingState());
        if (layer->getSystemName() == hw->getActiveSystemName()&&
            state.layerStack == hw->getLayerStack()) {
            if (state.z >= minLayerZ && state.z <= maxLayerZ) {
                if (layer->isVisible()) {
                    if (filtering) layer->setFiltering(true);
                    layer->draw(hw, useIdentityTransform);
                    if (filtering) layer->setFiltering(false);
                }
            }
        }
    }

    // compositionComplete is needed for older driver
    hw->compositionComplete();
    hw->setViewportAndProjection();
}


status_t SurfaceFlinger::captureScreenImplLocked(
        const sp<const DisplayDevice>& hw,
        const sp<IGraphicBufferProducer>& producer,
        Rect sourceCrop, uint32_t reqWidth, uint32_t reqHeight,
        uint32_t minLayerZ, uint32_t maxLayerZ,
        bool useIdentityTransform, Transform::orientation_flags rotation)
{
    ATRACE_CALL();

    // get screen geometry
    uint32_t hw_w = hw->getWidth();
    uint32_t hw_h = hw->getHeight();

    if (rotation & Transform::ROT_90) {
        std::swap(hw_w, hw_h);
    }

    if ((reqWidth > hw_w) || (reqHeight > hw_h)) {
        ALOGE("size mismatch (%d, %d) > (%d, %d)",
                reqWidth, reqHeight, hw_w, hw_h);
        return BAD_VALUE;
    }

    reqWidth  = (!reqWidth)  ? hw_w : reqWidth;
    reqHeight = (!reqHeight) ? hw_h : reqHeight;

    // create a surface (because we're a producer, and we need to
    // dequeue/queue a buffer)
    sp<Surface> sur = new Surface(producer, false);
    ANativeWindow* window = sur.get();

    status_t result = native_window_api_connect(window, NATIVE_WINDOW_API_EGL);
    if (result == NO_ERROR) {
        uint32_t usage = GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN |
                        GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE;

        int err = 0;
        err = native_window_set_buffers_dimensions(window, reqWidth, reqHeight);
        err |= native_window_set_scaling_mode(window, NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW);
        err |= native_window_set_buffers_format(window, HAL_PIXEL_FORMAT_RGBA_8888);
        err |= native_window_set_usage(window, usage);

        if (err == NO_ERROR) {
            ANativeWindowBuffer* buffer;
            /* TODO: Once we have the sync framework everywhere this can use
             * server-side waits on the fence that dequeueBuffer returns.
             */
            result = native_window_dequeue_buffer_and_wait(window,  &buffer);
            if (result == NO_ERROR) {
                int syncFd = -1;
                // create an EGLImage from the buffer so we can later
                // turn it into a texture
                EGLImageKHR image = eglCreateImageKHR(mEGLDisplay, EGL_NO_CONTEXT,
                        EGL_NATIVE_BUFFER_ANDROID, buffer, NULL);
                if (image != EGL_NO_IMAGE_KHR) {
                    // this binds the given EGLImage as a framebuffer for the
                    // duration of this scope.
                    RenderEngine::BindImageAsFramebuffer imageBond(getRenderEngine(), image);
                    if (imageBond.getStatus() == NO_ERROR) {
                        // this will in fact render into our dequeued buffer
                        // via an FBO, which means we didn't have to create
                        // an EGLSurface and therefore we're not
                        // dependent on the context's EGLConfig.
                        renderScreenImplLocked(
                            hw, sourceCrop, reqWidth, reqHeight, minLayerZ, maxLayerZ, true,
                            useIdentityTransform, rotation);

                        // Attempt to create a sync khr object that can produce a sync point. If that
                        // isn't available, create a non-dupable sync object in the fallback path and
                        // wait on it directly.
                        EGLSyncKHR sync;
                        if (!DEBUG_SCREENSHOTS) {
                           sync = eglCreateSyncKHR(mEGLDisplay, EGL_SYNC_NATIVE_FENCE_ANDROID, NULL);
                           // native fence fd will not be populated until flush() is done.
                           getRenderEngine().flush();
                        } else {
                            sync = EGL_NO_SYNC_KHR;
                        }
                        if (sync != EGL_NO_SYNC_KHR) {
                            // get the sync fd
                            syncFd = eglDupNativeFenceFDANDROID(mEGLDisplay, sync);
                            if (syncFd == EGL_NO_NATIVE_FENCE_FD_ANDROID) {
                                ALOGW("captureScreen: failed to dup sync khr object");
                                syncFd = -1;
                            }
                            eglDestroySyncKHR(mEGLDisplay, sync);
                        } else {
                            // fallback path
                            sync = eglCreateSyncKHR(mEGLDisplay, EGL_SYNC_FENCE_KHR, NULL);
                            if (sync != EGL_NO_SYNC_KHR) {
                                EGLint result = eglClientWaitSyncKHR(mEGLDisplay, sync,
                                    EGL_SYNC_FLUSH_COMMANDS_BIT_KHR, 2000000000 /*2 sec*/);
                                EGLint eglErr = eglGetError();
                                if (result == EGL_TIMEOUT_EXPIRED_KHR) {
                                    ALOGW("captureScreen: fence wait timed out");
                                } else {
                                    ALOGW_IF(eglErr != EGL_SUCCESS,
                                            "captureScreen: error waiting on EGL fence: %#x", eglErr);
                                }
                                eglDestroySyncKHR(mEGLDisplay, sync);
                            } else {
                                ALOGW("captureScreen: error creating EGL fence: %#x", eglGetError());
                            }
                        }
                        if (DEBUG_SCREENSHOTS) {
                            uint32_t* pixels = new uint32_t[reqWidth*reqHeight];
                            getRenderEngine().readPixels(0, 0, reqWidth, reqHeight, pixels);
                            checkScreenshot(reqWidth, reqHeight, reqWidth, pixels,
                                    hw, minLayerZ, maxLayerZ);
                            delete [] pixels;
                        }

                    } else {
                        ALOGE("got GL_FRAMEBUFFER_COMPLETE_OES error while taking screenshot");
                        result = INVALID_OPERATION;
                    }
                    // destroy our image
                    eglDestroyImageKHR(mEGLDisplay, image);
                } else {
                    result = BAD_VALUE;
                }
                // queueBuffer takes ownership of syncFd
                result = window->queueBuffer(window, buffer, syncFd);
            }
        } else {
            result = BAD_VALUE;
        }
        native_window_api_disconnect(window, NATIVE_WINDOW_API_EGL);
    }

    return result;
}

void SurfaceFlinger::checkScreenshot(size_t w, size_t s, size_t h, void const* vaddr,
        const sp<const DisplayDevice>& hw, uint32_t minLayerZ, uint32_t maxLayerZ) {
    if (DEBUG_SCREENSHOTS) {
        for (size_t y=0 ; y<h ; y++) {
            uint32_t const * p = (uint32_t const *)vaddr + y*s;
            for (size_t x=0 ; x<w ; x++) {
                if (p[x] != 0xFF000000) return;
            }
        }
        ALOGE("*** we just took a black screenshot ***\n"
                "requested minz=%d, maxz=%d, layerStack=%d",
                minLayerZ, maxLayerZ, hw->getLayerStack());
        const LayerVector& layers( mDrawingState.layersSortedByZ );
        const size_t count = layers.size();
        for (size_t i=0 ; i<count ; ++i) {
            const sp<Layer>& layer(layers[i]);
            const Layer::State& state(layer->getDrawingState());
            const bool visible = (layer->getSystemName() == hw->getActiveSystemName())
                                && (state.layerStack == hw->getLayerStack())
                                && (state.z >= minLayerZ && state.z <= maxLayerZ)
                                && (layer->isVisible());
            ALOGE("%c index=%zu, name=%s, layerStack=%d, z=%d, visible=%d, flags=%x, alpha=%x",
                    visible ? '+' : '-',
                            i, layer->getName().string(), state.layerStack, state.z,
                            layer->isVisible(), state.flags, state.alpha);
        }
    }
}

// ---------------------------------------------------------------------------

SurfaceFlinger::LayerVector::LayerVector() {
}

SurfaceFlinger::LayerVector::LayerVector(const LayerVector& rhs)
    : SortedVector<sp<Layer> >(rhs) {
}

int SurfaceFlinger::LayerVector::do_compare(const void* lhs,
    const void* rhs) const
{
    // sort layers per layer-stack, then by z-order and finally by sequence
    const sp<Layer>& l(*reinterpret_cast<const sp<Layer>*>(lhs));
    const sp<Layer>& r(*reinterpret_cast<const sp<Layer>*>(rhs));

    uint32_t ls = l->getCurrentState().layerStack;
    uint32_t rs = r->getCurrentState().layerStack;
    if (ls != rs)
        return ls - rs;

    uint32_t lz = l->getCurrentState().z;
    uint32_t rz = r->getCurrentState().z;
    if (lz != rz)
        return lz - rz;

    return l->sequence - r->sequence;
}

// ---------------------------------------------------------------------------

SurfaceFlinger::DisplayDeviceState::DisplayDeviceState()
    : type(DisplayDevice::DISPLAY_ID_INVALID), width(0), height(0) {
}

SurfaceFlinger::DisplayDeviceState::DisplayDeviceState(DisplayDevice::DisplayType type)
    : type(type), layerStack(DisplayDevice::NO_LAYER_STACK), orientation(0), width(0), height(0) {
    viewport.makeInvalid();
    frame.makeInvalid();
}

status_t SurfaceFlinger::enterSelf(){
    invalidateHwcGeometry();
    repaintEverything();
    return NO_ERROR;
}

status_t SurfaceFlinger::exitSelf(){
    //invalidateHwcGeometry();
    //repaintEverything();
    return NO_ERROR;
}

// ---------------------------------------------------------------------------

}; // namespace android


#if defined(__gl_h_)
#error "don't include gl/gl.h in this file"
#endif

#if defined(__gl2_h_)
#error "don't include gl2/gl2.h in this file"
#endif
