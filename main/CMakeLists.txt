idf_component_register(SRCS "src/app_main.c"
                           "src/wifi_manager.c"
                           "src/mqtt_manager.c"
                           "src/led_control.c"
                           "src/command_handler.c"
                           "src/nvs_storage.c"
                    INCLUDE_DIRS "includes")