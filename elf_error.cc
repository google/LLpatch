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
#include "elf_error.h"

#include <gelf.h>

#include <system_error>

namespace
{
struct ElfErrorCategory : std::error_category {
	const char *name() const noexcept override
	{
		return "elf";
	}
	std::string message(int ev) const override;
};

std::string ElfErrorCategory::message(int ev) const
{
	if (ev < static_cast<int>(ElfErrorCode::CUSTOM_ERROR))
		return elf_errmsg(ev);

	switch (static_cast<ElfErrorCode>(ev)) {
	case ElfErrorCode::NO_SYMTAB:
		return "no symbol table found";
	case ElfErrorCode::INVALID_KLP_PREFIX:
		return "invalid livepatch prefix";
	case ElfErrorCode::INVALID_ELF_SYMBOL:
		return "invalid ELF symbol";
	case ElfErrorCode::NO_RELA_SECTION:
		return "no rela section in an ELF file";
	case ElfErrorCode::RELA_SECTION_NOT_FOUND:
		return "(given) rela section cannot be found";
	case ElfErrorCode::SAME_SYMBOL_FILENAME:
		return "ELF contains same symbol && filename combination";
	default:
		return "unrecognized error";
	}
}

const ElfErrorCategory _ElfErrorCategory{};
} // namespace

std::error_code make_error_code(ElfErrorCode e)
{
	return { static_cast<int>(e), _ElfErrorCategory };
}
