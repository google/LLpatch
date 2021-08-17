# -------------------------------------------------------------------------
# Makefile: makefile template
#
# Copyright 2016 by Yonghyun Hwang <freeaion@gmail.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.  Some rights reserved.
# -------------------------------------------------------------------------

# 0: list of source codes
#    - if specified, only those are compiled
#    - if not specified, all *.c, *.cpp, and *.cc will be compiled
SRCS           =

# 1: list of sub directories
#    - if specified,	make recursively for the sub dirs
SUB_DIRS	      =

# 2: list of compile options (e.g., -Ddefine, -Iinc, ...)
LOCAL_CXXFLAGS = -I/usr/lib/llvm-11/include
LOCAL_CFLAGS   =

# 3: list of link options (e.g., -lm, -Labc, ...)
LOCAL_LIB      = -L/usr/lib/llvm-11/lib -lLLVM

# 4: name for a.out or library
#    - specify LIB_NAME if you want to create libLIB_NAME.so out of your SRCS
#    - specify EXE if you want to create executable binary w/ the name, EXE
#    - DO __NOT__ specify both of them. if nothing specified, EXE = project
LIB_NAME       = llvm-diff
EXE            = 

# 5: specify compiler
#    - if not specified, CC = gcc, CXX= g++
CC             =
CXX            = clang++

# 6: specify path to dir w/ Makefile.rules and Makefile.macros in it
PRJ_ROOT_DIR  ?= ../..

# --------------------------------------------
# Magic part: please don't touch if you don't know what you are doing
# --------------------------------------------
include $(PRJ_ROOT_DIR)/Makefile.rules
$(eval $(call play_magic,$(EXE)))
