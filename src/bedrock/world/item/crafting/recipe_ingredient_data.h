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

#include "bedrock/bedrock.h"
#include "bedrock/world/item/item_descriptor.h"

// The cereal serialized form of RecipeIngredient - it carries its own aux value and stack size
// instead of the uint16_t count that ItemDescriptorCount holds.
struct RecipeIngredientData {
    ItemDescriptor descriptor;  // +0
    std::int32_t aux_value;     // +16
    std::int32_t stack_size;    // +20
};
BEDROCK_STATIC_ASSERT_SIZE(RecipeIngredientData, 24, 24);
