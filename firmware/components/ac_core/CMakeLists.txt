idf_component_register(
    SRCS "src/ac_types.c" "src/ac_config.c" "src/ac_filter.c"
         "src/ac_baseline.c" "src/ac_airquality.c" "src/ac_event.c"
         "src/ac_history.c" "src/ac_engine.c" "src/ac_status.c"
    INCLUDE_DIRS "include")
# ac_core is deliberately free of ESP-IDF dependencies so the same sources
# build and run under firmware/test/host on a workstation.
