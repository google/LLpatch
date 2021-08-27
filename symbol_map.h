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
#ifndef SYMBOL_MAP_H_
#define SYMBOL_MAP_H_

#include <array>
#include <list>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

// This class parses an output file of `gen-symbol-map` to construct internal
// database for querying symbol along with name of the file that has the symbol
// in it. The exoected output format by `gen-symbol-map` is as follows;
//
// ${mod_name} ${path_to_c_file} ${symbol} ${llpatch_alias}
// test_klp kernel/livepatch/test/test-attr-apple.c fruit apple_fruit
class SymbolMap final {
    public:
	enum ElemIndex { MOD_NAME = 0, PATH = 1, SYMBOL = 2, NUM_OF_ELEMS = 3 };

	SymbolMap(std::string_view filename) noexcept(false);
	~SymbolMap() = default;

	// Don't allow copy.
	SymbolMap(const SymbolMap &rhs) = delete;
	SymbolMap &operator=(const SymbolMap &rhs) = delete;

	// Returns array of (mod_name, path, symbol) for given llpatch alias
	// name. If no match found, it throws an exception
	const std::array<std::string, NUM_OF_ELEMS> &
	QueryAlias(const std::string &alias) noexcept(false);

	static std::unique_ptr<SymbolMap> Create(const std::string &filename);

    private:
	// key: alias name, value: array of (mod_name, path, symbol)
	std::unordered_map<std::string, std::array<std::string, NUM_OF_ELEMS> >
		symbol_entries_;
};

#endif // SYMBOL_MAP_H_
