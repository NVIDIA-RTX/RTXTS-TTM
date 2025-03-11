# Copyright (c) 2025, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

project(rtxts-ttm)

file(GLOB rtxts-ttm_include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rtxts-ttm/*.h)
file(GLOB rtxts-ttm_src
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_library(rtxts-ttm STATIC
        ${rtxts-ttm_include}
        ${rtxts-ttm_src})
