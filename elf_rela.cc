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
#include "elf_rela.h"

#include <limits>
#include <string_view>
#include <system_error>

#include "elf_error.h"
#include "llvm/Support/raw_ostream.h"

namespace
{
void throw_gelf_error()
{
	throw std::error_code{ static_cast<ElfErrorCode>(elf_errno()) };
}

bool HasAllocFlag(Elf *elf, size_t sec_id)
{
	Elf_Scn *scn = elf_getscn(elf, sec_id);
	if (scn == nullptr) {
		throw_gelf_error();
	}

	GElf_Shdr sec_header{};
	if (!gelf_getshdr(scn, &sec_header)) {
		throw_gelf_error();
	}

	return sec_header.sh_flags & SHF_ALLOC;
}
} // namespace

ElfRela::ElfRela(Elf *elf) noexcept(false) : elf_(elf), symbol_(elf_)
{
	if (!GetNextRela()) {
		throw std::error_code{ ElfErrorCode::NO_RELA_SECTION };
	}
}

ElfRela::RelaEntry *ElfRela::Entry() noexcept(false)
{
	if (gelf_getrela(rela_data_, rela_cursor_, &rela_entry_) == nullptr) {
		throw_gelf_error();
	}
	return &rela_entry_;
}

std::string_view ElfRela::Name()
{
	return symbol_.Name(GELF_R_SYM(Entry()->r_info));
}

void ElfRela::PrintCurrentEntry()
{
	llvm::outs()
		<< "Section: " << SectionId() << ", Symbol: " << Name() << "\n";
}

bool ElfRela::HasSectionIndex(ElfSymbol::SectionIndex idx)
{
	return symbol_.HasSectionIndex(idx, GELF_R_SYM(Entry()->r_info));
}

void ElfRela::SetSectionIndex(ElfSymbol::SectionIndex idx)
{
	return symbol_.SetSectionIndex(idx, GELF_R_SYM(Entry()->r_info));
}

Elf_Scn *ElfRela::GetNextRela()
{
	while ((scn_ = elf_nextscn(elf_, scn_))) {
		gelf_getshdr(scn_, &rela_header_);
		// Section relocated by KLP RELA should have SHF_ALLOC flag because
		// kernel module loader frees sections without the flag before KLP
		// RELA kicks in. So, skip RELA sections for sections without the
		// flag.
		if (rela_header_.sh_type == SHT_RELA &&
		    HasAllocFlag(elf_, this->SectionId()))
			break;
	}
	if (scn_ == nullptr) {
		rela_cursor_ = std::numeric_limits<size_t>::max();
		rela_data_ = nullptr;
		rela_count_ = 0;
		return nullptr;
	}

	rela_data_ = elf_getdata(scn_, nullptr);
	rela_count_ = rela_header_.sh_size / rela_header_.sh_entsize;
	if (rela_count_ > 0) {
		// rela_cursor_ starts with 0 while terminating condition for next
		// iteration is rela_cursor_ == rela_count_.
		rela_count_--;
	}
	rela_cursor_ = 0;

	return scn_;
}

bool ElfRela::Iterator::operator==(const Iterator &other) const
{
	if (rela_ != other.rela_) {
		return false;
	}

	if (rela_ == nullptr) {
		return true;
	}

	return rela_->elf_ == other.rela_->elf_;
}

ElfRela::Iterator &ElfRela::Iterator::operator++()
{
	if (rela_ == nullptr) {
		return *this;
	}

	if (rela_->rela_cursor_ >= rela_->rela_count_) {
		if (!rela_->GetNextRela()) {
			rela_ = nullptr;
		}
		return *this;
	}
	rela_->rela_cursor_++;
	return *this;
}
