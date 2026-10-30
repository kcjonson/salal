#pragma once

// The Design Tokens Resolver Module 2025.10 (https://www.designtokens.org/tr/2025.10/resolver/): a
// resolver file's sets and modifiers merged in its resolution order for one set of inputs, and the
// layers a theme is built from, ui's tokens first and a game's over them.
//
// Choices the module leaves open: inputs match modifier and context names exactly; a modifier
// counts only if the resolution order uses it; a $ref outside #/ is a file relative to the resolver.

#include "ui/theme/dtcg/Document.h"

#include <span>

namespace ui::dtcg {

	struct Modifier {
		std::string				   name;
		std::vector<std::string>   contexts;
		std::optional<std::string> defaultContext;
	};

	// One tokens folder: its documents by name relative to the folder, reached through `read`. With
	// an `entry` (its resolver file) the resolver decides what merges; without one, every name in
	// `files` merges in order. `label` prefixes the names in messages.
	struct Layer {
		std::string				 label;
		std::string				 entry;
		std::vector<std::string> files;
		ReadFile				 read;
	};

	// The modifiers a layer's resolution order uses; none for a layer without a resolver.
	std::vector<Modifier> layerModifiers(const Layer& layer);

	// Every combination of the modifiers' contexts, for checking each mode at build time.
	std::vector<ResolverInputs> permutations(std::span<const Modifier> modifiers);

	// The layers merged in order for `inputs`. Every input names a modifier of some layer, and every
	// modifier a layer uses gets a context from the inputs or its default.
	TokenSet loadLayers(std::span<const Layer> layers, const ResolverInputs& inputs, std::vector<Diagnostic>& diagnostics);

} // namespace ui::dtcg
