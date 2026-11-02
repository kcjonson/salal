// salal_icons_tool, the build step salal_icons() runs (cmake/SalalIcons.cmake):
//
//   salal_icons_tool <icons dir> <out dir> <namespace> <header include> <header out> <source out> <cell px> <range px>
//
// Checks every SVG in the folder against the icon rules, builds the MSDF atlas, and writes
// <out dir>/icons.png and icons.json beside the header and the source that embed them with the
// name table. A problem goes to stderr naming the file, line and element, with exit status 1,
// which fails the build. A file is written only when its contents change.

#include "ui/iconbuild/IconAtlasBuilder.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {

	using namespace ui::iconbuild;
	namespace fs = std::filesystem;

	std::optional<std::string> readFile(const fs::path& path) {
		std::ifstream file(path, std::ios::binary);
		if (!file) {
			return std::nullopt;
		}
		std::ostringstream text;
		text << file.rdbuf();
		return text.str();
	}

	bool writeFile(const fs::path& path, std::string_view text) {
		if (readFile(path) == text) {
			return true;
		}
		std::error_code error;
		fs::create_directories(path.parent_path(), error);
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		file.write(text.data(), static_cast<std::streamsize>(text.size()));
		return static_cast<bool>(file);
	}

	int fail(const std::vector<Problem>& problems) {
		for (const Problem& problem : problems) {
			std::cerr << "salal_icons: error: " << problem.text() << '\n';
		}
		return 1;
	}

	int run(int argc, char** argv) {
		if (argc != 9) {
			std::cerr << "usage: salal_icons_tool <icons dir> <out dir> <namespace> <header include> <header out> <source out> <cell px> "
						 "<range px>\n";
			return 1;
		}
		const fs::path			dir = argv[1];
		std::vector<Problem>	problems;
		std::vector<IconSource> icons;
		std::error_code			error;
		for (const fs::directory_entry& entry : fs::directory_iterator(dir, error)) {
			const std::string name = entry.path().filename().generic_string();
			const std::string file = (dir / name).generic_string();
			if (name.starts_with('.')) {
				continue;
			}
			if (entry.is_directory() || entry.path().extension() != ".svg") {
				problems.push_back({.file = file, .message = "is not an .svg file; an icons folder holds SVGs only"});
				continue;
			}
			const std::optional<std::string> text = readFile(entry.path());
			if (!text.has_value()) {
				problems.push_back({.file = file, .message = "cannot be read"});
				continue;
			}
			icons.push_back({.name = entry.path().stem().generic_string(), .file = file, .text = *text});
		}
		if (error) {
			problems.push_back({.file = dir.generic_string(), .message = "cannot be listed"});
		}
		if (!problems.empty()) {
			return fail(problems);
		}
		const AtlasOptions				options{.cellSize = std::atoi(argv[7]), .distanceRange = std::atoi(argv[8])};
		const std::optional<BuiltAtlas> atlas = buildAtlas(std::move(icons), options, problems);
		if (!atlas.has_value()) {
			return fail(problems);
		}
		const std::vector<std::uint8_t> png = encodePng(*atlas);
		const IconSources				sources = generateIconSources(*atlas, png, argv[3], argv[4], dir.generic_string());
		const fs::path					out = argv[2];
		const std::string_view			pngText(reinterpret_cast<const char*>(png.data()), png.size());
		if (!writeFile(out / "icons.png", pngText) || !writeFile(out / "icons.json", atlas->json) || !writeFile(argv[5], sources.header) ||
			!writeFile(argv[6], sources.source)) {
			std::cerr << "salal_icons: error: cannot write into " << out.generic_string() << '\n';
			return 1;
		}
		return 0;
	}

} // namespace

int main(int argc, char** argv) {
	return run(argc, argv);
}
