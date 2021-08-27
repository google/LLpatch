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
#ifndef COMMAND_H_
#define COMMAND_H_

#include <memory>
#include <string>
#include <system_error>

// This class implements a factory to parse command line arguments and
// return command object based on the user arguments. It's an abstract
// class to provide an interface for user command. Hence, Any commands for
// livepatch should inherit from this class.
struct Command {
	// This enum class is used to create std::error_code for command line arguments to
	// throw exception.
	enum class ErrorCode {
		NO_ERROR = 0,
		INVALID_COMMAND = 1,
		NOT_ENOUGH_ARGS = 2,
		INVALID_LLVM_FILE = 3,
		DIFF_FAILED = 4,
		FILE_OPEN_FAILED = 5,
		INVALID_PATCH_FILE = 6,
		NOTHING_TO_PATCH = 7,
		SYM_FIND_FAILED = 8,
		INVALID_SYM_MAP = 9,
		ALIAS_FIND_FAILED = 10,
		NO_SYM_MAP = 11,
	};

	virtual ~Command() = default;

	// Creates a new command instance with a given command line arguments.
	static std::unique_ptr<Command> Create(int argc, char **argv)
		noexcept(false);

	// Run a user command.
	virtual std::error_code Run() = 0;
};

// This class implement a simple command to print out command usage.
class UsageCommand : public Command {
    public:
	static constexpr std::string_view kCommandName = "help";

	UsageCommand(const char *cmd) noexcept : cmd_(cmd)
	{
	}

	std::error_code Run() override;

    private:
	const char *cmd_;
};

namespace std
{
template <> struct is_error_code_enum<Command::ErrorCode> : true_type {
};
} // namespace std
std::error_code make_error_code(Command::ErrorCode);

#endif // COMMAND_H_
