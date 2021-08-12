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
#include "elf_symbol.h"

#include <fcntl.h>
#include <gelf.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <limits>
#include <string_view>
#include <system_error>

#include "elf_error.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace
{
StringRef kKLPLocalSym("klp.local.sym");

void throw_gelf_error()
{
	throw std::error_code{ static_cast<ElfErrorCode>(elf_errno()) };
}
} // namespace

ElfSymbol::ElfSymbol(Elf *elf) : elf_(elf)
{
	Elf_Scn *scn = nullptr;
	GElf_Shdr sym_sec_hdr;
	while ((scn = elf_nextscn(elf_, scn))) {
		gelf_getshdr(scn, &sym_sec_hdr);
		if (sym_sec_hdr.sh_type == SHT_SYMTAB)
			break;
	}
	if (!scn) {
		throw std::error_code{ ElfErrorCode::NO_SYMTAB };
	}

	str_sec_idx_ = sym_sec_hdr.sh_link;
	symtab_ = elf_getdata(scn, nullptr);
	sym_count_ = sym_sec_hdr.sh_size / sym_sec_hdr.sh_entsize;
	if (sym_count_ > 0) {
		// sym_cursor_ starts with 0 while terminating condition for next
		// iteration is sym_cursor_ == sym_count_.
		sym_count_--;
	}
}

ElfSymbol::Iterator ElfSymbol::begin()
{
	return Iterator(this);
}

ElfSymbol::Iterator ElfSymbol::end()
{
	return Iterator(nullptr);
}

std::string_view ElfSymbol::Name() const noexcept(false)
{
	return Name(sym_cursor_);
}

std::string_view ElfSymbol::Name(size_t cursor) const noexcept(false)
{
	GElf_Sym sym;
	GetGElfSymbol(&sym, cursor);
	return elf_strptr(elf_, str_sec_idx_, sym.st_name);
}

void ElfSymbol::Rename(uint32_t name_offset) noexcept(false)
{
	GElf_Sym sym;
	GetGElfSymbol(&sym);
	sym.st_name = name_offset;
	SetGElfSymbol(&sym);
}

ElfSymbol::SymbolType ElfSymbol::Type() noexcept(false)
{
	return Type(sym_cursor_);
}
ElfSymbol::SymbolType ElfSymbol::Type(size_t cursor) noexcept(false)
{
	GElf_Sym sym;
	GetGElfSymbol(&sym, cursor);

	return static_cast<ElfSymbol::SymbolType>(GELF_ST_TYPE(sym.st_info));
}

size_t ElfSymbol::GetStringSectionIndex()
{
	return str_sec_idx_;
}

bool ElfSymbol::HasSectionIndex(ElfSymbol::SectionIndex idx)
{
	return HasSectionIndex(idx, sym_cursor_);
}

bool ElfSymbol::HasSectionIndex(ElfSymbol::SectionIndex idx, size_t cursor)
{
	GElf_Sym sym;
	GetGElfSymbol(&sym, cursor);
	return sym.st_shndx == static_cast<uint16_t>(idx);
}

void ElfSymbol::SetSectionIndex(ElfSymbol::SectionIndex idx)
{
	SetSectionIndex(idx, sym_cursor_);
}

void ElfSymbol::SetSectionIndex(ElfSymbol::SectionIndex idx, size_t cursor)
{
	GElf_Sym sym;
	GetGElfSymbol(&sym, cursor);
	sym.st_shndx = static_cast<uint16_t>(idx);
	SetGElfSymbol(&sym, cursor);
}

void ElfSymbol::GetGElfSymbol(GElf_Sym *sym) const noexcept(false)
{
	GetGElfSymbol(sym, sym_cursor_);
}

void ElfSymbol::GetGElfSymbol(GElf_Sym *sym, size_t cursor) const
	noexcept(false)
{
	if (cursor == std::numeric_limits<size_t>::max()) {
		errs() << "sym cursor, " << cursor << ", is invalid\n";
		throw std::error_code{ ElfErrorCode::INVALID_ELF_SYMBOL };
	}
	if (gelf_getsym(symtab_, cursor, sym) == nullptr) {
		throw_gelf_error();
	}
}

void ElfSymbol::SetGElfSymbol(GElf_Sym *sym) noexcept(false)
{
	SetGElfSymbol(sym, sym_cursor_);
}

void ElfSymbol::SetGElfSymbol(GElf_Sym *sym, size_t cursor) noexcept(false)
{
	if (cursor == std::numeric_limits<size_t>::max()) {
		errs() << "sym cursor, " << cursor << ", is invalid\n";
		throw std::error_code{ ElfErrorCode::INVALID_ELF_SYMBOL };
	}
	gelf_update_sym(symtab_, cursor, sym);
}

bool ElfSymbol::IsKLPLocalSymbol() const noexcept(false)
{
	return StringRef(Name()).startswith(Twine(kKLPLocalSym, ":").str());
}

std::string ElfSymbol::CreateKlpLocalSymName(StringRef sym_name)
{
	return (Twine(kKLPLocalSym, ":") + Twine(sym_name)).str();
}

namespace
{
std::string RemoveBasePath(StringRef path, StringRef base_path)
{
	return path.split(base_path).second.ltrim("./").str();
}
} // namespace

std::string ElfSymbol::CreateLivepatchedFunctionName(const Function &fn,
						     StringRef base_path)
{
	return fn.getName().str() + ":" +
	       RemoveBasePath(fn.getParent()->getSourceFileName(), base_path);
}

// CreateLivepatchedSymbolName - Create a unique name for the global. The
// format we're using is:
//
//    klp.local.sym:orig_name:source_filename
//    ^           ^ ^       ^ ^             ^
//    |___________| |_______| |_____________|
//         [A]         [B]          [C]
//
// [A]: Prefix.
// [B]: The original symbol name.
// [C]: The source filename, to help with disambiguation.
std::string ElfSymbol::CreateLivepatchedSymbolName(StringRef orig_name,
						   StringRef filename,
						   StringRef base_path)
{
	return ElfSymbol::CreateKlpLocalSymName(orig_name) + ":" +
	       RemoveBasePath(filename, base_path);
}

ElfSymbol::Iterator::Iterator(ElfSymbol *symbol) noexcept(false)
	: symbol_(symbol)
{
	if (symbol_ == nullptr) {
		return;
	}
	// First symbol, sym_cursor = 0, is always a dummy symbol w/ a null
	// name. So, skip it.
	symbol_->sym_cursor_ = 1;
}

bool ElfSymbol::Iterator::operator==(const Iterator &other) const
{
	if (symbol_ != other.symbol_) {
		return false;
	}

	if (symbol_ == nullptr) {
		return true;
	}

	return symbol_->elf_ == other.symbol_->elf_;
}

bool ElfSymbol::Iterator::operator!=(const Iterator &other) const
{
	return !operator==(other);
}

ElfSymbol *ElfSymbol::Iterator::operator*() const
{
	return symbol_;
}

ElfSymbol::Iterator &ElfSymbol::Iterator::operator++()
{
	if (symbol_ == nullptr) {
		return *this;
	}

	if (symbol_->sym_cursor_ >= symbol_->sym_count_) {
		symbol_->sym_cursor_ = std::numeric_limits<size_t>::max();
		symbol_ = nullptr;
		return *this;
	}
	symbol_->sym_cursor_++;
	return *this;
}
