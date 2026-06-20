cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_SOURCE_DIR}/../../managed_components/espressif__esp_hosted"
    "${CMAKE_SOURCE_DIR}/../../managed_components/espressif__esp_wifi_remote"
    "${CMAKE_SOURCE_DIR}/../../managed_components/espressif__zlib"
)

set(COMPONENTS main sdmmc_bounce espressif__esp_hosted espressif__esp_wifi_remote espressif__zlib)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sdmmc_stress)

# In the full firmware the component manager wires esp_hosted and
# esp_wifi_remote together. This reduced COMPONENTS build has to provide the
# esp_wifi_remote public header path explicitly for esp_hosted.
idf_component_get_property(esp_hosted_lib espressif__esp_hosted COMPONENT_LIB)
if(esp_hosted_lib)
    get_target_property(esp_hosted_type ${esp_hosted_lib} TYPE)
    if(esp_hosted_type STREQUAL "INTERFACE_LIBRARY")
        target_include_directories(${esp_hosted_lib} INTERFACE
            "${CMAKE_SOURCE_DIR}/../../managed_components/espressif__esp_wifi_remote/include")
    else()
        target_include_directories(${esp_hosted_lib} PRIVATE
            "${CMAKE_SOURCE_DIR}/../../managed_components/espressif__esp_wifi_remote/include")
    endif()
endif()
