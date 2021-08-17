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
#ifndef FIXUP_COMMAND_H_
#define FIXUP_COMMAND_H_

#include <memory>
#include <string>
#include <string_view>
#include <system_error>

#include "command.h"
#include "elf_bin.h"
#include "llvm/Support/raw_ostream.h"

// This class implements fixup command for kernel livepatch generation. The
// 'fixup' command inputs an object file, klp_patch.o, that contains
// implementation of livepatched functions. It iterates through symbol
// table of the object file and identifies all UND (undefined) symbols.
// Then, it renames symbol names based on the following rule.
// https://www.kernel.org/doc/html/latest/livepatch/module-elf-format.html
// It also creates a non-standard relocation section for kernel livepatch
// subsystem.
class FixupCommand : public Command {
    public:
	static constexpr std::string_view kCommandName = "fixup";

	~FixupCommand() override = default;

	// Don't allow copy.
	FixupCommand(const FixupCommand &rhs) = delete;
	FixupCommand &operator=(const FixupCommand &rhs) = delete;

	static std::unique_ptr<FixupCommand> Create(int argc, char **argv)
		noexcept(false);

	// Runs fixup command to rename UND symbols and create a relocation
	// section for kernel livepatch subsystem.
	std::error_code Run() override;

    private:
	FixupCommand(llvm::raw_ostream &out) : out_(out)
	{
	}

	std::error_code CreateKlpRela(ElfBin *elf_bin);
	std::error_code RenameKlpSymbols(ElfBin *elf_bin,
					 std::string_view mod_filename,
					 std::string_view thin_archive);

	std::string klp_patch_filename_;
	// If changes for livepatch are made in kernel module, the path to the
	// module is required. For now, the fixup command assumes changes in
	// "single" kernel module.
	std::string mod_filename_;
	std::string thin_archive_;
	bool create_klp_rela_ = false;
	llvm::raw_ostream &out_;
	bool quiet_mode_ = false;
};

#endif // FIXUP_COMMAND_H_
