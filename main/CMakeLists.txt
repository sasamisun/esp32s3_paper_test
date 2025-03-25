# main/CMakeLists.txt
idf_component_register(
    SRCS 
        "epd_main.c" 
        "epd_board_m5papers3.c"
        "epd_wrapper.c"
        "epd_transition.c"
        "epd_text.c"
    PRIV_REQUIRES 
        "epdiy" 
        "driver" 
        "esp_timer"
    INCLUDE_DIRS 
        "."
)
# file paths relative to CMakeLists.txt
#set(COMPONENT_ADD_LDFRAGMENTS "./linker_fragment_file.lf")

#register_component()