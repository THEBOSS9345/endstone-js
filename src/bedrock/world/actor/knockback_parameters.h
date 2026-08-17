// Copyright (c) 2024, The Endstone Project. (https://endstone.dev) All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstdint>

#include "bedrock/core/math/vec2.h"

enum class ExtraKnockbackApproach : std::uint8_t {
    ReapplyDefault = 0,
    MultiplyReduced = 1,
};

struct KnockbackParameters {
    Vec2 power;
    float vertical_velocity_cap;
    float slowdown_scale;
    bool scale_with_damage;
    bool slow_down_attacker;
    bool check_legacy_pre_nether_update_knockback;
    float extra_knockback_power;
    ExtraKnockbackApproach extra_knockback_approach;
};
BEDROCK_STATIC_ASSERT_SIZE(KnockbackParameters, 28, 28);
