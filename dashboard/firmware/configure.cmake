# Loaded by the hardware firmware patch. Credentials stay in the local build.
foreach(key WIFI_SSID WIFI_PASSWORD BROKER_IPV4 MQTT_PASSWORD)
    set(value "$ENV{PEGATRON_${key}}")
    if(value MATCHES "[\r\n]")
        message(FATAL_ERROR "PEGATRON_${key} must be a single line")
    endif()
    string(REPLACE "\\" "\\\\" value "${value}")
    string(REPLACE "\"" "\\\"" value "${value}")
    set(PEGATRON_${key} "${value}")
endforeach()

foreach(station 1 2)
    set(key "PEGATRON_REFERENCE_${station}_MM")
    set(${key} "0" CACHE STRING "Measured empty-belt reference, 0 means unknown")
    if(NOT "${${key}}" MATCHES "^(0|[1-9][0-9]*)$" OR ${${key}} GREATER 2000)
        message(FATAL_ERROR "${key} must be an integer between 0 and 2000")
    endif()
endforeach()

configure_file("${CMAKE_CURRENT_LIST_DIR}/pegatron_local_config.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/pegatron_local_config.h" @ONLY)

