// Character studies: hand-tuned genomes that push the generator towards specific
// designs (an android monk with a power core, a grinning homunculus that hardens
// into an armoured shell, four-armed four-eyed giants...) plus stress combinations
// (four-armed centaur, full carapace horror, metal golem, decayed giants). They are
// exploration and regression material: each one exercises several systems at once.
#pragma once
#include <optional>
#include <string_view>
#include <vector>

#include "character.hpp"
#include "genome.hpp"

namespace cg {

struct Study {
    std::string_view name;
    std::string_view label;  // short caption (Portuguese)
};
const std::vector<Study>& studies();
// The study's genome for a seed (the seed varies the person, not the design) and the
// build options it wants (decay, for the corpse studies). nullopt for an unknown name.
std::optional<Genome> studyGenome(std::string_view name, uint64_t seed, GenOptions* opt = nullptr);

}  // namespace cg
