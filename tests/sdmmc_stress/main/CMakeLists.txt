set(TEST_REQUIRES driver esp_driver_sdmmc esp_timer fatfs sdmmc espressif__esp_hosted espressif__zlib)

idf_component_register(SRCS "sdmmc_stress.cpp"
        INCLUDE_DIRS "."
        REQUIRES ${TEST_REQUIRES})
