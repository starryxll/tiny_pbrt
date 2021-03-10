//
// Created by 秦浩晨 on 2020/1/5.
//

#include "cameras/perspective.h"
#include "paramset.h"
#include "sampler.h"
#include "sampling.h"
#include "light.h"
#include "stats.h"

namespace pbrt {

PerspectiveCamera::PerspectiveCamera(const AnimatedTransform &CameraToWorld,
                                     const Bounds2f &screenWindow,
                                     Float shutterOpen, Float shutterClose,
                                     Float lensRadius, Float focalDistance,
                                     Float fov, Film *film,
                                     const Medium *medium)
        : ProjectiveCamera(CameraToWorld, Perspective(fov, 1e-2f, 1000.f),
                           screenWindow, shutterOpen, shutterClose, lensRadius,
                           focalDistance, film, medium) {
    // Compute differential changes in origin for perspective camera rays
    dxCamera =
            (RasterToCamera(Point3f(1, 0, 0)) - RasterToCamera(Point3f(0, 0, 0)));
    dyCamera =
            (RasterToCamera(Point3f(0, 1, 0)) - RasterToCamera(Point3f(0, 0, 0)));

    // Compute image plane bounds at $z=1$ for _PerspectiveCamera_
    Point2i res = film->fullResolution;
    Point3f pMin = RasterToCamera(Point3f(0, 0, 0));
    Point3f pMax = RasterToCamera(Point3f(res.x, res.y, 0));
    pMin /= pMin.z;
    pMax /= pMax.z;
    A = std::abs((pMax.x - pMin.x) * (pMax.y - pMin.y));
}



}