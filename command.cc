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
#include "command.h"

#include <memory>
#include <string>
#include <system_error>

#include "align_command.h"
#include "diff_command.h"
#include "gen_command.h"
#include "fixup_command.h"
#include "llvm/Support/raw_ostream.h"

namespace
{
struct CommandErrorCategory : std::error_category {
	const char *name() const noexcept override
	{
		return "livepatch";
	}
	std::string message(int ev) const override;
};

std::string CommandErrorCategory::message(int ev) const
{
	switch (static_cast<Command::ErrorCode>(ev)) {
	case Command::ErrorCode::INVALID_COMMAND:
		return "invalid command";
	case Command::ErrorCode::NOT_ENOUGH_ARGS:
		return "not enough arguments";
	case Command::ErrorCode::INVALID_LLVM_FILE:
		return "invalid LLVM file";
	case Command::ErrorCode::DIFF_FAILED:
		return "diff failed";
	case Command::ErrorCode::FILE_OPEN_FAILED:
		return "failed to open file";
	case Command::ErrorCode::INVALID_PATCH_FILE:
		return "invalid patch file";
	case Command::ErrorCode::NOTHING_TO_PATCH:
		return "nothing to patch";
	default:
		return "unrecognized error";
	}
}

const CommandErrorCategory _CommandErrorCategory{};
}; // namespace

std::unique_ptr<Command> Command::Create(int argc, char **argv) noexcept(false)
{
	if (argc < 2) {
		return std::make_unique<UsageCommand>(argv[0]);
	}

	std::string command = argv[1];
	if (command == DiffCommand::kCommandName) {
		return std::make_unique<DiffCommand>(argc, argv);
	} else if (command == GenCommand::kCommandName) {
		return std::make_unique<GenCommand>(argc, argv);
	} else if (command == FixupCommand::kCommandName) {
		return FixupCommand::Create(argc, argv);
	} else if (command == AlignCommand::kCommandName) {
		return std::make_unique<AlignCommand>(argc, argv);
	} else {
		throw std::error_code{ ErrorCode::INVALID_COMMAND };
	}
}

std::error_code UsageCommand::Run()
{
	llvm::outs()
		<< "usage: " << cmd_
		<< " <command> [<args>]\n"
		   "Utility for kernel livepatch generation\n"
		   "\n"
		   "Available commands:\n"
		   "\n"
		   "align    align __LINE__ for original.c and patched.c for a given .patch\n"
		   "         by adding empty lines\n"
		   "diff     diff two LLVM IR files and output a new LLVM IR file\n"
		   "         that distills changed/new functions and global variables\n"
		   "fixup    rename UND symbols and create a relocation section for klp.\n"
		   "gen      generate livepatch wrapper, makefile, and linker script\n";

	return {};
}

std::error_code make_error_code(Command::ErrorCode e)
{
	return { static_cast<int>(e), _CommandErrorCategory };
}
