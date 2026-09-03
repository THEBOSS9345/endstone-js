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

// Binds include/endstone/event/weather/.

#include <endstone/event/weather/thunder_change_event.h>
#include <endstone/event/weather/weather_change_event.h>
#include <endstone/event/weather/weather_event.h>

#include "types/bind.h"

namespace endstone::node {

ESN_EVENT_BASE(WeatherEvent, Event)
{
    b.ro("level", &WeatherEvent::getLevel);
}

// The state being changed *to*, which is the only thing either event carries: without it a handler
// cannot tell rain starting from rain stopping.
ESN_EVENT(WeatherChangeEvent, WeatherEvent)
{
    b.cancellable();
    b.ro("raining", &WeatherChangeEvent::toWeatherState);
}

ESN_EVENT(ThunderChangeEvent, WeatherEvent)
{
    b.cancellable();
    b.ro("thundering", &ThunderChangeEvent::toThunderState);
}

}  // namespace endstone::node
