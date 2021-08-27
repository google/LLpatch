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
#include "symbol_map.h"

#include <cctype>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "auto_cleanup.h"
#include "command.h"

#include <iostream>

namespace
{
// Tokenize a given line from the output of `gen-symbol-map`.
std::vector<std::string> TokenizeSymbolLine(const std::string &line)
{
	std::vector<std::string> tokens;
	static const char *delim = " ";
	char *token = std::strtok(const_cast<char *>(line.c_str()), delim);
	while (token != nullptr) {
		tokens.push_back(std::string(token));
		token = std::strtok(nullptr, delim);
	}
	return tokens;
}
} // namespace

std::unique_ptr<SymbolMap> SymbolMap::Create(const std::string &filename)
{
	if (filename.empty())
		return nullptr;

	return std::make_unique<SymbolMap>(filename);
}

SymbolMap::SymbolMap(std::string_view filename) noexcept(false)
{
	std::fstream file(filename.data(), std::ios::in);
	if (!file.is_open()) {
		throw std::error_code{ errno, std::system_category() };
	}
	AutoCleanup fd_close([&file_ = file]() { file_.close(); });

	std::string line;
	while (getline(file, line)) {
		auto tokens = TokenizeSymbolLine(line);
		if (tokens.size() != ElemIndex::NUM_OF_ELEMS + 1) {
			throw std::error_code{
				Command::ErrorCode::INVALID_SYM_MAP
			};
		}
		std::array<std::string, ElemIndex::NUM_OF_ELEMS> sym_entry = {
			tokens[ElemIndex::MOD_NAME],
			tokens[ElemIndex::PATH],
			tokens[ElemIndex::SYMBOL],
		};
		// last element is an alias to a symbol
		symbol_entries_.emplace(tokens[ElemIndex::NUM_OF_ELEMS],
					std::move(sym_entry));
	}
}

const std::array<std::string, SymbolMap::ElemIndex::NUM_OF_ELEMS> &
SymbolMap::QueryAlias(const std::string &alias)
{
	if (symbol_entries_.find(alias) == symbol_entries_.end()) {
		throw std::error_code{ Command::ErrorCode::INVALID_SYM_MAP };
	}

	return symbol_entries_[alias];
}
