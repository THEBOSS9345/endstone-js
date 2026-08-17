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
#include <memory>
#include <string>

#include <gsl/gsl>

#include "bedrock/core/file/path.h"
#include "bedrock/core/threading/lockbox.h"
#include "bedrock/forward.h"
#include "bedrock/platform/brstd/function_ref.h"
#include "bedrock/resources/preload_cache.h"

enum class AssetEncryptionMode : std::uint8_t {
    CFB = 0,
    CTR = 1,
};

struct EncryptedAssetInfo {
    std::string key;
    AssetEncryptionMode mode;
};

enum class PackAccessState : std::uint8_t {
    Unknown = 0,
    Valid = 1,
    MissingContents = 2,
    InvalidContents = 3,
    MissingContentKey = 4,
};

class PackAssetSet {
public:
    virtual ~PackAssetSet();
    virtual void addAsset(Core::Path, EncryptedAssetInfo) = 0;
    virtual void clear() = 0;
    [[nodiscard]] virtual bool hasAsset(const Core::Path &, bool) const = 0;
    [[nodiscard]] virtual const EncryptedAssetInfo *getEncryptedAssetInfo(const Core::Path &, bool) const = 0;
    virtual void forEachAsset(brstd::function_ref<void(const Core::Path &)>) const = 0;
    [[nodiscard]] virtual bool isEmpty() const = 0;

private:
    PackAccessState access_state_;
};

class PackAccessStrategy {
public:
    virtual ~PackAccessStrategy() = 0;

protected:
    std::shared_ptr<Core::FileHandlePool> handle_pool_;
    gsl::not_null<std::shared_ptr<Bedrock::Resources::PreloadCache>> preloaded_;

private:
    Bedrock::Threading::SharedLockbox<gsl::not_null<std::unique_ptr<PackAssetSet>>> asset_set_;
};
