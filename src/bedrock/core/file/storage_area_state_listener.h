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

#include <functional>
#include <memory>

#include "bedrock/core/file/file_types.h"
#include "bedrock/core/file/level_storage_state.h"
#include "bedrock/forward.h"
#include "bedrock/platform/threading/mutex_details.h"

namespace Core {
class StorageAreaStateListener {
public:
    virtual ~StorageAreaStateListener() = 0;
    virtual void onExtendDiskSpace(bool, std::weak_ptr<FileStorageArea> const &, FileSize, std::function<void()>) = 0;
    virtual void onLowDiskSpace(bool, FileSize) = 0;
    virtual void onOutOfDiskSpace(bool, FileSize) = 0;
    virtual void onCriticalDiskError(bool, LevelStorageState const &) = 0;

private:
    std::shared_ptr<FileStorageArea> file_storage_area_;  // +8
    Bedrock::Threading::Mutex mutex_;                     // +24
};
}  // namespace Core
