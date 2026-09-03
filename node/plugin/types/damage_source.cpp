// Copyright (c) 2026 THEBOSS9345 (https://github.com/THEBOSS9345/endstone-js)
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

// Binds include/endstone/damage/damage_source.h.

#include <endstone/damage/damage_source.h>

#include "types/bind.h"

namespace endstone::node {

ESN_TYPE(DamageSource, DamageSource)
{
    // The cause, e.g. "entity_attack", "fall", "lava".
    b.ro("type", &DamageSource::getType);
    // True when the responsible actor is not the one that struck, e.g. a shooter and their arrow.
    b.ro("isIndirect", &DamageSource::isIndirect);
    // getActor() is who is answerable; getDamagingActor() is what actually struck.
    b.ro("actor", &DamageSource::getActor);
    b.ro("damagingActor", &DamageSource::getDamagingActor);
}

}  // namespace endstone::node
