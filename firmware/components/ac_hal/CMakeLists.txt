idf_component_register(
    SRCS "src/ac_hal.c" "src/sps30_uart.c" "src/sgp40.c" "src/scd41.c"
         "src/led.c" "src/battery.c" "src/button.c" "src/storage.c"
    INCLUDE_DIRS "include"
    REQUIRES ac_core sensirion_gas_index driver esp_driver_gpio esp_driver_uart
             esp_driver_i2c nvs_flash esp_timer esp_hw_support)
