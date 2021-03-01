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

#ifndef ANDROID_HWUI_SNAPSHOT_H
#define ANDROID_HWUI_SNAPSHOT_H

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <utils/LinearAllocator.h>
#include <utils/RefBase.h>
#include <ui/Region.h>

#include <SkRegion.h>

#include "ClipArea.h"
#include "Layer.h"
#include "Matrix.h"
#include "Outline.h"
#include "Rect.h"
#include "utils/Macros.h"

namespace android {
namespace uirenderer {

/**
 * Temporary structure holding information for a single outline clip.
 *
 * These structures are treated as immutable once created, and only exist for a single frame, which
 * is why they may only be allocated with a LinearAllocator.
 */
class RoundRectClipState {
public:
    /** static void* operator new(size_t size); PURPOSELY OMITTED, allocator only **/
    static void* operator new(size_t size, LinearAllocator& allocator) {
        return allocator.alloc(size);
    }

    bool areaRequiresRoundRectClip(const Rect& rect) const {
        return rect.intersects(dangerRects[0])
                || rect.intersects(dangerRects[1])
                || rect.intersects(dangerRects[2])
                || rect.intersects(dangerRects[3]);
    }

    bool highPriority;
    Matrix4 matrix;
    Rect dangerRects[4];
    Rect innerRect;
    float radius;
};

class ProjectionPathMask {
public:
    /** static void* operator new(size_t size); PURPOSELY OMITTED, allocator only **/
    static void* operator new(size_t size, LinearAllocator& allocator) {
        return allocator.alloc(size);
    }

    const SkPath* projectionMask;
    Matrix4 projectionMaskTransform;
};

/**
 * A snapshot holds information about the current state of the rendering
 * surface. A snapshot is usually created whenever the user calls save()
 * and discarded when the user calls restore(). Once a snapshot is created,
 * it can hold information for deferred rendering.
 *
 * Each snapshot has a link to a previous snapshot, indicating the previous
 * state of the renderer.
 */
class Snapshot: public LightRefBase<Snapshot> {
public:

    Snapshot();
    Snapshot(const sp<Snapshot>& s, int saveFlags);

    /**
     * Various flags set on ::flags.
     */
    enum Flags {
        /**
         * Indicates that the clip region was modified. When this
         * snapshot is restored so must the clip.
         */
        kFlagClipSet = 0x1,
        /**
         * Indicates that this snapshot was created when saving
         * a new layer.
         */
        kFlagIsLayer = 0x2,
        /**
         * Indicates that this snapshot is a special type of layer
         * backed by an FBO. This flag only makes sense when the
         * flag kFlagIsLayer is also set.
         *
         * Viewport has been modified to fit the new Fbo, and must be
         * restored when this snapshot is restored.
         */
        kFlagIsFboLayer = 0x4,
        /**
         * Indicates that this snapshot or an ancestor snapshot is
         * an FBO layer.
         */
        kFlagFboTarget = 0x8,
    };

    /**
     * Modifies the current clip with the new clip rectangle and
     * the specified operation. The specified rectangle is transformed
     * by this snapshot's trasnformation.
     */
    bool clip(float left, float top, float right, float bottom,
            SkRegion::Op op = SkRegion::kIntersect_Op);

    /**
     * Modifies the current clip with the new clip rectangle and
     * the specified operation. The specified rectangle is considered
     * already transformed.
     */
    bool clipTransformed(const Rect& r, SkRegion::Op op = SkRegion::kIntersect_Op);

    /**
     * Modifies the current clip with the specified region and operation.
     * The specified region is considered already transformed.
     */
    bool clipRegionTransformed(const SkRegion& region, SkRegion::Op op);

    /**
     * Modifies the current clip with the specified path and operation.
     */
    bool clipPath(const SkPath& path, SkRegion::Op op);

    /**
     * Sets the current clip.
     */
    void setClip(float left, float top, float right, float bottom);

    /**
     * Returns the current clip in local coordinates. The clip rect is
     * transformed by the inverse transform matrix.
     */
    ANDROID_API const Rect& getLocalClip();

    /**
     * Returns the current clip in render target coordinates.
     */
    const Rect& getRenderTargetClip() { return mClipArea->getClipRect(); }

    /*
     * Accessor functions so that the clip area can stay private
     */
    bool clipIsEmpty() const { return mClipArea->isEmpty(); }
    const Rect& getClipRect() const { return mClipArea->getClipRect(); }
    const SkRegion& getClipRegion() const { return mClipArea->getClipRegion(); }
    bool clipIsSimple() const { return mClipArea->isSimple(); }
    const ClipArea& getClipArea() const { return *mClipArea; }

    /**
     * Resets the clip to the specified rect.
     */
    void resetClip(float left, float top, float right, float bottom);

    /**
     * Resets the current transform to a pure 3D translation.
     */
    void resetTransform(float x, float y, float z);

    void initializeViewport(int width, int height) {
        mViewportData.initialize(width, height);
        mClipAreaRoot.setViewportDimensions(width, height);
    }

    int getViewportWidth() const { return mViewportData.mWidth; }
    int getViewportHeight() const { return mViewportData.mHeight; }
    const Matrix4& getOrthoMatrix() const { return mViewportData.mOrthoMatrix; }

    const Vector3& getRelativeLightCenter() const { return mRelativeLightCenter; }
    void setRelativeLightCenter(const Vector3& lightCenter) { mRelativeLightCenter = lightCenter; }

    /**
     * Sets (and replaces) the current clipping outline
     *
     * If the current round rect clip is high priority, the incoming clip is ignored.
     */
    void setClippingRoundRect(LinearAllocator& allocator, const Rect& bounds,
            float radius, bool highPriority);

    /**
     * Sets (and replaces) the current projection mask
     */
    void setProjectionPathMask(LinearAllocator& allocator, const SkPath* path);

    /**
     * Indicates whether this snapshot should be ignored. A snapshot
     * is typically ignored if its layer is invisible or empty.
     */
    bool isIgnored() const;

    /**
     * Indicates whether the current transform has perspective components.
     */
    bool hasPerspectiveTransform() const;

    /**
     * Fills outTransform with the current, total transform to screen space,
     * across layer boundaries.
     */
    void buildScreenSpaceTransform(Matrix4* outTransform) const;

    /**
     * Dirty flags.
     */
    int flags;

    /**
     * Previous snapshot.
     */
    sp<Snapshot> previous;

    /**
     * A pointer to the currently active layer.
     *
     * This snapshot does not own the layer, this pointer must not be freed.
     */
    Layer* layer;

    /**
     * Target FBO used for rendering. Set to 0 when rendering directly
     * into the framebuffer.
     */
    GLuint fbo;

    /**
     * Indicates that this snapshot is invisible and nothing should be drawn
     * inside it. This flag is set only when the layer clips drawing to its
     * bounds and is passed to subsequent snapshots.
     */
    bool invisible;

    /**
     * If set to true, the layer will not be composited. This is similar to
     * invisible but this flag is not passed to subsequent snapshots.
     */
    bool empty;

    /**
     * Local transformation. Holds the current translation, scale and
     * rotation values.
     *
     * This is a reference to a matrix owned by this snapshot or another
     *  snapshot. This pointer must not be freed. See ::mTransformRoot.
     */
    mat4* transform;

    /**
     * The ancestor layer's dirty region.
     *
     * This is a reference to a region owned by a layer. This pointer must
     * not be freed.
     */
    Region* region;

    /**
     * Current alpha value. This value is 1 by default, but may be set by a DisplayList which
     * has translucent rendering in a non-overlapping View. This value will be used by
     * the renderer to set the alpha in the current color being used for ensuing drawing
     * operations. The value is inherited by child snapshots because the same value should
     * be applied to descendants of the current DisplayList (for example, a TextView contains
     * the base alpha value which should be applied to the child DisplayLists used for drawing
     * the actual text).
     */
    float alpha;

    /**
     * Current clipping round rect.
     *
     * Points to data not owned by the snapshot, and may only be replaced by subsequent RR clips,
     * never modified.
     */
    const RoundRectClipState* roundRectClipState;

    /**
     * Current projection masking path - used exclusively to mask tessellated circles.
     */
    const ProjectionPathMask* projectionPathMask;

    void dump() const;

private:
    struct ViewportData {
        ViewportData() : mWidth(0), mHeight(0) {}
        void initialize(int width, int height) {
            mWidth = width;
            mHeight = height;
            mOrthoMatrix.loadOrtho(0, width, height, 0, -1, 1);
        }

        /*
         * Width and height of current viewport.
         *
         * The viewport is always defined to be (0, 0, width, height).
         */
        int mWidth;
        int mHeight;
        /**
         * Contains the current orthographic, projection matrix.
         */
        mat4 mOrthoMatrix;
    };

    mat4 mTransformRoot;

    ClipArea mClipAreaRoot;
    ClipArea* mClipArea;
    Rect mLocalClip;

    ViewportData mViewportData;
    Vector3 mRelativeLightCenter;

}; // class Snapshot

}; // namespace uirenderer
}; // namespace android

#endif // ANDROID_HWUI_SNAPSHOT_H
