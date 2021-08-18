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
#include "fixup_command.h"

#include <argp.h>
#include <fcntl.h>
#include <gelf.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "elf_error.h"
#include "elf_bin.h"
#include "elf_rela.h"
#include "elf_symbol.h"
#include "thin_archive.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
namespace
{
struct FixupArgs {
	char *klp_patch_filename = nullptr;
	char *mod_filename = nullptr;
	char *thin_archive = nullptr;
	bool create_klp_rela = false;
	bool quiet_mode = false;
};

const char kFixupArgsDoc[] = "<klp_patch.o>";
const char kFixupPrgDoc[] = "common fixup options:\n";
const struct argp_option kFixupOptions[] = {
	// name, key, arg, flags, doc,
	{ "mod", 'm', "MOD", 0,
	  "Path to kernel module. For vmlinux, don't specify" },
	{ "thin_archive", 't', "THIN_ARCHIVE", 0,
	  "Thin archive file for kernel module or vmlinux" },
	{ "rela", 'r', nullptr, 0, "Create relocation section for KLP" },
	{ "quiet", 'q', nullptr, 0, "Don't print out any messages on fixup" },
	{ nullptr }
};

constexpr std::string_view kKlpPrefix = ".klp.sym.";
constexpr std::string_view kKlpRelaPrefix = ".klp.rela.";
constexpr std::string_view kObjVmlinux = "vmlinux.";

error_t ParseFixupOpt(int key, char *arg, struct argp_state *state)
{
	FixupArgs *args = static_cast<FixupArgs *>(state->input);

	switch (key) {
	case 'm':
		args->mod_filename = arg;
		break;
	case 'r':
		args->create_klp_rela = true;
		break;
	case 'q':
		args->quiet_mode = true;
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
		if (!args->klp_patch_filename) {
			argp_usage(state);
		}
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

} // namespace

// Assumption: there is 1-to-1 correspondence between a relocation section
// and the section that the relocation section relocates. The following is
// the example.
//
// [Nr] Name        Type      Address          Off    Size   ES Flg Lk Inf Al
// [ 1] .text       PROGBITS  0000000000000000 000040 0014ca 00  AX  0   0 16
// [ 2] .rela.text  RELA      0000000000000000 001510 002268 18   I 18   1  8
//
// ".rela.text" should be only one relocation section for ".text".
std::error_code FixupCommand::CreateKlpRela(ElfBin *elf_bin)
{
	ElfRela::KlpRelaEntryMap klp_rela_entry_map;
	ElfRela::RelaEntryMap rela_entry_map;
	std::unordered_map<size_t, size_t> symtab_map;
	for (ElfRela *i : elf_bin->Relas()) {
		std::string sym_name(i->Name());
		if (sym_name.find(kKlpPrefix) != 0) {
			// Store rela entry for non-livepatched symbols.
			rela_entry_map[i->SectionId()].emplace_back(
				*(i->Entry()));
			continue;
		}
		i->SetSectionIndex(ElfSymbol::SectionIndex::LIVEPATCH);

		size_t mod_name_start = kKlpPrefix.length();
		size_t mod_name_end =
			sym_name.find('.', mod_name_start) - mod_name_start;
		const std::string &mod_name =
			sym_name.substr(mod_name_start, mod_name_end);

		if (!quiet_mode_) {
			out_ << "klp symbol[" << mod_name << "] :: ";
			i->PrintCurrentEntry();
		}

		// Collect all relocation entries for livepatched symbols.
		klp_rela_entry_map[std::make_pair(mod_name, i->SectionId())]
			.emplace_back(*(i->Entry()));
		symtab_map[i->SectionId()] = i->SymTabId();
	}

	// Update existing rela sections to avoid duplication with KLP rela
	// sections.
	for (auto &[section_id, rela_vector] : rela_entry_map) {
		// Update rela section that has livepatched symbols.
		elf_bin->UpdateRela(section_id, &rela_vector);
	}

	// Update RELA sections before adding new KLP RELA sections.
	elf_bin->ElfUpdate();

	// Create new relocation section for KLP.
	std::unique_ptr<std::vector<char> > str_section =
		elf_bin->GetSection(elf_bin->GetStringSectionIndex());

	for (auto &[entry_key, rela_vector] : klp_rela_entry_map) {
		elf_bin->CreateKlpRela(std::get</*section_id*/ 1>(entry_key),
				       symtab_map[std::get<1>(entry_key)],
				       str_section->size(), &rela_vector);

		// Format for The name of a livepatch relocation section:
		//
		// .klp.rela.objname.section_name
		// ^        ^^     ^ ^          ^
		// |________||_____| |__________|
		//    [A]      [B]        [C]
		// [A]: prefix
		// [B]: vmlinux or module name that the symbol belongs.
		// [C]: section name to which this relocation section applies. should be "text"
		const std::string kKlpRelaName =
			std::string(kKlpRelaPrefix) +
			std::get</*mod_name*/ 0>(entry_key) + "." +
			std::string(
				elf_bin->SectionName(std::get<1>(entry_key)));

		llvm::outs() << "KLP rela section::" << kKlpRelaName << "\n";
		std::copy(kKlpRelaName.begin(), kKlpRelaName.end(),
			  std::back_insert_iterator<std::vector<char> >(
				  *str_section));
		str_section->push_back('\0');
	}

	elf_bin->UpdateSection(elf_bin->GetStringSectionIndex(),
			       str_section->data(), str_section->size());

	elf_bin->ElfUpdate();

	return Command::ErrorCode::NO_ERROR;
}

std::error_code FixupCommand::RenameKlpSymbols(ElfBin *elf_bin,
					       std::string_view mod_filename,
					       std::string_view thin_archive)
{
	// Load names for all "defined" symbols in kernel module if specified.
	std::unordered_set<std::string> mod_symbol_set;
	std::string mod_name(kObjVmlinux);
	if (!mod_filename.empty()) {
		ElfBin mod_bin(mod_filename);
		for (ElfSymbol *i : mod_bin.Symbols()) {
			if (i->HasSectionIndex(
				    ElfSymbol::SectionIndex::UNDEF)) {
				continue;
			}
			mod_symbol_set.insert(std::string(i->Name()));
		}
		mod_name = mod_bin.ModName() + ".";
	}

	// Elf binary always starts w/ dummy undefined symbol. iterator from
	// ElfSymbol skips the first dummy symbol. Hence, default memory buffer
	// for string section has '\0' by default.
	std::vector<char> sym_name_buf{ '\0' };
	ElfSymbol elf_symbols = elf_bin->Symbols();

	// This loop iterates through all symbols in the ELF binary and renames
	// symbol if it's undefined. While renaming the symbol, it also builds
	// up a memory buffer for symbol names. The buffer is used to update
	// string section in ELF binary after this loop.
	std::unique_ptr<ThinArchive> tar =
		ThinArchive::Create(std::string(thin_archive));
	for (ElfSymbol *i : elf_symbols) {
		size_t sym_name_offset = sym_name_buf.size();

		// __fentry__ is for kernel's ftrace. don't touch even though it's UND.
		if (i->HasSectionIndex(ElfSymbol::SectionIndex::UNDEF) &&
		    i->Name() != "__fentry__") {
			StringRef RealSymName = i->Name();
			StringRef SrcFile;
			std::string SymName = std::string(i->Name());

			if (i->IsKLPLocalSymbol()) {
				auto SplitName = RealSymName.split(':');
				RealSymName = SplitName.second.split(':').first;
				SrcFile = SplitName.second.split(':').second;
			}

			std::string RealSymNameStr = RealSymName.str();

			if (mod_name != kObjVmlinux &&
			    mod_symbol_set.find(RealSymNameStr) ==
				    mod_symbol_set.end()) {
				// given kernel module doesn't have symbol name, which
				// implies EXPORTed symbol. So, do not mark this as
				// livepatched symbol.
				const std::string_view &name = RealSymNameStr;
				std::copy(name.begin(), name.end(),
					  std::back_insert_iterator<
						  std::vector<char> >(
						  sym_name_buf));
				sym_name_buf.push_back('\0');
				i->Rename(sym_name_offset);
				continue;
			}

			i->SetSectionIndex(ElfSymbol::SectionIndex::LIVEPATCH);

			// Rename the symbol for livepatching. The following is the format.
			//
			//   .klp.sym.objname.symbol_name,sympos
			//   ^       ^^     ^ ^         ^ ^
			//   |_______||_____| |_________| |
			//      [A]     [B]       [C]    [D]
			//
			// [A]: Prefix
			// [B]: vmlinux or module name that the symbol belongs.
			// [C]: Actual name of the symbol.
			// [D]: The position of the symbol in the object (as according
			//      to kallsyms) This is used to differentiate
			//      duplicate symbols within the same object. The
			//      symbol position is expressed numerically (0, 1,
			//      2, ...). The symbol position of a unique symbol
			//      is 0.
			unsigned pos = 0;
			if (tar) {
				const auto& symbol = RealSymName.str();
				const auto& filename = SrcFile.rsplit('.').first.str() + ".o";
					pos = tar->QuerySymbol(symbol, filename);
				if (pos < 0) {
					errs() << "Symbol: " << symbol
						<< ", Filename: " << filename << "\n"
						<< "Fail to find the symbol in thin archive\n";
					return Command::ErrorCode::SYM_FIND_FAILED;
				}
			}

			const std::string kKlpSymName =
				std::string(kKlpPrefix) + mod_name +
				RealSymNameStr + "," + std::to_string(pos);

			out_ << "KLP Symbols::" << RealSymNameStr << " --> "
			     << kKlpSymName << "\n";
			std::copy(kKlpSymName.begin(), kKlpSymName.end(),
				  std::back_insert_iterator<std::vector<char> >(
					  sym_name_buf));
		} else {
			const std::string_view &name = i->Name();
			std::copy(name.begin(), name.end(),
				  std::back_insert_iterator<std::vector<char> >(
					  sym_name_buf));
		}
		sym_name_buf.push_back('\0');
		i->Rename(sym_name_offset);
	}

	// A new memory buffer for symbol name is built up in
	// sym_name_buf. need to replace old buffer w/ the new one for string
	// section before calling Elfbin::ElfUpdate()
	elf_bin->UpdateSection(elf_symbols.GetStringSectionIndex(),
			       sym_name_buf.data(), sym_name_buf.size());

	elf_bin->ElfUpdate();

	return Command::ErrorCode::NO_ERROR;
}

std::unique_ptr<FixupCommand> FixupCommand::Create(int argc, char **argv)
	noexcept(false)
{
	if (argc < 1) {
		throw std::error_code{ ErrorCode::NOT_ENOUGH_ARGS };
	}

	FixupArgs arguments;
	struct argp argp = { kFixupOptions, ParseFixupOpt, kFixupArgsDoc,
			     kFixupPrgDoc };

	// First argument is a command, 'fixup' and it's already consumed. So,
	// argv[0] = argv[0] + argv[1] to let others used for options.
	std::string command = std::string(argv[0]) + " " + argv[1];
	--argc;
	++argv;
	argv[0] = const_cast<char *>(command.c_str());
	argp_parse(&argp, argc, argv, /*flags=*/0,
		   /*arg_index=*/nullptr, /*input=*/&arguments);

	auto *cmd = new FixupCommand(arguments.quiet_mode ? llvm::nulls() :
								  llvm::outs());

	cmd->quiet_mode_ = arguments.quiet_mode;
	cmd->klp_patch_filename_ = arguments.klp_patch_filename;
	if (arguments.mod_filename) {
		cmd->mod_filename_ = arguments.mod_filename;
	}
	cmd->create_klp_rela_ = arguments.create_klp_rela;

	if (arguments.thin_archive) {
		cmd->thin_archive_ = arguments.thin_archive;
	}

	return std::unique_ptr<FixupCommand>(cmd);
}

std::error_code FixupCommand::Run()
{
	std::error_code ec;
	ElfBin elf_bin(klp_patch_filename_);
	if (create_klp_rela_) {
		ec = CreateKlpRela(&elf_bin);
	} else {
		ec = RenameKlpSymbols(&elf_bin, mod_filename_, thin_archive_);
	}

	return ec;
}
