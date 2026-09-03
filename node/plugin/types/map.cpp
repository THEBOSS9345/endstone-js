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

// Binds include/endstone/map/.

#include <endstone/map/map_canvas.h>
#include <endstone/map/map_view.h>
#include <endstone/util/color.h>

#include "types/bind.h"

namespace endstone::node {

ESN_TYPE(MapView, MapView)
{
    b.ro("id", &MapView::getId);
    // True when a plugin supplied the lowermost renderer, i.e. the map is not a world map.
    b.ro("isVirtual", &MapView::isVirtual);
    b.rw("isLocked", &MapView::isLocked, &MapView::setLocked);
    b.rw("unlimitedTracking", &MapView::isUnlimitedTracking, &MapView::setUnlimitedTracking);
    b.rw("centerX", &MapView::getCenterX, &MapView::setCenterX);
    b.rw("centerZ", &MapView::getCenterZ, &MapView::setCenterZ);
    b.ro("dimension", &MapView::getDimension);
    // 0 (closest) to 4 (furthest); anything else would be an out-of-range enum value.
    b.rw("scale", [](const MapView &map) { return static_cast<std::int64_t>(map.getScale()); },
         [](MapView &map, const Value &in, Binder &) {
             if (in.integer < 0 || in.integer > 4) {
                 return static_cast<esn_status>(ESN_ERR_BAD_ARGUMENT);
             }
             map.setScale(static_cast<MapView::Scale>(in.integer));
             return static_cast<esn_status>(ESN_OK);
         });
    // addRenderer has its own ABI entry point, because the renderer is a JavaScript function rather
    // than a value - see ApiBridge::addMapRenderer.
}

ESN_TYPE(MapCanvas, MapCanvas)
{
    // A whole 128x128 RGBA frame in one crossing, row-major. Setting pixels one at a time would cross
    // the ABI 16384 times for every draw, for every viewer.
    b.bytes("pixels", [](const MapCanvas &) { return std::string{}; },
            [](MapCanvas &canvas, const std::string &frame) {
                constexpr int kMapSize = 128;
                const auto expected = static_cast<std::size_t>(kMapSize) * kMapSize * 4;
                if (frame.size() < expected) {
                    return;
                }
                const auto *bytes = reinterpret_cast<const unsigned char *>(frame.data());
                for (int y = 0; y < kMapSize; ++y) {
                    for (int x = 0; x < kMapSize; ++x) {
                        const auto at = (static_cast<std::size_t>(y) * kMapSize + x) * 4;
                        canvas.setPixelColor(x, y,
                                             Color{bytes[at], bytes[at + 1], bytes[at + 2], bytes[at + 3]});
                    }
                }
            });
    // For a renderer that touches a handful of pixels. Anything drawing a whole frame should assign
    // canvas.pixels instead.
    b.method("setPixel", [](MapCanvas &canvas, const Args &args, esn_handle *) {
        canvas.setPixelColor(static_cast<int>(args.number(0)), static_cast<int>(args.number(1)),
                             Color{static_cast<std::uint8_t>(args.number(2)),
                                   static_cast<std::uint8_t>(args.number(3)),
                                   static_cast<std::uint8_t>(args.number(4)),
                                   static_cast<std::uint8_t>(args.number(5, 255))});
        return static_cast<esn_status>(ESN_OK);
    });
}

}  // namespace endstone::node
