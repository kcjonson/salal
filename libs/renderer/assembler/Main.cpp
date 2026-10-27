// salal_shader_assembler, the build step salal_base_shader() runs (cmake/SalalBaseShader.cmake):
//
//   salal_shader_assembler assemble <shaders dir> <out dir> [<hooks dir>]
//       writes <out dir>/base.vert and base.frag, every include and hook resolved
//   salal_shader_assembler embed <out dir> <namespace> <header> <source> [<hooks dir>]
//       reads those and <out dir>/reflection.txt (glslangValidator -q), writes the C++
//
// A file is written only when its contents change, so what includes the header stays built. A
// problem goes to stderr with exit status 1, which fails the build.

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
		if (readFile(path) == text) {
			return true;
		}
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

	// The hooks folder's entries, read. Names starting with a dot (editor locks, .DS_Store) are left
	// out before anything looks at them; a folder is refused.
	bool readHookFiles(std::span<char*> folder, std::vector<HookFile>& files, std::string& error) {
		if (folder.empty()) {
			return true;
		}
		std::error_code listError;
		for (const fs::directory_entry& entry : fs::directory_iterator(fs::path(folder[0]), listError)) {
			const std::string name = entry.path().filename().generic_string();
			if (name.starts_with('.')) {
				continue;
			}
			std::error_code statError;
			if (entry.is_directory(statError)) {
				error = name + " is a folder; a hooks folder holds <name>.vert.glsl files only";
				return false;
			}
			const std::optional<std::string> text = readFile(entry.path());
			if (!text.has_value()) {
				error = "cannot read " + entry.path().generic_string();
				return false;
			}
			files.push_back({.fileName = name, .text = *text});
		}
		if (listError) {
			error = "cannot list " + std::string(folder[0]);
			return false;
		}
		return true;
	}

	int assembleStages(const fs::path& shaders, const fs::path& out, std::span<char*> hooksFolder) {
		std::vector<HookFile> files;
		std::string			  error;
		if (!readHookFiles(hooksFolder, files, error)) {
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
		std::span<char*>   hooksFolder
	) {
		std::vector<HookFile> files;
		std::string			  error;
		if (!readHookFiles(hooksFolder, files, error)) {
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
		// The source includes its header by a path from itself, so neither the target's include path
		// nor another assembly's header of the same name gets in the way.
		const Embedded embedded = embed({
			.nameSpace = nameSpace,
			.header = header.lexically_relative(source.parent_path()).generic_string(),
			.vertex = *vertex,
			.fragment = *fragment,
			.hooks = hooks.hooks,
			.blocks = reflectBlocks(*reflection),
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
	if (command == "assemble" && (args.size() == 4 || args.size() == 5)) {
		return assembleStages(args[2], args[3], args.subspan(4));
	}
	if (command == "embed" && (args.size() == 6 || args.size() == 7)) {
		return embedStages(args[2], args[3], args[4], args[5], args.subspan(6));
	}
	return fail(
		"usage: salal_shader_assembler assemble <shaders dir> <out dir> [<hooks dir>]\n"
		"       salal_shader_assembler embed <out dir> <namespace> <header> <source> [<hooks dir>]"
	);
}
