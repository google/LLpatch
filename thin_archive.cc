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

#include <cctype>
#include <fstream>
#include <regex>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>

#include "auto_cleanup.h"
#include "elf_error.h"
#include "llvm/Support/raw_ostream.h"

namespace
{
// Parses a given line assuming that line complies w/ posix output by nm.
// Note that returned symbol type is always 'W' if it's weak symbol or object.
std::pair</*symbol name*/std::string, /*symbol type*/char>
ParseSymbolLine(const std::string& line)
{
	size_t sym_end = line.find(' ');
	std::string symbol_name = line.substr(0, sym_end);

	char symbol_type = '?';
	if (sym_end != std::string::npos) {
		size_t sym_type_pos = line.find_first_not_of(' ', sym_end);
		symbol_type = toupper(line.at(sym_type_pos));
	}

	// V: The symbol is a weak object. W: The symbol is a weak symbol.
	// We need to know whether symbol is weak or not. So, use 'W' only
	symbol_type = (symbol_type == 'V') ? 'W' : symbol_type;
	return std::make_pair(symbol_name, symbol_type);
}
} // namespace

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
	std::unordered_set<std::string> non_weak_symbols;
	std::string line;
	while (getline(file, line)) {
		auto [symbol_name, symbol_type] = ParseSymbolLine(line);
		if (unique_symbols_.find(symbol_name) ==
		    unique_symbols_.end()) {
			unique_symbols_.emplace(symbol_name);

			if (symbol_type != 'W') {
				non_weak_symbols.emplace(std::move(symbol_name));
			}
			continue;
		}

		if (symbol_type == 'W') {
			continue;
		}

		// If symbol in unique_symbols_ is not weak, it's duplicated symbol.
		if (non_weak_symbols.find(symbol_name) !=
			non_weak_symbols.end()) {
			dup_symbols.emplace(std::move(symbol_name));
		}

		non_weak_symbols.emplace(std::move(symbol_name));
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
		if (unique_symbols_.find(symbol_name) != unique_symbols_.end()) {
			continue;
		}

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
