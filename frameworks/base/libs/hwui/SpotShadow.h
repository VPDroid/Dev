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

#ifndef ANDROID_HWUI_SPOT_SHADOW_H
#define ANDROID_HWUI_SPOT_SHADOW_H

#include "Debug.h"
#include "Vector.h"

namespace android {
namespace uirenderer {

class VertexBuffer;

class SpotShadow {
public:
    static void createSpotShadow(bool isCasterOpaque, const Vector3& lightCenter,
            float lightSize, const Vector3* poly, int polyLength,
            const Vector3& polyCentroid, VertexBuffer& retstrips);

private:
    struct VertexAngleData;

    static float projectCasterToOutline(Vector2& outline,
            const Vector3& lightCenter, const Vector3& polyVertex);

    static void computeLightPolygon(int points, const Vector3& lightCenter,
            float size, Vector3* ret);

    static void smoothPolygon(int level, int rays, float* rayDist);
    static float rayIntersectPoly(const Vector2* poly, int polyLength,
            const Vector2& point, float dx, float dy);

    static void xsort(Vector2* points, int pointsLength);
    static int hull(Vector2* points, int pointsLength, Vector2* retPoly);
    static bool ccw(float ax, float ay, float bx, float by, float cx, float cy);
    static void sort(Vector2* poly, int polyLength, const Vector2& center);

    static void swap(Vector2* points, int i, int j);
    static void quicksortCirc(Vector2* points, int low, int high, const Vector2& center);
    static void quicksortX(Vector2* points, int low, int high);

    static bool testPointInsidePolygon(const Vector2 testPoint, const Vector2* poly, int len);
    static void makeClockwise(Vector2* polygon, int len);
    static void reverse(Vector2* polygon, int len);

    static void generateTriangleStrip(bool isCasterOpaque, float shadowStrengthScale,
            Vector2* penumbra, int penumbraLength, Vector2* umbra, int umbraLength,
            const Vector3* poly, int polyLength, VertexBuffer& retstrips, const Vector2& centroid);

#if DEBUG_SHADOW
    static bool testConvex(const Vector2* polygon, int polygonLength,
            const char* name);
    static void testIntersection(const Vector2* poly1, int poly1Length,
        const Vector2* poly2, int poly2Length,
        const Vector2* intersection, int intersectionLength);
    static void updateBound(const Vector2 inVector, Vector2& lowerBound, Vector2& upperBound );
    static void dumpPolygon(const Vector2* poly, int polyLength, const char* polyName);
    static void dumpPolygon(const Vector3* poly, int polyLength, const char* polyName);
#endif

}; // SpotShadow

}; // namespace uirenderer
}; // namespace android

#endif // ANDROID_HWUI_SPOT_SHADOW_H
