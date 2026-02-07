idf_component_register(
  SRCS
    "app_main.c"
  INCLUDE_DIRS
    "."
  REQUIRES
    fatfs
    sdmmc
    esp_wifi
    esp_netif
    nvs_flash
    usb_console
    snac2
)

# Ensure tusb_config.h is visible to the managed espressif/tinyusb component.
# (Its sources include "tusb_config.h" and IDF already adds ${CMAKE_BINARY_DIR}/config to include paths.)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../tusb_config.h" "${CMAKE_BINARY_DIR}/config/tusb_config.h" COPYONLY)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
  HTTPD_AUTOMATIC_RESTART=1
)