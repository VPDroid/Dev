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

#ifndef SkiaCanvasProxy_DEFINED
#define SkiaCanvasProxy_DEFINED

#include <cutils/compiler.h>
#include <SkCanvas.h>

#include "Canvas.h"

namespace android {
namespace uirenderer {

/**
 * This class serves as a proxy between Skia's SkCanvas and Android Framework's
 * Canvas.  The class does not maintain any draw-related state and will pass
 * through most requests directly to the Canvas provided in the constructor.
 *
 * Upon construction it is expected that the provided Canvas has already been
 * prepared for recording and will continue to be in the recording state while
 * this proxy class is being used.
 *
 * If filterHwuiCalls is true, the proxy silently ignores away draw calls that
 * aren't supported by HWUI.
 */
class ANDROID_API SkiaCanvasProxy : public SkCanvas {
public:
    SkiaCanvasProxy(Canvas* canvas, bool filterHwuiCalls = false);
    virtual ~SkiaCanvasProxy() {}

protected:

    virtual SkSurface* onNewSurface(const SkImageInfo&, const SkSurfaceProps&) override;

    virtual void willSave() override;
    virtual SaveLayerStrategy willSaveLayer(const SkRect*, const SkPaint*, SaveFlags) override;
    virtual void willRestore() override;

    virtual void didConcat(const SkMatrix&) override;
    virtual void didSetMatrix(const SkMatrix&) override;

    virtual void onDrawPaint(const SkPaint& paint) override;
    virtual void onDrawPoints(PointMode, size_t count, const SkPoint pts[],
                              const SkPaint&) override;
    virtual void onDrawOval(const SkRect&, const SkPaint&) override;
    virtual void onDrawRect(const SkRect&, const SkPaint&) override;
    virtual void onDrawRRect(const SkRRect&, const SkPaint&) override;
    virtual void onDrawPath(const SkPath& path, const SkPaint&) override;
    virtual void onDrawBitmap(const SkBitmap&, SkScalar left, SkScalar top,
                              const SkPaint*) override;
    virtual void onDrawBitmapRect(const SkBitmap&, const SkRect* src, const SkRect& dst,
                                  const SkPaint* paint, DrawBitmapRectFlags flags) override;
    virtual void onDrawBitmapNine(const SkBitmap& bitmap, const SkIRect& center,
                                  const SkRect& dst, const SkPaint*) override;
    virtual void onDrawSprite(const SkBitmap&, int left, int top,
                              const SkPaint*) override;
    virtual void onDrawVertices(VertexMode, int vertexCount, const SkPoint vertices[],
                                const SkPoint texs[], const SkColor colors[], SkXfermode*,
                                const uint16_t indices[], int indexCount,
                                const SkPaint&) override;

    virtual void onDrawDRRect(const SkRRect&, const SkRRect&, const SkPaint&) override;

    virtual void onDrawText(const void* text, size_t byteLength, SkScalar x, SkScalar y,
                            const SkPaint&) override;
    virtual void onDrawPosText(const void* text, size_t byteLength, const SkPoint pos[],
                               const SkPaint&) override;
    virtual void onDrawPosTextH(const void* text, size_t byteLength, const SkScalar xpos[],
                                SkScalar constY, const SkPaint&) override;
    virtual void onDrawTextOnPath(const void* text, size_t byteLength, const SkPath& path,
                                  const SkMatrix* matrix, const SkPaint&) override;
    virtual void onDrawTextBlob(const SkTextBlob* blob, SkScalar x, SkScalar y,
                                const SkPaint& paint) override;

    virtual void onDrawPatch(const SkPoint cubics[12], const SkColor colors[4],
                             const SkPoint texCoords[4], SkXfermode* xmode,
                             const SkPaint& paint) override;

    virtual void onClipRect(const SkRect&, SkRegion::Op, ClipEdgeStyle) override;
    virtual void onClipRRect(const SkRRect&, SkRegion::Op, ClipEdgeStyle) override;
    virtual void onClipPath(const SkPath&, SkRegion::Op, ClipEdgeStyle) override;
    virtual void onClipRegion(const SkRegion&, SkRegion::Op) override;

private:
    Canvas* mCanvas;
    bool mFilterHwuiCalls;

    typedef SkCanvas INHERITED;
};

}; // namespace uirenderer
}; // namespace android

#endif // SkiaCanvasProxy_DEFINED
