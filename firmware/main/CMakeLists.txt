idf_component_register(SRCS "app_main.cpp" "ac_matter.cpp"
                       PRIV_INCLUDE_DIRS "."
                       REQUIRES ac_core ac_hal
                       PRIV_REQUIRES esp_matter chip openthread nvs_flash
                                     esp_timer esp_system esp_hw_support)

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
