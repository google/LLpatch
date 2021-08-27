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
#ifndef LLPATCH_H_
#define LLPATCH_H_

/*
 * This macro provides a way to access global variables defined in kmod or
 * vmlinux. This is highly useful if livepatch wants to access the variables
 * before/after applying the livepatch.  One possible example would be to
 * access/obtain a lock before applying the livepatch. This also can be used to
 * add "shadow" variables for livepatch. Accessing the original bindings of the
 * variables which the patch will redefine, to allow a constructor for a new
 * shadow variable to initialize it from old value For background on the shadow
 * variable, see kernel's Documentation/livepatch/shadow-vars.txt.
 *
 * The following example shows how to access global variable defined in
 * ${KDIR}/kernel/livepatch/test/test-attr-apple.c in livepatch.c
 *
 * // in kernel/livepatch/test/test-attr-apple.c, part of test-klp.ko
 * static char fruit[PAGE_SIZE] = "apple";
 *
 * // in the livepatch.c, livepatch wrapper
 * typedef char char_array_t[PAGE_SIZE];
 *
 * LLPATCH_DECLARE_SYMBOL(char_array_t, my_fruit, kernel/livepatch/test/test-attr-apple.c, fruit);
 * ...
 * pr_info("hello llpatch symbol: %s\n", LLPATCH_SYMBOL(my_fruit));
 *
 * IMPORTANT NOTES:
 *   - __DO NOT__ put comments in this macro
 *   - use the exactly __SAME__ type for variables to be accessed
 *   - SYMBOL_PATH and SYMBOL_NAME specified here is processed by `gen-symbol-map`
 */
#define LLPATCH_DECLARE_SYMBOL(\
	SYMBOL_TYPE, SYMBOL_ALIAS, SYMBOL_PATH, SYMBOL_NAME) \
	extern SYMBOL_TYPE __llpatch_symbol_ ## SYMBOL_ALIAS

#define LLPATCH_SYMBOL(SYMBOL_ALIAS) \
	__llpatch_symbol_ ## SYMBOL_ALIAS

#endif // LLPATCH_H_
