cmake_minimum_required(VERSION 3.15)
include(cmake/Utilities.cmake)
include(cmake/GetGitRevisionDescription.cmake)
include(cmake/ProjectVersion.cmake)

project(
  MMU
  LANGUAGES C CXX ASM
  VERSION ${PROJECT_VERSION}
  )

if(NOT CMAKE_CROSSCOMPILING)
  #
  # If we are not crosscompiling, include `utils` with host tools.
  #
  add_subdirectory(utils)
endif()

#
# Command Line Options
#
# You should specify those options when invoking CMake. Example:
# ~~~
# cmake .. <other options> -DPRINTER=MMU
# ~~~

set(PRINTER_VALID_OPTS "MMU")

set(PRINTER
    "MMU"
    CACHE
      STRING
      "Select the MMU unit for which you want to compile the project (valid values are ${PRINTER_VALID_OPTS})."
    )
set(PROJECT_VERSION_SUFFIX
    "<auto>"
    CACHE
      STRING
      "Full version suffix to be shown on the info screen in settings (e.g. full_version=4.0.3-BETA+1035.PR111.B4, suffix=-BETA+1035.PR111.B4). Defaults to '+<commit sha>.<dirty?>.<debug?>' if set to '<auto>'."
    )
set(PROJECT_VERSION_SUFFIX_SHORT
    "<auto>"
    CACHE
      STRING
      "Short version suffix to be shown on splash screen. Defaults to '+<BUILD_NUMBER>' if set to '<auto>'."
    )
set(BUILD_NUMBER
    ""
    CACHE STRING "Build number of the firmware. Resolved automatically if not specified."
    )
set(CUSTOM_COMPILE_OPTIONS
    ""
    CACHE STRING "Allows adding custom C/C++ flags"
    )

# Validate options
foreach(OPTION "PRINTER")
  if(NOT ${OPTION} IN_LIST ${OPTION}_VALID_OPTS)
    message(FATAL_ERROR "Invalid ${OPTION} ${${OPTION}}: Valid values are ${${OPTION}_VALID_OPTS}")
  endif()
endforeach()

# Resolve BUILD_NUMBER and PROJECT_VERSION_* variables
resolve_version_variables()

# Check GCC Version
get_recommended_gcc_version(RECOMMENDED_TOOLCHAIN_VERSION)
if(CMAKE_CROSSCOMPILING AND NOT CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL
                            ${RECOMMENDED_TOOLCHAIN_VERSION}
   )
  message(WARNING "Recommended AVR toolchain is ${RECOMMENDED_TOOLCHAIN_VERSION}"
                  ", but you have ${CMAKE_CXX_COMPILER_VERSION}"
          )

elseif(NOT CMAKE_CROSSCOMPILING AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  message(
    WARNING
      "Recommended compiler for host tools and unittests is GCC, you have ${CMAKE_CXX_COMPILER_ID}."
    )
endif()

# Inform user about the resolved settings
message(STATUS "Project version: ${PROJECT_VERSION}")
message(STATUS "Project version with full suffix: ${PROJECT_VERSION_FULL}")
message(
  STATUS "Project version with short suffix: ${PROJECT_VERSION}${PROJECT_VERSION_SUFFIX_SHORT}"
  )
message(STATUS "Using toolchain file: ${CMAKE_TOOLCHAIN_FILE}.")
message(STATUS "Printer: ${PRINTER}")

# eclipse sets those variables, so lets just use them so we don't get a warning about unused
# variables
set(unused "${CMAKE_VERBOSE_MAKEFILE} ${CMAKE_RULE_MESSAGES}")

# append custom C/C++ flags
if(CUSTOM_COMPILE_OPTIONS)
  string(REPLACE " " ";" CUSTOM_COMPILE_OPTIONS "${CUSTOM_COMPILE_OPTIONS}")
  add_compile_options(${CUSTOM_COMPILE_OPTIONS})
endif()

#
# MMUHeaders
#

# add_library(MMUHeaders INTERFACE) target_include_directories( MMUHeaders INTERFACE include
# include/stm32f4_hal include/usb_host include/usb_device include/marlin include/freertos )

# target_link_libraries(A3idesHeaders INTERFACE STM32F4::HAL FreeRTOS::FreeRTOS)

# target_compile_definitions( A3idesHeaders INTERFACE PRINTER_TYPE=PRINTER_PRUSA_${PRINTER} )

#
# Global Compiler & Linker Configuration
#

# include symbols
add_compile_options(-g)

# optimizations
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-Og)
else()
  add_compile_options(-Os)
endif()

if(CMAKE_CROSSCOMPILING)
  # mcu related settings
  set(MCU_FLAGS -mmcu=atmega32u4 -DF_CPU=16000000L)
  add_compile_options(${MCU_FLAGS})
  add_link_options(${MCU_FLAGS})

  # split and gc sections
  add_compile_options(-ffunction-sections -fdata-sections)
  add_link_options(-Wl,--gc-sections)

  # disable exceptions and related metadata
  add_compile_options(-fno-exceptions -fno-unwind-tables)
  add_link_options(-Wl,--defsym,__exidx_start=0,--defsym,__exidx_end=0)
endif()

# enable all warnings (well, not all, but some)
add_compile_options(-Wall -Wsign-compare)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-std=c++14>)

# support _DEBUG macro (some code uses to recognize debug builds)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_definitions(_DEBUG)
endif()

# if(CMAKE_CROSSCOMPILING) # configure linker script set(LINKER_SCRIPT
# "${CMAKE_CURRENT_SOURCE_DIR}/src/STM32F407VG_FLASH.ld")
# add_link_options("-Wl,-T,${LINKER_SCRIPT}") endif()

#
# Import definitions of all libraries
#

add_subdirectory(lib)

#
# MMU firmware
#

add_executable(firmware)

set_target_properties(firmware PROPERTIES CXX_STANDARD 14)

# generate firmware.bin file
objcopy(firmware "ihex" ".hex")

add_custom_command(TARGET firmware POST_BUILD COMMAND avr-objdump ARGS -CSd firmware > firmware.txt)

add_custom_command(TARGET firmware POST_BUILD COMMAND avr-size ARGS -C --mcu=atmega32u4 firmware)

# generate linker map file
target_link_options(firmware PUBLIC -Wl,-Map=firmware.map)

# inform about the firmware's size in terminal
report_size(firmware)

# add_link_dependency(firmware "${LINKER_SCRIPT}")

target_include_directories(firmware PRIVATE include src)

target_compile_options(firmware PRIVATE -Wdouble-promotion)

# target_link_libraries( firmware PRIVATE A3idesHeaders )

target_sources(firmware PRIVATE src/main.cpp src/hal/avr/cpu.cpp)

set_property(
  SOURCE src/version.c
  APPEND
  PROPERTY COMPILE_DEFINITIONS
           FW_BUILD_NUMBER=${BUILD_NUMBER}
           FW_VERSION_FULL=${PROJECT_VERSION_FULL}
           FW_VERSION=${PROJECT_VERSION}
           FW_VERSION_SUFFIX=${PROJECT_VERSION_SUFFIX}
           FW_VERSION_SUFFIX_SHORT=${PROJECT_VERSION_SUFFIX_SHORT}
  )

if(NOT CMAKE_CROSSCOMPILING)
  enable_testing()
  add_subdirectory(tests)
endif()