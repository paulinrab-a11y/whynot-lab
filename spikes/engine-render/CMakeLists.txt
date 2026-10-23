# Prova de conceito da Fase 2: o Tracktion Engine compila com o JUCE fixado
# e renderiza um WAV sem interface gráfica (base do futuro modo headless).

juce_add_console_app(whynot_render_spike
    PRODUCT_NAME "WHYNOT Render Spike")

juce_generate_juce_header(whynot_render_spike)

target_sources(whynot_render_spike PRIVATE main.cpp)

target_compile_definitions(whynot_render_spike PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(whynot_render_spike PRIVATE
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_recommended_config_flags)
