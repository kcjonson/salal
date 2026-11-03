// salal_art_tool, the build step salal_art() runs (cmake/SalalArt.cmake):
//
//   salal_art_tool <art dir> <namespace> <header include> <header out> <source out>
//
// Checks every SVG in the folder against what loadSVG reads (vector/ArtBuild.h) and writes the
// header and source that embed them. A problem goes to stderr naming the file, line and element,
// with exit status 1, which fails the build. A file is written only when its contents change.

#include "vector/ArtBuild.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>

namespace {

	namespace fs = std::filesystem;
	using renderer::svg::ArtFile;
	using renderer::svg::Problem;

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

	int run(int argc, char** argv) {
		if (argc != 6) {
			std::cerr << "usage: salal_art_tool <art dir> <namespace> <header include> <header out> <source out>\n";
			return 1;
		}
		const fs::path		 dir = argv[1];
		std::vector<Problem> problems;
		std::vector<ArtFile> files;
		std::error_code		 error;
		for (const fs::directory_entry& entry : fs::directory_iterator(dir, error)) {
			const std::string name = entry.path().filename().generic_string();
			const std::string file = (dir / name).generic_string();
			if (name.starts_with('.')) {
				continue;
			}
			if (entry.is_directory() || entry.path().extension() != ".svg") {
				problems.push_back({.file = file, .message = "is not an .svg file; an art folder holds SVGs only"});
				continue;
			}
			std::optional<std::string> text = readFile(entry.path());
			if (!text.has_value()) {
				problems.push_back({.file = file, .message = "cannot be read"});
				continue;
			}
			if (renderer::svg::checkArt(file, *text, problems)) {
				files.push_back({.name = entry.path().stem().generic_string(), .text = std::move(*text)});
			}
		}
		if (error) {
			problems.push_back({.file = dir.generic_string(), .message = "cannot be listed"});
		}
		if (!problems.empty()) {
			for (const Problem& problem : problems) {
				std::cerr << "salal_art: error: " << problem.text() << '\n';
			}
			return 1;
		}
		const renderer::svg::ArtSources sources = renderer::svg::generateArtSources(files, argv[2], argv[3], dir.generic_string());
		if (!writeFile(argv[4], sources.header) || !writeFile(argv[5], sources.source)) {
			std::cerr << "salal_art: error: cannot write " << argv[4] << " and " << argv[5] << '\n';
			return 1;
		}
		return 0;
	}

} // namespace

int main(int argc, char** argv) {
	return run(argc, argv);
}
