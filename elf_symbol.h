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
#ifndef ELF_SYMBOL_H_
#define ELF_SYMBOL_H_

#include <gelf.h>

#include <cstdint>
#include <limits>
#include <iterator>
#include <string_view>

#include "llvm/ADT/StringRef.h"

namespace llvm
{
class Function;
}

// This class creates an iterator to browse through all symbols in elf
// binary and handles the symbols in it.
class ElfSymbol final {
    public:
	enum class SectionIndex : std::uint16_t {
		UNDEF = 0, /* Undefined section */
		LORESERVE = 0xff00, /* Start of reserved indices */
		LOPROC = 0xff00, /* Start of processor-specific */
		HIPROC = 0xff1f, /* End of processor-specific */
		LIVEPATCH = 0xff20, /* Special for kernel livepatch */
		HIOS = 0xff3f, /* End of OS-specific */
		ABS = 0xfff1, /* Associated symbol is absolute */
		COMMON = 0xfff2, /* Associated symbol is common */
		XINDEX = 0xffff, /* Index is in extra table.  */
		HIRESERVE = 0xffff /* End of reserved indices */
	};

	// Symbol types. enum values, STT_*, come from gelf.h
	enum class SymbolType : std::uint8_t {
		NOTYPE = STT_NOTYPE,
		OBJECT = STT_OBJECT,
		FUNC = STT_FUNC,
		SECTION = STT_SECTION,
		FILE = STT_FILE,
		COMMON = STT_COMMON,
		TLS = STT_TLS,
		NUM = STT_NUM,
		LOOS = STT_LOOS,
		HIOS = STT_HIOS,
		LOPROC = STT_LOPROC,
		HIPROC = STT_HIPROC
	};

	ElfSymbol(Elf *elf) noexcept(false);
	~ElfSymbol() = default;

	// Don't allow copy.
	ElfSymbol(const ElfSymbol &rhs) = delete;
	ElfSymbol &operator=(const ElfSymbol &rhs) = delete;

	// Iterator to browse through all symbols. This iterator does not
	// support backward move. This is mainly used for ranged-for.
	class Iterator final
		: public std::iterator<std::forward_iterator_tag, ElfSymbol *> {
	    public:
		Iterator(ElfSymbol *symbol);
		~Iterator() = default;

		bool operator==(const Iterator &other) const;
		bool operator!=(const Iterator &other) const;
		ElfSymbol *operator*() const;
		Iterator &operator++();

	    private:
		ElfSymbol *symbol_ = nullptr;
	};

	Iterator begin();
	Iterator end();

	std::string_view Name() const noexcept(false);
	std::string_view Name(size_t cursor) const noexcept(false);

	// Symbol entry in a symbol table has an offset in string section
	// for symbols' name. Unfortunately, gelf doesn't provide a good
	// abstraction for renaming symbol, which requires a clear
	// understanding on the structure of ELF binary. For now, This
	// function takes a new offset for symbol name and simply updates
	// its index. Therefore, outside of this class, string section
	// should be modified, which is ugly.
	void Rename(uint32_t name_offset) noexcept(false);

	SymbolType Type() noexcept(false);
	SymbolType Type(size_t cursor) noexcept(false);

	size_t GetStringSectionIndex();

	bool HasSectionIndex(SectionIndex idx);
	bool HasSectionIndex(SectionIndex idx, size_t cursor);
	void SetSectionIndex(SectionIndex idx);
	void SetSectionIndex(SectionIndex idx, size_t cursor);

	bool IsKLPLocalSymbol() const noexcept(false);
	bool IsLLpatchSymbol() const noexcept(false);
	std::string_view GetLLpatchSymbolAlias() const;

	static std::string CreateKlpLocalSymName(llvm::StringRef sym_name);
	static std::string
	CreateLivepatchedFunctionName(const llvm::Function &fn,
				      llvm::StringRef base_path);
	static std::string
	CreateLivepatchedSymbolName(llvm::StringRef orig_name,
				    llvm::StringRef filename,
				    llvm::StringRef base_path);

    private:
	void GetGElfSymbol(GElf_Sym *sym) const noexcept(false);
	void GetGElfSymbol(GElf_Sym *sym, size_t cursor) const noexcept(false);
	void SetGElfSymbol(GElf_Sym *sym) noexcept(false);
	void SetGElfSymbol(GElf_Sym *sym, size_t cursor) noexcept(false);

	Elf *elf_ = nullptr;
	size_t str_sec_idx_ = 0;
	Elf_Data *symtab_ = nullptr;
	size_t sym_cursor_ = std::numeric_limits<size_t>::max();
	size_t sym_count_ = 0;
};

#endif // ELF_SYMBOL_H_
