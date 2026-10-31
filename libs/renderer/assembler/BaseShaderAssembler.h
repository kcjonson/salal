#pragma once

// The build step behind salal_base_shader() (cmake/SalalBaseShader.cmake, docs/shader-hooks.md):
// read a consumer's hooks folder, splice its vertex hooks and the chain that calls them into
// base.vert and its fill hooks and the switch that picks one into base.frag, resolve every include,
// and write the C++ that embeds the result. Functions over text, so the tests run them without a
// build; Main.cpp reads and writes the files.

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

	// A hook: `<name>.vert.glsl`, a vertex hook, every function it adds to the chain named
	// `<name>_<point>`, or `<name>.frag.glsl`, a fill hook, whose one point is `<name>_fill` and which a
	// rect picks by its index.
	struct Hook {
		std::string name;
		std::string fileName;
		std::string text;
		bool		instanceLocal = false; // defines <name>_instanceLocal
		bool		instanceWorld = false; // defines <name>_instanceWorld
		uint32_t	fillIndex = 0;		   // a fill hook's, 1 up in file-name order among the fill hooks; 0 for a vertex hook
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

	// A member of a uniform block as glslangValidator reflects it.
	struct ReflectedMember {
		std::string name;		// as GL names it: "Block.member" when the block has an instance name
		std::string type;		// the GLSL type, "vec2", "vec4[3]" for an array; empty for a type the step doesn't know
		uint32_t	offset = 0; // std140
		uint32_t	size = 0;	// bytes, an array's elements at their std140 stride
	};

	struct ReflectedBlock {
		uint32_t					 size = 0; // bytes, unpadded
		std::vector<ReflectedMember> members;  // in offset order
	};

	// GL 3.3 core promises 12 uniform blocks per stage, and DrawUniforms is one of them. Vertex and fill
	// hooks share the binding points, so the limit is across both.
	constexpr size_t kMaxHookBlocks = 11;

	// A rect names its fill hook in one byte, and 0 is none.
	constexpr size_t kMaxFillHooks = 255;

	// The hooks in `files`, sorted by file name, each checked: a `<name>.vert.glsl` or `<name>.frag.glsl`
	// with `<name>` a GLSL identifier that makes no reserved name and is not both; a vertex hook with at
	// least one hook point defined, a fill hook with `<name>_fill` and no derivatives or u_textures; no
	// #version, #extension or binding qualifier; and at most one uniform block, std140 and not an array,
	// which gets the next binding point, counted across both kinds. A fill hook gets the next fill
	// index. A leading UTF-8 byte-order mark is dropped, and entries whose names start with a dot
	// (editor and OS litter) are skipped.
	Hooks readHooks(std::vector<HookFile> files);

	// What `#include "hooks.glsl"` in base.vert stands for: every vertex hook's text in file-name order,
	// then applyInstanceLocalHooks() and applyInstanceWorldHooks(), which call each hook's function for
	// that point in the same order. With no vertex hooks both return the position they are given.
	std::string vertexStageHooks(const std::vector<Hook>& hooks);

	// What `#include "hooks.glsl"` in base.frag stands for: every fill hook's text in file-name order,
	// then applyFillHook(), a switch calling the fill hook a rect's index names. For index 0, and with no
	// fill hooks, it returns the colour it is given.
	std::string fragmentStageHooks(const std::vector<Hook>& hooks);

	// Reads a file by path, nothing when it cannot.
	using ReadFile = std::function<std::optional<std::string>(const std::filesystem::path&)>;

	// `file` with every `#include "..."` replaced by the file it names, relative to the including file,
	// each included once. `hooks` is what `#include "hooks.glsl"` becomes, the stage's hooks, which the
	// stage must include once. Line endings come out as \n, and a leading byte-order mark goes.
	Assembled assemble(const std::filesystem::path& file, const ReadFile& readFile, const std::string& hooks);

	// Each uniform block in glslangValidator's reflection report (`-q --reflect-all-block-variables`):
	// its size and its members. Lines the step doesn't recognise are skipped.
	std::map<std::string, ReflectedBlock> reflectBlocks(const std::string& reflection);

	// The generated code's C++ name for a hook's block: "offset" is kOffset, "wind_sway" kWindSway. A
	// fill hook's constant adds Fill: kPaperFill.
	std::string constantName(const std::string& hookName);

	struct EmbedArgs {
		std::string							  nameSpace;
		std::string							  header; // what the source's #include names, relative to the source
		std::string							  vertex;
		std::string							  fragment;
		std::vector<Hook>					  hooks;
		std::map<std::string, ReflectedBlock> blocks; // reflectBlocks() of the two stages linked
	};

	// The header and source that embed the assembled stages: `<nameSpace>::source()`, the
	// backend::ShaderSource Primitives::init() takes; per fill hook a backend::FillHook constant with
	// its index; and per hook with a block a backend::HookUniformBlock constant, its size padded to 16
	// bytes as a std140 block is, with a backend::HookUniformMember constant per member (offset and
	// size, for static_asserts).
	Embedded embed(const EmbedArgs& args);

} // namespace Renderer::assembler
