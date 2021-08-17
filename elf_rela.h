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
#ifndef ELF_RELA_H_
#define ELF_RELA_H_

#include <gelf.h>

#include <iterator>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "elf_symbol.h"

// This class creates an iterator to browse through all rela sections in
// elf binary where corresponding sections for the rela have 'ALLOC' flag.
// This class also provides simple API to handl the rela sections.
class ElfRela final {
    public:
	using RelaEntry = GElf_Rela;
	using RelaEntryMap =
		std::unordered_map<size_t, std::vector<ElfRela::RelaEntry> >;
	using KlpRelaEntryMap = std::map<
		std::pair</*mod_name*/ std::string, /*section_id*/ size_t>,
		std::vector<ElfRela::RelaEntry> >;

	ElfRela(Elf *elf) noexcept(false);
	~ElfRela() = default;

	// Don't allow copy.
	ElfRela(const ElfRela &rhs) = delete;
	ElfRela &operator=(const ElfRela &rhs) = delete;

	// Iterator to browse through all rela section. This iterator does not
	// support backward move. This is mainly used for ranged-for.
	class Iterator final
		: public std::iterator<std::forward_iterator_tag, ElfRela *> {
	    public:
		Iterator(ElfRela *rela) : rela_(rela)
		{
		}
		~Iterator() = default;

		bool operator==(const Iterator &other) const;
		bool operator!=(const Iterator &other) const
		{
			return !operator==(other);
		}
		ElfRela *operator*() const
		{
			return rela_;
		}
		Iterator &operator++();

	    private:
		ElfRela *rela_ = nullptr;
	};

	Iterator begin()
	{
		return Iterator(this);
	}
	Iterator end()
	{
		return Iterator(nullptr);
	}

	// Return current relocation entry that the current iterator points to.
	RelaEntry *Entry() noexcept(false);
	// Returns symbol name for current relocation entry.
	std::string_view Name() noexcept(false);
	// Returns section id that corresponds to the current relocation section.
	size_t SectionId()
	{
		return rela_header_.sh_info;
	}
	size_t SymTabId()
	{
		return rela_header_.sh_link;
	}
	bool HasSectionIndex(ElfSymbol::SectionIndex idx);
	void SetSectionIndex(ElfSymbol::SectionIndex idx);
	void PrintCurrentEntry();

    private:
	Elf_Scn *GetNextRela();

	Elf *elf_ = nullptr;
	Elf_Scn *scn_ = nullptr;
	GElf_Shdr rela_header_ = {};
	Elf_Data *rela_data_ = nullptr;
	RelaEntry rela_entry_ = {};
	size_t rela_cursor_ = std::numeric_limits<size_t>::max();
	size_t rela_count_ = 0;

	ElfSymbol symbol_;
};

#endif // ELF_RELA_H_
