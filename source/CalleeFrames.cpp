/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <Show.h>

#include <mariana-trench/Assert.h>
#include <mariana-trench/CalleeFrames.h>
#include <mariana-trench/Frame.h>
#include <mariana-trench/Log.h>

namespace marianatrench {

CalleeFrames::CalleeFrames(std::initializer_list<TaintConfig> configs)
    : callee_(nullptr) {
  for (const auto& config : configs) {
    add(config);
  }
}

void CalleeFrames::add(const TaintConfig& config) {
  if (callee_ == nullptr) {
    callee_ = config.callee();
  } else {
    mt_assert(callee_ == config.callee());
  }

  // TODO (T91357916): GroupHashedSetAbstractDomain could be more efficient.
  // It supports an `add` operation that avoids making a copy.
  frames_.update(
      config.call_position(), [&](const CallPositionFrames& old_frames) {
        auto new_frames = old_frames;
        new_frames.add(config);
        return new_frames;
      });
}

bool CalleeFrames::leq(const CalleeFrames& other) const {
  mt_assert(is_bottom() || other.is_bottom() || callee_ == other.callee());
  return frames_.leq(other.frames_);
}

bool CalleeFrames::equals(const CalleeFrames& other) const {
  mt_assert(is_bottom() || other.is_bottom() || callee_ == other.callee());
  return frames_.equals(other.frames_);
}

void CalleeFrames::join_with(const CalleeFrames& other) {
  mt_if_expensive_assert(auto previous = *this);

  if (is_bottom()) {
    mt_assert(callee_ == nullptr);
    callee_ = other.callee();
  }
  mt_assert(other.is_bottom() || callee_ == other.callee());

  frames_.join_with(other.frames_);

  mt_expensive_assert(previous.leq(*this) && other.leq(*this));
}

void CalleeFrames::widen_with(const CalleeFrames& other) {
  mt_if_expensive_assert(auto previous = *this);

  if (is_bottom()) {
    mt_assert(callee_ == nullptr);
    callee_ = other.callee();
  }
  mt_assert(other.is_bottom() || callee_ == other.callee());

  frames_.widen_with(other.frames_);

  mt_expensive_assert(previous.leq(*this) && other.leq(*this));
}

void CalleeFrames::meet_with(const CalleeFrames& other) {
  if (is_bottom()) {
    mt_assert(callee_ == nullptr);
    callee_ = other.callee();
  }
  mt_assert(other.is_bottom() || callee_ == other.callee());

  frames_.meet_with(other.frames_);
}

void CalleeFrames::narrow_with(const CalleeFrames& other) {
  if (is_bottom()) {
    mt_assert(callee_ == nullptr);
    callee_ = other.callee();
  }
  mt_assert(other.is_bottom() || callee_ == other.callee());

  frames_.narrow_with(other.frames_);
}

void CalleeFrames::difference_with(const CalleeFrames& other) {
  if (is_bottom()) {
    mt_assert(callee_ == nullptr);
    callee_ = other.callee();
  }
  mt_assert(other.is_bottom() || callee_ == other.callee());

  frames_.difference_like_operation(
      other.frames_,
      [](const CallPositionFrames& frames_left,
         const CallPositionFrames& frames_right) {
        auto frames_copy = frames_left;
        frames_copy.difference_with(frames_right);
        return frames_copy;
      });
}

void CalleeFrames::map(const std::function<void(Frame&)>& f) {
  frames_.map([&](const CallPositionFrames& frames) {
    auto new_frames = frames;
    new_frames.map(f);
    return new_frames;
  });
}

void CalleeFrames::set_origins_if_empty(const MethodSet& origins) {
  frames_.map([&](const CallPositionFrames& frames) {
    auto new_frames = frames;
    new_frames.set_origins_if_empty(origins);
    return new_frames;
  });
}

void CalleeFrames::set_field_origins_if_empty_with_field_callee(
    const Field* field) {
  frames_.map([&](const CallPositionFrames& frames) {
    auto new_frames = frames;
    new_frames.set_field_origins_if_empty_with_field_callee(field);
    return new_frames;
  });
}

FeatureMayAlwaysSet CalleeFrames::inferred_features() const {
  auto result = FeatureMayAlwaysSet::bottom();
  for (const auto& [_, frames] : frames_.bindings()) {
    result.join_with(frames.inferred_features());
  }
  return result;
}

void CalleeFrames::add_inferred_features(const FeatureMayAlwaysSet& features) {
  if (features.empty()) {
    return;
  }

  map([&features](Frame& frame) { frame.add_inferred_features(features); });
}

LocalPositionSet CalleeFrames::local_positions() const {
  auto result = LocalPositionSet::bottom();
  for (const auto& [_, frames] : frames_.bindings()) {
    result.join_with(frames.local_positions());
  }
  return result;
}

void CalleeFrames::add_local_position(const Position* position) {
  frames_.map([&](const CallPositionFrames& frames) {
    auto new_frames = frames;
    new_frames.add_local_position(position);
    return new_frames;
  });
}

void CalleeFrames::set_local_positions(const LocalPositionSet& positions) {
  frames_.map([&](const CallPositionFrames& frames) {
    auto new_frames = frames;
    new_frames.set_local_positions(positions);
    return new_frames;
  });
}

void CalleeFrames::add_inferred_features_and_local_position(
    const FeatureMayAlwaysSet& features,
    const Position* MT_NULLABLE position) {
  if (features.empty() && position == nullptr) {
    return;
  }

  map([&features](Frame& frame) {
    if (!features.empty()) {
      frame.add_inferred_features(features);
    }
  });

  if (position != nullptr) {
    add_local_position(position);
  }
}

CalleeFrames CalleeFrames::propagate(
    const Method* callee,
    const AccessPath& callee_port,
    const Position* call_position,
    int maximum_source_sink_distance,
    Context& context,
    const std::vector<const DexType * MT_NULLABLE>& source_register_types,
    const std::vector<std::optional<std::string>>& source_constant_arguments)
    const {
  if (is_bottom()) {
    return CalleeFrames::bottom();
  }

  CallPositionFrames result;
  for (const auto& [_, call_position_frames] : frames_.bindings()) {
    result.join_with(call_position_frames.propagate(
        callee,
        callee_port,
        call_position,
        maximum_source_sink_distance,
        context,
        source_register_types,
        source_constant_arguments));
  }

  if (result.is_bottom()) {
    return CalleeFrames::bottom();
  }

  mt_assert(call_position == result.position());
  return CalleeFrames(
      callee, FramesByCallPosition{std::pair(call_position, result)});
}

CalleeFrames CalleeFrames::attach_position(const Position* position) const {
  CallPositionFrames result;

  // NOTE: It is not sufficient to simply update the key in the underlying
  // frames_ map. This functions similarly to `propagate`. Frame features are
  // propagated here, and we must call `CallPositionFrames::attach_position`
  // to ensure that.
  for (const auto& [_, call_position_frames] : frames_.bindings()) {
    result.join_with(call_position_frames.attach_position(position));
  }

  return CalleeFrames(
      callee_, FramesByCallPosition{std::pair(position, result)});
}

void CalleeFrames::transform_kind_with_features(
    const std::function<std::vector<const Kind*>(const Kind*)>& transform_kind,
    const std::function<FeatureMayAlwaysSet(const Kind*)>& add_features) {
  frames_.map([&](const CallPositionFrames& frames) {
    auto frames_copy = frames;
    frames_copy.transform_kind_with_features(transform_kind, add_features);
    return frames_copy;
  });
}

void CalleeFrames::append_to_artificial_source_input_paths(
    Path::Element path_element) {
  // TODO (T91357916): GroupHashedSetAbstractDomain could be more efficient than
  // PatriciaTreeMapAbstractPartition for holding frames_. It supports in-place
  // modifying of the elements as long as the key does not change.
  frames_.map([&](const CallPositionFrames& frames) {
    auto frames_copy = frames;
    frames_copy.append_to_artificial_source_input_paths(path_element);
    return frames_copy;
  });
}

void CalleeFrames::add_inferred_features_to_real_sources(
    const FeatureMayAlwaysSet& features) {
  frames_.map([&](const CallPositionFrames& frames) {
    auto frames_copy = frames;
    frames_copy.add_inferred_features_to_real_sources(features);
    return frames_copy;
  });
}

void CalleeFrames::update_non_leaf_positions(
    const std::function<
        const Position*(const Method*, const AccessPath&, const Position*)>&
        new_call_position,
    const std::function<LocalPositionSet(const LocalPositionSet&)>&
        new_local_positions) {
  if (callee_ == nullptr) {
    // This is a leaf.
    return;
  }

  FramesByCallPosition result;
  for (const auto& [_, call_position_frames] : frames_.bindings()) {
    auto new_positions = call_position_frames.map_positions(
        [&](const auto& access_path, const auto* position) {
          return new_call_position(callee_, access_path, position);
        },
        new_local_positions);

    for (const auto& [position, new_frames] : new_positions) {
      // Lambda below refuses to capture `new_frames` from a structured
      // binding, so explicitly declare it here.
      const auto& frames = new_frames;
      result.update(
          position, [&](const CallPositionFrames& call_position_frames) {
            return call_position_frames.join(frames);
          });
    }
  }

  frames_ = std::move(result);
}

void CalleeFrames::filter_invalid_frames(
    const std::function<bool(const Method*, const AccessPath&, const Kind*)>&
        is_valid) {
  frames_.map([&](const CallPositionFrames& frames) {
    auto frames_copy = frames;
    frames_copy.filter_invalid_frames(is_valid);
    return frames_copy;
  });
}

bool CalleeFrames::contains_kind(const Kind* kind) const {
  auto frames_iterator = frames_.bindings();
  return std::any_of(
      frames_iterator.begin(),
      frames_iterator.end(),
      [&](const std::pair<const Position*, CallPositionFrames>&
              callee_frames_pair) {
        return callee_frames_pair.second.contains_kind(kind);
      });
}

RootPatriciaTreeAbstractPartition<PathTreeDomain> CalleeFrames::input_paths()
    const {
  return frames_.get(nullptr).input_paths();
}

Json::Value CalleeFrames::to_json() const {
  auto taint = Json::Value(Json::arrayValue);
  for (const auto& [_, call_position_frames] : frames_.bindings()) {
    auto frames_json = call_position_frames.to_json(callee_);
    mt_assert(frames_json.isArray());
    for (const auto& frame_json : frames_json) {
      taint.append(frame_json);
    }
  }
  return taint;
}

std::ostream& operator<<(std::ostream& out, const CalleeFrames& frames) {
  if (frames.is_top()) {
    return out << "T";
  } else {
    out << "[";
    for (const auto& [position, frames] : frames.frames_.bindings()) {
      out << "FramesByPosition(position=" << show(position) << ","
          << "frames=" << frames << "),";
    }
    return out << "]";
  }
}

} // namespace marianatrench
