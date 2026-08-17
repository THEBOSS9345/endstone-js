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

enum class FurnaceLayout : int {
    None = 0,
    InventoryOnly = 1,
    Default = 2,
    Count = 3,
};

enum class FurnaceLeftTabIndex : int {
    None = 0,
    RecipeFood = 1,
    RecipeItems = 2,
    RecipeBlocks = 3,
    RecipeSearch = 4,
    Inventory = 5,
    Count = 6,
};

struct FurnaceOptions {
    static constexpr FurnaceLeftTabIndex DEFAULT_LEFT_FURNACE_TAB = FurnaceLeftTabIndex::RecipeSearch;
    FurnaceOptions();
    FurnaceOptions(FurnaceLeftTabIndex, bool, FurnaceLayout);
    bool operator==(const FurnaceOptions &rhs) const;
    bool operator!=(const FurnaceOptions &rhs) const;
    FurnaceLeftTabIndex left_furnace_tab;
    bool filtering;
    FurnaceLayout layout;
};
