# Plugins VST3 de teste, todos próprios: o autoteste não depende de nada baixado.
# Saem em build/test_plugins/ (a pasta que o self-test escaneia).
#   WHYNOT Teste Ganho  efeito (JUCE)        WHYNOT Teste Synth  instrumento (JUCE)
#   WHYNOT Teste Crash  derruba o scan       WHYNOT Teste Trava  nunca responde ao scan

set(WHYNOT_TEST_PLUGIN_DIR "${CMAKE_BINARY_DIR}/test_plugins")

function(whynot_test_plugin target product code synth source)
    if(synth)
        set(category Instrument)
    else()
        set(category Fx)
    endif()

    juce_add_plugin(${target}
        PRODUCT_NAME "${product}"
        COMPANY_NAME "WHYNOT"
        BUNDLE_ID "com.whynot.test.${code}"
        PLUGIN_MANUFACTURER_CODE Wnot
        PLUGIN_CODE ${code}
        FORMATS VST3
        IS_SYNTH ${synth}
        NEEDS_MIDI_INPUT ${synth}
        VST3_CATEGORIES ${category}
        COPY_PLUGIN_AFTER_BUILD TRUE
        VST3_COPY_DIR "${WHYNOT_TEST_PLUGIN_DIR}")

    target_sources(${target} PRIVATE ${source})
    juce_generate_juce_header(${target})

    target_compile_definitions(${target} PUBLIC
        DONT_SET_USING_JUCE_NAMESPACE=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    target_link_libraries(${target}
        PRIVATE juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags)

    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8)
    endif()
endfunction()

whynot_test_plugin(whynot_test_gain  "WHYNOT Teste Ganho" WtGn FALSE gain/GainPlugin.cpp)
whynot_test_plugin(whynot_test_synth "WHYNOT Teste Synth" WtSy TRUE  synth/SynthPlugin.cpp)

function(whynot_broken_plugin target product hang)
    add_library(${target} MODULE broken/BrokenPlugin.cpp)
    target_compile_definitions(${target} PRIVATE WHYNOT_BROKEN_HANG=${hang})
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME "${product}"
        PREFIX ""
        SUFFIX ".vst3"
        LIBRARY_OUTPUT_DIRECTORY "${WHYNOT_TEST_PLUGIN_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/broken")
endfunction()

whynot_broken_plugin(whynot_test_crash "WHYNOT Teste Crash" 0)
whynot_broken_plugin(whynot_test_hang  "WHYNOT Teste Trava" 1)

add_custom_target(whynot_test_plugins
    DEPENDS whynot_test_gain_VST3 whynot_test_synth_VST3 whynot_test_crash whynot_test_hang)
