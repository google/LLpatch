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
#include "align_command.h"

#include <argp.h>

#include <fstream>
#include <limits>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "auto_cleanup.h"
#include "llvm/Support/raw_ostream.h"

namespace
{
struct AlignArgs {
	char *diffed_file = nullptr;
	char *original_c = nullptr;
	char *patched_c = nullptr;
	char *patch = nullptr;
	char *suffix = nullptr;
};

const char kAlignArgsDoc[] = "<original.c> <patched.c>";
const char kAlignPrgDoc[] = "common align options:\n";
const struct argp_option kAlignOptions[] = {
	// name, key, arg, flags, doc,
	{ /*name=*/"diffed_file", /*key=*/'d', /*arg=*/"DIFFED_FILE",
	  /*flag=*/0, /*doc=*/"Filename for diffed file" },
	{ /*name=*/"patch", /*key=*/'p', /*arg=*/"PATCH",
	  /*flag=*/0, /*doc=*/"Patch file" },
	{ /*name=*/"suffix", /*key=*/'s', /*arg=*/"SUFFIX",
	  /*flag=*/0, /*doc=*/"Suffix for output file" },
	{ nullptr }
};

constexpr std::string_view kDefaultAlignSuffix = "__aligned";

error_t ParseAlignOpt(int key, char *arg, struct argp_state *state)
{
	AlignArgs *args = static_cast<AlignArgs *>(state->input);

	switch (key) {
	case 'd':
		args->diffed_file = arg;
		break;
	case 'p':
		args->patch = arg;
		break;
	case 's':
		args->suffix = arg;
		break;
	case ARGP_KEY_ARG:
		if (!args->original_c) {
			args->original_c = arg;
		} else if (!args->patched_c) {
			args->patched_c = arg;
		} else {
			argp_usage(state);
		}
		break;
	case ARGP_KEY_END:
		if (!args->diffed_file) {
			llvm::errs()
				<< "filename for diffed file is not given\n";
			argp_usage(state);
		}
		if (!args->original_c) {
			llvm::errs() << "<original.c> is not given\n";
			argp_usage(state);
		}
		if (!args->patched_c) {
			llvm::errs() << "<patched.c> is not given\n";
			argp_usage(state);
		}
		if (!args->patch) {
			llvm::errs() << "patch file is not given\n";
			argp_usage(state);
		}
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

// Reads in_file and skips lines till read line starts with the given marker. Once this
// hits the marker, it returns the marked line. If it hits the end of file or stopper, it
// returns "".
std::string SkipToMarker(std::fstream &in_file, std::string_view marker,
			 std::string_view stopper = "")
{
	const std::regex marker_regex(marker.data());
	const std::regex stopper_regex(stopper.data());
	std::string line;
	while (std::getline(in_file, line)) {
		if (std::regex_search(line, marker_regex)) {
			return line;
		}
		if (!stopper.empty() &&
		    std::regex_search(line, stopper_regex)) {
			return "";
		}
	}
	return "";
}

void CopyLines(std::fstream &in_file, std::fstream &out_file, size_t lines)
{
	std::string line;
	for (size_t i = 0; i < lines && std::getline(in_file, line); i++) {
		out_file << line << std::endl;
	}
}

void AddEmptyLines(std::fstream &out_file, size_t lines)
{
	out_file << std::string(lines, '\n');
}

std::pair</*offset*/ size_t, /*lines*/ size_t>
GetOffsetLinesPair(const std::string &pair)
{
	// format of pair: [-+]${line#},${lines_changed}
	const auto pos = pair.find(',');
	std::string offset = pair.substr(1, pos - 1);
	std::string lines = pair.substr(pos + 1);

	return { std::stoi(offset), std::stoi(lines) };
}

// offset in Patch is absolute value from the file start. convert it to relative offset
// relative to the last changed. Do this because empty lines are added.
void ConvertToRelativeOffset(std::vector<AlignCommand::Patch> *patches)
{
	size_t last_patch_line = 0;
	const size_t patch_size = patches->size();
	for (size_t i = 0; i < patch_size; i++) {
		auto [offset, lines] = (*patches)[i];
		(*patches)[i] = { offset - last_patch_line, lines };
		last_patch_line = offset;
	}
}

std::tuple</*original*/ std::vector<AlignCommand::Patch>,
	   /*patched*/ std::vector<AlignCommand::Patch>,
	   /*patch context*/ std::vector<size_t> >
ParsePatchFile(const std::string &patch, const std::string &original)
	noexcept(false)
{
	std::fstream file(patch.data(), std::ios::in);
	if (!file.is_open()) {
		throw std::error_code{ errno, std::system_category() };
	}
	AutoCleanup fd_close([&file_ = file]() { file_.close(); });

	// format: diff -.* ${file1} ${file2}
	// NOTE: patch file can be generated without using 'git diff'
	// command. So, regular expression for matching.
	std::vector<AlignCommand::Patch> original_patch;
	std::vector<AlignCommand::Patch> patched_patch;
	std::vector<size_t> patch_context;

	static constexpr std::string_view diff_head = "^diff -.*";
	std::string diff_file_head = std::string(diff_head) + original + ".*";
	std::string line = SkipToMarker(file, diff_file_head);
	if (line.empty()) {
		// This happens when .c file includes "changed" header file.
		return {original_patch, patched_patch, patch_context};
	}

	size_t offset, lines;
	std::string token;
	for (line = SkipToMarker(file, "^@@", diff_head); line != "";
	     line = SkipToMarker(file, "^@@", diff_head)) {
		// format: @@ -${line#},${lines_changed} +${line#},{lines_changed} @@ ...
		// e.g.,: @@ -37,16 +37,17 @@ ...

		std::istringstream iss(line);
		std::getline(iss, token, ' '); // @@. so simply skip

		std::getline(iss, token, ' '); // -${line#},${lines_changed}
		std::tie(offset, lines) = GetOffsetLinesPair(token);
		original_patch.emplace_back(offset, lines);

		std::getline(iss, token, ' '); // +${line#},${lines_changed}
		std::tie(offset, lines) = GetOffsetLinesPair(token);
		patched_patch.emplace_back(offset, lines);

		// Then, there are few lines for patch context before actual changes are
		// specified.
		size_t i;
		for (i = 0; std::getline(file, line); i++) {
			if (line.find('-') == 0 || line.find('+') == 0) {
				break;
			}
		}
		// the line of context at ${line#}. so, -1 from the context
		patch_context.push_back(i ? i - 1 : 0);
	}

	ConvertToRelativeOffset(&original_patch);
	ConvertToRelativeOffset(&patched_patch);

	return { original_patch, patched_patch, patch_context };
}

} // namespace

AlignCommand::AlignCommand(int argc, char **argv) noexcept(false)
{
	if (argc < 1) {
		throw std::error_code{ ErrorCode::NOT_ENOUGH_ARGS };
	}

	AlignArgs arguments;
	struct argp argp = { /*options=*/kAlignOptions,
			     /*parser=*/ParseAlignOpt,
			     /*args_doc=*/kAlignArgsDoc,
			     /*args_doc=*/kAlignPrgDoc };

	// First argument is a command, 'align' and it's already consumed. So,
	// argv[0] = argv[0] + argv[1] to let others used for options.
	std::string command = std::string(argv[0]) + " " + argv[1];
	--argc;
	++argv;
	argv[0] = const_cast<char *>(command.c_str());
	argp_parse(&argp, argc, argv, /*flags=*/0,
		   /*arg_index=*/nullptr, /*input=*/&arguments);

	diffed_file_ = arguments.diffed_file;
	original_filename_ = arguments.original_c;
	patched_filename_ = arguments.patched_c;
	patch_filename_ = arguments.patch;
	output_suffix_ =
		arguments.suffix ? arguments.suffix : kDefaultAlignSuffix;
}

std::error_code AlignCommand::Run()
{
	auto [original, patched, context] =
		ParsePatchFile(patch_filename_, diffed_file_);

	AlignFile(original_filename_, original, patched, context);
	AlignFile(patched_filename_, patched, original, context);

	return Command::ErrorCode::NO_ERROR;
}

void AlignCommand::AlignFile(std::string_view filename,
			     const std::vector<Patch> &from,
			     const std::vector<Patch> &to,
			     const std::vector<size_t> &context)
{
	std::fstream in_file(filename.data(), std::ios::in);
	if (!in_file.is_open()) {
		throw std::error_code{ errno, std::system_category() };
	}
	AutoCleanup in_fd_close([&in_file_ = in_file]() { in_file_.close(); });

	std::fstream out_file(std::string(filename) + output_suffix_,
			      std::ios::out);
	if (!out_file.is_open()) {
		throw std::error_code{ errno, std::system_category() };
	}
	AutoCleanup out_fd_close(
		[&out_file_ = out_file]() { out_file_.close(); });

	size_t patch_size = from.size();
	std::string line;
	for (size_t i = 0; i < patch_size; i++) {
		// NOTE: *offset is relative offset from the last change
		auto [from_file_offset, from_file_lines] = from[i];
		auto [to_file_offset, to_file_lines] = to[i];
		CopyLines(in_file, out_file, from_file_offset);
		if (from_file_lines < to_file_lines) {
			// Skip over patch contexts and add empty lines after the contexts.
			CopyLines(in_file, out_file, context[i]);
			AddEmptyLines(out_file,
				      to_file_lines - from_file_lines);
		}
	}
	// Copy to the end
	CopyLines(in_file, out_file, std::numeric_limits<size_t>::max());
}
