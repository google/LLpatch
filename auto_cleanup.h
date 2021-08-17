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
#ifndef AUTO_CLEANUP_H_
#define AUTO_CLEANUP_H_

#include <functional>

// This class is a helper class to implement RAII (Resource Acquisition Is
// Initialization) for non class object, such as file descriptor by
// open(). When the object for this class gets out of scope, it checks
// internal flag, enabled_, and runs cleanup_ function.
class AutoCleanup final {
    public:
	AutoCleanup(std::function<void()> cleanup)
		: cleanup_(std::move(cleanup))
	{
	}
	~AutoCleanup()
	{
		if (enabled_) {
			cleanup_();
		}
	}

	// Don't allow copy.
	AutoCleanup(const AutoCleanup &rhs) = delete;
	AutoCleanup &operator=(const AutoCleanup &rhs) = delete;

	void Disable()
	{
		enabled_ = false;
	}

    private:
	const std::function<void()> cleanup_;
	bool enabled_ = true;
};

#endif // AUTO_CLEANUP_H_
