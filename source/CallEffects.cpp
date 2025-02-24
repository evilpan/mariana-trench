/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <optional>

#include <mariana-trench/AbstractTreeDomain.h>
#include <mariana-trench/Access.h>
#include <mariana-trench/Assert.h>
#include <mariana-trench/CallEffects.h>
#include <mariana-trench/Log.h>

namespace marianatrench {
namespace {

std::optional<CallEffect::Kind> string_to_call_effect_kind(
    std::string_view effect) {
  if (effect == "call-chain") {
    return CallEffect::Kind::CALL_CHAIN;
  } else {
    return std::nullopt;
  }
}

} // namespace

std::string CallEffect::to_string() const {
  switch (kind()) {
    case CallEffect::Kind::CALL_CHAIN:
      return "call-chain";
    default:
      mt_unreachable();
  }
}

AccessPath CallEffect::access_path() const {
  switch (kind()) {
    case CallEffect::Kind::CALL_CHAIN: {
      static const auto call_chain_path = AccessPath{
          Root{Root::Kind::CallEffect}, Path{PathElement::field(to_string())}};

      return call_chain_path;
    }

    default:
      mt_unreachable();
  }
}

Json::Value CallEffect::to_json() const {
  return access_path().to_json();
}

CallEffect CallEffect::from_json(const Json::Value& value) {
  auto elements = AccessPath::split_path(value);

  if (elements.empty() || elements.size() > 2) {
    throw JsonValidationError(
        value,
        /* field */ std::nullopt,
        "call effect to be specified as: `CallEffect.<type>` or `<type>`");
  }

  const auto& root_string =
      elements.size() == 2 ? elements.front() : "CallEffect";
  if (auto root = Root::from_json(root_string); !root.is_call_effect()) {
    throw JsonValidationError(
        value,
        /* field */ std::nullopt,
        "call effect root to be: `CallEffect`");
  }

  auto effect_kind = string_to_call_effect_kind(elements.back());
  if (!effect_kind.has_value()) {
    throw JsonValidationError(
        value,
        /* field */ std::nullopt,
        "one of existing call effect types: `call-chain`");
  }

  return CallEffect{effect_kind.value()};
}

std::ostream& operator<<(std::ostream& out, const CallEffect& effect) {
  return out << effect.to_string();
}

} // namespace marianatrench

namespace marianatrench {

bool CallEffectsAbstractDomain::leq(
    const CallEffectsAbstractDomain& other) const {
  return map_.leq(other.map_);
}

bool CallEffectsAbstractDomain::equals(
    const CallEffectsAbstractDomain& other) const {
  return map_.equals(other.map_);
}

void CallEffectsAbstractDomain::set_to_bottom() {
  map_.set_to_bottom();
}

void CallEffectsAbstractDomain::set_to_top() {
  map_.set_to_top();
}

void CallEffectsAbstractDomain::join_with(
    const CallEffectsAbstractDomain& other) {
  map_.join_with(other.map_);
}

void CallEffectsAbstractDomain::widen_with(
    const CallEffectsAbstractDomain& other) {
  map_.widen_with(other.map_);
}

void CallEffectsAbstractDomain::meet_with(
    const CallEffectsAbstractDomain& other) {
  map_.meet_with(other.map_);
}

void CallEffectsAbstractDomain::narrow_with(
    const CallEffectsAbstractDomain& other) {
  map_.narrow_with(other.map_);
}

const Taint& CallEffectsAbstractDomain::read(CallEffect effect) const {
  return map_.get(effect.encode());
}

void CallEffectsAbstractDomain::visit(
    std::function<void(const CallEffect&, const Taint&)> visitor) const {
  mt_assert(!is_top());

  for (const auto& [effect, taint] : *this) {
    visitor(effect, taint);
  }
}

void CallEffectsAbstractDomain::map(const std::function<void(Taint&)>& f) {
  map_.map([&](const Taint& taint) {
    auto copy = taint;
    f(copy);
    return copy;
  });
}

void CallEffectsAbstractDomain::write(const CallEffect& effect, Taint value) {
  map_.update(
      effect.encode(), [&](const Taint& taint) { return taint.join(value); });
}

std::ostream& operator<<(
    std::ostream& out,
    const CallEffectsAbstractDomain& effects) {
  if (!effects.is_bottom()) {
    out << "{\n";
    for (const auto& [effect, taint] : effects) {
      out << "    "
          << "CallEffects(" << effect << "): " << taint << ",\n";
    }
    out << "  }";
  }

  return out;
}

} // namespace marianatrench
