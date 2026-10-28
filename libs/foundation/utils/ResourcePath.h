#pragma once

// Resource lookup: a relative path (fonts, shaders, assets) resolved against the resource roots, the
// executable's directory and the working directory, in that order, so a resource is found whatever
// directory the program was started from.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Foundation {

	// The directory holding the running executable; empty if it cannot be determined.
	std::filesystem::path getExecutableDir();

	// The roots findResource searches first, in order, replacing any set before. Process-global, since
	// the renderer resolves through findResource too; the app sets them from AppConfig::resourceRoots.
	// A relative root is taken from the working directory at each lookup.
	void setResourceRoots(std::vector<std::filesystem::path> roots);

	// The first of root / relativePath for each resource root, then the executable's directory, then
	// the working directory (skipped when it no longer exists) that names something; nullopt if none.
	std::optional<std::filesystem::path> findResource(const std::filesystem::path& relativePath);

	// findResource's path as a string, or empty when nothing was found.
	std::string findResourceString(const std::filesystem::path& relativePath);

} // namespace Foundation
