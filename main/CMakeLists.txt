idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES board gfx wifi ota audio_echo nvs_flash
)
