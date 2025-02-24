/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <vector>

#include <mariana-trench/Context.h>
#include <mariana-trench/Model.h>
#include <mariana-trench/model-generator/ModelGenerator.h>

namespace marianatrench {

class ModelGeneratorError : public std::invalid_argument {
 public:
  explicit ModelGeneratorError(const std::string& message);
};

class ModelGeneration {
 public:
  static ModelGeneratorResult run(
      Context& context,
      const MethodMappings& method_mappings);

  static std::map<std::string, std::unique_ptr<ModelGenerator>>
  make_builtin_model_generators(Context& context);
};

} // namespace marianatrench
