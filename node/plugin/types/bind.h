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

#pragma once

#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include <endstone/util/result.h>

#include "types/descriptor.h"

/**
 * @file
 * @brief Declaring what a type exposes, one line per member.
 *
 * A binding names a member and hands over a pointer to the Endstone method behind it. Everything
 * else - which ABI accessor answers it, whether the result is a handle, whether the bridge has to
 * take ownership of what came back - is deduced from that method's own signature. There is no
 * accessor kind to choose and therefore none to get wrong, and a signature change upstream becomes a
 * compile error here rather than a member that silently stops working.
 */

namespace endstone::node {

/**
 * @brief Maps an Endstone class to the handle kind it is tracked under.
 *
 * `by_value` marks the ones Endstone returns by value rather than by reference, which the bridge has
 * to copy somewhere that outlives the call. `persistent` marks the ones that outlive a dispatch, so
 * their handles survive scope unwinding.
 */
template <typename T>
struct KindOf;

#define ESN_KIND(Type, KindValue, ByValue, Persistent)      \
    template <>                                             \
    struct KindOf<::endstone::Type> {                       \
        static constexpr Kind value = Kind::KindValue;      \
        static constexpr bool by_value = (ByValue);         \
        static constexpr bool persistent = (Persistent);    \
    }

ESN_KIND(Player, Player, false, false);
ESN_KIND(Mob, Mob, false, false);
ESN_KIND(Actor, Actor, false, false);
ESN_KIND(Item, Item, false, false);
ESN_KIND(Block, Block, false, false);
ESN_KIND(BlockData, BlockData, false, false);
ESN_KIND(BlockState, BlockState, false, false);
ESN_KIND(ItemStack, ItemStack, false, false);
ESN_KIND(Location, Location, true, false);
ESN_KIND(Vector, Vector, true, false);
ESN_KIND(CommandSender, CommandSender, false, false);
ESN_KIND(MapView, MapView, false, false);
ESN_KIND(MapCanvas, MapCanvas, false, false);
ESN_KIND(DamageSource, DamageSource, false, false);
ESN_KIND(BossBar, BossBar, false, false);
ESN_KIND(Scoreboard, Scoreboard, false, false);
// A level and its dimensions are singletons for the server's lifetime, so their handles outlive the
// dispatch that minted them.
ESN_KIND(Level, Level, false, true);
ESN_KIND(Dimension, Dimension, false, true);
ESN_KIND(Server, Server, false, true);

#undef ESN_KIND

namespace detail {

template <typename>
inline constexpr bool always_false = false;

template <typename T>
struct IsOptional : std::false_type {};
template <typename T>
struct IsOptional<std::optional<T>> : std::true_type {
    using value_type = T;
};

template <typename T>
struct IsUniquePtr : std::false_type {};
template <typename T>
struct IsUniquePtr<std::unique_ptr<T>> : std::true_type {
    using element_type = T;
};

template <typename T>
struct IsStringVector : std::false_type {};
template <>
struct IsStringVector<std::vector<std::string>> : std::true_type {};

/** Endstone returns Result<T> from anything that can fail with a message. */
template <typename T>
struct IsResult : std::false_type {};
template <typename T>
struct IsResult<::endstone::Result<T>> : std::true_type {
    using value_type = T;
};

template <typename T, typename = void>
struct HasKind : std::false_type {};
template <typename T>
struct HasKind<T, std::void_t<decltype(KindOf<T>::value)>> : std::true_type {};

template <typename T>
using Bare = std::remove_cv_t<std::remove_reference_t<T>>;

}  // namespace detail

/** Which ABI accessor a C++ return type answers on. The one place that decision is made. */
template <typename R>
constexpr ValueKind valueKindFor()
{
    using T = detail::Bare<R>;
    if constexpr (std::is_same_v<T, bool>) {
        return ValueKind::Bool;
    }
    else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
        return ValueKind::Int;
    }
    else if constexpr (std::is_floating_point_v<T>) {
        return ValueKind::Double;
    }
    else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view>) {
        return ValueKind::String;
    }
    else if constexpr (detail::IsStringVector<T>::value) {
        // A list crosses newline-joined, so a member that is a vector of strings needs no extra
        // declaration - it answers on the string accessor like any other text.
        return ValueKind::String;
    }
    else if constexpr (detail::IsOptional<T>::value) {
        return valueKindFor<typename detail::IsOptional<T>::value_type>();
    }
    else if constexpr (detail::IsResult<T>::value) {
        return valueKindFor<typename detail::IsResult<T>::value_type>();
    }
    else {
        return ValueKind::Handle;
    }
}

/** Writes a value the Endstone API returned into the form the ABI carries. */
template <typename R>
esn_status writeValue(R &&value, Binder &binder, Value &out)
{
    using T = detail::Bare<R>;
    out.kind = valueKindFor<T>();

    if constexpr (std::is_same_v<T, bool>) {
        out.boolean = value;
        return ESN_OK;
    }
    else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
        out.integer = static_cast<std::int64_t>(value);
        return ESN_OK;
    }
    else if constexpr (std::is_floating_point_v<T>) {
        out.real = static_cast<double>(value);
        return ESN_OK;
    }
    else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view>) {
        out.text = value;
        return ESN_OK;
    }
    else if constexpr (detail::IsStringVector<T>::value) {
        for (std::size_t index = 0; index < value.size(); ++index) {
            if (index != 0) {
                out.text.push_back('\n');
            }
            out.text += value[index];
        }
        return ESN_OK;
    }
    else if constexpr (detail::IsOptional<T>::value) {
        // An absent optional is not an error and not a zero: it is the member having no value, which
        // the runtime reads back as undefined.
        if (!value.has_value()) {
            return ESN_ERR_NO_SUCH_MEMBER;
        }
        return writeValue(*std::forward<R>(value), binder, out);
    }
    else if constexpr (detail::IsResult<T>::value) {
        if (!value.has_value()) {
            return ESN_ERR_INTERNAL;
        }
        if constexpr (std::is_void_v<typename detail::IsResult<T>::value_type>) {
            out.kind = ValueKind::None;
            return ESN_OK;
        }
        else {
            return writeValue(*std::forward<R>(value), binder, out);
        }
    }
    else if constexpr (detail::IsUniquePtr<T>::value) {
        // Endstone made this for us, so the bridge owns it until the dispatch scope closes.
        if (!value) {
            return ESN_ERR_INTERNAL;
        }
        out.handle = binder.own(std::move(value));
        return ESN_OK;
    }
    else if constexpr (std::is_pointer_v<T>) {
        using Pointee = detail::Bare<std::remove_pointer_t<T>>;
        static_assert(detail::HasKind<Pointee>::value, "returns a pointer to a type with no ESN_KIND");
        // A null pointer is a real answer - no cape, no dimension - so it is handle 0, not an error.
        out.handle = value == nullptr ? 0
                                      : binder.track(const_cast<Pointee *>(value), KindOf<Pointee>::value,
                                                     KindOf<Pointee>::persistent);
        return ESN_OK;
    }
    else if constexpr (detail::HasKind<T>::value) {
        if constexpr (KindOf<T>::by_value) {
            out.handle = binder.own(value);
        }
        else {
            out.handle = binder.track(const_cast<T *>(&value), KindOf<T>::value, KindOf<T>::persistent);
        }
        return ESN_OK;
    }
    else {
        static_assert(detail::always_false<T>, "no binding rule for this return type - add one here");
    }
}

/** Reads a value the ABI carried into the form an Endstone setter takes. */
template <typename A>
A readValue(const Value &in)
{
    using T = detail::Bare<A>;
    if constexpr (std::is_same_v<T, bool>) {
        return in.boolean;
    }
    else if constexpr (std::is_enum_v<T>) {
        return static_cast<T>(in.integer);
    }
    else if constexpr (std::is_integral_v<T>) {
        return static_cast<T>(in.integer);
    }
    else if constexpr (std::is_floating_point_v<T>) {
        return static_cast<T>(in.real);
    }
    else if constexpr (std::is_same_v<T, std::string>) {
        return in.text;
    }
    else {
        static_assert(detail::always_false<T>, "no binding rule for this setter argument type");
    }
}

namespace detail {

/** Strings and numbers arrive in separate arrays, so each parameter kind counts its own index. */
struct ArgCursor {
    std::size_t strings{0};
    std::size_t numbers{0};
    std::size_t handles{0};
};

template <typename A>
auto nextArg(const Args &args, ArgCursor &cursor)
{
    using T = Bare<A>;
    static_assert(!std::is_same_v<T, std::string_view>,
                  "take std::string, not string_view - the argument array outlives nothing");
    if constexpr (std::is_same_v<T, std::string>) {
        return args.str(cursor.strings++);
    }
    else if constexpr (std::is_same_v<T, bool>) {
        return args.number(cursor.numbers++) != 0.0;
    }
    else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
        return static_cast<T>(args.number(cursor.numbers++));
    }
    else if constexpr (std::is_floating_point_v<T>) {
        return static_cast<T>(args.number(cursor.numbers++));
    }
    else {
        static_assert(always_false<T>, "no binding rule for this parameter type - use the lambda form");
    }
}

}  // namespace detail

/**
 * @brief Collects one type's members.
 *
 * Handed to an ESN_TYPE block, which calls ro/rw/method/dynamic once per member.
 */
template <typename T>
class TypeBuilder {
public:
    explicit TypeBuilder(TypeDesc &desc) : desc_(desc) {}

    /** A read-only member, from a const getter. */
    template <typename R>
    void ro(const std::string_view name, R (T::*getter)() const)
    {
        add(name, valueKindFor<R>(), [getter](void *self, Binder &binder, Value &out) {
            return writeValue((static_cast<T *>(self)->*getter)(), binder, out);
        });
    }

    /** A read-only member whose getter is not const, as several of Endstone's are. */
    template <typename R>
    void ro(const std::string_view name, R (T::*getter)())
    {
        add(name, valueKindFor<R>(), [getter](void *self, Binder &binder, Value &out) {
            return writeValue((static_cast<T *>(self)->*getter)(), binder, out);
        });
    }

    /** A read-only member computed by a lambda, for anything that is not one plain call. */
    template <typename Fn, std::enable_if_t<!std::is_member_pointer_v<Fn> && std::is_invocable_v<Fn, T &>, int> = 0>
    void ro(const std::string_view name, Fn fn)
    {
        using R = std::invoke_result_t<Fn, T &>;
        add(name, valueKindFor<R>(), [fn](void *self, Binder &binder, Value &out) {
            return writeValue(fn(*static_cast<T *>(self)), binder, out);
        });
    }

    /** A member with both a getter and a setter. The setter's argument type drives the conversion. */
    template <typename Getter, typename R, typename A>
    void rw(const std::string_view name, Getter getter, R (T::*setter)(A))
    {
        ro(name, getter);
        desc_.members[std::string{name}].set = [setter](void *self, Binder &, const Value &in) {
            (void)(static_cast<T *>(self)->*setter)(readValue<A>(in));
            return ESN_OK;
        };
    }

    /** A setter written out by hand, for anything the plain form cannot express. */
    template <typename Getter, typename Fn,
              std::enable_if_t<!std::is_member_pointer_v<Fn> && std::is_invocable_v<Fn, T &, const Value &, Binder &>,
                               int> = 0>
    void rw(const std::string_view name, Getter getter, Fn fn)
    {
        ro(name, getter);
        desc_.members[std::string{name}].set = [fn](void *self, Binder &binder, const Value &in) {
            return fn(*static_cast<T *>(self), in, binder);
        };
    }

    /** A method, with its arguments read positionally from the signature. */
    template <typename R, typename... A>
    void method(const std::string_view name, R (T::*fn)(A...))
    {
        addMethod<R, A...>(name, [fn](T &self, std::tuple<detail::Bare<A>...> &values) {
            return std::apply([&](auto &...args) { return (self.*fn)(args...); }, values);
        });
    }

    template <typename R, typename... A>
    void method(const std::string_view name, R (T::*fn)(A...) const)
    {
        addMethod<R, A...>(name, [fn](T &self, std::tuple<detail::Bare<A>...> &values) {
            return std::apply([&](auto &...args) { return (self.*fn)(args...); }, values);
        });
    }

    /** A method written out by hand - overloads, or anything whose arguments do not map positionally. */
    template <typename Fn,
              std::enable_if_t<!std::is_member_pointer_v<Fn> && std::is_invocable_v<Fn, T &, const Args &, esn_handle *>,
                               int> = 0>
    void method(const std::string_view name, Fn fn)
    {
        desc_.members[std::string{name}].call = [fn](void *self, const Args &args, esn_handle *out_handle) {
            return fn(*static_cast<T *>(self), args, out_handle);
        };
    }

    /**
     * A prefixed accessor, e.g. "tag:<key>". These are the members whose name carries an argument,
     * which no signature can express - keeping them explicit is the point.
     */
    template <typename Fn>
    void dynamic(const std::string_view prefix, const ValueKind kind, Fn fn)
    {
        desc_.dynamic.push_back(DynamicDesc{
            std::string{prefix}, kind,
            [fn](void *self, const std::string_view suffix, Binder &binder, Value &out) {
                return fn(*static_cast<T *>(self), suffix, binder, out);
            }});
    }

private:
    void add(const std::string_view name, const ValueKind kind, GetFn getter)
    {
        auto &member = desc_.members[std::string{name}];
        member.kind = kind;
        member.get = std::move(getter);
    }

    template <typename R, typename... A, typename Invoke>
    void addMethod(const std::string_view name, Invoke invoke)
    {
        desc_.members[std::string{name}].call = [invoke](void *self, const Args &args, esn_handle *out_handle) {
            detail::ArgCursor cursor;
            // Braced initialisation, so the arguments are read left to right. A plain call would
            // leave the order unspecified and silently mis-pair them with the ABI's arrays.
            std::tuple<detail::Bare<A>...> values{detail::nextArg<A>(args, cursor)...};
            auto &target = *static_cast<T *>(self);
            if constexpr (std::is_void_v<R>) {
                invoke(target, values);
                return ESN_OK;
            }
            else {
                Value out;
                const auto status = writeValue(invoke(target, values), out_binder(args), out);
                if (status != ESN_OK) {
                    return status;
                }
                if (out.kind == ValueKind::Handle && out_handle != nullptr) {
                    *out_handle = out.handle;
                }
                return ESN_OK;
            }
        };
    }

    static Binder &out_binder(const Args &args) { return args.binder; }

    TypeDesc &desc_;
};

/** Runs one type's binding block during static initialisation. */
class TypeRegistrar {
public:
    TypeRegistrar(const Kind kind, const std::string_view name, const Kind base, void *(*to_base)(void *),
                  void (*bind)(TypeDesc &))
    {
        bind(declareType(kind, name, base, to_base));
    }
};

}  // namespace endstone::node

/**
 * Declares what one Endstone type exposes, for a type with no bound base.
 *
 * @code
 * ESN_TYPE(Block, Block)
 * {
 *     b.ro("x", &Block::getX);
 * }
 * @endcode
 */
#define ESN_TYPE(Type, KindValue)                                                                            static void esnBind##Type(::endstone::node::TypeBuilder<::endstone::Type> &b);                            namespace {                                                                                               const ::endstone::node::TypeRegistrar esnRegistrar##Type{                                                     ::endstone::node::Kind::KindValue, #Type, ::endstone::node::Kind::None, nullptr,                          [](::endstone::node::TypeDesc &desc) {                                                                        ::endstone::node::TypeBuilder<::endstone::Type> builder{desc};                                            esnBind##Type(builder);                                                                               }};                                                                                                   }                                                                                                         static void esnBind##Type([[maybe_unused]] ::endstone::node::TypeBuilder<::endstone::Type> &b)

/**
 * The same, for a type that derives from another bound one. Members declared on the base are reached
 * through it, so each is declared exactly once - the way Endstone's own hierarchy has them.
 *
 * The upcast is generated here rather than assumed: a handle carries `void *`, and Player derives from
 * both Mob and OfflinePlayer, so reaching a base member needs the compiler to adjust the pointer.
 */
#define ESN_SUBTYPE(Type, KindValue, Base)                                                                   static void esnBind##Type(::endstone::node::TypeBuilder<::endstone::Type> &b);                            namespace {                                                                                               const ::endstone::node::TypeRegistrar esnRegistrar##Type{                                                     ::endstone::node::Kind::KindValue, #Type, ::endstone::node::KindOf<::endstone::Base>::value,              [](void *self) -> void * {                                                                                    return static_cast<::endstone::Base *>(static_cast<::endstone::Type *>(self));                        },                                                                                                        [](::endstone::node::TypeDesc &desc) {                                                                        ::endstone::node::TypeBuilder<::endstone::Type> builder{desc};                                            esnBind##Type(builder);                                                                               }};                                                                                                   }                                                                                                         static void esnBind##Type([[maybe_unused]] ::endstone::node::TypeBuilder<::endstone::Type> &b)
