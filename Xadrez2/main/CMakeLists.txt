idf_component_register(
    SRCS "main.c" "led.c" "sensor.c" "server.c" "Jogo.cpp" "Fen_PGN.cpp" "valida_jogada.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES led_strip esp_http_server
)
