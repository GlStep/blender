/*
 * Copyright 2011-2021 Blender Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

CCL_NAMESPACE_BEGIN

class GuidingParams {

 public:
  GuidingParams() = default;

  // the subset of path guiding parameters that can
  // trigger a creation/rebuild of the guiding field
  bool use{false};
  GuidingDistributionType type{GUIDING_TYPE_PAVMM};

  bool modified(const GuidingParams &other) const
  {
    return !(use == other.use && type == other.type);
  }
};

CCL_NAMESPACE_END