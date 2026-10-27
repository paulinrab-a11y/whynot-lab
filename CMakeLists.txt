cmake_minimum_required(VERSION 3.25)

project(WhynotLab VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # Runtime estático, igual aos exemplos do Tracktion: o .exe não depende do VC++ Redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # A Tracktion e o JUCE geram unidades de compilação grandes.
    add_compile_options(/bigobj)
    # O __FILE__ (os textos de log do Tracktion, por exemplo) sai sem a pasta do repositório: o exe não guarda o
    # usuário do Windows de quem compilou (pedido do código aberto, 23/10). Sem a barra final, que escaparia aspas.
    file(TO_NATIVE_PATH "${CMAKE_SOURCE_DIR}" whynot_source_native)
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:/d1trimfile:${whynot_source_native}>")
endif()

# JUCE 8.0.13, na versão exata que o Tracktion Engine fixa como submódulo.
add_subdirectory(external/tracktion_engine/modules/juce EXCLUDE_FROM_ALL)
# Registra tracktion_core, tracktion_graph e tracktion_engine (aliases tracktion::).
add_subdirectory(external/tracktion_engine/modules EXCLUDE_FROM_ALL)

enable_testing()
add_subdirectory(tests)

# Plugins VST3 próprios para o autoteste (efeito, instrumento e dois quebrados de propósito).
option(WHYNOT_BUILD_TEST_PLUGINS "Compila os plugins de teste em build/test_plugins" ON)

if(WHYNOT_BUILD_TEST_PLUGINS)
    add_subdirectory(tests/plugins)
endif()

# Desligado por padrão: cada alvo JUCE recompila todos os módulos (~8 min nesta máquina).
option(WHYNOT_BUILD_SPIKES "Compila as provas de conceito em spikes/" OFF)

if(WHYNOT_BUILD_SPIKES)
    add_subdirectory(spikes/engine-render)
endif()

#==============================================================================
# SDKs de terceiros baixados à parte, em external/ e fora do git (ver docs/como-rodar.md).
# Sem eles o app compila igual, só sem MP3 ou sem ASIO.

# LAME 3.100 (LGPL): o codificador de MP3, como biblioteca estática.
set(WHYNOT_LAME_DIR "${CMAKE_SOURCE_DIR}/external/lame-3.100" CACHE PATH "Código-fonte do LAME 3.100")
set(WHYNOT_HAS_LAME OFF)

if(EXISTS "${WHYNOT_LAME_DIR}/include/lame.h")
    set(LAME_SRC "${WHYNOT_LAME_DIR}/libmp3lame")
    add_library(whynot_lame STATIC
        ${LAME_SRC}/bitstream.c ${LAME_SRC}/encoder.c ${LAME_SRC}/fft.c ${LAME_SRC}/gain_analysis.c
        ${LAME_SRC}/id3tag.c ${LAME_SRC}/lame.c ${LAME_SRC}/newmdct.c ${LAME_SRC}/psymodel.c
        ${LAME_SRC}/quantize.c ${LAME_SRC}/quantize_pvt.c ${LAME_SRC}/vector/xmm_quantize_sub.c
        ${LAME_SRC}/set_get.c ${LAME_SRC}/vbrquantize.c ${LAME_SRC}/reservoir.c ${LAME_SRC}/tables.c
        ${LAME_SRC}/takehiro.c ${LAME_SRC}/util.c ${LAME_SRC}/mpglib_interface.c ${LAME_SRC}/VbrTag.c
        ${LAME_SRC}/presets.c ${LAME_SRC}/version.c)

    # o LAME traz a configuração do MSVC pronta (configMS.h), no lugar do config.h do autoconf
    configure_file("${WHYNOT_LAME_DIR}/configMS.h" "${CMAKE_BINARY_DIR}/lame_config/config.h" COPYONLY)
    target_include_directories(whynot_lame
        PUBLIC "${WHYNOT_LAME_DIR}/include"
        PRIVATE "${LAME_SRC}" "${CMAKE_BINARY_DIR}/lame_config")
    target_compile_definitions(whynot_lame PRIVATE HAVE_CONFIG_H HAVE_XMMINTRIN_H _CRT_SECURE_NO_WARNINGS NDEBUG)

    if(MSVC)
        target_compile_options(whynot_lame PRIVATE /W0)
    endif()

    set(WHYNOT_HAS_LAME ON)
else()
    message(STATUS "LAME não encontrado em ${WHYNOT_LAME_DIR}: o app compila sem exportar MP3")
endif()

# ASIO SDK 2.3.4 da Steinberg (licença dupla; usado pela GPLv3): só o iasiodrv.h, que o JUCE inclui.
set(WHYNOT_ASIO_SDK_DIR "${CMAKE_SOURCE_DIR}/external/asiosdk" CACHE PATH "Pasta do ASIO SDK")
set(WHYNOT_HAS_ASIO OFF)

if(WIN32 AND EXISTS "${WHYNOT_ASIO_SDK_DIR}/common/iasiodrv.h")
    set(WHYNOT_HAS_ASIO ON)
else()
    message(STATUS "ASIO SDK não encontrado em ${WHYNOT_ASIO_SDK_DIR}: o app compila só com WASAPI")
endif()

#==============================================================================
# O app
juce_add_gui_app(whynot_lab
    PRODUCT_NAME "WHYNOT LAB"
    COMPANY_NAME "WHYNOT"
    VERSION 0.1.0
    ICON_BIG assets/brand/whynot-icon.svg
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)

juce_generate_juce_header(whynot_lab)

# Marca (logo do GUIDE WHYNOT) e fonte Montserrat (OFL) embutidas no executável.
juce_add_binary_data(whynot_assets
    HEADER_NAME WhynotAssets.h
    NAMESPACE WhynotAssets
    SOURCES
        assets/brand/whynot-logo-white.svg
        assets/brand/whynot-icon.svg
        assets/fonts/montserrat/Montserrat-Regular.ttf
        assets/fonts/montserrat/Montserrat-Medium.ttf
        assets/fonts/montserrat/Montserrat-SemiBold.ttf
        assets/fonts/montserrat/Montserrat-Bold.ttf)

set(WHYNOT_SOURCES
    src/app/Main.cpp
    src/app/AppController.cpp
    src/app/AutomationRecording.cpp
    src/app/Favorites.cpp
    src/app/KeyMap.cpp
    src/app/Session.cpp
    src/app/SelfTest.cpp
    src/app/WindowsPower.cpp
    src/app/selftest/Automation.cpp
    src/app/selftest/AutomationRecord.cpp
    src/app/selftest/BeatKey.cpp
    src/app/selftest/Community.cpp
    src/app/selftest/Composition.cpp
    src/app/selftest/Control.cpp
    src/app/selftest/Core.cpp
    src/app/selftest/Design.cpp
    src/app/selftest/Favorites.cpp
    src/app/selftest/Finishing.cpp
    src/app/selftest/Fluidity.cpp
    src/app/selftest/Ideas.cpp
    src/app/selftest/Library.cpp
    src/app/selftest/Mixing.cpp
    src/app/selftest/NoteTune.cpp
    src/app/selftest/Performance.cpp
    src/app/selftest/Piano.cpp
    src/app/selftest/PitchBend.cpp
    src/app/selftest/Plugins.cpp
    src/app/selftest/Producer.cpp
    src/app/selftest/ProducerPiano.cpp
    src/app/selftest/Recording.cpp
    src/app/selftest/RenderPack.cpp
    src/app/selftest/Robustness.cpp
    src/app/selftest/Sampler.cpp
    src/app/selftest/SelfTestKit.cpp
    src/app/selftest/Slicing.cpp
    src/app/selftest/Stretch.cpp
    src/app/selftest/Synth.cpp
    src/app/selftest/Tune.cpp
    src/app/selftest/Voice.cpp
    src/app/selftest/VoicePlaylist.cpp
    src/app/selftest/WarpCapture.cpp
    src/commands/AutomationCommands.cpp
    src/commands/BeatCommands.cpp
    src/commands/AudioMidiCommands.cpp
    src/commands/BrowserCommands.cpp
    src/commands/CommandRegistry.cpp
    src/commands/CoreCommands.cpp
    src/commands/CompositionCommands.cpp
    src/commands/DeviceCommands.cpp
    src/commands/ExportCommands.cpp
    src/commands/FavoritesCommands.cpp
    src/commands/PluginCommands.cpp
    src/commands/MixCommands.cpp
    src/commands/IdeasCommands.cpp
    src/commands/LayerCommands.cpp
    src/commands/LibraryCommands.cpp
    src/commands/MidiLearnCommands.cpp
    src/commands/NoteTuneCommands.cpp
    src/commands/SynthCommands.cpp
    src/commands/PerformanceCommands.cpp
    src/commands/PianoCommands.cpp
    src/commands/PitchBendCommands.cpp
    src/commands/PlaylistEditCommands.cpp
    src/commands/RecordCommands.cpp
    src/commands/SliceCommands.cpp
    src/control/AudioPreview.cpp
    src/control/CommandGate.cpp
    src/control/CommandPolicy.cpp
    src/control/ControlLog.cpp
    src/control/ControlPlatform.cpp
    src/control/ControlPolicy.cpp
    src/control/ControlServer.cpp
    src/control/WsProtocol.cpp
    src/dsp/Color.cpp
    src/dsp/Console.cpp
    src/dsp/Denoise.cpp
    src/dsp/Dynamics.cpp
    src/dsp/DynamicEq.cpp
    src/dsp/Enhance.cpp
    src/dsp/EqProcessor.cpp
    src/dsp/FxCatalog.cpp
    src/dsp/FxHost.cpp
    src/dsp/Gate.cpp
    src/dsp/Grit.cpp
    src/dsp/LoudnessMeter.cpp
    src/dsp/Master.cpp
    src/dsp/Modulation.cpp
    src/dsp/Multiband.cpp
    src/dsp/NoteTune.cpp
    src/dsp/PitchDetect.cpp
    src/dsp/Space.cpp
    src/dsp/Tune.cpp
    src/dsp/Vintage.cpp
    src/dsp/Vocal.cpp
    src/dsp/VoicePitch.cpp
    src/ideas/Analysis.cpp
    src/ideas/Generator.cpp
    src/ideas/Genres.cpp
    src/ideas/KeyDetect.cpp
    src/ideas/Prompt.cpp
    src/ideas/Theory.cpp
    src/library/DeckPlayer.cpp
    src/library/ImportDialog.cpp
    src/library/PreviewRenderer.cpp
    src/library/ProjectImport.cpp
    src/library/ProjectIndex.cpp
    src/library/ProjectLibrary.cpp
    src/library/ProjectsView.cpp
    src/library/SystemMemory.cpp
    src/engine/AudioToMidi.cpp
    src/engine/BeatKey.cpp
    src/engine/Exporter.cpp
    src/engine/KitGenerator.cpp
    src/engine/MidiExport.cpp
    src/engine/MidiImport.cpp
    src/engine/Mp3Encoder.cpp
    src/engine/PianoLibrary.cpp
    src/engine/PluginHost.cpp
    src/engine/Projector.cpp
    src/engine/SampleAnalysis.cpp
    src/engine/SoundInfo.cpp
    src/engine/SynthFavorites.cpp
    src/engine/UserMix.cpp
    src/engine/WhynotFx.cpp
    src/engine/PreviewPlayer.cpp
    src/engine/WhynotPiano.cpp
    src/engine/WhynotSampler.cpp
    src/engine/WhynotSynth.cpp
    src/model/WnlFormat.cpp
    src/plugins/PluginCatalog.cpp
    src/synth/Presets3Osc.cpp
    src/synth/Presets808Bass.cpp
    src/synth/PresetsFinal.cpp
    src/synth/PresetsGenres.cpp
    src/synth/PresetsKeys.cpp
    src/synth/PresetsLeads.cpp
    src/synth/PresetsMore.cpp
    src/synth/PresetsPads.cpp
    src/synth/Synth.cpp
    src/synth/SynthMeasure.cpp
    src/synth/SynthParams.cpp
    src/synth/SynthPresets.cpp
    src/synth/Wavetables.cpp
    src/record/InputRecorder.cpp
    src/record/NoteTuneFiles.cpp
    src/record/RecordEngine.cpp
    src/ui/AiBridgeUi.cpp
    src/ui/AutomationLanes.cpp
    src/ui/BeatKeyNotice.cpp
    src/ui/ChannelRack.cpp
    src/ui/ControlMenu.cpp
    src/ui/Controls.cpp
    src/ui/DeviceSettings.cpp
    src/ui/ExportDialogs.cpp
    src/ui/Fluid.cpp
    src/ui/FavoriteStar.cpp
    src/ui/fx/FxEditor.cpp
    src/ui/fx/FxVisuals.cpp
    src/ui/fx/FxWidgets.cpp
    src/ui/fx/InsertPanel.cpp
    src/ui/Icons.cpp
    src/ui/LevelMeter.cpp
    src/ui/MainComponent.cpp
    src/ui/MainWindow.cpp
    src/ui/MidiImportDialog.cpp
    src/ui/MidiLearnMenu.cpp
    src/ui/IdeasPanel.cpp
    src/ui/MixerView.cpp
    src/ui/NoteTuneEditor.cpp
    src/ui/PianoRoll.cpp
    src/ui/Playlist.cpp
    src/ui/PluginBrowser.cpp
    src/ui/PluginWindow.cpp
    src/ui/SampleBrowser.cpp
    src/ui/SamplerPanel.cpp
    src/ui/ShortcutEditor.cpp
    src/ui/SplashWindow.cpp
    src/ui/StatusBar.cpp
    src/ui/synth/SynthBrowser.cpp
    src/ui/synth/SynthEditor.cpp
    src/ui/synth/SynthVisuals.cpp
    src/ui/Theme.cpp
    src/ui/TransportBar.cpp
    src/ui/ViewRail.cpp
    src/ui/WelcomeCard.cpp
    src/ui/WindowOwner.cpp)

target_sources(whynot_lab PRIVATE ${WHYNOT_SOURCES})

target_compile_definitions(whynot_lab PRIVATE
    DONT_SET_USING_JUCE_NAMESPACE=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:whynot_lab,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:whynot_lab,JUCE_VERSION>")

# Esticar o áudio no tempo sem mudar o tom (pedidos de 21/10): o signalsmith-stretch (MIT) já vem no Tracktion.
target_compile_definitions(whynot_lab PRIVATE TRACKTION_ENABLE_TIMESTRETCH_SIGNALSMITH=1)

target_link_libraries(whynot_lab PRIVATE
    whynot_assets
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_recommended_config_flags)

if(WHYNOT_HAS_ASIO)
    target_compile_definitions(whynot_lab PRIVATE JUCE_ASIO=1)
    target_include_directories(whynot_lab PRIVATE "${WHYNOT_ASIO_SDK_DIR}/common")
endif()

if(WHYNOT_HAS_LAME)
    target_link_libraries(whynot_lab PRIVATE whynot_lame)
    target_compile_definitions(whynot_lab PRIVATE WHYNOT_HAS_LAME=1)
endif()

if(MSVC)
    # Literais em UTF-8 (textos em português) e avisos no nível 4 só no nosso código.
    target_compile_options(whynot_lab PRIVATE /utf-8)
    set_source_files_properties(${WHYNOT_SOURCES} PROPERTIES COMPILE_OPTIONS "/W4")
endif()
