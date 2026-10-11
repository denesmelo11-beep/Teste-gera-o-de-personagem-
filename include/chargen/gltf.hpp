// Binary glTF 2.0 export: skinned mesh (vertex colours, face decal primitives
// with the expression atlas), skeleton, and animation clips.
#pragma once
#include <string>
#include <vector>

#include "animation.hpp"
#include "character.hpp"

namespace cg {

bool exportGlb(const Character& c, const std::vector<Clip>& clips, const std::string& path, std::string* error = nullptr);

}  // namespace cg
