#include "gtcal/pose_solver.h"
#include "gtcal_test_utils.h"
#include "gtcal/camera.h"
#include <gtsam/geometry/PinholeCamera.h>
#include <gtest/gtest.h>

#include <algorithm>

struct PoseSolverFixture : public testing::Test {
protected:
  // Target grid point parameters.
  const double grid_spacing = 0.3;
  const size_t num_rows = 10;
  const size_t num_cols = 13;
  const size_t num_target_pts = num_rows * num_cols;

  // Create target object and define the target x and y center.
  const gtcal::utils::CalibrationTarget target{grid_spacing, num_rows, num_cols};
  double target_center_x = -1.0;
  double target_center_y = -1.0;

  // Camera model.
  std::shared_ptr<gtsam::Cal3Fisheye> K = nullptr;

  void SetUp() override {
    // Set the camera calibration.
    K = std::make_shared<gtsam::Cal3Fisheye>(gtsam::Cal3Fisheye(FX, FY, 0., CX, CY, 0., 0., 0., 0.));

    // Get the target center.
    const gtsam::Point3 target_center_pt3d = target.get3dCenter();
    target_center_x = target_center_pt3d.x();
    target_center_y = target_center_pt3d.y();
  }
};

// Tests that the solver is able to find a solution in the case of translation only.
TEST_F(PoseSolverFixture, SinglePoseTranslationOnly) {
  // First camera pose in target frame.
  const gtsam::Rot3 R0_target_cam = gtsam::Rot3::RzRyRx(0., 0., 0.);
  const gtsam::Point3 xyz0_target_cam = gtsam::Point3(target_center_x, target_center_y, -0.85);
  const gtsam::Pose3 pose0_target_cam = gtsam::Pose3(R0_target_cam, xyz0_target_cam);

  // Second camera pose in target frame.
  const gtsam::Rot3 R1_target_cam = gtsam::Rot3::RzRyRx(0., 0., 0.);
  const gtsam::Point3 xyz1_target_cam = gtsam::Point3(target_center_x, target_center_y, -0.825);
  const gtsam::Pose3 pose1_target_cam = gtsam::Pose3(R1_target_cam, xyz1_target_cam);

  // Create camera model.
  std::shared_ptr<gtcal::Camera> camera = std::make_shared<gtcal::Camera>();
  camera->setCameraModel<gtsam::Cal3Fisheye>(IMAGE_WIDTH, IMAGE_HEIGHT, *K, pose1_target_cam);

  // Get target point measurements at second pose.
  std::vector<gtcal::Measurement> measurements;
  measurements.reserve(target.grid_pts3d_target.size());
  for (size_t ii = 0; ii < target.grid_pts3d_target.size(); ii++) {
    const gtsam::Point3& pt3d_target = target.grid_pts3d_target.at(ii);
    const gtsam::Point2 uv = camera->project(pt3d_target);
    if (gtcal::utils::FilterPixelCoords(uv, camera->width(), camera->height())) {
      measurements.push_back(gtcal::Measurement(uv, 0, ii));
    }
  }

  // Check that the number of measurements is equal to the number of 3D points (should be for this case).
  EXPECT_EQ(measurements.size(), target.grid_pts3d_target.size());

  // Create pose solver problem.
  gtcal::PoseSolver pose_solver(true);
  gtsam::Pose3 pose_target_cam_init =
      gtsam::Pose3(gtsam::Rot3::RzRyRx(0.001, -0.0002, 0.01),
                   gtsam::Point3(target_center_x - 0.002, target_center_y + 0.0, -0.81));
  const bool success =
      pose_solver.solve(measurements, target.grid_pts3d_target, camera, pose_target_cam_init);
  EXPECT_TRUE(success) << "Pose solver failed.";

  // Check the estimated solution.
  const gtsam::Point3 solved_xyz_target_cam = pose_target_cam_init.translation();
  const gtsam::Point3 solved_pqr_target_cam = pose_target_cam_init.rotation().rpy();
  const gtsam::Point3 rpy1_target_cam = R1_target_cam.rpy();
  EXPECT_NEAR((solved_xyz_target_cam - xyz1_target_cam).norm(), 0.0, 1e-8);
  EXPECT_NEAR((solved_pqr_target_cam - rpy1_target_cam).norm(), 0.0, 1e-8);
}

// Tests that the solver is able to find a solution in the case of translation and rotation using the first
// two poses from the synthetic pose set.
TEST_F(PoseSolverFixture, FirstAndSecondPoses) {
  const gtsam::Point3 initial_offset = {target_center_x, target_center_y, -0.75};

  // Get synthetic poses around target.
  const auto poses_target_cam =
      gtcal::utils::GeneratePosesAroundTarget(target, -3.0, -target_center_y / 2, initial_offset);
  EXPECT_EQ(poses_target_cam.size(), 10);

  // Get the first two poses and build the camera model.
  const gtsam::Pose3& pose0_target_cam = poses_target_cam.at(0);
  const gtsam::Pose3& pose1_target_cam = poses_target_cam.at(1);

  // Create camera model.
  std::shared_ptr<gtcal::Camera> camera = std::make_shared<gtcal::Camera>();
  camera->setCameraModel<gtsam::Cal3Fisheye>(IMAGE_WIDTH, IMAGE_HEIGHT, *K, pose1_target_cam);

  // Get target point measurements at second pose.
  std::vector<gtcal::Measurement> measurements;
  measurements.reserve(target.grid_pts3d_target.size());
  for (size_t ii = 0; ii < target.grid_pts3d_target.size(); ii++) {
    const gtsam::Point3& pt3d_target = target.grid_pts3d_target.at(ii);
    const gtsam::Point2 uv = camera->project(pt3d_target);
    if (gtcal::utils::FilterPixelCoords(uv, camera->width(), camera->height())) {
      measurements.push_back(gtcal::Measurement(uv, 0, ii));
    }
  }

  // Check that the number of measurements is equal to the number of 3D points (should be for this case).
  EXPECT_EQ(measurements.size(), target.grid_pts3d_target.size());

  // Create pose solver problem.
  gtcal::PoseSolver pose_solver(false);
  gtsam::Pose3 pose_target_cam_init = pose0_target_cam;
  const bool success =
      pose_solver.solve(measurements, target.grid_pts3d_target, camera, pose_target_cam_init);
  EXPECT_TRUE(success) << "Pose solver failed.";

  // Check estimated solution.
  EXPECT_TRUE(pose1_target_cam.equals(pose_target_cam_init, 1e-7));
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
