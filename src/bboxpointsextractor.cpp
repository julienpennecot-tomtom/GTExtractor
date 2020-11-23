#include "bboxpointsextractor.hpp"

#include <eigen3/Eigen/Core>
#include <random>
#include <fstream>
#include <iostream>

BBoxPointsExtractor::BBoxPointsExtractor(const PointCloud& cloud,
                                         const std::vector<Camera>& cameras,
                                         const Intrinsics& intrinsics,
                                         const Detections& detections)
    : m_Cloud(cloud),
      m_Cameras(cameras),
      m_Intrinsics(intrinsics),
      m_Detections(detections)
{

}

PointCloud BBoxPointsExtractor::createPointCloudWithCameraCenters() const
{
    const int nPointsPerSphere = 100;
    const float sphereRadius = 0.2;
    Eigen::Vector3i color(255, 0, 0);
    int nPoints = m_Cameras.size() * nPointsPerSphere;

    std::default_random_engine generator;
    std::uniform_real_distribution<double> distribution(-1.0,1.0);

    PointCloud cloud;
    cloud.reserve(nPoints);

    for (const auto& camera : m_Cameras)
    {
        Eigen::Vector3d center(camera.center());
        for (int i = 0; i < nPointsPerSphere; i++)
        {
            double x = distribution(generator);
            double y = distribution(generator);
            double z = distribution(generator);
            Eigen::Vector3d p(x, y, z);
            Eigen::Vector3d pos = center + sphereRadius * p.normalized();
            Point point(pos, Eigen::Vector3i(255, 0, 0));
            cloud.push_back(point);
        }
    }
    return cloud;
}

PointCloud BBoxPointsExtractor::createPointCloudWithCameraDirections() const
{
    const int nPointsPerLine = 100;
    const float lineLength = 1.0;
    Eigen::Vector3i color(0, 255, 0);
    int nPoints = m_Cameras.size() * nPointsPerLine;

    PointCloud cloud;
    cloud.reserve(nPoints);

    for (const auto& camera : m_Cameras)
    {
        Eigen::Vector3d center(camera.center());
        Eigen::Quaterniond q = camera.quaternion().inverse();
        Eigen::Vector3d dir = q * Eigen::Vector3d(0.0, 0.0, 1.0);
        dir.normalize();
        for (int i = 0; i < nPointsPerLine; i++)
        {
            Eigen::Vector3d p = center + dir * float(i) * lineLength / nPointsPerLine;
            cloud.push_back(Point(p, color));
        }
    }
    return cloud;
}

PointCloud BBoxPointsExtractor::createPointCloudWithFrustum() const
{
    const int nPointsPerLine = 50;
    Eigen::Vector3i color(255, 255, 255);
    float lineLength = 1.0;
    int nPoints = m_Cameras.size() * nPointsPerLine * 4;
    float f = m_Intrinsics.fx;
    float cx = m_Intrinsics.cx;
    float cy = m_Intrinsics.cy;

    PointCloud cloud;
    cloud.reserve(nPoints);

    for (const auto& camera : m_Cameras)
    {
        Eigen::Vector3d center(camera.center());
        Eigen::Quaterniond q = camera.quaternion().inverse();


        // topLeft
        Eigen::Vector3d topLeft = q * Eigen::Vector3d(-cx, cy, f);
        topLeft.normalize();
        for (int i = 0; i < nPointsPerLine; i++)
        {
            Eigen::Vector3d p = center + topLeft * float(i) * lineLength / nPointsPerLine;
            cloud.push_back(Point(p, color));
        }

        // topRight
        Eigen::Vector3d topRight = q * Eigen::Vector3d(cx, cy, f);
        topRight.normalize();
        for (int i = 0; i < nPointsPerLine; i++)
        {
            Eigen::Vector3d p = center + topRight * float(i) * lineLength / nPointsPerLine;
            cloud.push_back(Point(p, color));
        }

        // bottomRight
        Eigen::Vector3d bottomRight = q * Eigen::Vector3d(cx, -cy, f);
        bottomRight.normalize();
        for (int i = 0; i < nPointsPerLine; i++)
        {
            Eigen::Vector3d p = center + bottomRight * float(i) * lineLength / nPointsPerLine;
            cloud.push_back(Point(p, color));
        }

        // bottomLeft
        Eigen::Vector3d bottomLeft = q * Eigen::Vector3d(-cx, -cy, f);
        bottomLeft.normalize();
        for (int i = 0; i < nPointsPerLine; i++)
        {
            Eigen::Vector3d p = center + bottomLeft * float(i) * lineLength / nPointsPerLine;
            cloud.push_back(Point(p, color));
        }
    }
    return cloud;
}

Eigen::Vector2d BBoxPointsExtractor::convertCoordSys(const Eigen::Vector2d& v) const
{
    // TODO get the image size automatically.
    const float W = 2048.f;
    const float H = 1536.f;

    float cx = m_Intrinsics.cx;
    float cy = m_Intrinsics.cy;

    float fx = m_Intrinsics.fx;
    float fy = m_Intrinsics.fy;

//    Eigen::Vector2d converted(v[0]*2*cx/W - cx, v[1]*2*cy/H - cy);
//    Eigen::Vector2d converted((v[0] - cx)/fx, (v[1] - cy)/fy);
//    std::cout << v.transpose() << " -> " << converted.transpose() << std::endl;

    return Eigen::Vector2d(v[0]*2*cx/W -cx, -v[1]*2*cy/H +cy);
//    return Eigen::Vector2d(v[0] - cx, v[1] - cy);
}

std::array<Eigen::Vector2d, 4> BBoxPointsExtractor::convertBBX(const Detection& detection) const
{
    float x = detection.x;
    float y = detection.y;
    float w = detection.w;
    float h = detection.h;
    std::cout << "detection: " << x << ", "<< y << ", "<< w << ", "<< h << std::endl;

    // Order: top left, top right, bottom right, bottom left
    std::array<Eigen::Vector2d, 4> corners;
    corners[0] = convertCoordSys(Eigen::Vector2d(x, y));
    corners[1] = convertCoordSys(Eigen::Vector2d(x+w, y));
    corners[2] = convertCoordSys(Eigen::Vector2d(x+w, y+h));
    corners[3] = convertCoordSys(Eigen::Vector2d(x, y+h));
    std::cout << "After conversion, top left: " << corners[0].transpose() << " , bottom right: " << corners[2].transpose() << std::endl;
    return corners;
}

// This method intersects a ray going through the camera center and a point in the sensor (in pixel coordinates) with a plane
// perpendicular to the principal axis at distance f of the center.
Eigen::Vector3d BBoxPointsExtractor::project2dPointIn3dSpace(const Eigen::Vector2d& p) const
{
    float f = m_Intrinsics.fx;
    float cx = m_Intrinsics.cx;
    float cy = m_Intrinsics.cy;

//    return Eigen::Vector3d(p[0] - cx, p[1] - cy, f);
    return Eigen::Vector3d(p[0] - cx, cy - p[1], f);
}

PointCloud BBoxPointsExtractor::getPointsAssociatedWithDetectionOnFrame(int i, Detection& detection) const
{
    float f = m_Intrinsics.fx;
    float cx = m_Intrinsics.cx;
    float cy = m_Intrinsics.cy;
    const Camera& camera = m_Cameras[i];
    Eigen::Vector3d center(camera.center());
    Eigen::Quaterniond q = camera.quaternion().inverse();


    Eigen::Vector3d tl_camera = project2dPointIn3dSpace(Eigen::Vector2d(detection.x, detection.y));
    Eigen::Vector3d tr_camera = project2dPointIn3dSpace(Eigen::Vector2d(detection.x + detection.w, detection.y));
    Eigen::Vector3d br_camera = project2dPointIn3dSpace(Eigen::Vector2d(detection.x + detection.w, detection.y + detection.h));
    Eigen::Vector3d bl_camera = project2dPointIn3dSpace(Eigen::Vector2d(detection.x, detection.y + detection.h));

    Eigen::Vector3d tl = center + (q * tl_camera);
    Eigen::Vector3d tr = center + (q * tr_camera);
    Eigen::Vector3d br = center + (q * br_camera);
    Eigen::Vector3d bl = center + (q * bl_camera);

//    auto corners = convertBBX(detection);

//    Eigen::Vector3d tl = center + (q * Eigen::Vector3d(corners[0][0], corners[0][1], f)).normalized();
//    Eigen::Vector3d tr = center + (q * Eigen::Vector3d(corners[1][0], corners[1][1], f)).normalized();
//    Eigen::Vector3d br = center + (q * Eigen::Vector3d(corners[2][0], corners[2][1], f)).normalized();
//    Eigen::Vector3d bl = center + (q * Eigen::Vector3d(corners[3][0], corners[3][1], f)).normalized();


    Eigen::Vector3d leftPlaneNormal = ((tl - center).cross(bl - center)).normalized();
    float dLeftPlane = leftPlaneNormal.dot(center);

    Eigen::Vector3d topPlaneNormal = ((tr - center).cross(tl - center)).normalized();
    float dTopPlane = topPlaneNormal.dot(center);

    Eigen::Vector3d rightPlaneNormal = ((br - center).cross(tr - center)).normalized();
    float dRightPlane = rightPlaneNormal.dot(center);

    Eigen::Vector3d bottomPlaneNormal = ((bl - center).cross(br - center)).normalized();
    float dBottomPlane = bottomPlaneNormal.dot(center);

    // Now that we have the equation of the left plane of the bounding box, we filter points to get only
    // the points on the right of this plane.
    PointCloud cloud;
    for (auto& point : m_Cloud.points())
    {
        if (point.imIndex() == i
            && (point.pos()).dot(leftPlaneNormal) > dLeftPlane
//            && (point.pos()).dot(topPlaneNormal) > dTopPlane
            && (point.pos()).dot(rightPlaneNormal) > dRightPlane
//            && (point.pos()).dot(bottomPlaneNormal) > dBottomPlane
           )
        {
            cloud.push_back(point);
        }
    }
    return cloud;
}
