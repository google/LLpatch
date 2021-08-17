#!/usr/bin/env bash
#
# Copyright 2021 Google LLC
# 
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# 
#     https://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Author: yonghyun@google.com (Yonghyun Hwang)
#
# Library for shell script to generate a kernel livepatch

#-------------------------------------------------------------
# Shell setting
#-------------------------------------------------------------
set -E

#-------------------------------------------------------------
# Global variables
#-------------------------------------------------------------
declare -r G_LOG_HEADER="${1}"
declare -r G_MESG_COLOR_RED=$(tput setaf 1)
declare -r G_MESG_COLOR_GREEN=$(tput setaf 2)
declare -r G_MESG_COLOR_YELLOW=$(tput setaf 3)
declare -r G_MESG_COLOR_NONE=$(tput sgr0)
declare -r G_PREFIX_LIVEPATCH="llpatch"

declare -r G_UTIL_SUFFIX_PATCH="patch"

#-------------------------------------------------------------
# Function definitions
#-------------------------------------------------------------
function util::log_ok()
{
	echo -e "${G_MESG_COLOR_GREEN}${G_LOG_HEADER}[ OK |$(date +'%m/%d %H:%M')]::${G_MESG_COLOR_NONE} $@"
	return 0
}

function util::log_warn()
{
	echo -e "${G_MESG_COLOR_YELLOW}${G_LOG_HEADER}[WARN|$(date +'%m/%d %H:%M')]::${G_MESG_COLOR_NONE} $@" >&2
	return 0
}

function util::log_info()
{
	echo "${G_LOG_HEADER}[INFO|$(date +'%m/%d %H:%M')]:: $@"
	return 0
}

function util::log_error()
{
	echo -e "${G_MESG_COLOR_RED}${G_LOG_HEADER}[ERR |$(date +'%m/%d %H:%M')]::${G_MESG_COLOR_NONE} $@" >&2
	return 0
}

function util::error()
{
	util::log_error "$@"
	return 1
}

function util::get_livepatch_name()
{
	local -r PATCH_NAME="${1}"
	local livepatch_name="$(basename "${PATCH_NAME}")"
	# sysfs is not happy with [-.] for the name of kernel modules.
	echo "${livepatch_name%.*}" | tr '\-.' '__'
}

# This funciton inputs a name of a patch file and generates a name of .ko file
# for kernel livepatch. This is the rule from a comment in kpatch-build:
# Only allow alphanumerics and '_' and '-' in the module name.  Everything else
# is replaced with '-'.  Also truncate to 48 chars so the full name fits in the
# kernel's 56-byte module name array.
function util::get_livepatch_ko_name()
{
	local -r PATCH_NAME="${1}"
	local ko_name="$(util::get_livepatch_name "${PATCH_NAME}")"
	ko_name="${G_PREFIX_LIVEPATCH}-${ko_name//[^a-zA-Z0-9_-]/-}"
	echo "${ko_name:0:56-${#G_PREFIX_LIVEPATCH}-1}.ko"
}

function util::get_livepatch_mod_name()
{
	local -r PATCH_NAME="${1}"
	echo "${G_PREFIX_LIVEPATCH}_$(util::get_livepatch_name "${PATCH_NAME}")"
}

# this function parses map elements that is a single string. The string
# consists of multiple elements seperated by seperator.
function util::parse_map_elements()
{
	local -r ELEM_SEP="${1}"
	local elem_list="${2}"

	# elem_list could start with ${ELEM_SEP}. so, remove it first
	elem_list=${elem_list#${ELEM_SEP}}

	local -a ret_list=()
	local elem=""
	while true; do
		elem=${elem_list%%${ELEM_SEP}*}
		elem_list=${elem_list#*${ELEM_SEP}}
		ret_list+=("${elem}")
		if [[ "${elem}" == "${elem_list}" ]]; then
			break
		fi
	done
	echo "${ret_list[@]}"
}
