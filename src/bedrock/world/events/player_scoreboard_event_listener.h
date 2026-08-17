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

#include <set>
#include <unordered_map>

#include "bedrock/core/string/string_hash.h"
#include "bedrock/entity/weak_entity_ref.h"
#include "bedrock/world/events/scoreboard_event_listener.h"
#include "bedrock/world/scores/scoreboard_id.h"

class SubscribedObjectives {
private:
    WeakEntityRef player_;
    std::set<HashedString> objectives_;
};

class PlayerScoreboardEventListener : public ScoreboardEventListener {
private:
    std::unordered_map<ScoreboardId, SubscribedObjectives> subscribed_objectives_;
};
