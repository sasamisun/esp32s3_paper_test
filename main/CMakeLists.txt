# main/CMakeLists.txt
idf_component_register(
    SRCS 
        "epd_main.c" 
        "epd_board_m5papers3.c"
    PRIV_REQUIRES 
        "epdiy" 
        "driver" 
        "esp_timer"
    INCLUDE_DIRS 
        "."
)