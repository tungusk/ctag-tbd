idf_component_register(SRCS "custom_sdmmc_cmd.cpp"
        INCLUDE_DIRS "."
        REQUIRES driver esp_driver_sdmmc sdmmc)

if(CONFIG_TEST_SDMMC_USE_BOUNCE_WRAPPER)
    target_link_libraries(${COMPONENT_LIB} INTERFACE
        "-Wl,--wrap=sdmmc_read_sectors"
        "-Wl,--undefined=__wrap_sdmmc_read_sectors"
        "-Wl,--wrap=sdmmc_write_sectors"
        "-Wl,--undefined=__wrap_sdmmc_write_sectors"
    )
endif()

