#pragma once

// The build step behind salal_base_shader() (cmake/SalalBaseShader.cmake, docs/shader-hooks.md):
// read a consumer's hooks folder, splice its hooks and the chain that calls them into base.vert,
// resolve every include, and write the C++ that embeds the result. Functions over text, so the
// tests run them without a build; Main.cpp reads and writes the files.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Renderer::assembler {

	// One entry of a hooks folder, as the build found it.
	struct HookFile {
		std::string fileName;
		std::string text;
	};

	// A hook: `<name>.vert.glsl`, every function it adds to the chain named `<name>_<point>`.
	struct Hook {
		std::string name;
		std::string fileName;
		std::string text;
		bool		instanceLocal = false; // defines <name>_instanceLocal
		bool		instanceWorld = false; // defines <name>_instanceWorld
		std::string block;				   // its uniform block's name, empty when it declares none
		uint32_t	binding = 0;		   // the block's binding point: 1 up in file-name order, 0 without a block
	};

	// What a step made, or the first problem it found, worded for the build log.
	struct Hooks {
		std::vector<Hook> hooks; // in file-name order
		std::string		  error;
	};

	struct Assembled {
		std::string text;
		std::string error;
	};

	struct Embedded {
		std::string header;
		std::string source;
		std::string error;
	};

	// GL 3.3 core promises 12 uniform blocks per stage, and DrawUniforms is one of them.
	constexpr size_t kMaxHookBlocks = 11;

	// The hooks in `files`, sorted by file name, each checked: a `<name>.vert.glsl` with `<name>` a GLSL
	// identifier, at least one hook point defined, and at most one uniform block, std140, which gets
	// the next binding point. Files whose names start with a dot (editor and OS litter) are skipped.
	Hooks readHooks(std::vector<HookFile> files);

	// What `#include "hooks.glsl"` in base.vert stands for: every hook's text in file-name order, then
	// applyInstanceLocalHooks() and applyInstanceWorldHooks(), which call each hook's function for that
	// point in the same order. With no hooks both return the position they are given.
	std::string hookChain(const std::vector<Hook>& hooks);

	// Reads a file by path, nothing when it cannot.
	using ReadFile = std::function<std::optional<std::string>(const std::filesystem::path&)>;

	// `file` with every `#include "..."` replaced by the file it names, relative to the including file,
	// each included once. `hooks` is what `#include "hooks.glsl"` becomes: base.vert must include it
	// once, and with no `hooks` (base.frag) it must not appear. Line endings come out as \n.
	Assembled assemble(const std::filesystem::path& file, const ReadFile& readFile, const std::optional<std::string>& hooks);

	// Each uniform block's size in bytes from glslangValidator's reflection report (`-q`), unpadded.
	std::map<std::string, uint32_t> reflectedBlockSizes(const std::string& reflection);

	// The generated code's C++ name for a hook's block: "offset" is kOffset, "wind_sway" kWindSway.
	std::string constantName(const std::string& hookName);

	struct EmbedArgs {
		std::string						nameSpace;
		std::string						header; // the header's file name, which the source includes
		std::string						vertex;
		std::string						fragment;
		std::vector<Hook>				hooks;
		std::map<std::string, uint32_t> blockSizes; // reflectedBlockSizes() of the two stages linked
	};

	// The header and source that embed the assembled stages: `<nameSpace>::source()`, the
	// backend::ShaderSource Primitives::init() takes, and a backend::HookUniformBlock constant per hook
	// with a block, its size padded to 16 bytes as a std140 block is.
	Embedded embed(const EmbedArgs& args);

} // namespace Renderer::assembler
