# main/CMakeLists.txt
idf_component_register(
    SRCS 
        "epd_main.c" 
        "epd_board_m5papers3.c"
        "epd_wrapper.c"
        "epd_transition.c"
        "epd_text.c"
        "gt911.c"
        "sdcard_manager.c"
        "uart_command.c"
        "file_transfer.c"
        "command_handlers.c"
    PRIV_REQUIRES 
        "epdiy" 
        "driver" 
        "esp_timer"
        "fatfs"
    INCLUDE_DIRS 
        "."
)
# file paths relative to CMakeLists.txt
#set(COMPONENT_ADD_LDFRAGMENTS "./linker_fragment_file.lf")

#register_component()