// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "ttnn/tensor/storage.hpp"

namespace tt::tt_metal {

std::vector<std::shared_ptr<Buffer>> MultiDeviceStorage::get_buffers() const {
    std::lock_guard<std::mutex> lock(buffer_mtx);
    std::vector<std::shared_ptr<Buffer>> buf_vec;
    buf_vec.reserve(buffers.size());
    for (const auto& pair : buffers) {
        buf_vec.push_back(pair.second);
    }
    return buf_vec;
}

MultiDeviceStorage::MultiDeviceStorage(
    const std::shared_ptr<distributed::MeshBuffer>& mesh_buffer_, const TensorSpec& tensor_spec) :
    strategy(ReplicateTensor{}),
    mesh_buffer(mesh_buffer_)  //
{
    // TODO: #17215 - In the long term, this code won't exist: no interactions will be made with individual Buffers, and
    // instead the APIs will use MeshBuffer directly. MeshBuffer will also guarantee that all shards have the same
    // tensor spec.
    //
    // For now, this code ensures MeshBuffer backed tensors are compatible with the rest of the ops infra.
    const auto [num_rows, num_cols] = mesh_buffer->device()->shape();

    ordered_device_ids.reserve(num_rows * num_cols);
    buffers.reserve(num_rows * num_cols);
    specs.reserve(num_rows * num_cols);

    for (int row = 0; row < num_rows; ++row) {
        for (int col = 0; col < num_cols; ++col) {
            auto buffer = mesh_buffer->get_device_buffer(distributed::MeshCoordinate(row, col));
            const int device_id = buffer->device()->id();
            ordered_device_ids.push_back(device_id);
            buffers.emplace(device_id, std::move(buffer));
            specs.emplace(device_id, tensor_spec);
        }
    }
}

}  // namespace tt::tt_metal
