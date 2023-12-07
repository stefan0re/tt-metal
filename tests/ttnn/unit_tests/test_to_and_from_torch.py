# SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

import pytest

import torch

import ttnn


@pytest.mark.parametrize("h", [7])
@pytest.mark.parametrize("w", [3])
def test_to_and_from_4D(h, w):
    torch_input = torch.rand((h, w), dtype=torch.bfloat16)
    tt_output = ttnn.from_torch(torch_input)
    torch_output = ttnn.to_torch(tt_output)
    assert torch.allclose(torch_output, torch_input)


@pytest.mark.parametrize("h", [7])
@pytest.mark.parametrize("w", [3])
def test_to_and_from_2D(h, w):
    torch_input = torch.rand((h, w), dtype=torch.bfloat16)
    tt_output = ttnn.from_torch(torch_input)
    torch_output = ttnn.to_torch(tt_output)
    assert torch.allclose(torch_output, torch_input)
