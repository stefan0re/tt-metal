# SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

import torch
import pytest
import ttnn
from tests.ttnn.unit_tests.operations.backward.utility_funcs import data_gen_with_range, compare_pcc


@pytest.mark.parametrize(
    "input_shapes",
    (
        (torch.Size([1, 1, 32, 32])),
        (torch.Size([1, 1, 320, 384])),
        (torch.Size([1, 3, 320, 384])),
    ),
)
@pytest.mark.parametrize("value", [0.05, 1.0, 0.5, 0.12])
def test_bw_addcmul(input_shapes, value, device):
    in_data, input_tensor = data_gen_with_range(input_shapes, -100, 100, device, True)
    tensor1_data, tensor1_tensor = data_gen_with_range(input_shapes, -100, 100, device, True)
    tensor2_data, tensor2_tensor = data_gen_with_range(input_shapes, -100, 100, device, True)
    grad_data, grad_tensor = data_gen_with_range(input_shapes, -100, 100, device)

    tt_output_tensor_on_device = ttnn.addcmul_bw(grad_tensor, input_tensor, tensor1_tensor, tensor2_tensor, value)

    in_data.retain_grad()
    tensor1_data.retain_grad()
    tensor2_data.retain_grad()

    pyt_y = torch.addcmul(in_data, tensor1_data, tensor2_data, value=value)

    pyt_y.backward(gradient=grad_data)

    golden_tensor = [in_data.grad, tensor1_data.grad, tensor2_data.grad]

    status = compare_pcc(tt_output_tensor_on_device, golden_tensor)
    assert status