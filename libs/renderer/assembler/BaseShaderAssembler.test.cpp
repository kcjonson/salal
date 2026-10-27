// The base shader's build step, run on text in memory: reading a hooks folder, the chain
// it generates, include resolution and the generated header. Headless; no GL, no files.

#include "assembler/BaseShaderAssembler.h"

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

namespace Renderer::assembler {

	namespace {
		// A hook defining both points, with a block when `block` is given.
		HookFile hookFile(const std::string& name, const std::string& block = "") {
			std::string text;
			if (!block.empty()) {
				text += "layout(std140) uniform " + block + " {\n\tvec2 shift;\n} " + name + ";\n";
			}
			text += "vec2 " + name + "_instanceLocal(vec2 position, vec2 instancePosition) { return position; }\n";
			text += "vec2 " + name + "_instanceWorld(vec2 position, vec2 localPosition, vec2 instancePosition) { return position; }\n";
			return {.fileName = name + ".vert.glsl", .text = text};
		}

		std::string errorFor(std::vector<HookFile> files) {
			return readHooks(std::move(files)).error;
		}

		// A folder of files in memory, by generic path.
		ReadFile reader(const std::map<std::string, std::string>& files) {
			return [files](const std::filesystem::path& path) -> std::optional<std::string> {
				const auto found = files.find(path.generic_string());
				return found != files.end() ? std::optional<std::string>(found->second) : std::nullopt;
			};
		}
	} // namespace

	TEST(ReadHooksTest, TheyComeInFileNameOrderAndTheirBlocksNumberFromOne) {
		const Hooks read = readHooks({hookFile("wind", "WindUniforms"), hookFile("bend"), hookFile("aura", "AuraUniforms")});
		ASSERT_TRUE(read.error.empty()) << read.error;
		ASSERT_EQ(read.hooks.size(), 3U);
		EXPECT_EQ(read.hooks[0].name, "aura");
		EXPECT_EQ(read.hooks[0].block, "AuraUniforms");
		EXPECT_EQ(read.hooks[0].binding, 1U);
		EXPECT_EQ(read.hooks[1].name, "bend");
		EXPECT_TRUE(read.hooks[1].block.empty());
		EXPECT_EQ(read.hooks[1].binding, 0U);
		EXPECT_EQ(read.hooks[2].name, "wind");
		EXPECT_EQ(read.hooks[2].binding, 2U);
		EXPECT_TRUE(read.hooks[2].instanceLocal);
		EXPECT_TRUE(read.hooks[2].instanceWorld);
	}

	TEST(ReadHooksTest, AnEmptyFolderMakesAnEmptyChain) {
		const Hooks read = readHooks({});
		EXPECT_TRUE(read.error.empty());
		EXPECT_TRUE(read.hooks.empty());
		const std::string chain = hookChain(read.hooks);
		EXPECT_NE(
			chain.find("vec2 applyInstanceLocalHooks(vec2 position, vec2 instancePosition) {\n\treturn position;\n}"), std::string::npos
		) << chain;
		EXPECT_NE(
			chain.find("vec2 applyInstanceWorldHooks(vec2 position, vec2 localPosition, vec2 instancePosition) {\n\treturn position;\n}"),
			std::string::npos
		) << chain;
	}

	TEST(ReadHooksTest, AFileThatIsNotAVertexHookFailsTheBuild) {
		EXPECT_NE(
			errorFor({hookFile("sway"), {.fileName = "sway.frag.glsl", .text = ""}}).find("sway.frag.glsl is not a vertex hook"),
			std::string::npos
		);
		EXPECT_NE(errorFor({{.fileName = "notes.txt", .text = ""}}).find("notes.txt is not a vertex hook"), std::string::npos);
		EXPECT_NE(errorFor({{.fileName = "sway.vert", .text = ""}}).find("is not a vertex hook"), std::string::npos);
	}

	TEST(ReadHooksTest, ANameThatIsNotAGlslIdentifierFailsTheBuild) {
		// sway_ and gl would make the reserved sway__instanceLocal and gl_instanceLocal
		for (const char* name : {"2sway", "tall-sway", "gl_sway", "tall__sway", "sway_", "gl"}) {
			HookFile file = hookFile(name);
			EXPECT_NE(errorFor({file}).find("is not a GLSL identifier"), std::string::npos) << name;
		}
		EXPECT_TRUE(errorFor({hookFile("_tall_sway2")}).empty());
	}

	TEST(ReadHooksTest, DotFilesAreSkipped) {
		const Hooks read = readHooks({{.fileName = ".DS_Store", .text = "\x01\x02"}, hookFile("sway")});
		EXPECT_TRUE(read.error.empty()) << read.error;
		ASSERT_EQ(read.hooks.size(), 1U);
	}

	TEST(ReadHooksTest, AHookMustDefineAHookPoint) {
		const HookFile file{
			.fileName = "sway.vert.glsl", .text = "// vec2 sway_instanceLocal(vec2 p, vec2 i) {}\nvec2 sway_helper(vec2 p) { return p; }\n"
		};
		EXPECT_NE(errorFor({file}).find("defines no hook point"), std::string::npos);

		const Hooks read =
			readHooks({{.fileName = "sway.vert.glsl", .text = "vec2 sway_instanceWorld(vec2 p, vec2 l, vec2 i) { return p; }\n"}});
		ASSERT_EQ(read.hooks.size(), 1U);
		EXPECT_FALSE(read.hooks[0].instanceLocal);
		EXPECT_TRUE(read.hooks[0].instanceWorld);
	}

	TEST(ReadHooksTest, UniformsGoInOneStd140Block) {
		const std::string points = "vec2 sway_instanceLocal(vec2 p, vec2 i) { return p; }\n";
		EXPECT_NE(
			errorFor({{.fileName = "sway.vert.glsl", .text = "uniform float sway_scale;\n" + points}}).find("uniform outside a block"),
			std::string::npos
		);
		EXPECT_NE(
			errorFor(
				{{.fileName = "sway.vert.glsl", .text = "uniform SwayUniforms { float scale; };\n" + points}}
			).find("not layout(std140)"),
			std::string::npos
		);
		EXPECT_NE(
			errorFor(
				{{.fileName = "sway.vert.glsl",
				  .text = "layout(std140) uniform A { float a; };\nlayout(std140) uniform B { float b; };\n" + points}}
			).find("more than one uniform block"),
			std::string::npos
		);
		EXPECT_NE(errorFor({hookFile("sway", "DrawUniforms")}).find("already declared"), std::string::npos);
		EXPECT_NE(errorFor({hookFile("sway", "Shared"), hookFile("tilt", "Shared")}).find("already declared"), std::string::npos);
		// In a comment it is not a declaration.
		EXPECT_TRUE(errorFor({{.fileName = "sway.vert.glsl", .text = "/* uniform float x; */\n" + points}}).empty());
	}

	TEST(ReadHooksTest, AHookBringsNoVersionExtensionOrBinding) {
		const std::string points = "vec2 sway_instanceLocal(vec2 p, vec2 i) { return p; }\n";
		EXPECT_NE(
			errorFor(
				{{.fileName = "sway.vert.glsl", .text = "#extension GL_ARB_shading_language_420pack : enable\n" + points}}
			).find("sway.vert.glsl:1: #extension in a hook"),
			std::string::npos
		);
		EXPECT_NE(
			errorFor({{.fileName = "sway.vert.glsl", .text = "\n  #  version 410 core\n" + points}}).find("sway.vert.glsl:2: #version"),
			std::string::npos
		);
		EXPECT_NE(
			errorFor(
				{{.fileName = "sway.vert.glsl", .text = "layout(std140, binding = 5) uniform SwayUniforms { float s; } sway;\n" + points}}
			).find("gives a binding"),
			std::string::npos
		);
		// In a comment it is neither.
		EXPECT_TRUE(
			errorFor({{.fileName = "sway.vert.glsl", .text = "// #extension GL_X : enable, layout(binding = 1)\n" + points}}).empty()
		);
	}

	TEST(ReadHooksTest, AnArrayOfBlocksFailsTheBuild) {
		const std::string points = "vec2 sway_instanceLocal(vec2 p, vec2 i) { return p; }\n";
		EXPECT_NE(
			errorFor(
				{{.fileName = "sway.vert.glsl", .text = "layout(std140) uniform SwayUniforms { float s; } sway[2];\n" + points}}
			).find("uniform block SwayUniforms is an array"),
			std::string::npos
		);
		EXPECT_TRUE(errorFor(
						{{.fileName = "sway.vert.glsl", .text = "layout(std140) uniform SwayUniforms { float s[2]; } sway;\n" + points}}
		).empty());
	}

	TEST(ReadHooksTest, AByteOrderMarkIsDropped) {
		HookFile file = hookFile("sway");
		file.text = "\xEF\xBB\xBF" + file.text;
		const Hooks read = readHooks({file});
		ASSERT_TRUE(read.error.empty()) << read.error;
		EXPECT_TRUE(read.hooks[0].text.starts_with("vec2 sway_instanceLocal"));
	}

	TEST(ReadHooksTest, NoMoreBlocksThanAStageHolds) {
		std::vector<HookFile> files;
		for (int i = 0; i < static_cast<int>(kMaxHookBlocks) + 1; ++i) {
			const std::string name = "hook" + std::to_string(10 + i);
			files.push_back(hookFile(name, "Block" + std::to_string(i)));
		}
		EXPECT_NE(errorFor(files).find("more than 11 hooks declare a uniform block"), std::string::npos);
		files.pop_back();
		EXPECT_TRUE(errorFor(files).empty());
	}

	TEST(HookChainTest, ItCallsEachHooksPointsInFileNameOrder) {
		const Hooks read = readHooks({
			hookFile("wind"),
			{.fileName = "bend.vert.glsl", .text = "vec2 bend_instanceWorld(vec2 p, vec2 l, vec2 i) { return p; }"},
			hookFile("aura"),
		});
		ASSERT_TRUE(read.error.empty()) << read.error;
		const std::string chain = hookChain(read.hooks);

		// The hooks' text first, in order, the last one given a newline it lacked.
		EXPECT_LT(chain.find("// --- hook aura.vert.glsl ---"), chain.find("// --- hook bend.vert.glsl ---"));
		EXPECT_LT(chain.find("// --- hook bend.vert.glsl ---"), chain.find("// --- hook wind.vert.glsl ---"));
		EXPECT_NE(chain.find("{ return p; }\n// --- end of hook bend.vert.glsl ---"), std::string::npos);

		const size_t local = chain.find("vec2 applyInstanceLocalHooks(");
		const size_t world = chain.find("vec2 applyInstanceWorldHooks(");
		ASSERT_LT(chain.find("// --- end of hook wind.vert.glsl ---"), local);
		ASSERT_LT(local, world);
		EXPECT_EQ(
			chain.substr(local),
			"vec2 applyInstanceLocalHooks(vec2 position, vec2 instancePosition) {\n"
			"\tposition = aura_instanceLocal(position, instancePosition);\n"
			"\tposition = wind_instanceLocal(position, instancePosition);\n"
			"\treturn position;\n"
			"}\n"
			"\n"
			"// The same vertex in world space, after the instance transform and before world to screen.\n"
			"vec2 applyInstanceWorldHooks(vec2 position, vec2 localPosition, vec2 instancePosition) {\n"
			"\tposition = aura_instanceWorld(position, localPosition, instancePosition);\n"
			"\tposition = bend_instanceWorld(position, localPosition, instancePosition);\n"
			"\tposition = wind_instanceWorld(position, localPosition, instancePosition);\n"
			"\treturn position;\n"
			"}\n"
		);
	}

	TEST(AssembleTest, IncludesResolveRelativeToTheIncludingFileAndTheHooksGoWhereBaseVertSays) {
		const ReadFile	read = reader({
			 {"shaders/base.vert", "#version 330 core\r\n#include \"includes/a.glsl\"\r\n#include \"hooks.glsl\"\r\nvoid main() {}\r\n"},
			 {"shaders/includes/a.glsl", "float a;\n  #  include \"b.glsl\"\n"},
			 {"shaders/includes/b.glsl",
			  "\xEF\xBB\xBF"
			   "float b;"}, // a byte-order mark goes
		 });
		const Assembled vertex = assemble("shaders/base.vert", read, std::string("float hooks;\n"));
		ASSERT_TRUE(vertex.error.empty()) << vertex.error;
		EXPECT_EQ(
			vertex.text,
			"#version 330 core\n"
			"// BEGIN INCLUDE: includes/a.glsl\n"
			"float a;\n"
			"// BEGIN INCLUDE: b.glsl\n"
			"float b;\n"
			"// END INCLUDE: b.glsl\n"
			"// END INCLUDE: includes/a.glsl\n"
			"// BEGIN INCLUDE: hooks.glsl, generated\n"
			"float hooks;\n"
			"// END INCLUDE: hooks.glsl\n"
			"void main() {}\n"
		);
	}

	TEST(AssembleTest, ItRefusesAMissingARepeatedOrAMisplacedInclude) {
		EXPECT_NE(
			assemble("base.vert", reader({{"base.vert", "#include \"gone.glsl\"\n#include \"hooks.glsl\"\n"}}), std::string())
				.error.find("base.vert:1: cannot read gone.glsl"),
			std::string::npos
		);
		EXPECT_NE(
			assemble(
				"base.vert",
				reader({{"base.vert", "#include \"a.glsl\"\n#include \"a.glsl\"\n#include \"hooks.glsl\"\n"}, {"a.glsl", ""}}),
				std::string()
			)
				.error.find("a.glsl is included twice"),
			std::string::npos
		);
		EXPECT_NE(
			assemble("base.vert", reader({{"base.vert", "#include \"hooks.glsl\"\n#include \"hooks.glsl\"\n"}}), std::string())
				.error.find("hooks.glsl is included twice"),
			std::string::npos
		);
		// The fragment stage has no hooks, and the vertex stage must take them.
		EXPECT_NE(
			assemble("base.frag", reader({{"base.frag", "#include \"hooks.glsl\"\n"}}), std::nullopt)
				.error.find("hooks.glsl is for base.vert alone"),
			std::string::npos
		);
		EXPECT_NE(
			assemble("base.vert", reader({{"base.vert", "void main() {}\n"}}), std::string()).error.find("never includes hooks.glsl"),
			std::string::npos
		);
	}

	TEST(ReflectionTest, BlocksCarryTheirSizeAndMembersInOffsetOrder) {
		// glslangValidator -q --reflect-all-block-variables, as it prints it, with a line it doesn't.
		const std::string report =
			"base.vert\nbase.frag\nUniform reflection:\n"
			"OffsetUniforms.world: offset 8, type 8b50, size 1, index 0, binding -1, stages 1\n"
			"OffsetUniforms.local: offset 0, type 8b50, size 1, index 0, binding -1, stages 1\n"
			"TestUniforms.arr: offset 32, type 8b52, size 3, index 2, binding -1, stages 0, arrayStride 16, topLevelArrayStride 16\n"
			"TestUniforms.farr: offset 80, type 1406, size 2, index 2, binding -1, stages 0, arrayStride 16, topLevelArrayStride 16\n"
			"TestUniforms.odd: offset 112, type 8b65, size 1, index 2, binding -1, stages 0\n"
			"u_projection: offset 0, type 8b5c, size 1, index 1, binding -1, stages 1\n"
			"u_atlas: offset -1, type 8b5e, size 1, index -1, binding -1, stages 16\n\n"
			"Uniform block reflection:\n"
			"OffsetUniforms: offset -1, type ffffffff, size 16, index 0, binding -1, stages 1, numMembers 2\n"
			"something glslang may print one day\n"
			"DrawUniforms: offset -1, type ffffffff, size 168, index 1, binding -1, stages 17, numMembers 10\n"
			"TestUniforms: offset -1, type ffffffff, size 136, index 2, binding -1, stages 1, numMembers 3\n\n"
			"Buffer variable reflection:\n\nBuffer block reflection:\n";
		const std::map<std::string, ReflectedBlock> blocks = reflectBlocks(report);
		ASSERT_EQ(blocks.size(), 3U);
		EXPECT_EQ(blocks.at("DrawUniforms").size, 168U);
		EXPECT_EQ(blocks.at("DrawUniforms").members.size(), 1U);
		const ReflectedBlock& offset = blocks.at("OffsetUniforms");
		EXPECT_EQ(offset.size, 16U);
		ASSERT_EQ(offset.members.size(), 2U);
		EXPECT_EQ(offset.members[0].name, "OffsetUniforms.local");
		EXPECT_EQ(offset.members[0].type, "vec2");
		EXPECT_EQ(offset.members[0].offset, 0U);
		EXPECT_EQ(offset.members[0].size, 8U);
		EXPECT_EQ(offset.members[1].name, "OffsetUniforms.world");
		const ReflectedBlock& test = blocks.at("TestUniforms");
		ASSERT_EQ(test.members.size(), 3U);
		EXPECT_EQ(test.members[0].type, "vec4[3]");
		EXPECT_EQ(test.members[0].size, 48U);
		EXPECT_EQ(test.members[1].type, "float[2]"); // std140 strides a float array at 16
		EXPECT_EQ(test.members[1].size, 32U);
		EXPECT_TRUE(test.members[2].type.empty()); // mat2x3, which the step doesn't handle
	}

	TEST(EmbedTest, TheHeaderCarriesEachBlocksBindingAndPaddedSize) {
		const Hooks read = readHooks({hookFile("wind_sway", "WindUniforms"), hookFile("bend"), hookFile("aura", "AuraUniforms")});
		ASSERT_TRUE(read.error.empty()) << read.error;
		const Embedded embedded = embed({
			.nameSpace = "game_shader",
			.header = "GameShader.h",
			.vertex = "vertex text\n",
			.fragment = "fragment text\n",
			.hooks = read.hooks,
			.blocks = {
				{"AuraUniforms", {.size = 4, .members = {{.name = "AuraUniforms.glow", .type = "float", .offset = 0, .size = 4}}}},
				{"WindUniforms",
				 {.size = 28,
				  .members =
					  {
						  {.name = "WindUniforms.gust", .type = "vec2", .offset = 0, .size = 8},
						  {.name = "WindUniforms.dir_x", .type = "float", .offset = 24, .size = 4},
					  }}},
				{"DrawUniforms", {.size = 168}},
			},
		});
		ASSERT_TRUE(embedded.error.empty()) << embedded.error;
		EXPECT_NE(embedded.header.find("namespace game_shader {"), std::string::npos);
		EXPECT_NE(embedded.header.find("const Renderer::backend::ShaderSource& source();"), std::string::npos);
		EXPECT_NE(embedded.header.find("Hooks, in chain order: aura.vert.glsl, bend.vert.glsl, wind_sway.vert.glsl."), std::string::npos);
		EXPECT_NE(
			embedded.header.find(
				"HookUniformBlock kAura{\n\t\t.hook = \"aura\",\n\t\t.block = \"AuraUniforms\",\n\t\t.binding = 1,\n\t\t.size = 16,\n\t\t."
				"members = kAuraMembers,\n\t};"
			),
			std::string::npos
		) << embedded.header;
		EXPECT_NE(
			embedded.header.find(
				"HookUniformBlock kWindSway{\n\t\t.hook = \"wind_sway\",\n\t\t.block = \"WindUniforms\",\n\t\t.binding = 2,\n\t\t.size = "
				"32,\n\t\t.members = kWindSwayMembers,\n\t};"
			),
			std::string::npos
		) << embedded.header;
		// Each member's constant, its offset and size for a static_assert, then the list the block points at.
		EXPECT_NE(
			embedded.header.find(
				"HookUniformMember kWindSwayDirX{.name = \"WindUniforms.dir_x\", .type = \"float\", .offset = 24, .size = 4};"
			),
			std::string::npos
		) << embedded.header;
		EXPECT_NE(embedded.header.find("HookUniformMember kWindSwayMembers[] = {kWindSwayGust, kWindSwayDirX};"), std::string::npos);
		EXPECT_NE(embedded.header.find("HookUniformMember kAuraGlow{"), std::string::npos);
		EXPECT_EQ(embedded.header.find("kBend"), std::string::npos); // no block, nothing to fill

		EXPECT_NE(embedded.source.find("#include \"GameShader.h\""), std::string::npos);
		EXPECT_NE(embedded.source.find("R\"glsl(vertex text\n)glsl\","), std::string::npos);
		EXPECT_NE(embedded.source.find("R\"glsl(fragment text\n)glsl\","), std::string::npos);
		EXPECT_NE(
			embedded.source.find("std::array<Renderer::backend::HookUniformBlock, 2> kHookBlocks = {kAura, kWindSway};"), std::string::npos
		);
		EXPECT_NE(embedded.source.find(".hookBlocks = kHookBlocks,"), std::string::npos);
	}

	TEST(EmbedTest, NoHooksMeansNoBlocks) {
		const Embedded embedded = embed({.nameSpace = "base_shader", .header = "BaseShader.h", .vertex = "v\n", .fragment = "f\n"});
		ASSERT_TRUE(embedded.error.empty()) << embedded.error;
		EXPECT_NE(embedded.header.find("Hooks, in chain order: none."), std::string::npos);
		EXPECT_NE(embedded.source.find(".hookBlocks = {},"), std::string::npos);
		EXPECT_EQ(embedded.source.find("kHookBlocks"), std::string::npos);
	}

	TEST(EmbedTest, LongSourcesArePiecedAndJoinBackWhole) {
		std::string text;
		for (int i = 0; i < 2000; ++i) {
			text += "float line" + std::to_string(i) + " = 1.0;\n";
		}
		const Embedded embedded = embed({.nameSpace = "n", .header = "H.h", .vertex = text, .fragment = "f\n"});
		ASSERT_TRUE(embedded.error.empty());
		// Every piece between R"glsl( and )glsl" of the vertex list, joined.
		const size_t begin = embedded.source.find("kVertex[] = {");
		const size_t end = embedded.source.find("kFragment[] = {");
		std::string	 joined;
		size_t		 pieces = 0;
		for (size_t at = embedded.source.find("R\"glsl(", begin); at < end; at = embedded.source.find("R\"glsl(", at)) {
			const size_t close = embedded.source.find(")glsl\"", at);
			const size_t length = close - (at + 7);
			EXPECT_LE(length, 8192U);
			joined += embedded.source.substr(at + 7, length);
			at = close;
			++pieces;
		}
		EXPECT_GT(pieces, 1U);
		EXPECT_EQ(joined, text);
	}

	TEST(EmbedTest, TheSourceIncludesItsHeaderByThePathItIsGiven) {
		const Embedded embedded = embed({.nameSpace = "n", .header = "include/game/Shader.h", .vertex = "v\n", .fragment = "f\n"});
		ASSERT_TRUE(embedded.error.empty()) << embedded.error;
		EXPECT_NE(embedded.source.find("#include \"include/game/Shader.h\""), std::string::npos);
	}

	TEST(EmbedTest, AMemberTheStepCantDescribeOrNameFailsTheBuild) {
		const Hooks	   read = readHooks({hookFile("aura", "AuraUniforms")});
		const Embedded unknown = embed(
			{.nameSpace = "n",
			 .header = "H.h",
			 .vertex = "v",
			 .fragment = "f",
			 .hooks = read.hooks,
			 .blocks = {{"AuraUniforms", {.size = 24, .members = {{.name = "AuraUniforms.m", .offset = 0, .size = 0}}}}}}
		);
		EXPECT_NE(unknown.error.find("member AuraUniforms.m of AuraUniforms has a type the build doesn't handle"), std::string::npos);
		const Embedded taken = embed(
			{.nameSpace = "n",
			 .header = "H.h",
			 .vertex = "v",
			 .fragment = "f",
			 .hooks = read.hooks,
			 .blocks = {
				 {"AuraUniforms",
				  {.size = 8,
				   .members = {
					   {.name = "AuraUniforms.glow_x", .type = "float", .offset = 0, .size = 4},
					   {.name = "AuraUniforms.glowX", .type = "float", .offset = 4, .size = 4},
				   }}}
			 }}
		);
		EXPECT_NE(taken.error.find("kAuraGlowX, is already taken"), std::string::npos);
	}

	TEST(EmbedTest, ABlockMissingFromTheLinkedProgramFailsTheBuild) {
		const Hooks	   read = readHooks({hookFile("aura", "AuraUniforms")});
		const Embedded embedded = embed({.nameSpace = "n", .header = "H.h", .vertex = "v", .fragment = "f", .hooks = read.hooks});
		EXPECT_NE(embedded.error.find("AuraUniforms is not in the linked program"), std::string::npos);
		const Embedded closing = embed({.nameSpace = "n", .header = "H.h", .vertex = "// )glsl\"\n", .fragment = "f"});
		EXPECT_FALSE(closing.error.empty());
	}

	TEST(ConstantNameTest, HookNamesBecomeKConstants) {
		EXPECT_EQ(constantName("offset"), "kOffset");
		EXPECT_EQ(constantName("wind_sway"), "kWindSway");
		EXPECT_EQ(constantName("_x2"), "kX2");
	}

} // namespace Renderer::assembler
