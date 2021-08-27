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
/*
 * This file implements default callbacks for kernel livepatch. This file
 * is one of default inputs to `llpatch` for livepatch generation. If
 * custom callback functions are required, the following callback functions
 * can be customized and the updated llpatch-callbacks.c can be given to
 * `llpatch` when generating kernel livepatch. For `llpatch`, check
 * '-c|--callbacks' commandline option.
 *
 * While customizing the callbacks, access to global variables defined in
 * ${kmod} and/or vmlinux, two macros from llpatch.h can be entertained.
 * Those are LLPATCH_DECLARE_SYMBOL and LLPATCH_SYMBOL. Please see
 * llpatch.h for the macros and examples.
 *
 * For background, see kernel's Documentation/livepatch/callbacks.txt.
 *
 * IMPORTANT NOTES:
 *  - DO NOT change name of callback functions. their names are used in
 *    auto-generated livepatch wrapper.
 *  - DO NOT remove any callback functions here. they are used in
 *    auto-generated livepatch wrapper.
 *    auto-generated livepatch wrapper.
 *  - DO NOT remove any headers included here
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/livepatch.h>

#include "llpatch.h"

/* Executed on object patching */
static int pre_patch_callback(struct klp_object *obj)
{
	return 0;
}

/* Executed after object patching */
static void post_patch_callback(struct klp_object *obj)
{
}

/* Executed on object unpatching */
static void pre_unpatch_callback(struct klp_object *obj)
{
}

/* Executed after object unpatching */
static void post_unpatch_callback(struct klp_object *obj)
{
}
