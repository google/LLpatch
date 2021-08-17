/*
 * Copyright 2021 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *     Author: Yonghyun Hwang <yonghyun@google.com>
 */
#include "gen_command.h"

#include <argp.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>

#include "elf_error.h"
#include "elf_symbol.h"
#include "thin_archive.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
namespace fs = std::filesystem;
namespace
{
struct GenArgs {
	char *klp_patch_filename = nullptr;
	char *output_directory = nullptr;
	char *kernel_directory = nullptr;
	char *mod_filename = nullptr;
	char *klp_mod_name = nullptr;
	char *thin_archive = nullptr;
};

const char kGenArgsDoc[] = "<klp_patch.o>";
const char kGenPrgDoc[] = "common gen options:\n";
const struct argp_option kGenOptions[] = {
	// name, key, arg, flags, doc,
	{ "odir", 'o', "ODIR", 0, "Path to output dir" },
	{ "kdir", 'k', "KDIR", 0, "Path to kernel dir" },
	{ "mod", 'm', "MOD", 0,
	  "Path to kernel module. for vmlinux, don't specify" },
	{ "name", 'n', "NAME", 0, "KLP module name" },
	{ "thin_archive", 't', "THIN_ARCHIVE", 0,
	  "Thin archive file for kernel module or vmlinux" },
	{ nullptr }
};

constexpr std::string_view kLivepatchPrefixElf = "__livepatch_";
constexpr std::string_view kLivepatchPrefixTmpl = "livepatch_";
constexpr std::string_view kTemplateExtension = ".tmpl";
constexpr std::string_view kPathToTemplate = "templates";

error_t ParseGenOpt(int key, char *arg, struct argp_state *state)
{
	GenArgs *args = static_cast<GenArgs *>(state->input);

	switch (key) {
	case 'o':
		args->output_directory = arg;
		break;
	case 'k':
		args->kernel_directory = arg;
		break;
	case 'm':
		args->mod_filename = arg;
		break;
	case 'n':
		args->klp_mod_name = arg;
		break;
	case 't':
		args->thin_archive = arg;
		break;
	case ARGP_KEY_ARG:
		if (!args->klp_patch_filename) {
			args->klp_patch_filename = arg;
		} else {
			argp_usage(state);
		}
		break;
	case ARGP_KEY_END:
		if (!args->klp_patch_filename || !args->output_directory ||
		    !args->kernel_directory || !args->klp_mod_name) {
			argp_usage(state);
		}
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

// Reads in_file and dumps its contents onto out_file till read line
// contains the given marker. Once this hits the marker, it returns the
// marked line.
std::string DumpToMarker(std::fstream &in_file, std::fstream &out_file,
			 std::string_view marker)
{
	std::string line;
	bool marker_empty = marker.empty();
	while (std::getline(in_file, line)) {
		if (!marker_empty && line.find(marker) != std::string::npos) {
			return line;
		}
		out_file << line << std::endl;
	}
	return "";
}

// Opens two files, in and out, for given in_filename and
// out_filename. Then, it returns tuple with in&out fstreams and error
// code.
std::tuple<std::fstream, std::fstream, std::error_code>
OpenInOutfiles(std::string_view in_filename, std::string_view out_filename)
{
	std::fstream in_file(in_filename.data(), std::ios::in);
	std::fstream out_file(out_filename.data(),
			      std::ios::out | std::ios::trunc);
	std::error_code ec;
	if (!in_file.is_open()) {
		errs() << "filename: " << in_filename << "\n";
		ec = Command::ErrorCode::FILE_OPEN_FAILED;
	} else if (!out_file.is_open()) {
		errs() << "filename: " << out_filename << "\n";
		ec = Command::ErrorCode::FILE_OPEN_FAILED;
	}

	return { std::move(in_file), std::move(out_file), ec };
}
} // namespace

GenCommand::GenCommand(int argc, char **argv) noexcept(false)
{
	if (argc < 1) {
		throw std::error_code{ ErrorCode::NOT_ENOUGH_ARGS };
	}

	GenArgs arguments;
	struct argp argp = { kGenOptions, ParseGenOpt, kGenArgsDoc,
			     kGenPrgDoc };

	// First argument is a command, 'gen' and it's already consumed. So,
	// argv[0] = argv[0] + argv[1] to let others used for options.
	std::string command = std::string(argv[0]) + " " + argv[1];
	--argc;
	++argv;
	argv[0] = const_cast<char *>(command.c_str());
	argp_parse(&argp, argc, argv, /*flags=*/0,
		   /*arg_index=*/nullptr, /*input=*/&arguments);

	klp_patch_filename_ = arguments.klp_patch_filename;
	output_directory_ = arguments.output_directory;
	kernel_directory_ = arguments.kernel_directory;
	if (arguments.mod_filename) {
		mod_filename_ = arguments.mod_filename;
	}
	klp_mod_name_ = arguments.klp_mod_name;
	if (arguments.thin_archive) {
		thin_archive_ = arguments.thin_archive;
	}

	static constexpr int buf_size = 4096;
	char livepatch_path[buf_size] = {};
	if (readlink("/proc/self/exe", livepatch_path, buf_size) < 0) {
		throw std::error_code(errno, std::generic_category());
	}
	livepatch_bin_directory_.assign(livepatch_path);

	// Remove name of executable binary from the path
	size_t file_pos = livepatch_bin_directory_.rfind("/");
	livepatch_bin_directory_.erase(
		file_pos, livepatch_bin_directory_.length() - file_pos);
}

std::error_code GenCommand::Run()
{
	// To generate the wrapper and linker script, names of livepatched
	// functions are required. Iterate through all symbols in ELF and get
	// the names. A name of livepatched function has a special prefix,
	// kLivepatchPrefixElf.
	std::vector<std::pair<StringRef, StringRef>> klp_func_names;
	ElfBin elf_bin(klp_patch_filename_);
	size_t prefix_len = kLivepatchPrefixElf.length();
	for (ElfSymbol *i : elf_bin.Symbols()) {
		StringRef symbol = i->Name();

		if (symbol.empty() || !symbol.startswith(kLivepatchPrefixElf)) {
			continue;
		}

		if (symbol.substr(1).find(kLivepatchPrefixElf) != std::string::npos) {
			// This means kLivepatchPrefixElf is matched in the middle of
			// string. This is not expected. error out.
			errs() << "symbol name: " << symbol << "\n";
			return ElfErrorCode::INVALID_KLP_PREFIX;
		}

		auto [func_name, src_file] = symbol.drop_front(prefix_len).split(':');
		klp_func_names.emplace_back(std::make_pair(func_name, src_file));
	}

	if (klp_func_names.empty()) {
		errs() << "There are no livepatched functions.\n";
		return std::error_code{ Command::ErrorCode::NOTHING_TO_PATCH };
	}

	std::string mod_name =
		mod_filename_.empty() ? "" : ElfBin(mod_filename_).ModName();
	std::error_code ec = GenerateWrapper(klp_func_names, mod_name);
	if (ec) {
		return ec;
	}

	ec = GenerateLdScript(klp_func_names);
	if (ec) {
		return ec;
	}

	ec = GenerateMakefile();
	if (ec) {
		return ec;
	}

	ec = FixupKlpSymbols(&elf_bin);
	if (ec) {
		return ec;
	}

	return ErrorCode::NO_ERROR;
}

std::error_code
GenCommand::GenerateWrapper(const std::vector<std::pair<StringRef, StringRef>> &klp_func_names,
			    const std::string &mod_name)
{
	static constexpr std::string_view kWrapperName = "livepatch.c";
	static constexpr std::string_view kFuncMarker =
		"{{LIST_OF_LIVEPATCH_FUNCTIONS}}";
	static constexpr std::string_view kStructMarker =
		"{{LIST_FOR_KLP_FUNC_STRUCT}}";
	static constexpr std::string_view kObjMarker = "{{NAME_OF_OBJECT}}";

	fs::path kTmplFilename = fs::path(livepatch_bin_directory_) /
				 fs::path(kPathToTemplate) /
				 fs::path(kWrapperName);
	kTmplFilename += fs::path(kTemplateExtension);

	const fs::path kOutFilename =
		fs::path(output_directory_) / fs::path(kWrapperName);
	auto [tmpl_file, out_file, ec] =
		OpenInOutfiles(kTmplFilename.c_str(), kOutFilename.c_str());
	if (ec) {
		return ec;
	}

	DumpToMarker(tmpl_file, out_file, kFuncMarker);
	for (auto [func_name, src_file] : klp_func_names) {
		// void livepatch_${name_of_func}(void)
		out_file << "void "
			 << std::string(kLivepatchPrefixTmpl)
			 << func_name.str()
			 << "(void);\n";
	}

	std::unique_ptr<ThinArchive> tar =
		ThinArchive::Create(std::string(thin_archive_));
	DumpToMarker(tmpl_file, out_file, kStructMarker);
	for (auto [func_name, src_file] : klp_func_names) {
		unsigned pos = 0;
		if (tar) {
			pos = tar->QuerySymbol(
				func_name.str(),
				src_file.rsplit('.').first.str() + ".o");
		}

		//{
		//    .old_name = "${name_of_func},"
		//    .new_func = "livepatch_${name_of_func},"
		//    .old_sympos = ${old_symbol_position},
		//},
		out_file << "\t{\n"
			 << "\t\t.old_name = \"" << func_name.str() << "\",\n"
			 << "\t\t.new_func = "
			 << std::string(kLivepatchPrefixTmpl) << func_name.str()<< ",\n"
			 << "\t\t.old_sympos = " << std::to_string(pos) << ",\n"
			 << "\t},\n";
	}

	DumpToMarker(tmpl_file, out_file, kObjMarker);

	// NULL means vmlinux. unless, it's module name
	// .name = NULL,
	// or
	// .name = "${mod_name}"
	out_file << "\t\t.name = "
		 << (mod_name.empty() ? "NULL" : "\"" + mod_name + "\"")
		 << ",\n";

	// "" marker doesn't exist. hence dump to the end of file.
	DumpToMarker(tmpl_file, out_file, "");
	return ErrorCode::NO_ERROR;
}

std::error_code
GenCommand::GenerateLdScript(const std::vector<std::pair<StringRef, StringRef>> &klp_func_names)
{
	static constexpr std::string_view kLdScriptName = "livepatch.lds";

	fs::path kTmplFilename = fs::path(livepatch_bin_directory_) /
				 fs::path(kPathToTemplate) /
				 fs::path(kLdScriptName);
	kTmplFilename += fs::path(kTemplateExtension);

	const fs::path kOutFilename =
		fs::path(output_directory_) / fs::path(kLdScriptName);
	auto [tmpl_file, out_file, ec] =
		OpenInOutfiles(kTmplFilename.c_str(), kOutFilename.c_str());
	if (ec) {
		return ec;
	}

	// "" marker doesn't exist. hence dump to the end of file.
	DumpToMarker(tmpl_file, out_file, "");

	for (auto [func_name, src_file] : klp_func_names) {
		// ${func} = __${func}
		out_file << std::string(kLivepatchPrefixTmpl) << func_name.str() + " = "
			 << std::string(kLivepatchPrefixElf) << func_name.str() + ";\n";
	}

	return ErrorCode::NO_ERROR;
}

std::error_code GenCommand::GenerateMakefile()
{
	static constexpr std::string_view kMakefileName = "Makefile";
	static constexpr std::string_view kKernelPath =
		"{{PATH_TO_LINUX_KERNEL_SOURCE_TREE}}";
	static constexpr std::string_view kKlpName = "{{NAME_OF_LIVEPATCH}}";

	fs::path kTmplFilename = fs::path(livepatch_bin_directory_) /
				 fs::path(kPathToTemplate) /
				 fs::path(kMakefileName);
	kTmplFilename += fs::path(kTemplateExtension);

	const fs::path kOutFilename =
		fs::path(output_directory_) / fs::path(kMakefileName);
	auto [tmpl_file, out_file, ec] =
		OpenInOutfiles(kTmplFilename.c_str(), kOutFilename.c_str());
	if (ec) {
		return ec;
	}

	std::string line = DumpToMarker(tmpl_file, out_file, kKernelPath);
	//KLP_BUILD = ...
	out_file << line.substr(0, line.find(kKernelPath)) + kernel_directory_ +
			    "\n";

	line = DumpToMarker(tmpl_file, out_file, kKlpName);
	//KLP_NAME = ...
	out_file << line.substr(0, line.find(kKlpName)) + klp_mod_name_ + "\n";

	// "" marker doesn't exist. hence dump to the end of file.
	DumpToMarker(tmpl_file, out_file, "");

	return ErrorCode::NO_ERROR;
}

std::error_code GenCommand::FixupKlpSymbols(ElfBin *elf_bin)
{
	std::vector<char> sym_name_buf{ '\0' };
	ElfSymbol elf_symbols = elf_bin->Symbols();

	for (ElfSymbol *symbol : elf_symbols) {
		size_t sym_name_offset = sym_name_buf.size();
		StringRef sym_name = StringRef(symbol->Name()).split(':').first;

		std::copy(sym_name.begin(), sym_name.end(),
			  std::back_insert_iterator<std::vector<char> >(
				  sym_name_buf));

		sym_name_buf.push_back('\0');
		symbol->Rename(sym_name_offset);
	}

	elf_bin->UpdateSection(elf_symbols.GetStringSectionIndex(),
			       sym_name_buf.data(), sym_name_buf.size());
	elf_bin->ElfUpdate();

	return ErrorCode::NO_ERROR;
}
