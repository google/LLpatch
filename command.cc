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
	const char *msg = nullptr;

	switch (static_cast<Command::ErrorCode>(ev)) {
	case Command::ErrorCode::INVALID_COMMAND:
		msg = "invalid command";
		break;
	case Command::ErrorCode::NOT_ENOUGH_ARGS:
		msg = "not enough arguments";
		break;
	case Command::ErrorCode::INVALID_LLVM_FILE:
		msg = "invalid LLVM file";
		break;
	case Command::ErrorCode::DIFF_FAILED:
		msg = "diff failed";
		break;
	case Command::ErrorCode::FILE_OPEN_FAILED:
		msg = "failed to open file";
		break;
	case Command::ErrorCode::INVALID_PATCH_FILE:
		msg = "invalid patch file";
		break;
	case Command::ErrorCode::NOTHING_TO_PATCH:
		msg = "nothing to patch";
		break;
	default:
		msg = "unrecognized error";
		break;
	}

	return std::string(name()) + ": " + msg;
}

const CommandErrorCategory _CommandErrorCategory{};
}; // namespace

std::unique_ptr<Command> Command::Create(int argc, char **argv) noexcept(false)
{
	const char *exec_name = argv[0];
	if (const char *slash = rindex(exec_name, '/')) {
		exec_name = slash + 1;
	}

	if (argc < 2) {
		return std::make_unique<UsageCommand>(exec_name);
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
	} else if (command == UsageCommand::kCommandName) {
		return std::make_unique<UsageCommand>(exec_name);
	}

	throw std::error_code{ ErrorCode::INVALID_COMMAND };
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
