#include "assembler/BaseShaderAssembler.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>

namespace Renderer::assembler {

	namespace {
		constexpr std::string_view kHookSuffix = ".vert.glsl";
		constexpr std::string_view kHooksInclude = "hooks.glsl";
		constexpr std::string_view kDrawUniformsBlock = "DrawUniforms";
		// The raw string literals embed() writes: the text may not contain the closing sequence, and
		// MSVC refuses a literal much past 16 KB, so each piece stays well under it.
		constexpr std::string_view kRawOpen = "R\"glsl(";
		constexpr std::string_view kRawClose = ")glsl\"";
		constexpr size_t		   kPieceBytes = 8192;

		// `text` with \n line endings and no leading UTF-8 byte-order mark (Visual Studio saves one).
		std::string withUnixNewlines(std::string text) {
			if (text.starts_with("\xEF\xBB\xBF")) {
				text.erase(0, 3);
			}
			text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
			return text;
		}

		// `text` with its comments blanked to spaces, newlines kept, so a declaration in a comment is not one.
		std::string withoutComments(const std::string& text) {
			std::string out = text;
			size_t		i = 0;
			while (i < out.size()) {
				if (out.compare(i, 2, "//") == 0) {
					for (; i < out.size() && out[i] != '\n'; ++i) {
						out[i] = ' ';
					}
				} else if (out.compare(i, 2, "/*") == 0) {
					const size_t close = out.find("*/", i + 2);
					const size_t end = close == std::string::npos ? out.size() : close + 2;
					for (; i < end; ++i) {
						if (out[i] != '\n') {
							out[i] = ' ';
						}
					}
				} else {
					++i;
				}
			}
			return out;
		}

		// A GLSL identifier a hook may use as its prefix, and one whose generated names (<name>_<point>)
		// are not reserved: no gl_ prefix, no double underscore anywhere, so no trailing underscore,
		// and not "gl" itself.
		bool isHookName(const std::string& name) {
			static const std::regex kIdentifier("[A-Za-z_][A-Za-z0-9_]*");
			return std::regex_match(name, kIdentifier) && name != "gl" && !name.starts_with("gl_") && !name.ends_with('_') &&
				   name.find("__") == std::string::npos;
		}

		bool defines(const std::string& code, const std::string& function) {
			return std::regex_search(code, std::regex("\\b" + function + "\\s*\\("));
		}

		// False, with `error` set, when the hook has a #version or #extension line or a binding
		// layout qualifier: the base shader's #version 330 core is the only one, desktop GLSL wants an
		// #extension before any code, which spliced-in text never is, and GL 3.3 core has no binding
		// qualifier (the build gives the block its point).
		bool checkDirectives(const std::string& code, const Hook& hook, std::string& error) {
			static const std::regex kDirective("^\\s*#\\s*(version|extension)\\b");
			static const std::regex kLayout("\\blayout\\s*\\(([^)]*)\\)");
			static const std::regex kBinding("\\bbinding\\b");
			std::istringstream		lines(code);
			std::string				line;
			int						number = 0;
			while (std::getline(lines, line)) {
				++number;
				std::smatch directive;
				if (std::regex_search(line, directive, kDirective)) {
					error = hook.fileName + ":" + std::to_string(number) + ": #" + directive[1].str() +
							" in a hook; the base shader's #version 330 core is the only one, and no hook can bring an extension";
					return false;
				}
			}
			const auto end = std::sregex_iterator();
			for (auto it = std::sregex_iterator(code.begin(), code.end(), kLayout); it != end; ++it) {
				const std::string qualifiers = (*it)[1].str();
				if (std::regex_search(qualifiers, kBinding)) {
					error = hook.fileName + ": layout(" + qualifiers +
							") gives a binding; the build assigns the block's binding point, and GL 3.3 core has no binding qualifier";
					return false;
				}
			}
			return true;
		}

		// The hook's uniform block, into hook.block; false, with `error` set, when the hook has a
		// uniform outside a block, more than one block, a block that is not std140, or an array of blocks.
		bool readBlock(const std::string& code, Hook& hook, std::string& error) {
			static const std::regex kUniform("\\buniform\\b");
			static const std::regex kBlockName("\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\{");
			static const std::regex kLayout("layout\\s*\\(([^)]*)\\)\\s*$");
			static const std::regex kStd140("\\bstd140\\b");
			static const std::regex kInstanceArray("\\s*([A-Za-z_][A-Za-z0-9_]*)?\\s*\\[");
			const auto				end = std::sregex_iterator();
			for (auto it = std::sregex_iterator(code.begin(), code.end(), kUniform); it != end; ++it) {
				const size_t	  at = static_cast<size_t>(it->position());
				const std::string after = code.substr(at + it->length());
				std::smatch		  name;
				if (!std::regex_search(after, name, kBlockName, std::regex_constants::match_continuous)) {
					error = hook.fileName + " declares a uniform outside a block; a hook's uniforms go in its one std140 block";
					return false;
				}
				if (!hook.block.empty()) {
					error = hook.fileName + " declares more than one uniform block; a hook may declare one";
					return false;
				}
				const std::string before = code.substr(0, at);
				std::smatch		  layout;
				const bool		  std140 = std::regex_search(before, layout, kLayout) && std::regex_search(layout[1].str(), kStd140);
				if (!std140) {
					error = hook.fileName + ": uniform block " + name[1].str() + " is not layout(std140)";
					return false;
				}
				// Members hold no braces, so the first one closes the block.
				const size_t close = after.find('}', static_cast<size_t>(name.length()));
				if (close != std::string::npos &&
					std::regex_search(after.substr(close + 1), kInstanceArray, std::regex_constants::match_continuous)) {
					error = hook.fileName + ": uniform block " + name[1].str() + " is an array; a hook's block is one instance";
					return false;
				}
				hook.block = name[1].str();
			}
			return true;
		}

		// Resolves includes depth first into `out`, each file once per stage.
		struct Resolver {
			const ReadFile&					  readFile;
			const std::optional<std::string>& hooks;
			std::set<std::filesystem::path>	  included;
			bool							  hooksIncluded = false;
			std::string						  error;

			bool resolve(const std::filesystem::path& file, const std::string& site, std::string& out) {
				const std::optional<std::string> text = readFile(file);
				if (!text.has_value()) {
					error = site + "cannot read " + file.generic_string();
					return false;
				}
				static const std::regex kInclude("^\\s*#\\s*include\\s*\"([^\"]+)\"\\s*$");
				std::istringstream		lines(withUnixNewlines(*text));
				std::string				line;
				int						number = 0;
				while (std::getline(lines, line)) {
					++number;
					std::smatch match;
					if (!std::regex_match(line, match, kInclude)) {
						out += line;
						out += '\n';
						continue;
					}
					const std::string target = match[1].str();
					const std::string here = file.filename().generic_string() + ":" + std::to_string(number) + ": ";
					if (target == kHooksInclude) {
						if (!hooks.has_value()) {
							error = here + "hooks.glsl is for base.vert alone; only vertex hooks exist";
							return false;
						}
						if (hooksIncluded) {
							error = here + "hooks.glsl is included twice";
							return false;
						}
						hooksIncluded = true;
						out += "// BEGIN INCLUDE: hooks.glsl, generated\n";
						out += *hooks;
						out += "// END INCLUDE: hooks.glsl\n";
						continue;
					}
					const std::filesystem::path path = (file.parent_path() / target).lexically_normal();
					if (!included.insert(path).second) {
						error = here + target + " is included twice";
						return false;
					}
					out += "// BEGIN INCLUDE: " + target + "\n";
					if (!resolve(path, here, out)) {
						return false;
					}
					out += "// END INCLUDE: " + target + "\n";
				}
				return true;
			}
		};

		// A block member's part of its C++ constant: "Block.windDir" is WindDir, "Block.s.x" SX, and an
		// array's [0] goes.
		std::string memberSuffix(const std::string& member, const std::string& block) {
			const std::string name = member.starts_with(block + ".") ? member.substr(block.size() + 1) : member;
			std::string		  out;
			bool			  upper = true;
			bool			  inIndex = false;
			for (const char c : name) {
				if (c == '[' || c == ']') {
					inIndex = c == '[';
					continue;
				}
				if (inIndex) {
					continue;
				}
				if (c == '.' || c == '_') {
					upper = true;
					continue;
				}
				out += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
				upper = false;
			}
			return out;
		}

		uint32_t paddedTo16(uint32_t size) {
			return (size + 15U) / 16U * 16U;
		}

		// `text` as raw string literals of at most kPieceBytes each (a longer line gets a piece of its
		// own), one per line of the generated source.
		std::string rawPieces(const std::string& text) {
			std::string out;
			size_t		start = 0;
			while (start < text.size()) {
				size_t end = start;
				while (end < text.size()) {
					const size_t newline = text.find('\n', end);
					const size_t next = newline == std::string::npos ? text.size() : newline + 1;
					if (next - start > kPieceBytes && end > start) {
						break;
					}
					end = next;
				}
				out += "\t\t\t";
				out += kRawOpen;
				out += text.substr(start, end - start);
				out += kRawClose;
				out += ",\n";
				start = end;
			}
			if (out.empty()) {
				out = "\t\t\t" + std::string(kRawOpen) + std::string(kRawClose) + ",\n";
			}
			return out;
		}
	} // namespace

	Hooks readHooks(std::vector<HookFile> files) {
		std::sort(files.begin(), files.end(), [](const HookFile& a, const HookFile& b) { return a.fileName < b.fileName; });
		Hooks				  result;
		uint32_t			  nextBinding = 1;
		std::set<std::string> blocks{std::string(kDrawUniformsBlock)};
		std::set<std::string> constants;
		for (HookFile& file : files) {
			if (file.fileName.starts_with('.')) {
				continue;
			}
			if (!file.fileName.ends_with(kHookSuffix)) {
				result.error = file.fileName + " is not a vertex hook: a hook is <name>.vert.glsl, and only vertex hooks exist";
				return result;
			}
			Hook hook{
				.name = file.fileName.substr(0, file.fileName.size() - kHookSuffix.size()),
				.fileName = file.fileName,
				.text = withUnixNewlines(std::move(file.text)),
			};
			if (!isHookName(hook.name)) {
				result.error = file.fileName + ": \"" + hook.name +
							   "\" is not a GLSL identifier (letters, digits and underscores, not starting with a digit), or its "
							   "generated names would be reserved (gl, a gl_ prefix, a trailing or double underscore)";
				return result;
			}
			const std::string code = withoutComments(hook.text);
			hook.instanceLocal = defines(code, hook.name + "_instanceLocal");
			hook.instanceWorld = defines(code, hook.name + "_instanceWorld");
			if (!hook.instanceLocal && !hook.instanceWorld) {
				result.error =
					file.fileName + " defines no hook point: neither " + hook.name + "_instanceLocal nor " + hook.name + "_instanceWorld";
				return result;
			}
			if (!checkDirectives(code, hook, result.error) || !readBlock(code, hook, result.error)) {
				return result;
			}
			if (!hook.block.empty()) {
				if (!blocks.insert(hook.block).second) {
					result.error =
						file.fileName + ": uniform block " + hook.block + " is already declared by the base shader or another hook";
					return result;
				}
				if (!constants.insert(constantName(hook.name)).second) {
					result.error = file.fileName + ": its block's C++ constant, " + constantName(hook.name) + ", is another hook's too";
					return result;
				}
				if (nextBinding > kMaxHookBlocks) {
					result.error = file.fileName + ": more than " + std::to_string(kMaxHookBlocks) +
								   " hooks declare a uniform block; GL 3.3 promises 12 blocks a stage, one of them DrawUniforms";
					return result;
				}
				hook.binding = nextBinding++;
			}
			result.hooks.push_back(std::move(hook));
		}
		return result;
	}

	std::string hookChain(const std::vector<Hook>& hooks) {
		std::string out = "// The consumer's hooks (salal docs/shader-hooks.md), in file-name order, then the chain base.vert calls.\n";
		for (const Hook& hook : hooks) {
			out += "\n// --- hook " + hook.fileName + " ---\n";
			out += hook.text;
			if (!hook.text.ends_with('\n')) {
				out += '\n';
			}
			out += "// --- end of hook " + hook.fileName + " ---\n";
		}
		out += "\n// An instanced vertex in mesh space, before the instance's rotation, scale and translation.\n";
		out += "vec2 applyInstanceLocalHooks(vec2 position, vec2 instancePosition) {\n";
		for (const Hook& hook : hooks) {
			if (hook.instanceLocal) {
				out += "\tposition = " + hook.name + "_instanceLocal(position, instancePosition);\n";
			}
		}
		out += "\treturn position;\n}\n";
		out += "\n// The same vertex in world space, after the instance transform and before world to screen.\n";
		out += "vec2 applyInstanceWorldHooks(vec2 position, vec2 localPosition, vec2 instancePosition) {\n";
		for (const Hook& hook : hooks) {
			if (hook.instanceWorld) {
				out += "\tposition = " + hook.name + "_instanceWorld(position, localPosition, instancePosition);\n";
			}
		}
		out += "\treturn position;\n}\n";
		return out;
	}

	Assembled assemble(const std::filesystem::path& file, const ReadFile& readFile, const std::optional<std::string>& hooks) {
		Resolver  resolver{.readFile = readFile, .hooks = hooks};
		Assembled result;
		resolver.included.insert(file.lexically_normal());
		if (!resolver.resolve(file, "", result.text)) {
			return {.error = resolver.error};
		}
		if (hooks.has_value() && !resolver.hooksIncluded) {
			return {.error = file.filename().generic_string() + " never includes hooks.glsl, where the hooks go"};
		}
		return result;
	}

	std::map<std::string, ReflectedBlock> reflectBlocks(const std::string& reflection) {
		// "<name>: offset <n>, type <hex>, size <count>, index <block>, ..., arrayStride <n>" in the
		// uniform section, "<name>: ..., size <bytes>, index <n>, ..." in the block section.
		static const std::regex kEntry(
			"^([A-Za-z_][A-Za-z0-9_.\\[\\]]*): offset (-?[0-9]+), type ([0-9a-fA-F]+), size ([0-9]+), index (-?[0-9]+)(.*)$"
		);
		static const std::regex kStride("\\barrayStride ([0-9]+)\\b");
		// GL type codes glslang reports, with the GLSL name and std140 size of one of them.
		struct GlType {
			uint32_t	code;
			const char* name;
			uint32_t	bytes;
		};
		static constexpr GlType kTypes[] = {
			{0x1406, "float", 4},  {0x8B50, "vec2", 8},	  {0x8B51, "vec3", 12},	 {0x8B52, "vec4", 16}, {0x1404, "int", 4},
			{0x8B53, "ivec2", 8},  {0x8B54, "ivec3", 12}, {0x8B55, "ivec4", 16}, {0x1405, "uint", 4},  {0x8DC6, "uvec2", 8},
			{0x8DC7, "uvec3", 12}, {0x8DC8, "uvec4", 16}, {0x8B56, "bool", 4},	 {0x8B57, "bvec2", 8}, {0x8B58, "bvec3", 12},
			{0x8B59, "bvec4", 16}, {0x8B5A, "mat2", 32},  {0x8B5B, "mat3", 48},	 {0x8B5C, "mat4", 64},
		};

		struct Entry {
			std::string name;
			uint32_t	offset = 0;
			uint32_t	type = 0;
			uint32_t	count = 0;
			int			index = -1;
			uint32_t	stride = 0;
		};
		enum class Section : uint8_t { Other, Uniforms, Blocks };
		std::vector<Entry>					  uniforms;
		std::map<int, std::string>			  blockNames;
		std::map<std::string, ReflectedBlock> blocks;
		std::istringstream					  lines(withUnixNewlines(reflection));
		std::string							  line;
		Section								  section = Section::Other;
		while (std::getline(lines, line)) {
			if (line.ends_with("reflection:")) {
				section = line == "Uniform reflection:"			? Section::Uniforms
						  : line == "Uniform block reflection:" ? Section::Blocks
																: Section::Other;
				continue;
			}
			std::smatch match;
			if (section == Section::Other || !std::regex_match(line, match, kEntry)) {
				continue;
			}
			Entry entry{
				.name = match[1].str(),
				.offset = static_cast<uint32_t>(std::max(0L, std::stol(match[2].str()))),
				.type = static_cast<uint32_t>(std::stoul(match[3].str(), nullptr, 16)),
				.count = static_cast<uint32_t>(std::stoul(match[4].str())),
				.index = std::stoi(match[5].str()),
			};
			const std::string rest = match[6].str();
			std::smatch		  stride;
			if (std::regex_search(rest, stride, kStride)) {
				entry.stride = static_cast<uint32_t>(std::stoul(stride[1].str()));
			}
			if (section == Section::Blocks) {
				blockNames[entry.index] = entry.name;
				blocks[entry.name].size = entry.count; // a block's "size" is its bytes
			} else {
				uniforms.push_back(std::move(entry));
			}
		}

		for (const Entry& uniform : uniforms) {
			const auto block = blockNames.find(uniform.index);
			if (block == blockNames.end()) {
				continue; // outside any block: a sampler
			}
			ReflectedMember member{.name = uniform.name, .offset = uniform.offset};
			const auto type = std::find_if(std::begin(kTypes), std::end(kTypes), [&](const GlType& t) { return t.code == uniform.type; });
			if (type != std::end(kTypes)) {
				member.type = type->name;
				member.size = type->bytes;
				if (uniform.count > 1) {
					const uint32_t stride = uniform.stride != 0 ? uniform.stride : paddedTo16(type->bytes);
					member.type += "[" + std::to_string(uniform.count) + "]";
					member.size = stride * uniform.count;
				}
			}
			blocks[block->second].members.push_back(std::move(member));
		}
		for (auto& [name, block] : blocks) {
			std::sort(block.members.begin(), block.members.end(), [](const ReflectedMember& a, const ReflectedMember& b) {
				return a.offset < b.offset;
			});
		}
		return blocks;
	}

	std::string constantName(const std::string& hookName) {
		std::string name = "k";
		bool		upper = true;
		for (const char c : hookName) {
			if (c == '_') {
				upper = true;
				continue;
			}
			name += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
			upper = false;
		}
		return name;
	}

	Embedded embed(const EmbedArgs& args) {
		if (args.vertex.find(kRawClose) != std::string::npos || args.fragment.find(kRawClose) != std::string::npos) {
			return {.error = "the assembled base shader contains )glsl\", which ends the raw string that embeds it"};
		}

		std::string			  hookList;
		std::string			  constants;
		std::string			  blockList;
		size_t				  blockCount = 0;
		std::set<std::string> names;
		for (const Hook& hook : args.hooks) {
			hookList += (hookList.empty() ? " " : ", ") + hook.fileName;
			if (hook.block.empty()) {
				continue;
			}
			const auto reflected = args.blocks.find(hook.block);
			if (reflected == args.blocks.end()) {
				return {
					.error = hook.fileName + ": uniform block " + hook.block +
							 " is not in the linked program; glslangValidator drops a block nothing reads"
				};
			}
			const std::string blockConstant = constantName(hook.name);
			const std::string membersConstant = blockConstant + "Members";
			const std::string padded = std::to_string(paddedTo16(reflected->second.size));
			const std::string binding = std::to_string(hook.binding);
			if (!names.insert(blockConstant).second || !names.insert(membersConstant).second) {
				return {
					.error = hook.fileName + ": its C++ constants, " + blockConstant + " and " + membersConstant + ", are already taken"
				};
			}
			constants += "\n\t// " + hook.fileName + "'s uniform block " + hook.block + ": binding point " + binding + ", " + padded +
						 " bytes (std140, padded to 16), and its members' std140 offsets and sizes in bytes.\n";
			std::string memberList;
			for (const ReflectedMember& member : reflected->second.members) {
				const std::string constant = blockConstant + memberSuffix(member.name, hook.block);
				if (member.type.empty()) {
					return {
						.error = hook.fileName + ": member " + member.name + " of " + hook.block + " has a type the build doesn't handle"
					};
				}
				if (!names.insert(constant).second) {
					return {.error = hook.fileName + ": member " + member.name + "'s C++ constant, " + constant + ", is already taken"};
				}
				constants += "\tinline constexpr Renderer::backend::HookUniformMember " + constant + "{.name = \"" + member.name +
							 "\", .type = \"" + member.type + "\", .offset = " + std::to_string(member.offset) +
							 ", .size = " + std::to_string(member.size) + "};\n";
				memberList += (memberList.empty() ? "" : ", ") + constant;
			}
			constants += "\tinline constexpr Renderer::backend::HookUniformMember " + membersConstant + "[] = {" + memberList + "};\n";
			constants += "\tinline constexpr Renderer::backend::HookUniformBlock " + blockConstant + "{\n\t\t.hook = \"" + hook.name +
						 "\",\n\t\t.block = \"" + hook.block + "\",\n\t\t.binding = " + binding + ",\n\t\t.size = " + padded +
						 ",\n\t\t.members = " + membersConstant + ",\n\t};\n";
			blockList += (blockList.empty() ? "" : ", ") + blockConstant;
			++blockCount;
		}
		if (hookList.empty()) {
			hookList = " none";
		}

		Embedded result;
		result.header = "// Generated by salal_base_shader() from base.vert, base.frag and the hooks below; do not edit\n"
						"// (salal docs/shader-hooks.md). Hooks, in chain order:" +
						hookList +
						".\n"
						"#pragma once\n\n"
						"#include \"backend/Backend.h\"\n\n"
						"namespace " +
						args.nameSpace +
						" {\n\n"
						"\t// The assembled base shader, its two stages checked by glslangValidator: what Primitives::init() takes.\n"
						"\tconst Renderer::backend::ShaderSource& source();\n" +
						constants + "\n} // namespace " + args.nameSpace + "\n";

		std::string blocks;
		std::string hookBlocks = "{}";
		if (blockCount > 0) {
			blocks = "\t\tconstexpr std::array<Renderer::backend::HookUniformBlock, " + std::to_string(blockCount) + "> kHookBlocks = {" +
					 blockList + "};\n\n";
			hookBlocks = "kHookBlocks";
		}
		result.source = "// Generated by salal_base_shader(); do not edit.\n\n"
						"#include \"" +
						args.header +
						"\"\n\n"
						"#include <array>\n"
						"#include <span>\n"
						"#include <string>\n"
						"#include <string_view>\n\n"
						"namespace " +
						args.nameSpace +
						" {\n\n"
						"\tnamespace {\n"
						"\t\t// In pieces: MSVC refuses a string literal much past 16 KB.\n"
						"\t\tconstexpr std::string_view kVertex[] = {\n" +
						rawPieces(args.vertex) +
						"\t\t};\n"
						"\t\tconstexpr std::string_view kFragment[] = {\n" +
						rawPieces(args.fragment) + "\t\t};\n\n" + blocks +
						"\t\tstd::string join(std::span<const std::string_view> pieces) {\n"
						"\t\t\tstd::string text;\n"
						"\t\t\tfor (const std::string_view piece : pieces) {\n"
						"\t\t\t\ttext += piece;\n"
						"\t\t\t}\n"
						"\t\t\treturn text;\n"
						"\t\t}\n"
						"\t} // namespace\n\n"
						"\tconst Renderer::backend::ShaderSource& source() {\n"
						"\t\tstatic const std::string vertex = join(kVertex);\n"
						"\t\tstatic const std::string fragment = join(kFragment);\n"
						"\t\tstatic const Renderer::backend::ShaderSource kSource{\n"
						"\t\t\t.vertex = vertex.c_str(),\n"
						"\t\t\t.fragment = fragment.c_str(),\n"
						"\t\t\t.hookBlocks = " +
						hookBlocks +
						",\n"
						"\t\t};\n"
						"\t\treturn kSource;\n"
						"\t}\n\n"
						"} // namespace " +
						args.nameSpace + "\n";
		return result;
	}

} // namespace Renderer::assembler
