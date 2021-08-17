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
# -------------------------------------------------------------------------
# Makefile: makefile template
#
# Author: yonghyun@google.com (Yonghyun Hwang)
# -------------------------------------------------------------------------

# 0: list of source codes
#    - if specified, only those are compiled
#    - if not specified, all *.c, *.cpp, and *.cc will be compiled
SRCS           =

# 1: list of sub directories
#    - if specified,	make recursively for the sub dirs
SUB_DIRS	      = third_party/llvm-diff

# 2: list of compile options (e.g., -Ddefine, -Iinc, ...)
LOCAL_CXXFLAGS = -I/usr/lib/llvm-11/include -std=c++17
LOCAL_CFLAGS   =

# 3: list of link options (e.g., -lm, -Labc, ...)
LOCAL_LIB      = -L/usr/lib/llvm-11/lib -Lthird_party/llvm-diff -lllvm-diff -lLLVM -lelf

# 4: name for a.out or library
#    - specify LIB_NAME if you want to create libLIB_NAME.so out of your SRCS
#    - specify EXE if you want to create executable binary w/ the name, EXE
#    - DO __NOT__ specify both of them. if nothing specified, EXE = project
LIB_NAME       =
EXE            = livepatch

# 5: specify compiler
#    - if not specified, CC = gcc, CXX= g++
CC             =
CXX            = clang++

# 6: specify path to dir w/ Makefile.rules and Makefile.macros in it
PRJ_ROOT_DIR  ?= .

# --------------------------------------------
# Magic part: please don't touch if you don't know what you are doing
# --------------------------------------------
include $(PRJ_ROOT_DIR)/Makefile.rules
$(eval $(call play_magic,$(EXE)))
