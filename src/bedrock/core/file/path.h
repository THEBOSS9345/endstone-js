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

#include <string>
#include <string_view>
#include <utility>

#include <gsl/gsl>

namespace Core {

template <typename T>
class PathBuffer {
public:
    PathBuffer() = default;
    explicit PathBuffer(T const &container) : container_(container) {}
    explicit PathBuffer(T &&container) : container_(std::move(container)) {}
    explicit PathBuffer(const char *c_str) : container_(c_str) {}
    explicit PathBuffer(const char *c_str, std::size_t size) : container_(c_str, size) {}

    [[nodiscard]] char const *getUtf8CString() const { return container_.c_str(); }
    [[nodiscard]] std::string_view getUtf8StringView() const { return container_; }
    [[nodiscard]] std::string const &getUtf8StdString() const { return container_; }
    [[nodiscard]] std::size_t size() const { return container_.size(); }
    [[nodiscard]] bool empty() const { return container_.empty(); }
    T const &getContainer() const { return container_; };

private:
    T container_;
};

using HeapPathBuffer = PathBuffer<std::string>;

class Path : public HeapPathBuffer {
public:
    using HeapPathBuffer::HeapPathBuffer;

    static Path const EMPTY;
};

}  // namespace Core
