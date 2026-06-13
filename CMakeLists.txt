# ============================================================================
# CMakeLists.txt — BloodStrike ESP
#
# Builds the usermode overlay application.
# The kernel driver must be built separately using the WDK (MSBuild).
# ============================================================================

cmake_minimum_required(VERSION 3.20)
project(BloodStrikeESP
    VERSION     1.0.0
    DESCRIPTION "BloodStrike External ESP (Kernel Driver + Usermode Overlay)"
    LANGUAGES   CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Architecture check ----
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "x64 build required. Please use a x64 toolchain.")
endif()

# ---- User-mode overlay executable ----
add_executable(BloodStrikeESP
    User/main.cpp
)

target_include_directories(BloodStrikeESP PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/User
)

target_link_libraries(BloodStrikeESP PRIVATE
    d3d11
    d2d1
    dxgi
    dwrite
)

# Set subsystem to Windows to avoid console window (optional: use CONSOLE for debugging)
set_target_properties(BloodStrikeESP PROPERTIES
    WIN32_EXECUTABLE TRUE
    OUTPUT_NAME "BloodStrikeESP"
)

# ---- Copy kernel driver source to build dir (informational) ----
add_custom_target(KernelDriverSource
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "======================================"
    COMMAND ${CMAKE_COMMAND} -E echo " Kernel Driver build (WDK required):"
    COMMAND ${CMAKE_COMMAND} -E echo "======================================"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "  Open Kernel/ in Visual Studio with"
    COMMAND ${CMAKE_COMMAND} -E echo "  the WDK x64 toolchain and build"
    COMMAND ${CMAKE_COMMAND} -E echo "  driver.c as a Kernel Mode Driver."
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "  Or from the command line:"
    COMMAND ${CMAKE_COMMAND} -E echo "    cd Kernel"
    COMMAND ${CMAKE_COMMAND} -E echo "    cl.exe /c /I. driver.c /LD"
    COMMAND ${CMAKE_COMMAND} -E echo "    link.exe driver.obj /DRIVER /OUT:BS_KernelDriver.sys"
    COMMAND ${CMAKE_COMMAND} -E echo ""
)

# ---- Install instructions ----
install(TARGETS BloodStrikeESP
    RUNTIME DESTINATION bin
)
