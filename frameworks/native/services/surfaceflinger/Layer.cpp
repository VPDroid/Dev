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

#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <math.h>

#include <cutils/compiler.h>
#include <cutils/native_handle.h>
#include <cutils/properties.h>

#include <utils/Errors.h>
#include <utils/Log.h>
#include <utils/NativeHandle.h>
#include <utils/StopWatch.h>
#include <utils/Trace.h>

#include <ui/GraphicBuffer.h>
#include <ui/PixelFormat.h>

#include <gui/BufferItem.h>
#include <gui/Surface.h>

#include "clz.h"
#include "Colorizer.h"
#include "DisplayDevice.h"
#include "Layer.h"
#include "MonitoredProducer.h"
#include "SurfaceFlinger.h"

#include "DisplayHardware/HWComposer.h"

#include "RenderEngine/RenderEngine.h"

#define DEBUG_RESIZE    0

namespace android {

// ---------------------------------------------------------------------------

int32_t Layer::sSequence = 1;

Layer::Layer(SurfaceFlinger* flinger, const sp<Client>& client,
        const String8& name, const String8& systemname,  uint32_t w, uint32_t h, uint32_t flags)
    :   contentDirty(false),
        sequence(uint32_t(android_atomic_inc(&sSequence))),
        mFlinger(flinger),
        mTextureName(-1U),
        mPremultipliedAlpha(true),
        mName("unnamed"),
        mSystemName(""),
        mFormat(PIXEL_FORMAT_NONE),
        mTransactionFlags(0),
        mQueuedFrames(0),
        mSidebandStreamChanged(false),
        mCurrentTransform(0),
        mCurrentScalingMode(NATIVE_WINDOW_SCALING_MODE_FREEZE),
        mCurrentOpacity(true),
        mRefreshPending(false),
        mFrameLatencyNeeded(false),
        mFiltering(false),
        mNeedsFiltering(false),
        mMesh(Mesh::TRIANGLE_FAN, 4, 2, 2),
        mProtectedByApp(false),
        mHasSurface(false),
        mClientRef(client),
        mPotentialCursor(false),
        mQueueItemLock(),
        mQueueItemCondition(),
        mQueueItems(),
        mLastFrameNumberReceived(0),
        mUpdateTexImageFailed(false)
{
    mCurrentCrop.makeInvalid();
    mFlinger->getRenderEngine().genTextures(1, &mTextureName);
    mTexture.init(Texture::TEXTURE_EXTERNAL, mTextureName);

    uint32_t layerFlags = 0;
    if (flags & ISurfaceComposerClient::eHidden)
        layerFlags |= layer_state_t::eLayerHidden;
    if (flags & ISurfaceComposerClient::eOpaque)
        layerFlags |= layer_state_t::eLayerOpaque;
    if (flags & ISurfaceComposerClient::eSecure)
        layerFlags |= layer_state_t::eLayerSecure;

    if (flags & ISurfaceComposerClient::eNonPremultiplied)
        mPremultipliedAlpha = false;

    mName = name;
    mSystemName = systemname;

    mCurrentState.active.w = w;
    mCurrentState.active.h = h;
    mCurrentState.active.crop.makeInvalid();
    mCurrentState.z = 0;
    mCurrentState.alpha = 0xFF;
    mCurrentState.layerStack = 0;
    mCurrentState.flags = layerFlags;
    mCurrentState.sequence = 0;
    mCurrentState.transform.set(0, 0);
    mCurrentState.requested = mCurrentState.active;

    // drawing state & current state are identical
    mDrawingState = mCurrentState;

    nsecs_t displayPeriod =
            flinger->getHwComposer().getRefreshPeriod(HWC_DISPLAY_PRIMARY);
    mFrameTracker.setDisplayRefreshPeriod(displayPeriod);
}

void Layer::onFirstRef() {
    // Creates a custom BufferQueue for SurfaceFlingerConsumer to use
    sp<IGraphicBufferProducer> producer;
    sp<IGraphicBufferConsumer> consumer;
    BufferQueue::createBufferQueue(&producer, &consumer);
    mProducer = new MonitoredProducer(producer, mFlinger);
    mSurfaceFlingerConsumer = new SurfaceFlingerConsumer(consumer, mTextureName);
    mSurfaceFlingerConsumer->setConsumerUsageBits(getEffectiveUsage(0));
    mSurfaceFlingerConsumer->setContentsChangedListener(this);
    mSurfaceFlingerConsumer->setName(mName);

#ifdef TARGET_DISABLE_TRIPLE_BUFFERING
#warning "disabling triple buffering"
    mSurfaceFlingerConsumer->setDefaultMaxBufferCount(2);
#else
    mSurfaceFlingerConsumer->setDefaultMaxBufferCount(3);
#endif

    const sp<const DisplayDevice> hw(mFlinger->getDefaultDisplayDevice());
    updateTransformHint(hw);
}

Layer::~Layer() {
    sp<Client> c(mClientRef.promote());
    if (c != 0) {
        c->detachLayer(this);
    }
    mFlinger->deleteTextureAsync(mTextureName);
    mFrameTracker.logAndResetStats(mName);
}

// ---------------------------------------------------------------------------
// callbacks
// ---------------------------------------------------------------------------

void Layer::onLayerDisplayed(const sp<const DisplayDevice>& /* hw */,
        HWComposer::HWCLayerInterface* layer) {
    if (layer) {
        layer->onDisplayed();
        mSurfaceFlingerConsumer->setReleaseFence(layer->getAndResetReleaseFence());
    }
}

void Layer::onFrameAvailable(const BufferItem& item) {
    // Add this buffer from our internal queue tracker
    { // Autolock scope
        Mutex::Autolock lock(mQueueItemLock);

        // Reset the frame number tracker when we receive the first buffer after
        // a frame number reset
        if (item.mFrameNumber == 1) {
            mLastFrameNumberReceived = 0;
        }

        // Ensure that callbacks are handled in order
        while (item.mFrameNumber != mLastFrameNumberReceived + 1) {
            status_t result = mQueueItemCondition.waitRelative(mQueueItemLock,
                    ms2ns(500));
            if (result != NO_ERROR) {
                ALOGE("[%s] Timed out waiting on callback", mName.string());
            }
        }

        mQueueItems.push_back(item);
        android_atomic_inc(&mQueuedFrames);

        // Wake up any pending callbacks
        mLastFrameNumberReceived = item.mFrameNumber;
        mQueueItemCondition.broadcast();
    }

    mFlinger->signalLayerUpdate();
}

void Layer::onFrameReplaced(const BufferItem& item) {
    Mutex::Autolock lock(mQueueItemLock);

    // Ensure that callbacks are handled in order
    while (item.mFrameNumber != mLastFrameNumberReceived + 1) {
        status_t result = mQueueItemCondition.waitRelative(mQueueItemLock,
                ms2ns(500));
        if (result != NO_ERROR) {
            ALOGE("[%s] Timed out waiting on callback", mName.string());
        }
    }

    if (mQueueItems.empty()) {
        ALOGE("Can't replace a frame on an empty queue");
        return;
    }
    mQueueItems.editItemAt(0) = item;

    // Wake up any pending callbacks
    mLastFrameNumberReceived = item.mFrameNumber;
    mQueueItemCondition.broadcast();
}

void Layer::onSidebandStreamChanged() {
    if (android_atomic_release_cas(false, true, &mSidebandStreamChanged) == 0) {
        // mSidebandStreamChanged was false
        mFlinger->signalLayerUpdate();
    }
}

// called with SurfaceFlinger::mStateLock from the drawing thread after
// the layer has been remove from the current state list (and just before
// it's removed from the drawing state list)
void Layer::onRemoved() {
    mSurfaceFlingerConsumer->abandon();
}

// ---------------------------------------------------------------------------
// set-up
// ---------------------------------------------------------------------------

const String8& Layer::getName() const {
    return mName;
}

status_t Layer::setBuffers( uint32_t w, uint32_t h,
                            PixelFormat format, uint32_t flags)
{
    uint32_t const maxSurfaceDims = min(
            mFlinger->getMaxTextureSize(), mFlinger->getMaxViewportDims());

    // never allow a surface larger than what our underlying GL implementation
    // can handle.
    if ((uint32_t(w)>maxSurfaceDims) || (uint32_t(h)>maxSurfaceDims)) {
        ALOGE("dimensions too large %u x %u", uint32_t(w), uint32_t(h));
        return BAD_VALUE;
    }

    mFormat = format;

    mPotentialCursor = (flags & ISurfaceComposerClient::eCursorWindow) ? true : false;
    mProtectedByApp = (flags & ISurfaceComposerClient::eProtectedByApp) ? true : false;
    mCurrentOpacity = getOpacityForFormat(format);

    mSurfaceFlingerConsumer->setDefaultBufferSize(w, h);
    mSurfaceFlingerConsumer->setDefaultBufferFormat(format);
    mSurfaceFlingerConsumer->setConsumerUsageBits(getEffectiveUsage(0));

    return NO_ERROR;
}

sp<IBinder> Layer::getHandle() {
    Mutex::Autolock _l(mLock);

    LOG_ALWAYS_FATAL_IF(mHasSurface,
            "Layer::getHandle() has already been called");

    mHasSurface = true;

    /*
     * The layer handle is just a BBinder object passed to the client
     * (remote process) -- we don't keep any reference on our side such that
     * the dtor is called when the remote side let go of its reference.
     *
     * LayerCleaner ensures that mFlinger->onLayerDestroyed() is called for
     * this layer when the handle is destroyed.
     */

    class Handle : public BBinder, public LayerCleaner {
        wp<const Layer> mOwner;
    public:
        Handle(const sp<SurfaceFlinger>& flinger, const sp<Layer>& layer)
            : LayerCleaner(flinger, layer), mOwner(layer) {
        }
    };

    return new Handle(mFlinger, this);
}

sp<IGraphicBufferProducer> Layer::getProducer() const {
    return mProducer;
}

// ---------------------------------------------------------------------------
// h/w composer set-up
// ---------------------------------------------------------------------------

Rect Layer::getContentCrop() const {
    // this is the crop rectangle that applies to the buffer
    // itself (as opposed to the window)
    Rect crop;
    if (!mCurrentCrop.isEmpty()) {
        // if the buffer crop is defined, we use that
        crop = mCurrentCrop;
    } else if (mActiveBuffer != NULL) {
        // otherwise we use the whole buffer
        crop = mActiveBuffer->getBounds();
    } else {
        // if we don't have a buffer yet, we use an empty/invalid crop
        crop.makeInvalid();
    }
    return crop;
}

static Rect reduce(const Rect& win, const Region& exclude) {
    if (CC_LIKELY(exclude.isEmpty())) {
        return win;
    }
    if (exclude.isRect()) {
        return win.reduce(exclude.getBounds());
    }
    return Region(win).subtract(exclude).getBounds();
}

Rect Layer::computeBounds() const {
    const Layer::State& s(getDrawingState());
    return computeBounds(s.activeTransparentRegion);
}

Rect Layer::computeBounds(const Region& activeTransparentRegion) const {
    const Layer::State& s(getDrawingState());
    Rect win(s.active.w, s.active.h);
    if (!s.active.crop.isEmpty()) {
        win.intersect(s.active.crop, &win);
    }
    // subtract the transparent region and snap to the bounds
    return reduce(win, activeTransparentRegion);
}

FloatRect Layer::computeCrop(const sp<const DisplayDevice>& hw) const {
    // the content crop is the area of the content that gets scaled to the
    // layer's size.
    FloatRect crop(getContentCrop());

    // the active.crop is the area of the window that gets cropped, but not
    // scaled in any ways.
    const State& s(getDrawingState());

    // apply the projection's clipping to the window crop in
    // layerstack space, and convert-back to layer space.
    // if there are no window scaling involved, this operation will map to full
    // pixels in the buffer.
    // FIXME: the 3 lines below can produce slightly incorrect clipping when we have
    // a viewport clipping and a window transform. we should use floating point to fix this.

    Rect activeCrop(s.active.w, s.active.h);
    if (!s.active.crop.isEmpty()) {
        activeCrop = s.active.crop;
    }

    activeCrop = s.transform.transform(activeCrop);
    activeCrop.intersect(hw->getViewport(), &activeCrop);
    activeCrop = s.transform.inverse().transform(activeCrop);

    // This needs to be here as transform.transform(Rect) computes the
    // transformed rect and then takes the bounding box of the result before
    // returning. This means
    // transform.inverse().transform(transform.transform(Rect)) != Rect
    // in which case we need to make sure the final rect is clipped to the
    // display bounds.
    activeCrop.intersect(Rect(s.active.w, s.active.h), &activeCrop);

    // subtract the transparent region and snap to the bounds
    activeCrop = reduce(activeCrop, s.activeTransparentRegion);

    if (!activeCrop.isEmpty()) {
        // Transform the window crop to match the buffer coordinate system,
        // which means using the inverse of the current transform set on the
        // SurfaceFlingerConsumer.
        uint32_t invTransform = mCurrentTransform;
        if (mSurfaceFlingerConsumer->getTransformToDisplayInverse()) {
            /*
             * the code below applies the display's inverse transform to the buffer
             */
            uint32_t invTransformOrient = hw->getOrientationTransform();
            // calculate the inverse transform
            if (invTransformOrient & NATIVE_WINDOW_TRANSFORM_ROT_90) {
                invTransformOrient ^= NATIVE_WINDOW_TRANSFORM_FLIP_V |
                        NATIVE_WINDOW_TRANSFORM_FLIP_H;
                // If the transform has been rotated the axis of flip has been swapped
                // so we need to swap which flip operations we are performing
                bool is_h_flipped = (invTransform & NATIVE_WINDOW_TRANSFORM_FLIP_H) != 0;
                bool is_v_flipped = (invTransform & NATIVE_WINDOW_TRANSFORM_FLIP_V) != 0;
                if (is_h_flipped != is_v_flipped) {
                    invTransform ^= NATIVE_WINDOW_TRANSFORM_FLIP_V |
                            NATIVE_WINDOW_TRANSFORM_FLIP_H;
                }
            }
            // and apply to the current transform
            invTransform = (Transform(invTransform) * Transform(invTransformOrient)).getOrientation();
        }

        int winWidth = s.active.w;
        int winHeight = s.active.h;
        if (invTransform & NATIVE_WINDOW_TRANSFORM_ROT_90) {
            // If the activeCrop has been rotate the ends are rotated but not
            // the space itself so when transforming ends back we can't rely on
            // a modification of the axes of rotation. To account for this we
            // need to reorient the inverse rotation in terms of the current
            // axes of rotation.
            bool is_h_flipped = (invTransform & NATIVE_WINDOW_TRANSFORM_FLIP_H) != 0;
            bool is_v_flipped = (invTransform & NATIVE_WINDOW_TRANSFORM_FLIP_V) != 0;
            if (is_h_flipped == is_v_flipped) {
                invTransform ^= NATIVE_WINDOW_TRANSFORM_FLIP_V |
                        NATIVE_WINDOW_TRANSFORM_FLIP_H;
            }
            winWidth = s.active.h;
            winHeight = s.active.w;
        }
        const Rect winCrop = activeCrop.transform(
                invTransform, s.active.w, s.active.h);

        // below, crop is intersected with winCrop expressed in crop's coordinate space
        float xScale = crop.getWidth()  / float(winWidth);
        float yScale = crop.getHeight() / float(winHeight);

        float insetL = winCrop.left                 * xScale;
        float insetT = winCrop.top                  * yScale;
        float insetR = (winWidth - winCrop.right )  * xScale;
        float insetB = (winHeight - winCrop.bottom) * yScale;

        crop.left   += insetL;
        crop.top    += insetT;
        crop.right  -= insetR;
        crop.bottom -= insetB;
    }
    return crop;
}

void Layer::setGeometry(
    const sp<const DisplayDevice>& hw,
        HWComposer::HWCLayerInterface& layer)
{
    layer.setDefaultState();

    // enable this layer
    layer.setSkip(false);

    if (isSecure() && !hw->isSecure()) {
        layer.setSkip(true);
    }

    // this gives us only the "orientation" component of the transform
    const State& s(getDrawingState());
    if (!isOpaque(s) || s.alpha != 0xFF) {
        layer.setBlending(mPremultipliedAlpha ?
                HWC_BLENDING_PREMULT :
                HWC_BLENDING_COVERAGE);
    }

    // apply the layer's transform, followed by the display's global transform
    // here we're guaranteed that the layer's transform preserves rects
    Region activeTransparentRegion(s.activeTransparentRegion);
    if (!s.active.crop.isEmpty()) {
        Rect activeCrop(s.active.crop);
        activeCrop = s.transform.transform(activeCrop);
        activeCrop.intersect(hw->getViewport(), &activeCrop);
        activeCrop = s.transform.inverse().transform(activeCrop);
        // This needs to be here as transform.transform(Rect) computes the
        // transformed rect and then takes the bounding box of the result before
        // returning. This means
        // transform.inverse().transform(transform.transform(Rect)) != Rect
        // in which case we need to make sure the final rect is clipped to the
        // display bounds.
        activeCrop.intersect(Rect(s.active.w, s.active.h), &activeCrop);
        // mark regions outside the crop as transparent
        activeTransparentRegion.orSelf(Rect(0, 0, s.active.w, activeCrop.top));
        activeTransparentRegion.orSelf(Rect(0, activeCrop.bottom,
                s.active.w, s.active.h));
        activeTransparentRegion.orSelf(Rect(0, activeCrop.top,
                activeCrop.left, activeCrop.bottom));
        activeTransparentRegion.orSelf(Rect(activeCrop.right, activeCrop.top,
                s.active.w, activeCrop.bottom));
    }
    Rect frame(s.transform.transform(computeBounds(activeTransparentRegion)));
    frame.intersect(hw->getViewport(), &frame);
    const Transform& tr(hw->getTransform());
    layer.setFrame(tr.transform(frame));
    layer.setCrop(computeCrop(hw));
    layer.setPlaneAlpha(s.alpha);

    /*
     * Transformations are applied in this order:
     * 1) buffer orientation/flip/mirror
     * 2) state transformation (window manager)
     * 3) layer orientation (screen orientation)
     * (NOTE: the matrices are multiplied in reverse order)
     */

    const Transform bufferOrientation(mCurrentTransform);
    Transform transform(tr * s.transform * bufferOrientation);

    if (mSurfaceFlingerConsumer->getTransformToDisplayInverse()) {
        /*
         * the code below applies the display's inverse transform to the buffer
         */
        uint32_t invTransform = hw->getOrientationTransform();
        uint32_t t_orientation = transform.getOrientation();
        // calculate the inverse transform
        if (invTransform & NATIVE_WINDOW_TRANSFORM_ROT_90) {
            invTransform ^= NATIVE_WINDOW_TRANSFORM_FLIP_V |
                    NATIVE_WINDOW_TRANSFORM_FLIP_H;
            // If the transform has been rotated the axis of flip has been swapped
            // so we need to swap which flip operations we are performing
            bool is_h_flipped = (t_orientation & NATIVE_WINDOW_TRANSFORM_FLIP_H) != 0;
            bool is_v_flipped = (t_orientation & NATIVE_WINDOW_TRANSFORM_FLIP_V) != 0;
            if (is_h_flipped != is_v_flipped) {
                t_orientation ^= NATIVE_WINDOW_TRANSFORM_FLIP_V |
                        NATIVE_WINDOW_TRANSFORM_FLIP_H;
            }
        }
        // and apply to the current transform
        transform = Transform(t_orientation) * Transform(invTransform);
    }

    // this gives us only the "orientation" component of the transform
    const uint32_t orientation = transform.getOrientation();
    if (orientation & Transform::ROT_INVALID) {
        // we can only handle simple transformation
        layer.setSkip(true);
    } else {
        layer.setTransform(orientation);
    }
}

void Layer::setPerFrameData(const sp<const DisplayDevice>& hw,
        HWComposer::HWCLayerInterface& layer) {
    // we have to set the visible region on every frame because
    // we currently free it during onLayerDisplayed(), which is called
    // after HWComposer::commit() -- every frame.
    // Apply this display's projection's viewport to the visible region
    // before giving it to the HWC HAL.
    const Transform& tr = hw->getTransform();
    Region visible = tr.transform(visibleRegion.intersect(hw->getViewport()));
    layer.setVisibleRegionScreen(visible);
    layer.setSurfaceDamage(surfaceDamageRegion);

    if (mSidebandStream.get()) {
        layer.setSidebandStream(mSidebandStream);
    } else {
        // NOTE: buffer can be NULL if the client never drew into this
        // layer yet, or if we ran out of memory
        layer.setBuffer(mActiveBuffer);
    }
}

void Layer::setAcquireFence(const sp<const DisplayDevice>& /* hw */,
        HWComposer::HWCLayerInterface& layer) {
    int fenceFd = -1;

    // TODO: there is a possible optimization here: we only need to set the
    // acquire fence the first time a new buffer is acquired on EACH display.

    if (layer.getCompositionType() == HWC_OVERLAY || layer.getCompositionType() == HWC_CURSOR_OVERLAY) {
        sp<Fence> fence = mSurfaceFlingerConsumer->getCurrentFence();
        if (fence->isValid()) {
            fenceFd = fence->dup();
            if (fenceFd == -1) {
                ALOGW("failed to dup layer fence, skipping sync: %d", errno);
            }
        }
    }
    layer.setAcquireFenceFd(fenceFd);
}

Rect Layer::getPosition(
    const sp<const DisplayDevice>& hw)
{
    // this gives us only the "orientation" component of the transform
    const State& s(getCurrentState());

    // apply the layer's transform, followed by the display's global transform
    // here we're guaranteed that the layer's transform preserves rects
    Rect win(s.active.w, s.active.h);
    if (!s.active.crop.isEmpty()) {
        win.intersect(s.active.crop, &win);
    }
    // subtract the transparent region and snap to the bounds
    Rect bounds = reduce(win, s.activeTransparentRegion);
    Rect frame(s.transform.transform(bounds));
    frame.intersect(hw->getViewport(), &frame);
    const Transform& tr(hw->getTransform());
    return Rect(tr.transform(frame));
}

// ---------------------------------------------------------------------------
// drawing...
// ---------------------------------------------------------------------------

void Layer::draw(const sp<const DisplayDevice>& hw, const Region& clip) const {
    onDraw(hw, clip, false);
}

void Layer::draw(const sp<const DisplayDevice>& hw,
        bool useIdentityTransform) const {
    onDraw(hw, Region(hw->bounds()), useIdentityTransform);
}

void Layer::draw(const sp<const DisplayDevice>& hw) const {
    onDraw(hw, Region(hw->bounds()), false);
}

void Layer::onDraw(const sp<const DisplayDevice>& hw, const Region& clip,
        bool useIdentityTransform) const
{
    ATRACE_CALL();

    if (CC_UNLIKELY(mActiveBuffer == 0)) {
        // the texture has not been created yet, this Layer has
        // in fact never been drawn into. This happens frequently with
        // SurfaceView because the WindowManager can't know when the client
        // has drawn the first time.

        // If there is nothing under us, we paint the screen in black, otherwise
        // we just skip this update.

        // figure out if there is something below us
        Region under;
        const SurfaceFlinger::LayerVector& drawingLayers(
                mFlinger->mDrawingState.layersSortedByZ);
        const size_t count = drawingLayers.size();
        for (size_t i=0 ; i<count ; ++i) {
            const sp<Layer>& layer(drawingLayers[i]);
            if (layer.get() == static_cast<Layer const*>(this))
                break;
            under.orSelf( hw->getTransform().transform(layer->visibleRegion) );
        }
        // if not everything below us is covered, we plug the holes!
        Region holes(clip.subtract(under));
        if (!holes.isEmpty()) {
            clearWithOpenGL(hw, holes, 0, 0, 0, 1);
        }
        return;
    }

    // Bind the current buffer to the GL texture, and wait for it to be
    // ready for us to draw into.
    status_t err = mSurfaceFlingerConsumer->bindTextureImage();
    if (err != NO_ERROR) {
        ALOGW("onDraw: bindTextureImage failed (err=%d)", err);
        // Go ahead and draw the buffer anyway; no matter what we do the screen
        // is probably going to have something visibly wrong.
    }

    bool blackOutLayer = isProtected() || (isSecure() && !hw->isSecure());

    RenderEngine& engine(mFlinger->getRenderEngine());

    if (!blackOutLayer) {
        // TODO: we could be more subtle with isFixedSize()
        const bool useFiltering = getFiltering() || needsFiltering(hw) || isFixedSize();

        // Query the texture matrix given our current filtering mode.
        float textureMatrix[16];
        mSurfaceFlingerConsumer->setFilteringEnabled(useFiltering);
        mSurfaceFlingerConsumer->getTransformMatrix(textureMatrix);

        if (mSurfaceFlingerConsumer->getTransformToDisplayInverse()) {

            /*
             * the code below applies the display's inverse transform to the texture transform
             */

            // create a 4x4 transform matrix from the display transform flags
            const mat4 flipH(-1,0,0,0,  0,1,0,0, 0,0,1,0, 1,0,0,1);
            const mat4 flipV( 1,0,0,0, 0,-1,0,0, 0,0,1,0, 0,1,0,1);
            const mat4 rot90( 0,1,0,0, -1,0,0,0, 0,0,1,0, 1,0,0,1);

            mat4 tr;
            uint32_t transform = hw->getOrientationTransform();
            if (transform & NATIVE_WINDOW_TRANSFORM_ROT_90)
                tr = tr * rot90;
            if (transform & NATIVE_WINDOW_TRANSFORM_FLIP_H)
                tr = tr * flipH;
            if (transform & NATIVE_WINDOW_TRANSFORM_FLIP_V)
                tr = tr * flipV;

            // calculate the inverse
            tr = inverse(tr);

            // and finally apply it to the original texture matrix
            const mat4 texTransform(mat4(static_cast<const float*>(textureMatrix)) * tr);
            memcpy(textureMatrix, texTransform.asArray(), sizeof(textureMatrix));
        }

        // Set things up for texturing.
        mTexture.setDimensions(mActiveBuffer->getWidth(), mActiveBuffer->getHeight());
        mTexture.setFiltering(useFiltering);
        mTexture.setMatrix(textureMatrix);

        engine.setupLayerTexturing(mTexture);
    } else {
        engine.setupLayerBlackedOut();
    }
    drawWithOpenGL(hw, clip, useIdentityTransform);
    engine.disableTexturing();
}


void Layer::clearWithOpenGL(const sp<const DisplayDevice>& hw,
        const Region& /* clip */, float red, float green, float blue,
        float alpha) const
{
    RenderEngine& engine(mFlinger->getRenderEngine());
    computeGeometry(hw, mMesh, false);
    engine.setupFillWithColor(red, green, blue, alpha);
    engine.drawMesh(mMesh);
}

void Layer::clearWithOpenGL(
        const sp<const DisplayDevice>& hw, const Region& clip) const {
    clearWithOpenGL(hw, clip, 0,0,0,0);
}

void Layer::drawWithOpenGL(const sp<const DisplayDevice>& hw,
        const Region& /* clip */, bool useIdentityTransform) const {
    const State& s(getDrawingState());

    computeGeometry(hw, mMesh, useIdentityTransform);

    /*
     * NOTE: the way we compute the texture coordinates here produces
     * different results than when we take the HWC path -- in the later case
     * the "source crop" is rounded to texel boundaries.
     * This can produce significantly different results when the texture
     * is scaled by a large amount.
     *
     * The GL code below is more logical (imho), and the difference with
     * HWC is due to a limitation of the HWC API to integers -- a question
     * is suspend is whether we should ignore this problem or revert to
     * GL composition when a buffer scaling is applied (maybe with some
     * minimal value)? Or, we could make GL behave like HWC -- but this feel
     * like more of a hack.
     */
    const Rect win(computeBounds());

    float left   = float(win.left)   / float(s.active.w);
    float top    = float(win.top)    / float(s.active.h);
    float right  = float(win.right)  / float(s.active.w);
    float bottom = float(win.bottom) / float(s.active.h);

    // TODO: we probably want to generate the texture coords with the mesh
    // here we assume that we only have 4 vertices
    Mesh::VertexArray<vec2> texCoords(mMesh.getTexCoordArray<vec2>());
    texCoords[0] = vec2(left, 1.0f - top);
    texCoords[1] = vec2(left, 1.0f - bottom);
    texCoords[2] = vec2(right, 1.0f - bottom);
    texCoords[3] = vec2(right, 1.0f - top);

    RenderEngine& engine(mFlinger->getRenderEngine());
    engine.setupLayerBlending(mPremultipliedAlpha, isOpaque(s), s.alpha);
    engine.drawMesh(mMesh);
    engine.disableBlending();
}

uint32_t Layer::getProducerStickyTransform() const {
    int producerStickyTransform = 0;
    int ret = mProducer->query(NATIVE_WINDOW_STICKY_TRANSFORM, &producerStickyTransform);
    if (ret != OK) {
        ALOGW("%s: Error %s (%d) while querying window sticky transform.", __FUNCTION__,
                strerror(-ret), ret);
        return 0;
    }
    return static_cast<uint32_t>(producerStickyTransform);
}

void Layer::setFiltering(bool filtering) {
    mFiltering = filtering;
}

bool Layer::getFiltering() const {
    return mFiltering;
}

// As documented in libhardware header, formats in the range
// 0x100 - 0x1FF are specific to the HAL implementation, and
// are known to have no alpha channel
// TODO: move definition for device-specific range into
// hardware.h, instead of using hard-coded values here.
#define HARDWARE_IS_DEVICE_FORMAT(f) ((f) >= 0x100 && (f) <= 0x1FF)

bool Layer::getOpacityForFormat(uint32_t format) {
    if (HARDWARE_IS_DEVICE_FORMAT(format)) {
        return true;
    }
    switch (format) {
        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_BGRA_8888:
            return false;
    }
    // in all other case, we have no blending (also for unknown formats)
    return true;
}

// ----------------------------------------------------------------------------
// local state
// ----------------------------------------------------------------------------

void Layer::computeGeometry(const sp<const DisplayDevice>& hw, Mesh& mesh,
        bool useIdentityTransform) const
{
    const Layer::State& s(getDrawingState());
    const Transform tr(useIdentityTransform ?
            hw->getTransform() : hw->getTransform() * s.transform);
    const uint32_t hw_h = hw->getHeight();
    Rect win(s.active.w, s.active.h);
    if (!s.active.crop.isEmpty()) {
        win.intersect(s.active.crop, &win);
    }
    // subtract the transparent region and snap to the bounds
    win = reduce(win, s.activeTransparentRegion);

    Mesh::VertexArray<vec2> position(mesh.getPositionArray<vec2>());
    position[0] = tr.transform(win.left,  win.top);
    position[1] = tr.transform(win.left,  win.bottom);
    position[2] = tr.transform(win.right, win.bottom);
    position[3] = tr.transform(win.right, win.top);
    for (size_t i=0 ; i<4 ; i++) {
        position[i].y = hw_h - position[i].y;
    }
}

bool Layer::isOpaque(const Layer::State& s) const
{
    // if we don't have a buffer yet, we're translucent regardless of the
    // layer's opaque flag.
    if (mActiveBuffer == 0) {
        return false;
    }

    // if the layer has the opaque flag, then we're always opaque,
    // otherwise we use the current buffer's format.
    return ((s.flags & layer_state_t::eLayerOpaque) != 0) || mCurrentOpacity;
}

bool Layer::isSecure() const
{
    const Layer::State& s(mDrawingState);
    return (s.flags & layer_state_t::eLayerSecure);
}

bool Layer::isProtected() const
{
    const sp<GraphicBuffer>& activeBuffer(mActiveBuffer);
    return (activeBuffer != 0) &&
            (activeBuffer->getUsage() & GRALLOC_USAGE_PROTECTED);
}

bool Layer::isFixedSize() const {
    return mCurrentScalingMode != NATIVE_WINDOW_SCALING_MODE_FREEZE;
}

bool Layer::isCropped() const {
    return !mCurrentCrop.isEmpty();
}

bool Layer::needsFiltering(const sp<const DisplayDevice>& hw) const {
    return mNeedsFiltering || hw->needsFiltering();
}

void Layer::setVisibleRegion(const Region& visibleRegion) {
    // always called from main thread
    this->visibleRegion = visibleRegion;
}

void Layer::setCoveredRegion(const Region& coveredRegion) {
    // always called from main thread
    this->coveredRegion = coveredRegion;
}

void Layer::setVisibleNonTransparentRegion(const Region&
        setVisibleNonTransparentRegion) {
    // always called from main thread
    this->visibleNonTransparentRegion = setVisibleNonTransparentRegion;
}

// ----------------------------------------------------------------------------
// transaction
// ----------------------------------------------------------------------------

uint32_t Layer::doTransaction(uint32_t flags) {
    ATRACE_CALL();

    const Layer::State& s(getDrawingState());
    const Layer::State& c(getCurrentState());

    const bool sizeChanged = (c.requested.w != s.requested.w) ||
                             (c.requested.h != s.requested.h);

    if (sizeChanged) {
        // the size changed, we need to ask our client to request a new buffer
        ALOGD_IF(DEBUG_RESIZE,
                "doTransaction: geometry (layer=%p '%s'), tr=%02x, scalingMode=%d\n"
                "  current={ active   ={ wh={%4u,%4u} crop={%4d,%4d,%4d,%4d} (%4d,%4d) }\n"
                "            requested={ wh={%4u,%4u} crop={%4d,%4d,%4d,%4d} (%4d,%4d) }}\n"
                "  drawing={ active   ={ wh={%4u,%4u} crop={%4d,%4d,%4d,%4d} (%4d,%4d) }\n"
                "            requested={ wh={%4u,%4u} crop={%4d,%4d,%4d,%4d} (%4d,%4d) }}\n",
                this, getName().string(), mCurrentTransform, mCurrentScalingMode,
                c.active.w, c.active.h,
                c.active.crop.left,
                c.active.crop.top,
                c.active.crop.right,
                c.active.crop.bottom,
                c.active.crop.getWidth(),
                c.active.crop.getHeight(),
                c.requested.w, c.requested.h,
                c.requested.crop.left,
                c.requested.crop.top,
                c.requested.crop.right,
                c.requested.crop.bottom,
                c.requested.crop.getWidth(),
                c.requested.crop.getHeight(),
                s.active.w, s.active.h,
                s.active.crop.left,
                s.active.crop.top,
                s.active.crop.right,
                s.active.crop.bottom,
                s.active.crop.getWidth(),
                s.active.crop.getHeight(),
                s.requested.w, s.requested.h,
                s.requested.crop.left,
                s.requested.crop.top,
                s.requested.crop.right,
                s.requested.crop.bottom,
                s.requested.crop.getWidth(),
                s.requested.crop.getHeight());

        // record the new size, form this point on, when the client request
        // a buffer, it'll get the new size.
        mSurfaceFlingerConsumer->setDefaultBufferSize(
                c.requested.w, c.requested.h);
    }

    if (!isFixedSize()) {

        const bool resizePending = (c.requested.w != c.active.w) ||
                                   (c.requested.h != c.active.h);

        if (resizePending && mSidebandStream == NULL) {
            // don't let Layer::doTransaction update the drawing state
            // if we have a pending resize, unless we are in fixed-size mode.
            // the drawing state will be updated only once we receive a buffer
            // with the correct size.
            //
            // in particular, we want to make sure the clip (which is part
            // of the geometry state) is latched together with the size but is
            // latched immediately when no resizing is involved.
            //
            // If a sideband stream is attached, however, we want to skip this
            // optimization so that transactions aren't missed when a buffer
            // never arrives

            flags |= eDontUpdateGeometryState;
        }
    }

    // always set active to requested, unless we're asked not to
    // this is used by Layer, which special cases resizes.
    if (flags & eDontUpdateGeometryState)  {
    } else {
        Layer::State& editCurrentState(getCurrentState());
        editCurrentState.active = c.requested;
    }

    if (s.active != c.active) {
        // invalidate and recompute the visible regions if needed
        flags |= Layer::eVisibleRegion;
    }

    if (c.sequence != s.sequence) {
        // invalidate and recompute the visible regions if needed
        flags |= eVisibleRegion;
        this->contentDirty = true;

        // we may use linear filtering, if the matrix scales us
        const uint8_t type = c.transform.getType();
        mNeedsFiltering = (!c.transform.preserveRects() ||
                (type >= Transform::SCALE));
    }

    // Commit the transaction
    commitTransaction();
    return flags;
}

void Layer::commitTransaction() {
    mDrawingState = mCurrentState;
}

uint32_t Layer::getTransactionFlags(uint32_t flags) {
    return android_atomic_and(~flags, &mTransactionFlags) & flags;
}

uint32_t Layer::setTransactionFlags(uint32_t flags) {
    return android_atomic_or(flags, &mTransactionFlags);
}

bool Layer::setPosition(float x, float y) {
    if (mCurrentState.transform.tx() == x && mCurrentState.transform.ty() == y)
        return false;
    mCurrentState.sequence++;
    mCurrentState.transform.set(x, y);
    setTransactionFlags(eTransactionNeeded);
    return true;
}
bool Layer::setLayer(uint32_t z) {
    if (mCurrentState.z == z)
        return false;
    mCurrentState.sequence++;
    mCurrentState.z = z;
    setTransactionFlags(eTransactionNeeded);
    return true;
}
bool Layer::setSize(uint32_t w, uint32_t h) {
    if (mCurrentState.requested.w == w && mCurrentState.requested.h == h)
        return false;
    mCurrentState.requested.w = w;
    mCurrentState.requested.h = h;
    setTransactionFlags(eTransactionNeeded);
    return true;
}
bool Layer::setAlpha(uint8_t alpha) {
    if (mCurrentState.alpha == alpha)
        return false;
    mCurrentState.sequence++;
    mCurrentState.alpha = alpha;
    setTransactionFlags(eTransactionNeeded);
    return true;
}
bool Layer::setMatrix(const layer_state_t::matrix22_t& matrix) {
    mCurrentState.sequence++;
    mCurrentState.transform.set(
            matrix.dsdx, matrix.dsdy, matrix.dtdx, matrix.dtdy);
    setTransactionFlags(eTransactionNeeded);
    return true;
}
bool Layer::setTransparentRegionHint(const Region& transparent) {
    mCurrentState.requestedTransparentRegion = transparent;
    setTransactionFlags(eTransactionNeeded);
    return true;
}
bool Layer::setFlags(uint8_t flags, uint8_t mask) {
    const uint32_t newFlags = (mCurrentState.flags & ~mask) | (flags & mask);
    if (mCurrentState.flags == newFlags)
        return false;
    mCurrentState.sequence++;
    mCurrentState.flags = newFlags;
    setTransactionFlags(eTransactionNeeded);
    return true;
}
bool Layer::setCrop(const Rect& crop) {
    if (mCurrentState.requested.crop == crop)
        return false;
    mCurrentState.sequence++;
    mCurrentState.requested.crop = crop;
    setTransactionFlags(eTransactionNeeded);
    return true;
}

bool Layer::setLayerStack(uint32_t layerStack) {
    if (mCurrentState.layerStack == layerStack)
        return false;
    mCurrentState.sequence++;
    mCurrentState.layerStack = layerStack;
    setTransactionFlags(eTransactionNeeded);
    return true;
}

void Layer::useSurfaceDamage() {
    if (mFlinger->mForceFullDamage) {
        surfaceDamageRegion = Region::INVALID_REGION;
    } else {
        surfaceDamageRegion = mSurfaceFlingerConsumer->getSurfaceDamage();
    }
}

void Layer::useEmptyDamage() {
    surfaceDamageRegion.clear();
}

// ----------------------------------------------------------------------------
// pageflip handling...
// ----------------------------------------------------------------------------

bool Layer::shouldPresentNow(const DispSync& dispSync) const {
    if (mSidebandStreamChanged) {
        return true;
    }

    Mutex::Autolock lock(mQueueItemLock);
    if (mQueueItems.empty()) {
        return false;
    }
    auto timestamp = mQueueItems[0].mTimestamp;
    nsecs_t expectedPresent =
            mSurfaceFlingerConsumer->computeExpectedPresent(dispSync);

    // Ignore timestamps more than a second in the future
    bool isPlausible = timestamp < (expectedPresent + s2ns(1));
    ALOGW_IF(!isPlausible, "[%s] Timestamp %" PRId64 " seems implausible "
            "relative to expectedPresent %" PRId64, mName.string(), timestamp,
            expectedPresent);

    bool isDue = timestamp < expectedPresent;
    return isDue || !isPlausible;
}

bool Layer::onPreComposition() {
    mRefreshPending = false;
    return mQueuedFrames > 0 || mSidebandStreamChanged;
}

void Layer::onPostComposition() {
    if (mFrameLatencyNeeded) {
        nsecs_t desiredPresentTime = mSurfaceFlingerConsumer->getTimestamp();
        mFrameTracker.setDesiredPresentTime(desiredPresentTime);

        sp<Fence> frameReadyFence = mSurfaceFlingerConsumer->getCurrentFence();
        if (frameReadyFence->isValid()) {
            mFrameTracker.setFrameReadyFence(frameReadyFence);
        } else {
            // There was no fence for this frame, so assume that it was ready
            // to be presented at the desired present time.
            mFrameTracker.setFrameReadyTime(desiredPresentTime);
        }

        const HWComposer& hwc = mFlinger->getHwComposer();
        sp<Fence> presentFence = hwc.getDisplayFence(HWC_DISPLAY_PRIMARY);
        if (presentFence->isValid()) {
            mFrameTracker.setActualPresentFence(presentFence);
        } else {
            // The HWC doesn't support present fences, so use the refresh
            // timestamp instead.
            nsecs_t presentTime = hwc.getRefreshTimestamp(HWC_DISPLAY_PRIMARY);
            mFrameTracker.setActualPresentTime(presentTime);
        }

        mFrameTracker.advanceFrame();
        mFrameLatencyNeeded = false;
    }
}

bool Layer::isVisible() const {
    const Layer::State& s(mDrawingState);
    return !(s.flags & layer_state_t::eLayerHidden) && s.alpha
            && (mActiveBuffer != NULL || mSidebandStream != NULL);
}

Region Layer::latchBuffer(bool& recomputeVisibleRegions)
{
    ATRACE_CALL();

    if (android_atomic_acquire_cas(true, false, &mSidebandStreamChanged) == 0) {
        // mSidebandStreamChanged was true
        mSidebandStream = mSurfaceFlingerConsumer->getSidebandStream();
        if (mSidebandStream != NULL) {
            setTransactionFlags(eTransactionNeeded);
            mFlinger->setTransactionFlags(eTraversalNeeded);
        }
        recomputeVisibleRegions = true;

        const State& s(getDrawingState());
        return s.transform.transform(Region(Rect(s.active.w, s.active.h)));
    }

    Region outDirtyRegion;
    if (mQueuedFrames > 0) {

        // if we've already called updateTexImage() without going through
        // a composition step, we have to skip this layer at this point
        // because we cannot call updateTeximage() without a corresponding
        // compositionComplete() call.
        // we'll trigger an update in onPreComposition().
        if (mRefreshPending) {
            return outDirtyRegion;
        }

        // Capture the old state of the layer for comparisons later
        const State& s(getDrawingState());
        const bool oldOpacity = isOpaque(s);
        sp<GraphicBuffer> oldActiveBuffer = mActiveBuffer;

        struct Reject : public SurfaceFlingerConsumer::BufferRejecter {
            Layer::State& front;
            Layer::State& current;
            bool& recomputeVisibleRegions;
            bool stickyTransformSet;
            Reject(Layer::State& front, Layer::State& current,
                    bool& recomputeVisibleRegions, bool stickySet)
                : front(front), current(current),
                  recomputeVisibleRegions(recomputeVisibleRegions),
                  stickyTransformSet(stickySet) {
            }

            virtual bool reject(const sp<GraphicBuffer>& buf,
                    const BufferItem& item) {
                if (buf == NULL) {
                    return false;
                }

                uint32_t bufWidth  = buf->getWidth();
                uint32_t bufHeight = buf->getHeight();

                // check that we received a buffer of the right size
                // (Take the buffer's orientation into account)
                if (item.mTransform & Transform::ROT_90) {
                    swap(bufWidth, bufHeight);
                }

                bool isFixedSize = item.mScalingMode != NATIVE_WINDOW_SCALING_MODE_FREEZE;
                if (front.active != front.requested) {

                    if (isFixedSize ||
                            (bufWidth == front.requested.w &&
                             bufHeight == front.requested.h))
                    {
                        // Here we pretend the transaction happened by updating the
                        // current and drawing states. Drawing state is only accessed
                        // in this thread, no need to have it locked
                        front.active = front.requested;

                        // We also need to update the current state so that
                        // we don't end-up overwriting the drawing state with
                        // this stale current state during the next transaction
                        //
                        // NOTE: We don't need to hold the transaction lock here
                        // because State::active is only accessed from this thread.
                        current.active = front.active;

                        // recompute visible region
                        recomputeVisibleRegions = true;
                    }

                    ALOGD_IF(DEBUG_RESIZE,
                            "latchBuffer/reject: buffer (%ux%u, tr=%02x), scalingMode=%d\n"
                            "  drawing={ active   ={ wh={%4u,%4u} crop={%4d,%4d,%4d,%4d} (%4d,%4d) }\n"
                            "            requested={ wh={%4u,%4u} crop={%4d,%4d,%4d,%4d} (%4d,%4d) }}\n",
                            bufWidth, bufHeight, item.mTransform, item.mScalingMode,
                            front.active.w, front.active.h,
                            front.active.crop.left,
                            front.active.crop.top,
                            front.active.crop.right,
                            front.active.crop.bottom,
                            front.active.crop.getWidth(),
                            front.active.crop.getHeight(),
                            front.requested.w, front.requested.h,
                            front.requested.crop.left,
                            front.requested.crop.top,
                            front.requested.crop.right,
                            front.requested.crop.bottom,
                            front.requested.crop.getWidth(),
                            front.requested.crop.getHeight());
                }

                if (!isFixedSize && !stickyTransformSet) {
                    if (front.active.w != bufWidth ||
                        front.active.h != bufHeight) {
                        // reject this buffer
                        ALOGE("rejecting buffer: bufWidth=%d, bufHeight=%d, front.active.{w=%d, h=%d}",
                                bufWidth, bufHeight, front.active.w, front.active.h);
                        return true;
                    }
                }

                // if the transparent region has changed (this test is
                // conservative, but that's fine, worst case we're doing
                // a bit of extra work), we latch the new one and we
                // trigger a visible-region recompute.
                if (!front.activeTransparentRegion.isTriviallyEqual(
                        front.requestedTransparentRegion)) {
                    front.activeTransparentRegion = front.requestedTransparentRegion;

                    // We also need to update the current state so that
                    // we don't end-up overwriting the drawing state with
                    // this stale current state during the next transaction
                    //
                    // NOTE: We don't need to hold the transaction lock here
                    // because State::active is only accessed from this thread.
                    current.activeTransparentRegion = front.activeTransparentRegion;

                    // recompute visible region
                    recomputeVisibleRegions = true;
                }

                return false;
            }
        };

        Reject r(mDrawingState, getCurrentState(), recomputeVisibleRegions,
                getProducerStickyTransform() != 0);

        uint64_t maxFrameNumber = 0;
        {
            Mutex::Autolock lock(mQueueItemLock);
            maxFrameNumber = mLastFrameNumberReceived;
        }

        status_t updateResult = mSurfaceFlingerConsumer->updateTexImage(&r,
                mFlinger->mPrimaryDispSync, maxFrameNumber);
        if (updateResult == BufferQueue::PRESENT_LATER) {
            // Producer doesn't want buffer to be displayed yet.  Signal a
            // layer update so we check again at the next opportunity.
            mFlinger->signalLayerUpdate();
            return outDirtyRegion;
        } else if (updateResult == SurfaceFlingerConsumer::BUFFER_REJECTED) {
            // If the buffer has been rejected, remove it from the shadow queue
            // and return early
            Mutex::Autolock lock(mQueueItemLock);
            mQueueItems.removeAt(0);
            android_atomic_dec(&mQueuedFrames);
            return outDirtyRegion;
        } else if (updateResult != NO_ERROR || mUpdateTexImageFailed) {
            // This can occur if something goes wrong when trying to create the
            // EGLImage for this buffer. If this happens, the buffer has already
            // been released, so we need to clean up the queue and bug out
            // early.
            {
                Mutex::Autolock lock(mQueueItemLock);
                mQueueItems.clear();
                android_atomic_and(0, &mQueuedFrames);
            }

            // Once we have hit this state, the shadow queue may no longer
            // correctly reflect the incoming BufferQueue's contents, so even if
            // updateTexImage starts working, the only safe course of action is
            // to continue to ignore updates.
            mUpdateTexImageFailed = true;

            return outDirtyRegion;
        }

        { // Autolock scope
            auto currentFrameNumber = mSurfaceFlingerConsumer->getFrameNumber();

            Mutex::Autolock lock(mQueueItemLock);

            // Remove any stale buffers that have been dropped during
            // updateTexImage
            while (mQueueItems[0].mFrameNumber != currentFrameNumber) {
                mQueueItems.removeAt(0);
                android_atomic_dec(&mQueuedFrames);
            }

            mQueueItems.removeAt(0);
        }


        // Decrement the queued-frames count.  Signal another event if we
        // have more frames pending.
        if (android_atomic_dec(&mQueuedFrames) > 1) {
            mFlinger->signalLayerUpdate();
        }

        if (updateResult != NO_ERROR) {
            // something happened!
            recomputeVisibleRegions = true;
            return outDirtyRegion;
        }

        // update the active buffer
        mActiveBuffer = mSurfaceFlingerConsumer->getCurrentBuffer();
        if (mActiveBuffer == NULL) {
            // this can only happen if the very first buffer was rejected.
            return outDirtyRegion;
        }

        mRefreshPending = true;
        mFrameLatencyNeeded = true;
        if (oldActiveBuffer == NULL) {
             // the first time we receive a buffer, we need to trigger a
             // geometry invalidation.
            recomputeVisibleRegions = true;
         }

        Rect crop(mSurfaceFlingerConsumer->getCurrentCrop());
        const uint32_t transform(mSurfaceFlingerConsumer->getCurrentTransform());
        const uint32_t scalingMode(mSurfaceFlingerConsumer->getCurrentScalingMode());
        if ((crop != mCurrentCrop) ||
            (transform != mCurrentTransform) ||
            (scalingMode != mCurrentScalingMode))
        {
            mCurrentCrop = crop;
            mCurrentTransform = transform;
            mCurrentScalingMode = scalingMode;
            recomputeVisibleRegions = true;
        }

        if (oldActiveBuffer != NULL) {
            uint32_t bufWidth  = mActiveBuffer->getWidth();
            uint32_t bufHeight = mActiveBuffer->getHeight();
            if (bufWidth != uint32_t(oldActiveBuffer->width) ||
                bufHeight != uint32_t(oldActiveBuffer->height)) {
                recomputeVisibleRegions = true;
            }
        }

        mCurrentOpacity = getOpacityForFormat(mActiveBuffer->format);
        if (oldOpacity != isOpaque(s)) {
            recomputeVisibleRegions = true;
        }

        // FIXME: postedRegion should be dirty & bounds
        Region dirtyRegion(Rect(s.active.w, s.active.h));

        // transform the dirty region to window-manager space
        outDirtyRegion = (s.transform.transform(dirtyRegion));
    }
    return outDirtyRegion;
}

uint32_t Layer::getEffectiveUsage(uint32_t usage) const
{
    // TODO: should we do something special if mSecure is set?
    if (mProtectedByApp) {
        // need a hardware-protected path to external video sink
        usage |= GraphicBuffer::USAGE_PROTECTED;
    }
    if (mPotentialCursor) {
        usage |= GraphicBuffer::USAGE_CURSOR;
    }
    usage |= GraphicBuffer::USAGE_HW_COMPOSER;
    return usage;
}

void Layer::updateTransformHint(const sp<const DisplayDevice>& hw) const {
    uint32_t orientation = 0;
    if (!mFlinger->mDebugDisableTransformHint) {
        // The transform hint is used to improve performance, but we can
        // only have a single transform hint, it cannot
        // apply to all displays.
        const Transform& planeTransform(hw->getTransform());
        orientation = planeTransform.getOrientation();
        if (orientation & Transform::ROT_INVALID) {
            orientation = 0;
        }
    }
    mSurfaceFlingerConsumer->setTransformHint(orientation);
}

// ----------------------------------------------------------------------------
// debugging
// ----------------------------------------------------------------------------

void Layer::dump(String8& result, Colorizer& colorizer) const
{
    const Layer::State& s(getDrawingState());

    colorizer.colorize(result, Colorizer::GREEN);
    result.appendFormat(
            "+ %s %p (%s)\n",
            getTypeId(), this, getName().string());
    colorizer.reset(result);

    s.activeTransparentRegion.dump(result, "transparentRegion");
    visibleRegion.dump(result, "visibleRegion");
    surfaceDamageRegion.dump(result, "surfaceDamageRegion");
    sp<Client> client(mClientRef.promote());

    result.appendFormat(            "      "
            "layerStack=%4d, z=%9d, pos=(%g,%g), size=(%4d,%4d), crop=(%4d,%4d,%4d,%4d), "
            "isOpaque=%1d, invalidate=%1d, "
            "alpha=0x%02x, flags=0x%08x, tr=[%.2f, %.2f][%.2f, %.2f]\n"
            "      client=%p\n",
            s.layerStack, s.z, s.transform.tx(), s.transform.ty(), s.active.w, s.active.h,
            s.active.crop.left, s.active.crop.top,
            s.active.crop.right, s.active.crop.bottom,
            isOpaque(s), contentDirty,
            s.alpha, s.flags,
            s.transform[0][0], s.transform[0][1],
            s.transform[1][0], s.transform[1][1],
            client.get());

    sp<const GraphicBuffer> buf0(mActiveBuffer);
    uint32_t w0=0, h0=0, s0=0, f0=0;
    if (buf0 != 0) {
        w0 = buf0->getWidth();
        h0 = buf0->getHeight();
        s0 = buf0->getStride();
        f0 = buf0->format;
    }
    result.appendFormat(
            "      "
            "format=%2d, activeBuffer=[%4ux%4u:%4u,%3X],"
            " queued-frames=%d, mRefreshPending=%d\n",
            mFormat, w0, h0, s0,f0,
            mQueuedFrames, mRefreshPending);

    if (mSurfaceFlingerConsumer != 0) {
        mSurfaceFlingerConsumer->dump(result, "            ");
    }
}

void Layer::dumpFrameStats(String8& result) const {
    mFrameTracker.dumpStats(result);
}

void Layer::clearFrameStats() {
    mFrameTracker.clearStats();
}

void Layer::logFrameStats() {
    mFrameTracker.logAndResetStats(mName);
}

void Layer::getFrameStats(FrameStats* outStats) const {
    mFrameTracker.getStats(outStats);
}

const char*  Layer::systemName() const{
    return mSystemName.string();
}

String8  Layer::getSystemName() const{
    return mSystemName;
}

// ---------------------------------------------------------------------------

Layer::LayerCleaner::LayerCleaner(const sp<SurfaceFlinger>& flinger,
        const sp<Layer>& layer)
    : mFlinger(flinger), mLayer(layer) {
}

Layer::LayerCleaner::~LayerCleaner() {
    // destroy client resources
    mFlinger->onLayerDestroyed(mLayer);
}

// ---------------------------------------------------------------------------
}; // namespace android

#if defined(__gl_h_)
#error "don't include gl/gl.h in this file"
#endif

#if defined(__gl2_h_)
#error "don't include gl2/gl2.h in this file"
#endif
