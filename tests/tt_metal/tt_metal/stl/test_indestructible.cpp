// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "tt_metal/tt_stl/indestructible.hpp"

namespace tt::stl {
namespace {

TEST(IndestructibleTest, Basic) {
    struct DangerouslyDestructible {
        ~DangerouslyDestructible() {
            // Wrapping in a lambda, as `FAIL()` returns `void`.
            []() { FAIL(); }();
        }
    };

    Indestructible<DangerouslyDestructible> obj;
}

}  // namespace
}  // namespace tt::stl
