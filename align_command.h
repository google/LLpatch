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
#ifndef ALIGN_COMMAND_H_
#define ALIGN_COMMAND_H_

#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "command.h"

// This class implements align command for kernel livepatch generation. The 'align'
// command inputs three files, .patch, original.c, and patched.c. The command adds empty
// lines to original.c and/or patched.c to make their __LINE__ macros aligned
// respectively. Name of output file has a given output_suffix_. This is required to avoid
// false-positive diffs.
//
// e.g., .patch removes 10 lines from original.c and adds 20 lines into .patched.c. Then,
// c's __LINE__ macros used after patch's change is translated onto different # in a final
// ELF binary and LLVM IR file. This, in turn, makes unexpected diffs for kernel livepatch
// generation.
class AlignCommand : public Command {
    public:
	// struct Patch contains "relative" offset from the previous patched lines and how
	// many lines are changed from the offset.
	struct Patch {
		Patch(size_t offset, size_t lines)
			: offset(offset), lines(lines)
		{
		}
		~Patch() = default;
		size_t offset = 0;
		size_t lines = 0;
	};

	static constexpr std::string_view kCommandName = "align";

	AlignCommand(int argc, char **argv) noexcept(false);
	~AlignCommand() override = default;

	// Don't allow copy.
	AlignCommand(const AlignCommand &rhs) = delete;
	AlignCommand &operator=(const AlignCommand &rhs) = delete;

	// Runs align command for __LINE__ macro in original.c and patched.c.
	std::error_code Run() override;

	void AlignFile(std::string_view filename,
		       const std::vector<Patch> &from,
		       const std::vector<Patch> &to,
		       const std::vector<size_t> &context);

    private:
	// name of original diffed file. this is used as a marker while parsing .patch
	// file.
	std::string diffed_file_;
	std::string original_filename_;
	std::string patched_filename_;
	std::string patch_filename_;
	std::string output_suffix_;
};

#endif // ALIGN_COMMAND_H_
