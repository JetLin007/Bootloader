# arm-gcc.cmake ARM交叉编译工具链配置
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# 指定交叉编译器绝对路径
set(TOOLCHAIN_ROOT "C:\\Program Files\\Arm\\GNU Toolchain mingw-w64-x86_64-arm-none-eabi\\bin")
set(CMAKE_C_COMPILER ${TOOLCHAIN_ROOT}/arm-none-eabi-gcc.exe)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_ROOT}/arm-none-eabi-gcc.exe)
set(CMAKE_OBJCOPY ${TOOLCHAIN_ROOT}/arm-none-eabi-objcopy.exe)
set(CMAKE_SIZE ${TOOLCHAIN_ROOT}/arm-none-eabi-size.exe)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_ROOT}/arm-none-eabi-g++.exe)

# 关键：禁止CMake添加Windows平台链接器参数
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)