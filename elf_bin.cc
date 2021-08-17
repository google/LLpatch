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
#include "elf_bin.h"

#include <fcntl.h>
#include <gelf.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string_view>
#include <system_error>

#include "auto_cleanup.h"
#include "elf_error.h"
#include "llvm/Support/raw_ostream.h"

namespace
{
constexpr size_t kSectionFlagRelaLivepatch = 0x00100000;

void throw_gelf_error()
{
	throw std::error_code{ static_cast<ElfErrorCode>(elf_errno()) };
}
} // namespace

ElfBin::ElfBin(std::string_view elf_filename) noexcept(false)
{
	elf_fd_ = open(elf_filename.data(), O_RDWR, 0);
	if (elf_fd_ < 0) {
		throw std::error_code{ errno, std::system_category() };
	}
	AutoCleanup elf_fd_close([elf_fd = elf_fd_]() { close(elf_fd); });

	if (elf_version(EV_CURRENT) == EV_NONE) {
		throw_gelf_error();
	}

	elf_ = elf_begin(elf_fd_, ELF_C_RDWR, nullptr);
	if (!elf_) {
		throw_gelf_error();
	}

	elf_fd_close.Disable();
}

ElfBin::~ElfBin()
{
	elf_end(elf_);
	close(elf_fd_);
}

Elf_Data *ElfBin::GetElfSectionData(size_t sec_idx) noexcept(false)
{
	Elf_Scn *scn = elf_getscn(elf_, sec_idx);
	if (scn == nullptr) {
		throw_gelf_error();
	}
	Elf_Data *elf_data = elf_getdata(scn, nullptr);
	if (elf_data == nullptr) {
		throw_gelf_error();
	}

	return elf_data;
}

std::string_view ElfBin::SectionName(size_t sec_idx)
{
	Elf_Scn *scn = elf_getscn(elf_, sec_idx);
	if (scn == nullptr) {
		throw_gelf_error();
	}

	GElf_Shdr section_header;
	if (gelf_getshdr(scn, &section_header) == nullptr) {
		throw_gelf_error();
	}

	return elf_strptr(elf_, GetStringSectionIndex(),
			  section_header.sh_name);
}

std::string ElfBin::ModName()
{
	static constexpr std::string_view kModInfoSecName = ".modinfo";
	static constexpr std::string_view kModNameTag = "name=";

	// if this for-loop fails to find mod section, it throws exception in
	// the loop.
	size_t i;
	for (i = 0; SectionName(i) != kModInfoSecName; i++)
		;

	Elf_Data *elf_data = GetElfSectionData(i);
	std::string_view mod_info(static_cast<char *>(elf_data->d_buf),
				  elf_data->d_size);

	// search for "name=${kernel_module_name}"
	size_t mod_name_start = mod_info.find(kModNameTag) + kModNameTag.size();
	size_t mod_name_end = mod_info.find('\0', mod_name_start);
	return std::string(
		mod_info.substr(mod_name_start, mod_name_end - mod_name_start));
}

void ElfBin::UpdateSection(size_t sec_idx, void *data, size_t size)
	noexcept(false)
{
	Elf_Data *elf_data = GetElfSectionData(sec_idx);

	elf_data->d_buf = data;
	elf_data->d_size = size;

	if (!elf_flagdata(elf_data, ELF_C_SET, ELF_F_DIRTY)) {
		throw_gelf_error();
	}
}

std::unique_ptr<std::vector<char> > ElfBin::GetSection(size_t sec_idx)
{
	Elf_Data *elf_data = GetElfSectionData(sec_idx);
	char *d_buf = static_cast<char *>(elf_data->d_buf);
	return std::make_unique<std::vector<char> >(d_buf,
						    d_buf + elf_data->d_size);
}

size_t ElfBin::GetStringSectionIndex() noexcept(false)
{
	size_t idx;
	if (elf_getshdrstrndx(elf_, &idx)) {
		throw_gelf_error();
	}

	return idx;
}

void ElfBin::UpdateRela(size_t section_id,
			std::vector<ElfRela::RelaEntry> *rela_vector)
	noexcept(false)
{
	GElf_Shdr rela_header = {};
	Elf_Scn *scn = nullptr;
	while ((scn = elf_nextscn(elf_, scn))) {
		gelf_getshdr(scn, &rela_header);
		if (rela_header.sh_type == SHT_RELA &&
		    rela_header.sh_info == section_id)
			break;
	}
	if (scn == nullptr) {
		throw std::error_code{ ElfErrorCode::RELA_SECTION_NOT_FOUND };
	}

	Elf_Data *data = elf_getdata(scn, nullptr);
	if (!data) {
		throw_gelf_error();
	}

	if (!elf_flagdata(data, ELF_C_SET, ELF_F_DIRTY)) {
		throw_gelf_error();
	}

	data->d_buf = rela_vector->data();
	data->d_size = rela_vector->size() * sizeof(ElfRela::RelaEntry);

	rela_header.sh_size = rela_vector->size() * sizeof(ElfRela::RelaEntry);

	if (!gelf_update_shdr(scn, &rela_header)) {
		throw_gelf_error();
	}
}

void ElfBin::CreateKlpRela(size_t section_id, size_t symtab_id,
			   size_t section_name,
			   std::vector<ElfRela::RelaEntry> *rela_vector)
	noexcept(false)
{
	Elf_Scn *scn = elf_newscn(elf_);
	if (!scn) {
		throw_gelf_error();
	}

	Elf_Data *data = elf_newdata(scn);
	if (!data) {
		throw_gelf_error();
	}

	if (!elf_flagdata(data, ELF_C_SET, ELF_F_DIRTY)) {
		throw_gelf_error();
	}

	data->d_type = ELF_T_RELA;
	data->d_buf = rela_vector->data();
	data->d_size = rela_vector->size() * sizeof(ElfRela::RelaEntry);

	GElf_Shdr shdr;
	if (!gelf_getshdr(scn, &shdr)) {
		throw_gelf_error();
	}

	shdr.sh_name = section_name;
	// id for text section that needs relocation.
	shdr.sh_info = section_id;
	// id for symbol table.
	shdr.sh_link = symtab_id;
	shdr.sh_type = SHT_RELA;
	shdr.sh_entsize = sizeof(ElfRela::RelaEntry);
	shdr.sh_size = rela_vector->size() * shdr.sh_entsize;
	shdr.sh_addralign = 8;
	shdr.sh_flags = kSectionFlagRelaLivepatch | SHF_INFO_LINK | SHF_ALLOC;

	if (!gelf_update_shdr(scn, &shdr)) {
		throw_gelf_error();
	}
}

void ElfBin::ElfUpdate() noexcept(false)
{
	if (elf_update(elf_, ELF_C_WRITE) < 0) {
		throw_gelf_error();
	}
}
