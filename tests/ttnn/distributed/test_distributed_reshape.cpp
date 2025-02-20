// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <cstddef>
#include <array>
#include <ttnn/core.hpp>
#include <ttnn/distributed/api.hpp>
#include "mesh_coord.hpp"
#include "tests/tt_metal/test_utils/env_vars.hpp"

namespace ttnn::distributed::test {
namespace {

using ::testing::SizeIs;

// Helper function to check test environment
void check_t3k_test_environment() {
    auto slow_dispatch = getenv("TT_METAL_SLOW_DISPATCH_MODE");
    const auto arch = tt::get_arch_from_string(tt::test_utils::get_umd_arch_name());
    const size_t num_devices = tt::tt_metal::GetNumAvailableDevices();
    if (slow_dispatch) {
        GTEST_SKIP() << "Skipping Multi-Device test suite, since it can only be run in Fast Dispatch Mode.";
    }
    if (num_devices < 8 or arch != tt::ARCH::WORMHOLE_B0) {
        GTEST_SKIP() << "Skipping T3K Multi-Device test suite on non T3K machine.";
    }
}

std::vector<chip_id_t> get_physical_device_ids(const MeshDevice& mesh) {
    std::vector<chip_id_t> device_ids;
    for (auto* device : mesh.get_devices()) {
        device_ids.push_back(device->id());
    }
    return device_ids;
}

static constexpr std::array<MeshShape, 24> kMeshShapes{
    {{1, 1}, {1, 2}, {1, 3}, {1, 4}, {1, 5}, {1, 6}, {1, 7}, {1, 8}, {2, 1}, {2, 2}, {2, 3}, {2, 4},
     {3, 1}, {3, 2}, {4, 1}, {4, 2}, {8, 1}, {7, 1}, {6, 1}, {5, 1}, {4, 1}, {3, 1}, {2, 1}, {1, 1}}};

class MeshConfigurationTest : public ::testing::TestWithParam<MeshShape> {
protected:
    void SetUp() override { check_t3k_test_environment(); }
};

TEST_P(MeshConfigurationTest, MeshConfigurations) {
    const auto& shape = GetParam();
    auto mesh = ttnn::distributed::open_mesh_device(
        {shape.num_rows, shape.num_cols},
        DEFAULT_L1_SMALL_SIZE,
        DEFAULT_TRACE_REGION_SIZE,
        1,
        tt::tt_metal::DispatchCoreType::WORKER);
    EXPECT_EQ(mesh->num_rows(), shape.num_rows);
    EXPECT_EQ(mesh->num_cols(), shape.num_cols);
    ttnn::distributed::close_mesh_device(mesh);
}

TEST_P(MeshConfigurationTest, GetPhysicalDeviceIds) {
    const auto& shape = GetParam();

    auto& system_mesh = tt::tt_metal::distributed::SystemMesh::instance();
    EXPECT_THAT(
        system_mesh.get_mapped_physical_device_ids(MeshDeviceConfig{.mesh_shape = SimpleMeshShape(shape)}),
        SizeIs(shape.num_cols * shape.num_rows));
}

// Test all possible mesh configurations on T3000
INSTANTIATE_TEST_SUITE_P(MeshShapes, MeshConfigurationTest, ::testing::ValuesIn(kMeshShapes));

class MeshReshapeTest : public ::testing::TestWithParam<std::tuple<MeshShape, MeshShape>> {
protected:
    void SetUp() override { check_t3k_test_environment(); }
};

TEST_P(MeshReshapeTest, ReshapeBetweenConfigurations) {
    const auto& [old_shape, new_shape] = GetParam();

    if ((old_shape.num_rows * old_shape.num_cols) != (new_shape.num_rows * new_shape.num_cols)) {
        GTEST_SKIP() << "Device counts don't match; we test this in InvalidReshapeDimensions";
    }
    if (old_shape.num_rows == 1 or old_shape.num_cols == 1) {
        GTEST_SKIP() << "Old shape is 1xN or Nx1; we test this in From1x4To2x2Invalid";
    }

    auto mesh = ttnn::distributed::open_mesh_device(
        {old_shape.num_rows, old_shape.num_cols},
        DEFAULT_L1_SMALL_SIZE,
        DEFAULT_TRACE_REGION_SIZE,
        1,
        tt::tt_metal::DispatchCoreType::WORKER);

    EXPECT_EQ(mesh->num_rows(), old_shape.num_rows);
    EXPECT_EQ(mesh->num_cols(), old_shape.num_cols);

    auto original_order = mesh->get_device_ids();

    // Attempt reshape
    mesh->reshape({new_shape.num_rows, new_shape.num_cols});

    // Verify new shape
    EXPECT_EQ(mesh->num_rows(), new_shape.num_rows);
    EXPECT_EQ(mesh->num_cols(), new_shape.num_cols);

    // Verify device ordering is preserved
    EXPECT_EQ(mesh->get_device_ids(), original_order);
}

// Generate all possible combinations of shapes from kMeshShapes
INSTANTIATE_TEST_SUITE_P(
    ReshapeConfigurations,
    MeshReshapeTest,
    ::testing::Combine(::testing::ValuesIn(kMeshShapes), ::testing::ValuesIn(kMeshShapes)));

// Base class for non-parameterized tests
class T3000ReshapeTest : public ::testing::Test {
protected:
    void SetUp() override { check_t3k_test_environment(); }
};

TEST_F(T3000ReshapeTest, InvalidRequestedShape) {
    auto& system_mesh = tt::tt_metal::distributed::SystemMesh::instance();

    // Shape too big.
    EXPECT_ANY_THROW(system_mesh.get_mapped_physical_device_ids(MeshDeviceConfig{.mesh_shape = SimpleMeshShape(9)}));
    EXPECT_ANY_THROW(system_mesh.get_mapped_physical_device_ids(MeshDeviceConfig{.mesh_shape = SimpleMeshShape(2, 5)}));

    // Invalid offset.
    EXPECT_ANY_THROW(system_mesh.get_mapped_physical_device_ids(
        MeshDeviceConfig{.mesh_shape = SimpleMeshShape(1, 8), .offset = MeshCoordinate(0, 1)}));
    EXPECT_ANY_THROW(system_mesh.get_mapped_physical_device_ids(
        MeshDeviceConfig{.mesh_shape = SimpleMeshShape(2, 3), .offset = MeshCoordinate(1, 1)}));

    // Offset dimensionality mismatch.
    EXPECT_ANY_THROW(system_mesh.get_mapped_physical_device_ids(
        MeshDeviceConfig{.mesh_shape = SimpleMeshShape(2, 3), .offset = MeshCoordinate(1)}));

    // Mismatch system mesh shape.
    EXPECT_ANY_THROW(system_mesh.get_mapped_physical_device_ids(
        MeshDeviceConfig{.mesh_shape = SimpleMeshShape(8), .offset = MeshCoordinate(1)}));
}

TEST_F(T3000ReshapeTest, InvalidReshapeDimensions) {
    auto mesh = ttnn::distributed::open_mesh_device(
        {1, 8}, DEFAULT_L1_SMALL_SIZE, DEFAULT_TRACE_REGION_SIZE, 1, tt::tt_metal::DispatchCoreType::WORKER);

    // Test reshaping to dimensions that don't match total device count
    EXPECT_THROW(mesh->reshape({3, 3}), std::runtime_error);  // 9 devices != 8
    EXPECT_THROW(mesh->reshape({1, 9}), std::runtime_error);  // 9 devices != 8

    // Verify original shape is preserved after failed reshapes
    EXPECT_EQ(mesh->num_rows(), 1);
    EXPECT_EQ(mesh->num_cols(), 8);
}

TEST_F(T3000ReshapeTest, From1x8To2x4ThenBackTo1x8) {
    auto mesh = ttnn::distributed::open_mesh_device(
        {1, 8}, DEFAULT_L1_SMALL_SIZE, DEFAULT_TRACE_REGION_SIZE, 1, tt::tt_metal::DispatchCoreType::WORKER);

    EXPECT_EQ(mesh->num_rows(), 1);
    EXPECT_EQ(mesh->num_cols(), 8);
    auto original_order = mesh->get_device_ids();

    mesh->reshape({2, 4});

    EXPECT_EQ(mesh->num_rows(), 2);
    EXPECT_EQ(mesh->num_cols(), 4);
    std::vector<chip_id_t> expected_physical_device_id_order = {
        original_order[0],
        original_order[1],
        original_order[2],
        original_order[3],
        original_order[7],
        original_order[6],
        original_order[5],
        original_order[4],
    };

    auto new_order = mesh->get_device_ids();
    EXPECT_EQ(new_order, expected_physical_device_id_order);

    mesh->reshape({1, 8});
    EXPECT_EQ(mesh->get_device_ids(), original_order);
}

TEST_F(T3000ReshapeTest, InvalidTotalDeviceCount) {
    auto mesh = ttnn::distributed::open_mesh_device(
        {1, 8}, DEFAULT_L1_SMALL_SIZE, DEFAULT_TRACE_REGION_SIZE, 1, tt::tt_metal::DispatchCoreType::WORKER);

    // Test reshaping to dimensions that don't match total device count
    EXPECT_THROW(mesh->reshape({3, 3}), std::runtime_error);  // 9 devices != 8
    EXPECT_THROW(mesh->reshape({1, 9}), std::runtime_error);  // 9 devices != 8

    // Verify original shape is preserved after failed reshapes
    EXPECT_EQ(mesh->num_rows(), 1);
    EXPECT_EQ(mesh->num_cols(), 8);
}

TEST_F(T3000ReshapeTest, RingPreservation) {
    auto mesh = ttnn::distributed::open_mesh_device(
        {1, 8}, DEFAULT_L1_SMALL_SIZE, DEFAULT_TRACE_REGION_SIZE, 1, tt::tt_metal::DispatchCoreType::WORKER);

    // Store original device positions
    std::vector<chip_id_t> original_layout;
    for (size_t i = 0; i < mesh->num_rows(); ++i) {
        for (size_t j = 0; j < mesh->num_cols(); ++j) {
            original_layout.push_back(mesh->get_device(i, j)->id());
        }
    }

    mesh->reshape({2, 4});

    // Verify devices are still connected in a Ring topology
    std::vector<chip_id_t> new_layout;
    for (size_t i = 0; i < mesh->num_rows(); ++i) {
        for (size_t j = 0; j < mesh->num_cols(); ++j) {
            new_layout.push_back(mesh->get_device(i, j)->id());
        }
    }
    EXPECT_EQ(new_layout, original_layout);
}

TEST_F(T3000ReshapeTest, From1x4To2x2Invalid) {
    auto mesh = ttnn::distributed::open_mesh_device(
        {1, 4}, DEFAULT_L1_SMALL_SIZE, DEFAULT_TRACE_REGION_SIZE, 1, tt::tt_metal::DispatchCoreType::WORKER);

    // This is an invalid reshape because the 1x4 mesh does not fully cover the 2x2 mesh
    EXPECT_THROW(mesh->reshape({2, 2}), std::runtime_error);
}

TEST_F(T3000ReshapeTest, From1x4To2x2Valid) {
    auto& system_mesh = tt::tt_metal::distributed::SystemMesh::instance();

    // Fetch the device ids for a physically connected 2x2 mesh.
    auto physical_device_ids = system_mesh.get_mapped_physical_device_ids(MeshDeviceConfig{
        .mesh_shape = SimpleMeshShape(2, 2),
    });

    // Supply the physical device ids to the mesh constructor that we know we know is 2x2 physically connected.
    // We will create a 1x4 mesh and then reshape it to 2x2.
    auto mesh = ttnn::distributed::open_mesh_device(
        {1, 4},
        DEFAULT_L1_SMALL_SIZE,
        DEFAULT_TRACE_REGION_SIZE,
        1,
        tt::tt_metal::DispatchCoreType::WORKER,
        MeshOffset{0, 0},
        physical_device_ids);

    mesh->reshape({2, 2});
    EXPECT_EQ(mesh->num_rows(), 2);
    EXPECT_EQ(mesh->num_cols(), 2);
    auto new_layout = mesh->get_device_ids();
    for (auto physical_device_id : physical_device_ids) {
        EXPECT_TRUE(std::find(new_layout.begin(), new_layout.end(), physical_device_id) != new_layout.end());
    }
}

TEST_F(T3000ReshapeTest, From2x2To1x4) {
    auto mesh = ttnn::distributed::open_mesh_device(
        {2, 2}, DEFAULT_L1_SMALL_SIZE, DEFAULT_TRACE_REGION_SIZE, 1, tt::tt_metal::DispatchCoreType::WORKER);

    auto mesh_2x2_device_ids = mesh->get_device_ids();

    mesh->reshape({1, 4});
    EXPECT_EQ(mesh->num_rows(), 1);
    EXPECT_EQ(mesh->num_cols(), 4);

    auto mesh_1x4_device_ids = mesh->get_device_ids();
    std::vector<chip_id_t> expected_1x4_device_ids = {
        mesh_2x2_device_ids[0],
        mesh_2x2_device_ids[1],
        mesh_2x2_device_ids[3],
        mesh_2x2_device_ids[2],
    };

    EXPECT_EQ(mesh_1x4_device_ids, expected_1x4_device_ids);
}

}  // namespace
}  // namespace ttnn::distributed::test
