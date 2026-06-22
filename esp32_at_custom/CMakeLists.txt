# See COPYING.txt for license details.
#
# ESP-AT custom component for the M1 phase-3c WiFi capture feature.

file(GLOB_RECURSE srcs custom/*.c)

set(includes "include")

set(require_components at freertos nvs_flash esp_wifi log esp_event)

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS ${includes}
    REQUIRES ${require_components})

idf_component_set_property(${COMPONENT_NAME} WHOLE_ARCHIVE TRUE)
