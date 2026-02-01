set(SNAC2_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/../../..")

idf_component_register(
  SRCS
    "${SNAC2_ROOT_DIR}/snac.c"
    "${SNAC2_ROOT_DIR}/data.c"
    "${SNAC2_ROOT_DIR}/http.c"
    "${SNAC2_ROOT_DIR}/httpd.c"
    "${SNAC2_ROOT_DIR}/webfinger.c"
    "${SNAC2_ROOT_DIR}/activitypub.c"
    "${SNAC2_ROOT_DIR}/html.c"
    "${SNAC2_ROOT_DIR}/utils.c"
    "${SNAC2_ROOT_DIR}/format.c"
    "${SNAC2_ROOT_DIR}/upgrade.c"
    "${SNAC2_ROOT_DIR}/mastoapi.c"
    "${SNAC2_ROOT_DIR}/rss.c"
    "${SNAC2_ROOT_DIR}/sandbox.c"
    "esp32_compat.c"
  INCLUDE_DIRS
    "${SNAC2_ROOT_DIR}"
  REQUIRES
    esp_http_client
    esp-tls
    mbedtls
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
  SNAC_ESP32=1
  WITHOUT_SHM=1
)

