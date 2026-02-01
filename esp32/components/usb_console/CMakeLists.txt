idf_component_register(
  SRCS
    "usb_console.c"
  INCLUDE_DIRS
    "."
  REQUIRES
    espressif__esp_tinyusb
    sdmmc
    snac2
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
  SNAC_ESP32=1
)

