idf_component_register(SRCS "app_main.cpp" "ac_matter.cpp"
                       PRIV_INCLUDE_DIRS "."
                       REQUIRES ac_core ac_hal)

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
