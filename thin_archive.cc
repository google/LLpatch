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
#include "thin_archive.h"

#include <fstream>
#include <regex>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>

#include "auto_cleanup.h"
#include "elf_error.h"
#include "llvm/Support/raw_ostream.h"

std::unique_ptr<ThinArchive> ThinArchive::Create(const std::string &filename)
{
	if (filename.empty())
		return nullptr;

	return std::make_unique<ThinArchive>(filename);
}

ThinArchive::ThinArchive(std::string_view filename) noexcept(false)
{
	std::fstream file(filename.data(), std::ios::in);
	if (!file.is_open()) {
		throw std::error_code{ errno, std::system_category() };
	}
	AutoCleanup fd_close([&file_ = file]() { file_.close(); });

	// Two pass algorithm to build unique_symbols_ and duplicated_symbols_
	// Step 1: Build unique_symbols_ while finding duplicated symbols.
	std::unordered_set<std::string> dup_symbols;
	std::string line;
	while (getline(file, line)) {
		std::string symbol_name = line.substr(0, line.find(" "));
		if (unique_symbols_.find(symbol_name) ==
		    unique_symbols_.end()) {
			unique_symbols_.emplace(std::move(symbol_name));
		} else {
			dup_symbols.emplace(std::move(symbol_name));
		}
	}
	for (const std::string &i : dup_symbols) {
		unique_symbols_.erase(i);
	}

	// Step 2: Build duplicated symbols by inserting filename to
	// duplicated_symbols_.
	std::string current_filename;
	// matching format example: built-in.a[arch/x86/kernel/head_64.o]:
	std::regex file_path_match(".+\\.a\\[.+\\.o\\]:");
	std::unordered_set<std::string> same_sym_file;
	std::string symbol_name;
	file.clear();
	file.seekg(0);
	while (getline(file, line)) {
		if (std::regex_match(line, file_path_match)) {
			auto pos_start = line.find("[") + 1;
			auto pos_end = line.find("]");
			current_filename =
				line.substr(pos_start, pos_end - pos_start);
			continue;
		}

		symbol_name = line.substr(0, line.find(" "));
		if (dup_symbols.find(symbol_name) != dup_symbols.end()) {
			auto sym_file = std::string(symbol_name) +
					std::string(current_filename);
			if (same_sym_file.find(sym_file) ==
			    same_sym_file.end()) {
				same_sym_file.insert(sym_file);
			} else {
				// Oops. this ELF has same symbol+filename combination,
				// which cannot be handled. :'( throw exception.
				llvm::outs()
					<< "sym: " << symbol_name
					<< ", filename: " << current_filename
					<< "\n";
				throw std::error_code{
					ElfErrorCode::SAME_SYMBOL_FILENAME
				};
			}

			duplicated_symbols_[symbol_name].push_back(
				current_filename);
		}
	}
}

int ThinArchive::QuerySymbol(const std::string &symbol,
			     const std::string &filename)
{
	if (unique_symbols_.find(symbol) != unique_symbols_.end()) {
		// pos for unique symbols is always 0
		return 0;
	}

	auto dup_symbol = duplicated_symbols_.find(symbol);
	if (dup_symbol != duplicated_symbols_.end()) {
		int pos = 1;
		for (std::string_view fn : dup_symbol->second) {
			if (filename == fn) {
				return pos;
			}
			pos++;
		}
	}

	// No match found for given symbol && filename. return negative value
	// to indicate that.
	return -1;
}
