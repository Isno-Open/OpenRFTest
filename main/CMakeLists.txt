# board_pins.h est ENGENDRE a la configuration depuis boards/${BOARD}.json, dans
# le dossier de construction : rien d'engendre n'est commis, et changer de carte
# c'est changer une variable, pas un fichier source.
#
# ESP-IDF lit ce fichier une premiere fois en « expansion precoce », dans un
# autre processus CMake : on n'engendre qu'a la vraie configuration.
set(BOARD_JSON "${CMAKE_CURRENT_LIST_DIR}/../boards/${BOARD}.json")
set(BOARD_HDR_DIR "${CMAKE_BINARY_DIR}/board")
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    if(NOT EXISTS "${BOARD_JSON}")
        message(FATAL_ERROR "carte inconnue : ${BOARD_JSON} n'existe pas (-DBOARD=isno-super | m5-atom-lite)")
    endif()
    file(MAKE_DIRECTORY "${BOARD_HDR_DIR}")
    execute_process(COMMAND python3 "${CMAKE_CURRENT_LIST_DIR}/../tools/gen_board.py" "${BOARD_JSON}" "${BOARD_HDR_DIR}/board_pins.h"
                    RESULT_VARIABLE r)
    if(NOT r EQUAL 0)
        message(FATAL_ERROR "gen_board.py a refuse ${BOARD_JSON}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${BOARD_JSON}" "${CMAKE_CURRENT_LIST_DIR}/../tools/gen_board.py")
endif()

idf_component_register(SRCS "main.c" "ctrl.c" "cc1101.c" "rf_regs.c" "ap.c" "web.c"
                       INCLUDE_DIRS "." "${BOARD_HDR_DIR}"
                       EMBED_FILES "rftest.html"
                       PRIV_REQUIRES console driver esp_driver_gpio esp_driver_spi app_update
                                     esp_wifi esp_http_server esp_netif esp_event nvs_flash esp_hw_support)
