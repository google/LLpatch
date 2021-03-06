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
#ifndef ELF_ERROR_H_
#define ELF_ERROR_H_

#include <system_error>

// This enum class is used to create std::error_code for elf handling.
// Most of elf error codes come from elf_errno() while their error messages
// are generated by elf_errmsg(). So, there are few error codes defined
// here. 1~0xFFF are reserved for elf_errno(). From 0x1000, custom error
// codes are defined.
enum class ElfErrorCode : int {
	NO_ERROR = 0,
	CUSTOM_ERROR = 0x1000,
	NO_SYMTAB,
	INVALID_KLP_PREFIX,
	INVALID_ELF_SYMBOL,
	NO_RELA_SECTION,
	RELA_SECTION_NOT_FOUND,
	SAME_SYMBOL_FILENAME,
};

namespace std
{
template <> struct is_error_code_enum<ElfErrorCode> : true_type {
};
} // namespace std

std::error_code make_error_code(ElfErrorCode e);

#endif // ELF_ERROR_H_
