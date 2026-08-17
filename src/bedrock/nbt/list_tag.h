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
#include <vector>

#include "bedrock/bedrock.h"
#include "bedrock/nbt/tag.h"
#include "bedrock/platform/brstd/function_ref.h"

class CompoundTag;

class ListTag : public Tag {
    using List = std::vector<std::unique_ptr<Tag>>;

public:
    ListTag() = default;
    void deleteChildren() override;
    void write(IDataOutput &output) const override;
    void write(IDataOutput &output, int depth) const;
    Bedrock::Result<void> load(IDataInput &input) override;
    Bedrock::Result<void> load(IDataInput &input, int depth);
    [[nodiscard]] std::string toString() const override;
    [[nodiscard]] Type getId() const override;
    [[nodiscard]] bool equals(const Tag &other) const override;
    [[nodiscard]] std::unique_ptr<Tag> copy() const override;
    [[nodiscard]] std::size_t hash() const override;
    void print(const std::string &string, PrintStream &stream) const override;

    [[nodiscard]] Type getType() const;
    void add(std::unique_ptr<Tag> tag);
    [[nodiscard]] Tag *get(int index) const;
    [[nodiscard]] float getFloat(int index) const;
    [[nodiscard]] int getInt(int index) const;
    [[nodiscard]] double getDouble(int index) const;
    [[nodiscard]] const std::string &getString(int index) const;
    [[nodiscard]] std::int64_t getInt64(int index) const;
    [[nodiscard]] std::int16_t getShort(int index) const;
    [[nodiscard]] std::uint8_t getByte(int index) const;
    [[nodiscard]] int size() const;
    [[nodiscard]] std::unique_ptr<ListTag> copyList() const;
    [[nodiscard]] const CompoundTag *getCompound(int index) const;
    CompoundTag *getCompound(int index);
    void erase(int index);
    void popBack();
    void forEachCompoundTag(brstd::function_ref<void(const CompoundTag &)> func) const;

private:
    List list_;  // +8
    Type type_;  // +32
};
BEDROCK_STATIC_ASSERT_SIZE(ListTag, 40, 40);
