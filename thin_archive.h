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
#ifndef THIN_ARCHIVE_H_
#define THIN_ARCHIVE_H_

#include <list>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <unordered_map>

// This class parses an output file of `nm` to construct internal database
// for querying symbol along with name of the file that has the symbol in
// it. The class assumes the "posix" output format by `nm -f posix`. The
// format is as follows;
//
// ${thin_archive_name}.a[${full_path_to_obj_file}]:
// ${symbol} ${symbol_type} ${symbol_value}
// ${symbol1} ${symbol_type1} ${symbol_value1}
// ...
// ${thin_archive_name}.a[${full_path_to_obj_file2}]:
// ...
// built-in.a[arch/x86/events/intel/core.o]:
// allow_tsx_force_abort d 2b8 1
// any_show t 38f0 24
//
// This class takes ${full_path_to_obj_file} and ${symbol}s only to service the query. To
// generate the text file, output of nm, for this class, use the following command.
//
// $ nm -f posix --defined-only ${built-in}.a
class ThinArchive final {
    public:
	ThinArchive(std::string_view filename) noexcept(false);
	~ThinArchive() = default;

	// Don't allow copy.
	ThinArchive(const ThinArchive &rhs) = delete;
	ThinArchive &operator=(const ThinArchive &rhs) = delete;

	// Returns pos for given symbol and filename. The pos is used to construct KLP
	// symbol name or pos for livepatched function.  Note that internal database for
	// symbols is aware of relocatable symbols, such as UND, OBJECT and FUNC. filename
	// is used to match duplicated symbols. NOTE that filename is ignored if symbol is
	// unique. If no symbol found, returns negative value.
	int QuerySymbol(const std::string &symbol, const std::string &filename);

	static std::unique_ptr<ThinArchive> Create(const std::string &filename);

    private:
	std::unordered_set<std::string> unique_symbols_;
	// key: symbol name, value: list of filename
	std::unordered_map<std::string, std::list<std::string> >
		duplicated_symbols_;
};

#endif // THIN_ARCHIVE_H_
