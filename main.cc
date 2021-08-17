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
 *
 * This file implements main() for kernel livepatch generation. The main()
 * instantiates command object with given user arguments and run the command.
 */
#include <string>
#include <system_error>

#include "command.h"
#include "llvm/Support/raw_ostream.h"

int main(int argc, char **argv)
{
	try {
		std::unique_ptr<Command> command = Command::Create(argc, argv);
		std::error_code ec = command->Run();
		if (ec) {
			llvm::errs() << ec.message() << "\n";
			return ec.value();
		}
	} catch (std::error_code e) {
		llvm::errs() << e.message() << "\n";
		return e.value();
	}

	return 0;
}
