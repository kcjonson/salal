// salal_shader_assembler, the build step salal_base_shader() runs (cmake/SalalBaseShader.cmake):
//
//   salal_shader_assembler assemble <shaders dir> <out dir> [<hook file>...]
//       writes <out dir>/base.vert and base.frag, every include and hook resolved
//   salal_shader_assembler embed <out dir> <namespace> <header> <source> [<hook file>...]
//       reads those and <out dir>/reflection.txt (glslangValidator -q), writes the C++
//
// A problem goes to stderr with exit status 1, which fails the build.

#include "assembler/BaseShaderAssembler.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace {

	using namespace Renderer::assembler;
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

	bool writeFile(const fs::path& path, const std::string& text) {
		std::error_code error;
		fs::create_directories(path.parent_path(), error);
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		file << text;
		return static_cast<bool>(file);
	}

	int fail(const std::string& message) {
		std::cerr << "salal_base_shader: error: " << message << '\n';
		return 1;
	}

	// The hooks folder's entries as the build listed them; a directory is an entry too, and is refused.
	bool readHookFiles(std::span<char*> paths, std::vector<HookFile>& files, std::string& error) {
		for (const char* argument : paths) {
			const fs::path path(argument);
			if (fs::is_directory(path)) {
				error = path.filename().generic_string() + " is a folder; a hooks folder holds <name>.vert.glsl files only";
				return false;
			}
			const std::optional<std::string> text = readFile(path);
			if (!text.has_value()) {
				error = "cannot read " + path.generic_string();
				return false;
			}
			files.push_back({.fileName = path.filename().generic_string(), .text = *text});
		}
		return true;
	}

	int assembleStages(const fs::path& shaders, const fs::path& out, std::span<char*> hookPaths) {
		std::vector<HookFile> files;
		std::string			  error;
		if (!readHookFiles(hookPaths, files, error)) {
			return fail(error);
		}
		const Hooks hooks = readHooks(std::move(files));
		if (!hooks.error.empty()) {
			return fail(hooks.error);
		}
		const Assembled vertex = assemble(shaders / "base.vert", readFile, hookChain(hooks.hooks));
		if (!vertex.error.empty()) {
			return fail(vertex.error);
		}
		const Assembled fragment = assemble(shaders / "base.frag", readFile, std::nullopt);
		if (!fragment.error.empty()) {
			return fail(fragment.error);
		}
		if (!writeFile(out / "base.vert", vertex.text) || !writeFile(out / "base.frag", fragment.text)) {
			return fail("cannot write the assembled stages to " + out.generic_string());
		}
		return 0;
	}

	int embedStages(
		const fs::path&	   out,
		const std::string& nameSpace,
		const fs::path&	   header,
		const fs::path&	   source,
		std::span<char*>   hookPaths
	) {
		std::vector<HookFile> files;
		std::string			  error;
		if (!readHookFiles(hookPaths, files, error)) {
			return fail(error);
		}
		const Hooks						 hooks = readHooks(std::move(files));
		const std::optional<std::string> vertex = readFile(out / "base.vert");
		const std::optional<std::string> fragment = readFile(out / "base.frag");
		const std::optional<std::string> reflection = readFile(out / "reflection.txt");
		if (!hooks.error.empty()) {
			return fail(hooks.error);
		}
		if (!vertex.has_value() || !fragment.has_value() || !reflection.has_value()) {
			return fail("the assembled stages or their reflection are missing from " + out.generic_string());
		}
		const Embedded embedded = embed({
			.nameSpace = nameSpace,
			.header = header.filename().generic_string(),
			.vertex = *vertex,
			.fragment = *fragment,
			.hooks = hooks.hooks,
			.blockSizes = reflectedBlockSizes(*reflection),
		});
		if (!embedded.error.empty()) {
			return fail(embedded.error);
		}
		if (!writeFile(header, embedded.header) || !writeFile(source, embedded.source)) {
			return fail("cannot write " + header.generic_string() + " and " + source.generic_string());
		}
		return 0;
	}

} // namespace

int main(int argc, char** argv) {
	const std::span<char*> args(argv, static_cast<size_t>(argc));
	const std::string	   command = args.size() > 1 ? args[1] : "";
	if (command == "assemble" && args.size() >= 4) {
		return assembleStages(args[2], args[3], args.subspan(4));
	}
	if (command == "embed" && args.size() >= 6) {
		return embedStages(args[2], args[3], args[4], args[5], args.subspan(6));
	}
	return fail(
		"usage: salal_shader_assembler assemble <shaders dir> <out dir> [<hook file>...]\n"
		"       salal_shader_assembler embed <out dir> <namespace> <header> <source> [<hook file>...]"
	);
}
