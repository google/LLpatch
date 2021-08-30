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
 * This file implements example code to show
 *
 *   1) how to implement callbacks for kernel livepatch,
 *
 *   2) how to access global variables defined in kernel module or vmlinux.
 *
 * For more details, refer to ${llpatch}/templates/llpatch.h and
 * ${llpatch}/templates/llpatch-callbacks.c
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/livepatch.h>

#include "llpatch.h"

typedef char c_array_t[PAGE_SIZE];

// c_array_t: type of global variable defined in kernel module
// apple_fruit: alias to the variable
// kernel/livepatch/test/test-attr-apple.c: path to the file w/ the variable
// fruit: name of variable in the file for the kernel module
LLPATCH_DECLARE_SYMBOL(c_array_t, apple_fruit,
		kernel/livepatch/test/test-attr-apple.c, fruit);

LLPATCH_DECLARE_SYMBOL(
		c_array_t, banana_fruit,
		kernel/livepatch/test/test-attr-banana.c, fruit);

#define SHADOW_DATA_ID 1

/* Executed on object patching (ie, patch enablement) */
static int pre_patch_callback(struct klp_object *obj)
{
	char *shadow_apple, *shadow_banana;

	pr_info("\nAccessing llpatch symbol in %s\n", __func__);

	pr_info("apple_fruit is %s at %p\n",
		LLPATCH_SYMBOL(apple_fruit),
		LLPATCH_SYMBOL(apple_fruit));
	pr_info("banana_fruit is %s at %p\n",
		LLPATCH_SYMBOL(banana_fruit),
		LLPATCH_SYMBOL(banana_fruit));

	/*
	 * @obj:	pointer to parent object
	 * @id:		data identifier
	 * @size:	size of attached data
	 * @gfp_flags:	GFP mask for allocation
	 * @ctor:	custom constructor to initialize the shadow data (optional)
	 * @ctor_data:	pointer to any data needed by @ctor (optional)
	 */
	shadow_apple = klp_shadow_alloc(/*obj*/LLPATCH_SYMBOL(apple_fruit),
			/*id*/SHADOW_DATA_ID,
			/*size*/sizeof(c_array_t),
			/*gfp_flags*/GFP_KERNEL,
			/*ctor*/NULL, /*ctor_data*/NULL);
	strcpy(shadow_apple, "Pink Lady");
	pr_info("shadow for apple is added\n");

	shadow_banana = klp_shadow_alloc(/*obj*/LLPATCH_SYMBOL(banana_fruit),
			/*id*/SHADOW_DATA_ID,
			/*size*/sizeof(c_array_t),
			/*gfp_flags*/GFP_KERNEL,
			/*ctor*/NULL, /*ctor_data*/NULL);
	strcpy(shadow_banana, "Plantain");
	pr_info("shadow for banana is added\n");

	pr_info("Done in %s\n\n", __func__);

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
	pr_info("\nAccessing llpatch symbol in %s\n", __func__);

	pr_info("freeing shadow data for apple at %p\n", LLPATCH_SYMBOL(apple_fruit));
	klp_shadow_free(LLPATCH_SYMBOL(apple_fruit), SHADOW_DATA_ID, NULL);

	pr_info("freeing shadow data for banana at %p\n", LLPATCH_SYMBOL(banana_fruit));
	klp_shadow_free(LLPATCH_SYMBOL(banana_fruit), SHADOW_DATA_ID, NULL);

	pr_info("Done in %s\n\n", __func__);
}
