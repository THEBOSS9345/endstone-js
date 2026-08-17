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

#include "bedrock/network/packet.h"
#include "bedrock/network/packet/cerealize/core/serialization_mode.h"
#include "bedrock/network/packet/types/world/actor/serialized_abilities_data.h"

struct UpdateAbilitiesPacketPayload {
    SerializedAbilitiesData data;  // +0
};
BEDROCK_STATIC_ASSERT_SIZE(UpdateAbilitiesPacketPayload, 40, 40);

class UpdateAbilitiesPacket : public Packet {
public:
    UpdateAbilitiesPacketPayload payload;                                               // +48
    SerializationMode serialization_mode{SerializationMode::SideBySide_LogOnMismatch};  // +88
};
BEDROCK_STATIC_ASSERT_SIZE(UpdateAbilitiesPacket, 96, 96);
