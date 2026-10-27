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

		std::string withUnixNewlines(std::string text) {
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

		// A GLSL identifier a hook may use as its prefix: gl_ and double underscores are reserved.
		bool isHookName(const std::string& name) {
			static const std::regex kIdentifier("[A-Za-z_][A-Za-z0-9_]*");
			return std::regex_match(name, kIdentifier) && !name.starts_with("gl_") && name.find("__") == std::string::npos;
		}

		bool defines(const std::string& code, const std::string& function) {
			return std::regex_search(code, std::regex("\\b" + function + "\\s*\\("));
		}

		// The hook's uniform block, into hook.block; false, with `error` set, when the hook has a
		// uniform outside a block, more than one block, or a block that is not std140.
		bool readBlock(const std::string& code, Hook& hook, std::string& error) {
			static const std::regex kUniform("\\buniform\\b");
			static const std::regex kBlockName("\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\{");
			static const std::regex kLayout("layout\\s*\\(([^)]*)\\)\\s*$");
			static const std::regex kStd140("\\bstd140\\b");
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
							   "\" is not a GLSL identifier (letters, digits and underscores, not starting with a digit), or it is "
							   "reserved (a gl_ prefix or a double underscore)";
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
			if (!readBlock(code, hook, result.error)) {
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

	std::map<std::string, uint32_t> reflectedBlockSizes(const std::string& reflection) {
		static const std::regex			kBlock("^([A-Za-z_][A-Za-z0-9_]*): .*\\bsize ([0-9]+)\\b.*$");
		std::map<std::string, uint32_t> sizes;
		std::istringstream				lines(withUnixNewlines(reflection));
		std::string						line;
		bool							inBlocks = false;
		while (std::getline(lines, line)) {
			if (line == "Uniform block reflection:") {
				inBlocks = true;
				continue;
			}
			if (!inBlocks) {
				continue;
			}
			std::smatch match;
			if (!std::regex_match(line, match, kBlock)) {
				break; // the blank line before the next section
			}
			sizes[match[1].str()] = static_cast<uint32_t>(std::stoul(match[2].str()));
		}
		return sizes;
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

		std::string hookList;
		std::string constants;
		std::string blockList;
		size_t		blockCount = 0;
		for (const Hook& hook : args.hooks) {
			hookList += (hookList.empty() ? " " : ", ") + hook.fileName;
			if (hook.block.empty()) {
				continue;
			}
			const auto size = args.blockSizes.find(hook.block);
			if (size == args.blockSizes.end()) {
				return {
					.error = hook.fileName + ": uniform block " + hook.block +
							 " is not in the linked program; glslangValidator drops a block nothing reads"
				};
			}
			const std::string padded = std::to_string(paddedTo16(size->second));
			const std::string binding = std::to_string(hook.binding);
			constants += "\n\t// " + hook.fileName + "'s uniform block " + hook.block + ": binding point " + binding + ", " + padded +
						 " bytes (std140, padded to 16).\n";
			constants += "\tinline constexpr Renderer::backend::HookUniformBlock " + constantName(hook.name) + "{\n\t\t.hook = \"" +
						 hook.name + "\",\n\t\t.block = \"" + hook.block + "\",\n\t\t.binding = " + binding + ",\n\t\t.size = " + padded +
						 ",\n\t};\n";
			blockList += (blockList.empty() ? "" : ", ") + constantName(hook.name);
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
