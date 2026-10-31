// salal_tokens_tool, the build step salal_tokens() runs (cmake/SalalTokens.cmake):
//
//   salal_tokens_tool <tokens dir> <namespace> <header include> <header out> <source out> [<base dir>]
//
// Loads the folder over the base folder (ui's tokens, for a game's) in every mode its resolver
// has, and writes the header of typed styles and tokens and the source holding its table. Any error
// in any mode, or a token some modes have and others don't, goes to stderr with exit status 1,
// which fails the build. A file is written only when its contents change.

#include "ui/theme/dtcg/Codegen.h"
#include "ui/theme/dtcg/Resolver.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

namespace {

	using namespace ui::dtcg;
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

	struct Folder {
		Layer					layer;
		std::vector<SourceFile> files;
	};

	// A tokens folder: every *.tokens.json and at most one *.resolver.json under it, by name relative
	// to it. Entries whose names start with a dot are left out; anything else is an error.
	std::optional<Folder> readFolder(const fs::path& dir, std::vector<Diagnostic>& diagnostics) {
		Folder			folder;
		std::error_code error;
		std::string		label = dir.generic_string();
		for (auto it = fs::recursive_directory_iterator(dir, error); !error && it != fs::recursive_directory_iterator();
			 it.increment(error)) {
			const std::string name = it->path().filename().generic_string();
			if (name.starts_with('.')) {
				if (it->is_directory()) {
					it.disable_recursion_pending();
				}
				continue;
			}
			if (it->is_directory()) {
				continue;
			}
			const std::string relative = fs::relative(it->path(), dir).generic_string();
			if (!name.ends_with(".tokens.json") && !name.ends_with(".resolver.json")) {
				diagnostics.push_back({.file = label + "/" + relative, .message = "is not a .tokens.json or .resolver.json file"});
				continue;
			}
			const std::optional<std::string> text = readFile(it->path());
			if (!text.has_value()) {
				diagnostics.push_back({.file = label + "/" + relative, .message = "cannot be read"});
				continue;
			}
			folder.files.push_back({.name = relative, .text = *text});
		}
		if (error) {
			diagnostics.push_back({.file = label, .message = "cannot be listed"});
			return std::nullopt;
		}
		std::sort(folder.files.begin(), folder.files.end(), [](const SourceFile& a, const SourceFile& b) { return a.name < b.name; });
		folder.layer.label = label;
		for (const SourceFile& file : folder.files) {
			if (!file.name.ends_with(".resolver.json")) {
				folder.layer.files.push_back(file.name);
			} else if (folder.layer.entry.empty()) {
				folder.layer.entry = file.name;
			} else {
				diagnostics.push_back(
					{.file = label,
					 .message = "has two resolver files, " + folder.layer.entry + " and " + file.name + "; a folder has at most one"}
				);
			}
		}
		folder.layer.read = [files = folder.files](const std::string& name) -> std::optional<std::string> {
			for (const SourceFile& file : files) {
				if (file.name == name) {
					return file.text;
				}
			}
			return std::nullopt;
		};
		return folder;
	}

	std::string modeName(const ResolverInputs& inputs) {
		std::string name;
		for (const auto& [modifier, context] : inputs) {
			name += (name.empty() ? "" : ", ") + modifier + "=" + context;
		}
		return name;
	}

	void report(const std::vector<Diagnostic>& diagnostics) {
		std::set<std::string> seen;
		for (const Diagnostic& diagnostic : diagnostics) {
			const std::string text = diagnostic.text();
			if (seen.insert(text).second) {
				std::cerr << "salal_tokens: " << text << '\n';
			}
		}
	}

	int run(int argc, char** argv) {
		if (argc != 6 && argc != 7) {
			std::cerr << "usage: salal_tokens_tool <tokens dir> <namespace> <header include> <header out> <source out> [<base dir>]\n";
			return 1;
		}
		std::vector<Diagnostic> diagnostics;
		std::optional<Folder>	folder = readFolder(argv[1], diagnostics);
		std::optional<Folder>	base = argc == 7 ? readFolder(argv[6], diagnostics) : std::nullopt;
		if (!folder.has_value() || (argc == 7 && !base.has_value()) || hasErrors(diagnostics)) {
			report(diagnostics);
			return 1;
		}

		// What the base defines on its own, which its table names and this one leaves out.
		std::set<std::string, std::less<>> inherited;
		std::vector<Layer>				   layers;
		if (base.has_value()) {
			std::vector<Diagnostic> baseDiagnostics;
			const Layer				baseLayer[] = {base->layer};
			for (const auto& [path, token] : resolve(loadLayers(baseLayer, {}, baseDiagnostics), {}, baseDiagnostics)) {
				inherited.insert(path);
			}
			layers.push_back(base->layer);
		}
		layers.push_back(folder->layer);

		std::vector<Modifier> modifiers;
		for (const Layer& layer : layers) {
			for (Modifier& modifier : layerModifiers(layer)) {
				modifiers.push_back(std::move(modifier));
			}
		}

		// Every mode must load cleanly and define the same tokens with the same types.
		std::optional<ResolvedTokens> first;
		std::string					  firstMode;
		for (const ResolverInputs& inputs : permutations(modifiers)) {
			std::vector<Diagnostic> modeDiagnostics;
			const ResolvedTokens	resolved = resolve(loadLayers(layers, inputs, modeDiagnostics), {}, modeDiagnostics);
			const std::string		mode = modeName(inputs);
			for (Diagnostic& diagnostic : modeDiagnostics) {
				if (!mode.empty() && diagnostic.severity == Diagnostic::Severity::Error) {
					diagnostic.message += " (in " + mode + ")";
				}
				diagnostics.push_back(std::move(diagnostic));
			}
			if (!first.has_value()) {
				first = resolved;
				firstMode = mode;
				continue;
			}
			for (const auto& [path, token] : *first) {
				const auto other = resolved.find(path);
				if (other == resolved.end()) {
					diagnostics.push_back(
						{.file = token.file, .path = path, .message = "is in " + firstMode + " but missing from " + mode}
					);
				} else if (other->second.type != token.type) {
					diagnostics.push_back(
						{.file = other->second.file, .path = path, .message = "changes type between " + firstMode + " and " + mode}
					);
				}
			}
			for (const auto& [path, token] : resolved) {
				if (!first->contains(path)) {
					diagnostics.push_back(
						{.file = token.file, .path = path, .message = "is in " + mode + " but missing from " + firstMode}
					);
				}
			}
		}
		if (base.has_value()) {
			std::set<std::string, std::less<>> roots;
			for (const std::string& path : inherited) {
				roots.insert(path.substr(0, path.find('.')));
			}
			for (const auto& [path, token] : *first) {
				const std::string root = path.substr(0, path.find('.'));
				if (!inherited.contains(path) && roots.contains(root) && root != kStylesGroup) {
					diagnostics.push_back(
						{.severity = Diagnostic::Severity::Warning,
						 .file = token.file,
						 .path = path,
						 .message = "is in ui's " + root +
									" group but ui's tokens have no such token; check the name if it was meant to override one"}
					);
				}
			}
		}
		if (hasErrors(diagnostics)) {
			report(diagnostics);
			return 1;
		}

		const Catalog		catalogue = catalog(*first, inherited, diagnostics);
		const CodegenOutput out = generate(
			catalogue,
			{.nameSpace = argv[2],
			 .header = argv[3],
			 .origin = folder->layer.label,
			 .entry = folder->layer.entry,
			 .files = folder->files,
			 .hasBase = base.has_value()},
			diagnostics
		);
		report(diagnostics);
		if (hasErrors(diagnostics)) {
			return 1;
		}
		if (!writeFile(argv[4], out.header) || !writeFile(argv[5], out.source)) {
			std::cerr << "salal_tokens: error: cannot write " << argv[4] << " or " << argv[5] << '\n';
			return 1;
		}
		return 0;
	}

} // namespace

int main(int argc, char** argv) {
	return run(argc, argv);
}
