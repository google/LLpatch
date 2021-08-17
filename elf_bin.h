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
#ifndef ELF_BIN_H_
#define ELF_BIN_H_

#include <gelf.h>

#include <memory>
#include <string_view>
#include <system_error>
#include <vector>

#include "elf_rela.h"
#include "elf_symbol.h"

// This class is an adapter class to abstract away gelf library. This
// parses elf binary to handle it. The class can create iterator for elf
// symbols and create non-standard relocation section for livepatch
// generation. Note that this class is specially designed for kernel
// livepatch generation. Hence, it implements minimum set of operations for
// manipulating elf binary.
class ElfBin final {
    public:
	ElfBin(std::string_view elf_filename) noexcept(false);
	~ElfBin();

	// Don't allow copy.
	ElfBin(const Elf &rhs) = delete;
	ElfBin &operator=(const Elf &rhs) = delete;

	// Creates an ElfSymbol object to iterate through elf symbols and
	// manipulate them.
	ElfSymbol Symbols()
	{
		return ElfSymbol{ elf_ };
	}

	// Creates an ElfRela object to iterate through elf rela sections and
	// manipulate them.
	ElfRela Relas()
	{
		return ElfRela{ elf_ };
	}

	void UpdateSection(size_t sec_idx, void *data, size_t size)
		noexcept(false);

	// Gets section data w/ given section index.
	std::unique_ptr<std::vector<char> > GetSection(size_t sec_idx);

	std::string_view SectionName(size_t sec_idx);

	// Locates the section, .modinfo, and returns module name
	// The section consists of key=value pair seperated by '\0'
	//
	// Contents of section .modinfo:
	//  0000 6c697665 70617463 683d5900 6c696365  livepatch=Y.lice
	//  0010 6e73653d 47504c00 64657065 6e64733d  nse=GPL.depends=
	//  0020 00726574 706f6c69 6e653d59 006e616d  .retpoline=Y.nam
	//  0030 653d6b65 726e656c 5f6c6976 65706174  e=kernel_livepat
	//  0040 63680076 65726d61 6769633d 342e3135  ch.vermagic=4.15
	//  0050 2e302d73 6d702d44 45562053 4d50206d  .0-smp-DEV SMP m
	//  0060 6f645f75 6e6c6f61 64206d6f 64766572  od_unload modver
	//  0070 73696f6e 732000                      sions .
	std::string ModName();

	// Gets string section index for section names.
	size_t GetStringSectionIndex();

	// Creates a new relocation section for livepatched symbols. This is
	// "non-standard" relocation section for kernel livepatch subsystem. section_id
	// points to text section that requires relocation. section_name is offset in
	// section name string section. Relocation infomation is stored in rela_vector for
	// creating rela section.
	void CreateKlpRela(size_t section_id, size_t symtab_id,
			   size_t section_name,
			   std::vector<ElfRela::RelaEntry> *rela_vector)
		noexcept(false);

	void UpdateRela(size_t section_id,
			std::vector<ElfRela::RelaEntry> *rela_vector)
		noexcept(false);

	// Write down changes to the elf binary. This should be called
	// before this object goes away.
	void ElfUpdate() noexcept(false);

    private:
	Elf_Data *GetElfSectionData(size_t sec_idx) noexcept(false);

	int elf_fd_ = -1;
	Elf *elf_ = nullptr;
};

#endif // ELF_BIN_H_
