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
#include "diff_command.h"

#include <argp.h>

#include <memory>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_set>

#include "elf_symbol.h"
#include "third_party/llvm-diff/DifferenceEngine.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;

namespace
{
struct DiffArgs {
	char *original_ll = nullptr;
	char *patched_ll = nullptr;
	char *base_dir = nullptr;
	bool quiet = false;
};

const char kDiffArgsDoc[] = "<original.ll> <patched.ll>";
const char kDiffPrgDoc[] = "common diff options:\n";
const struct argp_option kDiffOptions[] = {
	// name, key, arg, flags, doc,
	{ /*name=*/"quiet", /*key=*/'q', /*arg=*/nullptr,
	  /*flag=*/0, /*doc=*/"Quiet mode. don't output diffed functions" },
	{ /*name=*/"base_dir", /*key=*/'b', /*arg=*/"BASE_DIR",
	  /*flag=*/0, /*doc=*/"The base directory for the diffed files" },
	{ nullptr }
};

constexpr std::string_view kLivepatchPrefix = "__livepatch_";

error_t ParseDiffOpt(int key, char *arg, struct argp_state *state)
{
	DiffArgs *args = static_cast<DiffArgs *>(state->input);

	switch (key) {
	case 'q':
		args->quiet = true;
		break;
	case 'b':
		args->base_dir = arg;
		break;
	case ARGP_KEY_ARG:
		if (!args->original_ll) {
			args->original_ll = arg;
		} else if (!args->patched_ll) {
			args->patched_ll = arg;
		} else {
			argp_usage(state);
		}
		break;
	case ARGP_KEY_END:
		if (!args->original_ll || !args->patched_ll) {
			argp_usage(state);
		}
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

// Loads a LLVM module from a file. On error, nullptr is returned.
std::unique_ptr<Module> LoadModule(LLVMContext &Context, std::string_view Name)
{
	SMDiagnostic Diag;
	return parseIRFile(Name, Diag, Context);
}

// Dumps a LLVM module to a file.
std::error_code DumpModule(std::unique_ptr<Module> output)
{
	std::error_code ec;
	raw_fd_ostream fout(output->getSourceFileName() + "__klp_diff.ll", ec);
	output->print(fout, nullptr);
	return ec;
}

// Returns true if function is assigned to special sections such as .init*
// or .exit*.
bool FuncInSpecialSection(Function *func)
{
	if (!func->hasSection()) {
		return false;
	}

	auto section_name = std::string(func->getSection());
	if (section_name.find(".init") == 0 ||
	    section_name.find(".exit") == 0) {
		return true;
	}

	return false;
}

// There are few LLVM aliases that need to be removed to avoid clang crash.
//
// 1) STATIC_DIRECT_CALLABLE defines alias to function that should have its
//    definition. Unfortuntely, KLP generator removes function's definition
//    and makes the function as 'extern' if it's not changed by .patch
//    file. If alias for extern function is not removed, clang will
//    seg-faults.
// 2) syscall defines LLVM aliases. Here are examples:
//    sys_set_tid_address, sys_clone, sys_unshare, ...
//
void RemoveFuncAlias(Module *mod)
{
	std::vector<GlobalAlias *> alias_removed;

	std::string alias_name;
	for (GlobalAlias &alias : mod->aliases()) {
		alias_name.assign(alias.getName());
		if (alias_name.find("__direct_call") == 0 ||
		    alias_name.find("sys_") == 0) {
			alias_removed.push_back(&alias);
		}
	}
	for (GlobalAlias *alias : alias_removed) {
		alias->removeFromParent();
	}
}

std::error_code DistillDiffFunctions(DiffConsumer *consumer, Module *original,
				     Module *patched, StringRef base_path)
{
	DifferenceEngine diff_engine(*consumer);
	// Assumption: LLVM functions are unique in LLVM module && the iterator
	// returns the unique LLVM function. If this is not the case, pointer
	// for Function should be replace with std::string to have a function
	// name for key.
	std::unordered_set<Function *> klp_func_set;
	std::unordered_set<Function *> new_func_set;
	std::unordered_set<Function *> spc_func_set;

	// Iterate all c functions in the 'patched' and diff them with the
	// functions in the 'original'. The functions with differences are
	// pushed onto sets. This step is required to identify different
	// functions without modification in LLVM module. Note that any
	// modification while diffing could result in new diffs.
	for (Function &RFn : *patched) {
		if (RFn.getName().empty()) {
			// A function is anonymous. Do nothing.
			continue;
		}

		if (FuncInSpecialSection(&RFn)) {
			spc_func_set.insert(&RFn);
			continue;
		}

		Function *LFn = original->getFunction(RFn.getName());
		if (!LFn) {
			new_func_set.insert(&RFn);
			continue;
		}

		diff_engine.diff(LFn, &RFn);
		if (consumer->hadDifferences()) {
			klp_func_set.insert(&RFn);

			// Reset the consumer to detect new differences for the
			// next C function in the patched file.
			consumer->reset();
		}
	}

	if (klp_func_set.empty() && new_func_set.empty()) {
		outs() << "All functions are same but no new functions. Nothing to patch.\n";
		throw std::error_code{ Command::ErrorCode::NOTHING_TO_PATCH };
	}

	// Do not allow livepatch over functions allocated to special sections
	// such as .init*.  remove them here.
	for (Function *func : spc_func_set) {
		func->removeFromParent();
	}

	RemoveFuncAlias(patched);

	// Iterate all c functions in the 'patched' and add specical prefix to
	// function name for livepatched function.
	for (Function &RFn : *patched) {
		if (RFn.getName().empty()) {
			// A function is anonymous. Do nothing.
			continue;
		}

		if (new_func_set.find(&RFn) != new_func_set.end()) {
			// The function is new in the 'patched'. Do nothing.
			continue;
		}

		if (klp_func_set.find(&RFn) != klp_func_set.end()) {
			// Add a special prefix to a function name to specify this function
			// needs to be livepatched. The postfix specifies the
			// source file for this change.
			RFn.setName(std::string(kLivepatchPrefix) +
				    ElfSymbol::CreateLivepatchedFunctionName(RFn, base_path));

			// clang could remove livepatched function during its
			// optimization step. To prevent that, the livepatched function
			// is added to 'llvm.used' list.
			appendToUsed(*patched, &RFn);

			// Livepatched function needs external linkage to be linked to
			// livepatch wrapper. For now, KLP subsystem in kernel doesn't
			// allow changes in the functions with the same name. Hence, we
			// don't worry about name conflicts for static functions at
			// this moment.
			RFn.setLinkage(GlobalValue::ExternalLinkage);
		} else {
			// The function is the same in both the 'patched' and
			// 'original'. Convert it to an extern function by deleting a
			// function body.
			RFn.deleteBody();
		}
	}

	return Command::ErrorCode::NO_ERROR;
}

bool GvarInSpecialSection(GlobalVariable *gvar)
{
	if (!gvar->hasSection()) {
		return false;
	}

	auto section_name = std::string(gvar->getSection());
	if (section_name.find(".discard.func_stack_frame_non_standard") == 0) {
		return true;
	}

	return false;
}

// TODO: One way, which might be a bit better to detect the correct types, is to
// create a dummy global variable that has the correct type to be matched. Then its type
// can be taked to find the other globals that have that type. (It's just a pointer
// comparison, because of the uniqueness of the types.)
//
// echo "DEFINE_STATIC_KEY_FALSE(DUMMY_JUMP_LABEL_GLOBAL)" >> file.c
bool GvarIsJumpLabel(GlobalVariable *gvar)
{
	llvm::Type *gvar_type = gvar->getType();
	std::string type_str;
	llvm::raw_string_ostream llvm_rso(type_str);
	gvar_type->print(llvm_rso);

	if (llvm_rso.str().find("struct.jump_entry") != std::string::npos) {
		return true;
	}

	return false;
}

// Take a vector of regexs and strings for inline assembly. Then removes any
// lines in the assembly matched with the regex.
std::string RemoveInlineAssembly(const std::vector<std::regex> &regex_strs,
				 std::string_view inline_assembly)
{
	std::string original_inline_assembly(inline_assembly);
	std::string inline_assembly_removed = "";
	for (const auto &regex : regex_strs) {
		std::regex_replace(std::back_inserter(inline_assembly_removed),
				   original_inline_assembly.begin(),
				   original_inline_assembly.end(), regex, "");
		original_inline_assembly = inline_assembly_removed;
		inline_assembly_removed = "";
	}

	return inline_assembly_removed;
}

// Remove special global variables for init && exit section, exported symbols.
// Global variable for init && exit section: starts with __init, __exit
// Global variable for exported symbols: starts with __kstrtab, __ksymtab
void RemoveSpecialGlobals(Module *mod)
{
	std::vector<GlobalVariable *> spc_vars;

	for (GlobalVariable &GVR : mod->globals()) {
		auto gvar_name = std::string(GVR.getName());
		if (gvar_name.find("__init") == 0 ||
		    gvar_name.find("__exit") == 0 ||
		    gvar_name.find("__kstrtab") == 0 ||
		    gvar_name.find("__ksymtab") == 0) {
			spc_vars.push_back(&GVR);
		}
	}
	for (GlobalVariable *gvr : spc_vars) {
		gvr->removeFromParent();
	}

	std::vector<std::regex> regex_strs;
	// Exported symbol uses inline assembly to define __crc_${global_var}s and assign them
	// to special sections. Format of inline assembly for exported symbol is as follows;
	//
	//    .section "___kcrctab_gpl+${exported_symbol}", "a"
	//    .weak   __crc_${exported_symbol}
	//    .long   __crc_${exported_symbol}
	//    .previous
	//
	// Remove the inline assembly for exported symbols.
	regex_strs.emplace_back(
		"[ \t]*\\.section.*kcrctab.*\n.*__crc.*\n.*__crc.*\n[ \t]*\\.previous.*\n");

	// Initcall uses inline assembly to instantiate special section. Format of
	// inline assembly for the initcalls is as follows;
	//
	//    .section.*.initcall*"
	//    __initcall_*"
	//    .long*"
	//    .previous.*"
	//
	// Remove the inline assembly for init sections
	regex_strs.emplace_back(
		"[ \t]*\\.section.*initcall.*\n.*__initcall.*\n.*long.*\n[ \t]*\\.previous.*\n");

	std::string inline_assembly =
		RemoveInlineAssembly(regex_strs, mod->getModuleInlineAsm());
	mod->setModuleInlineAsm(inline_assembly);
}

std::error_code DistillDiffGlobals(Module *original, Module *patched, StringRef base_path)
{
	RemoveSpecialGlobals(patched);

	for (GlobalVariable &GVR : patched->globals()) {
		auto gvar_name = std::string(GVR.getName());
		if (gvar_name.find("__const") == 0) {
			// Keep constant value in the 'patched'. This global variable is created by
			// clang when struct has an initial value w/ all constants in it.
			continue;
		}

		if (GvarInSpecialSection(&GVR)) {
			continue;
		}

		if (GVR.isConstant() && GVR.hasInitializer() &&
		    ConstantData::classof(GVR.getInitializer())) {
			// Keep pure constant value in the 'patched'. Pure constant means global
			// variable is defined as constant and its initial value is also constant. So,
			// readonly always.
			continue;
		}

		if (GvarIsJumpLabel(&GVR)) {
			// jump label needs to be sticky to patched file. so, keep them in the
			// patched.
			continue;
		}

		GlobalVariable *GVL = original->getGlobalVariable(
			gvar_name, /*AllowInternal=*/true);
		if (!GVL) {
			// If global variable exists only in the 'patched', do nothing.
			continue;
		}

		// Both the 'original' and 'patched' have the same global variable.
		if (GVL->getType()->getTypeID() !=
		    GVR.getType()->getTypeID()) {
			errs() << "WARN: type of global variable, "
			       << gvar_name << ", is changed\n"
			       << "  type in original: "
			       << GVL->getType()->getTypeID() << "\n"
			       << "  type in patched: "
			       << GVR.getType()->getTypeID() << "\n";
		}

		if (GVL->getAttributes() != GVR.getAttributes()) {
			errs() << "WARN: attributes of global variable, "
			       << gvar_name << ", are changed\n";
		}

		if (GVL->hasInitializer() != GVR.hasInitializer() ||
		    (GVR.hasInitializer() &&
		     GVL->getInitializer()->getValueID() !=
			     GVR.getInitializer()->getValueID())) {
			errs() << "WARN: Initializer mismatch for global variable, "
			       << gvar_name << ".\n";
		}

		GVR.setInitializer(nullptr);
		GVR.setLinkage(GlobalValue::ExternalLinkage);

		if (GVR.isDSOLocal() && GVR.getName() !="__fentry__") {
			GVR.setName(
				ElfSymbol::CreateLivepatchedSymbolName(
					GVR.getName(), original->getSourceFileName(),
					base_path));
		}
	}

	return Command::ErrorCode::NO_ERROR;
}

} // namespace

DiffCommand::DiffCommand(int argc, char **argv) noexcept(false)
{
	if (argc < 1) {
		throw std::error_code{ ErrorCode::NOT_ENOUGH_ARGS };
	}

	DiffArgs arguments;
	struct argp argp = { /*options=*/kDiffOptions, /*parser=*/ParseDiffOpt,
			     /*args_doc=*/kDiffArgsDoc,
			     /*args_doc=*/kDiffPrgDoc };

	// First argument is a command, 'diff' and it's already consumed. So,
	// argv[0] = argv[0] + argv[1] to let others used for options.
	std::string command = std::string(argv[0]) + " " + argv[1];
	--argc;
	++argv;
	argv[0] = const_cast<char *>(command.c_str());
	argp_parse(&argp, argc, argv, /*flags=*/0,
		   /*arg_index=*/nullptr, /*input=*/&arguments);

	original_filename_ = arguments.original_ll;
	patched_filename_ = arguments.patched_ll;
	if (arguments.base_dir) {
		base_dir_ = arguments.base_dir;
	}
	quiet_mode_ = arguments.quiet;
}

std::error_code DiffCommand::Run()
{
	LLVMContext Context;

	std::unique_ptr<Module> OriginalModule =
		LoadModule(Context, original_filename_);
	if (!OriginalModule) {
		errs() << "Original file is not valid LLVM\n";
		return std::error_code{ ErrorCode::INVALID_LLVM_FILE };
	}

	std::unique_ptr<Module> PatchedModule =
		LoadModule(Context, patched_filename_);
	if (!PatchedModule) {
		errs() << "Patched file is not valid LLVM\n";
		return std::error_code{ ErrorCode::INVALID_LLVM_FILE };
	}

	std::unique_ptr<Module> PatchModule = DistillDiff(
		std::move(OriginalModule), std::move(PatchedModule));
	if (!PatchModule) {
		return std::error_code{ ErrorCode::DIFF_FAILED };
	}

	return DumpModule(std::move(PatchModule));
}

std::unique_ptr<Module>
DiffCommand::DistillDiff(std::unique_ptr<Module> original,
			 std::unique_ptr<Module> patched)
{
	DiffConsumer consumer(quiet_mode_ ? nulls() : outs());
	std::error_code ec =
		DistillDiffFunctions(&consumer, original.get(), patched.get(),
				     base_dir_);
	if (ec) {
		return nullptr;
	}

	ec = DistillDiffGlobals(original.get(), patched.get(), base_dir_);
	if (ec) {
		return nullptr;
	}

	return patched;
}
