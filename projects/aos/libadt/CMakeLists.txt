#
# Copyright 2019, Data61
# Commonwealth Scientific and Industrial Research Organisation (CSIRO)
# ABN 41 687 119 230.
#
# This software may be distributed and modified according to the terms of
# the GNU General Public License version 2. Note that NO WARRANTY is provided.
# See "LICENSE_GPLv2.txt" for details.
#
# @TAG(DATA61_GPL)
#
cmake_minimum_required(VERSION 3.7.2)

project(libadt C)

add_library(adt EXCLUDE_FROM_ALL src/dynamic.c src/priority_q.c src/dynamicQ.c)
target_include_directories(adt PUBLIC include)
target_link_libraries(adt Configuration muslc sel4 utils)
