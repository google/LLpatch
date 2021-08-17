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
#ifndef DIFF_COMMAND_H_
#define DIFF_COMMAND_H_

#include <memory>
#include <string>
#include <string_view>
#include <system_error>

#include "command.h"
#include "llvm/IR/Module.h"

// This class implements diff command for kernel livepatch generation. The
// 'diff' command inputs two LLVM IR files, original.ll and patched.ll, and
// distills differences between them for C functions and global
// variables. The 'diff' command outputs an LLVM IR file with patched/new C
// functions and global variables in it.
class DiffCommand : public Command {
    public:
	static constexpr std::string_view kCommandName = "diff";

	DiffCommand(int argc, char **argv) noexcept(false);
	~DiffCommand() override = default;

	// Don't allow copy.
	DiffCommand(const DiffCommand &rhs) = delete;
	DiffCommand &operator=(const DiffCommand &rhs) = delete;

	// Runs diff command and outputs an LLVM IR file with patched/new C functions and
	// global variables in it.
	std::error_code Run() override;

	// Diffs two LLVM modules and returns a LLVM module that distills difference
	// between them.
	std::unique_ptr<llvm::Module>
	DistillDiff(std::unique_ptr<llvm::Module> original,
		    std::unique_ptr<llvm::Module> patched);

    private:
	std::string original_filename_;
	std::string patched_filename_;
	std::string base_dir_;
	bool quiet_mode_ = false;
};

#endif // DIFF_COMMAND_H_
