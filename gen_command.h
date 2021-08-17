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
#ifndef GEN_COMMAND_H_
#define GEN_COMMAND_H_

#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "command.h"
#include "elf_bin.h"

#include "llvm/ADT/StringRef.h"

// This class implements gen command for kernel livepatch generation. The
// 'gen' command inputs an object file, klp_patch.o, that contains
// implementation of livepatched functions. It iterates through symbol
// table of the object file and indentifies all livepatched functions.
// Then, it generates livepatch wrapper, livepatch.c, makefile, and linker
// script out of template files. The wrapper is linked to the object file
// to let kernel know what functions are available for the livepatch. The
// linker script is used to resolve the address for livepatched functions
// "declared" in the wrapper.
class GenCommand : public Command {
    public:
	static constexpr std::string_view kCommandName = "gen";

	GenCommand(int argc, char **argv) noexcept(false);
	~GenCommand() override = default;

	// Don't allow copy.
	GenCommand(const GenCommand &rhs) = delete;
	GenCommand &operator=(const GenCommand &rhs) = delete;

	// Runs gen command and generates the wrapper, makefile, and linker
	// script.
	std::error_code Run() override;

    private:
	// Takes a vector of livepatched function names and generates a wrapper.
	std::error_code
	GenerateWrapper(const std::vector<std::pair<llvm::StringRef, llvm::StringRef>> &klp_func_names,
			const std::string &mod_name);
	// Takes a vector of livepatched function names and generates an ld script.
	std::error_code
	GenerateLdScript(const std::vector<std::pair<llvm::StringRef, llvm::StringRef>> &klp_func_names);
	std::error_code GenerateMakefile();
	std::error_code FixupKlpSymbols(ElfBin *elf_bin);

	std::string klp_patch_filename_;
	std::string output_directory_;
	std::string kernel_directory_;
	std::string livepatch_bin_directory_;
	// If changes for livepatch are made in kernel module, the path to
	// the module is required. For now, the command assumes changes in
	// "single" kernel module.
	std::string mod_filename_;
	std::string klp_mod_name_;
	std::string thin_archive_;
};

#endif // GEN_COMMAND_H_
