if(ENABLE_HIP)
    find_package(HIP)

    if (HIP_FOUND)
        OPTION(HIP_NVCC_FLAGS "Flags used by HIP for compiling with nvcc")
        MARK_AS_ADVANCED(HIP_NVCC_FLAGS)

        # call hipcc to tell us about the nvcc options
        set(ENV{HIPCC_VERBOSE} 1)

        FILE(WRITE ${CMAKE_CURRENT_BINARY_DIR}/hip_test.cc "
int main(int argc, char **argv)
{ }
")
        EXECUTE_PROCESS(COMMAND ${HIP_HIPCC_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/hip_test.cc OUTPUT_VARIABLE _hipcc_verbose_out)

        string(REPLACE " " ";" _hipcc_verbose_options ${_hipcc_verbose_out})

        # get the compiler executable for device code
        LIST(GET _hipcc_verbose_options 1 _hip_compiler)

        # set it as the compiler
        if (${_hip_compiler} MATCHES nvcc)
            set(HIP_PLATFORM nvcc)
        elseif(${_hip_compiler} MATCHES hcc)
            set(HIP_PLATFORM hcc)
        elseif(${_hip_compiler} MATCHES clang)
            # fixme
            message(ERROR "hip-clang backend not supported")
            set(HIP_PLATFORM hip-clang)
        else()
            message(ERROR "Unknown HIP backend " ${_hip_compiler})
        endif()

        # use hipcc as C++ linker for shared libraries
        SET(CMAKE_CUDA_COMPILER ${HIP_HIPCC_EXECUTABLE})
        string(REPLACE "<CMAKE_CXX_COMPILER>" "${HIP_HIPCC_EXECUTABLE}" _link_exec ${CMAKE_CXX_CREATE_SHARED_LIBRARY})
        SET(CMAKE_CXX_CREATE_SHARED_LIBRARY ${_link_exec})

        # use hipcc as C++ linker for executables
        SET(CMAKE_CUDA_COMPILER ${HIP_HIPCC_EXECUTABLE})
        string(REPLACE "<CMAKE_CXX_COMPILER>" "${HIP_HIPCC_EXECUTABLE}" _link_exec ${CMAKE_CXX_LINK_EXECUTABLE})
        SET(CMAKE_CXX_LINK_EXECUTABLE ${_link_exec})

        # this is hack to set the right options on hipcc, may not be portable
        include(hipcc)

        # override command line, so that it doesn't contain "-x cu"
        set(CMAKE_CUDA_COMPILE_WHOLE_COMPILATION
            "<CMAKE_CUDA_COMPILER> ${CMAKE_CUDA_HOST_FLAGS} <DEFINES> <INCLUDES> <FLAGS> -c <SOURCE> -o <OBJECT>")

        # don't let CMake examine the compiler, because it will fail
        SET(CMAKE_CUDA_COMPILER_FORCED TRUE)

        # drop the compiler exeuctable and the "hipcc-cmd"
        LIST(REMOVE_AT _hipcc_verbose_options 0 1)

        # drop the -x cu option to not duplicate it with CMake's options
        LIST(FIND _hipcc_verbose_options "-x" _idx)
        if (NOT ${_idx} EQUAL "-1")
        math(EXPR _idx_plus_one "${_idx} + 1")
        LIST(REMOVE_AT _hipcc_verbose_options ${_idx} ${_idx_plus_one})
        endif()

        # finally drop the test file
        LIST(FILTER _hipcc_verbose_options EXCLUDE REGEX test.cc)
        SET(HIP_NVCC_FLAGS ${_hipcc_options_str})

        #search for HIP include directory
        find_path(HIP_INCLUDE_DIR hip/hip_runtime.h
                PATHS
               "${HIP_ROOT_DIR}"
                ENV ROCM_PATH
                ENV HIP_PATH
                PATH_SUFFIXES include)

    else()
        # here we go if hipcc is not available, fall back on internal HIP->CUDA headers
        set(HIP_INCLUDE_DIR "$<IF:$<STREQUAL:${CMAKE_PROJECT_NAME},HOOMD>,${CMAKE_CURRENT_SOURCE_DIR},${HOOMD_INSTALL_PREFIX}/${PYTHON_SITE_INSTALL_DIR}/include>/hoomd/extern/HIP/include/")

        # use CUDA runtime version
        set(HIP_VERSION_MAJOR "(CUDART_VERSION/1000)")
        set(HIP_VERSION_MINOR "(CUDART_VERSION - (CUDART_VERSION/1000)*1000)/10")
        set(HIP_VERSION_PATCH "0")
        set(HIP_NVCC_FLAGS "")
        set(HIP_PLATFORM "nvcc")
        set(CUB_INCLUDE_DIR "$<IF:$<STREQUAL:${CMAKE_PROJECT_NAME},HOOMD>,${CMAKE_CURRENT_SOURCE_DIR},${HOOMD_INSTALL_PREFIX}/${PYTHON_SITE_INSTALL_DIR}/include>/hoomd/extern/cub/")
    endif()

    ENABLE_LANGUAGE(CUDA)

    # hipCUB
    set(HIPCUB_INCLUDE_DIR "$<IF:$<STREQUAL:${CMAKE_PROJECT_NAME},HOOMD>,${CMAKE_CURRENT_SOURCE_DIR},${HOOMD_INSTALL_PREFIX}/${PYTHON_SITE_INSTALL_DIR}/include>/hoomd/extern/hipCUB/hipcub/include/;${CUB_INCLUDE_DIR}")

    if(NOT TARGET HIP::hip)
        add_library(HIP::hip INTERFACE IMPORTED)
        set_target_properties(HIP::hip PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${HIP_INCLUDE_DIR};${HIPCUB_INCLUDE_DIR}")

        if(HIP_PLATFORM STREQUAL "hip-clang")
            # needed with hip-clang
            set_property(TARGET HIP::hip APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS "__HIP_PLATFORM_HCC__")
        endif()

        # set HIP_VERSION_* on non-CUDA targets (the version is already defined on CUDA targets through HIP_NVCC_FLAGS)
        set_property(TARGET HIP::hip APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
            $<$<NOT:$<COMPILE_LANGUAGE:CUDA>>:HIP_VERSION_MAJOR=${HIP_VERSION_MAJOR}>)
        set_property(TARGET HIP::hip APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
            $<$<NOT:$<COMPILE_LANGUAGE:CUDA>>:HIP_VERSION_MINOR=${HIP_VERSION_MINOR}>)
        set_property(TARGET HIP::hip APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
            $<$<NOT:$<COMPILE_LANGUAGE:CUDA>>:HIP_VERSION_PATCH=${HIP_VERSION_PATCH}>)

        # branch upon HCC or NVCC target
        if(${HIP_PLATFORM} STREQUAL "nvcc")
            set_property(TARGET HIP::hip APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
                $<$<NOT:$<COMPILE_LANGUAGE:CUDA>>:__HIP_PLATFORM_NVCC__>)
        elseif(${HIP_PLATFORM} STREQUAL "hcc")
            set_property(TARGET HIP::hip APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
                $<$<NOT:$<COMPILE_LANGUAGE:CUDA>>:__HIP_PLATFORM_HCC__>)
        endif()
    endif()
    find_package(CUDALibs REQUIRED)
endif()
