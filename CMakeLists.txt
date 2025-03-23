# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32s3_paper_test)
# PSRAMのサポートを追加
target_compile_options(${PROJECT_NAME}.elf PRIVATE -DCONFIG_SPIRAM_SUPPORT)
# PSRAMの使用を許可するリンカーフラグ
target_link_options(${PROJECT_NAME}.elf PRIVATE "-Wl,--allow-multiple-definition")
# カスタムリンカースクリプト
# target_link_options(${PROJECT_NAME}.elf PRIVATE "-T${CMAKE_CURRENT_SOURCE_DIR}/custom_linker_sections.ld")
