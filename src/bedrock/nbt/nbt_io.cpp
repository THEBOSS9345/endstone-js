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

#include "bedrock/nbt/nbt_io.h"

#include "bedrock/nbt/compound_tag.h"
#include "bedrock/nbt/list_tag.h"

Bedrock::Result<void> NbtIo::loadTagPayload(Tag &tag, IDataInput &dis, int depth)
{
    if (depth > MAX_NESTING_DEPTH) {
        return BEDROCK_NEW_ERROR(std::errc::bad_message);
    }
    switch (tag.getId()) {
    case Tag::Type::Compound:
        return static_cast<CompoundTag &>(tag).load(dis, depth);
    case Tag::Type::List:
        return static_cast<ListTag &>(tag).load(dis, depth);
    default:
        return tag.load(dis);
    }
}

Bedrock::Result<std::unique_ptr<Tag>> NbtIo::readNamedTag(IDataInput &dis, std::string &name, int depth)
{
    auto type_result = dis.readByteResult();
    if (!type_result.ignoreError()) {
        return BEDROCK_RETHROW(type_result);
    }

    auto type = static_cast<Tag::Type>(type_result.discardError().value());
    if (type == Tag::Type::End) {
        return std::make_unique<EndTag>();
    }
    if (type >= Tag::Type::NumTagTypes) {
        return BEDROCK_NEW_ERROR(std::errc::bad_message);
    }

    auto name_result = dis.readStringResult();
    if (!name_result.ignoreError()) {
        return BEDROCK_RETHROW(name_result);
    }
    name = name_result.discardError().value();

    auto tag_result = Tag::newTag(type);
    if (!tag_result.ignoreError()) {
        return BEDROCK_RETHROW(tag_result);
    }

    auto tag = std::move(tag_result.discardError().value());
    auto payload_result = loadTagPayload(*tag, dis, depth);
    if (!payload_result.ignoreError()) {
        return BEDROCK_RETHROW(payload_result);
    }
    return std::move(tag);
}

Bedrock::Result<std::unique_ptr<CompoundTag>> NbtIo::readNamedCompoundTag(IDataInput &dis, std::string &name)
{
    auto type_result = dis.readByteResult();
    if (!type_result.ignoreError()) {
        return BEDROCK_RETHROW(type_result);
    }

    auto type = static_cast<Tag::Type>(type_result.discardError().value());
    if (type != Tag::Type::Compound) {
        return BEDROCK_NEW_ERROR(std::errc::bad_message);
    }

    auto name_result = dis.readStringResult();
    if (!name_result.ignoreError()) {
        return BEDROCK_RETHROW(name_result);
    }
    name = name_result.discardError().value();

    auto tag = std::make_unique<CompoundTag>();
    auto load_result = tag->load(dis, 0);
    if (!load_result.ignoreError()) {
        return BEDROCK_RETHROW(load_result);
    }
    return std::move(tag);
}

void NbtIo::writeNamedTag(const std::string &name, const Tag &tag, IDataOutput &output)
{
    writeNamedTag(name, tag, output, 0);
}

void NbtIo::writeTagPayload(const Tag &tag, IDataOutput &output, int depth)
{
    switch (tag.getId()) {
    case Tag::Type::Compound:
        static_cast<const CompoundTag &>(tag).write(output, depth);
        break;
    case Tag::Type::List:
        static_cast<const ListTag &>(tag).write(output, depth);
        break;
    default:
        tag.write(output);
        break;
    }
}

void NbtIo::writeNamedTag(const std::string &name, const Tag &tag, IDataOutput &output, int depth)
{
    auto type = tag.getId();
    output.writeByte(static_cast<std::uint8_t>(type));
    if (type != Tag::Type::End) {
        output.writeString(name);
        writeTagPayload(tag, output, depth);
    }
}

std::unique_ptr<CompoundTag> NbtIo::readOrGetEmpty(IDataInput &dis)
{
    std::string name;
    auto result = readNamedCompoundTag(dis, name);
    if (!result.logError()) {
        return std::make_unique<CompoundTag>();
    }
    return std::move(result.discardError().value());
}
