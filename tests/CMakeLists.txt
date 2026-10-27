# Testes do núcleo de tempo real: compilam sem JUCE nem Tracktion, em segundos.
add_executable(whynot_audio_tests audio/test_audio_core.cpp)
target_compile_features(whynot_audio_tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(whynot_audio_tests PRIVATE /W4 /utf-8)
else()
    target_compile_options(whynot_audio_tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME audio_core COMMAND whynot_audio_tests)

# Efeitos nativos (EQ, compressor, saturador, reverb, delay, limitador...): também sem JUCE.
add_executable(whynot_dsp_tests
    dsp/test_dsp.cpp
    ../src/dsp/Color.cpp
    ../src/dsp/Console.cpp
    ../src/dsp/Denoise.cpp
    ../src/dsp/Dynamics.cpp
    ../src/dsp/DynamicEq.cpp
    ../src/dsp/Enhance.cpp
    ../src/dsp/EqProcessor.cpp
    ../src/dsp/FxCatalog.cpp
    ../src/dsp/FxHost.cpp
    ../src/dsp/Gate.cpp
    ../src/dsp/Grit.cpp
    ../src/dsp/LoudnessMeter.cpp
    ../src/dsp/Master.cpp
    ../src/dsp/Modulation.cpp
    ../src/dsp/Multiband.cpp
    ../src/dsp/Space.cpp
    ../src/dsp/Tune.cpp
    ../src/dsp/Vintage.cpp
    ../src/dsp/Vocal.cpp
    ../src/dsp/VoicePitch.cpp)
target_compile_features(whynot_dsp_tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(whynot_dsp_tests PRIVATE /W4 /utf-8 /O2)
else()
    target_compile_options(whynot_dsp_tests PRIVATE -Wall -Wextra -O2)
endif()

add_test(NAME dsp COMMAND whynot_dsp_tests)

# Detecção da nota de um sample (YIN): 808, timbres com harmônicos, ruído e casos-limite.
add_executable(whynot_pitch_tests dsp/test_pitch.cpp ../src/dsp/PitchDetect.cpp)
target_compile_features(whynot_pitch_tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(whynot_pitch_tests PRIVATE /W4 /utf-8 /O2)
else()
    target_compile_options(whynot_pitch_tests PRIVATE -Wall -Wextra -O2)
endif()

add_test(NAME pitch COMMAND whynot_pitch_tests)

# WHYNOT Ideias (gerador de acordes, melodia, baixo, 808 e bateria): também sem JUCE.
add_executable(whynot_ideas_tests
    ideas/test_ideas.cpp
    ../src/ideas/Analysis.cpp
    ../src/ideas/Generator.cpp
    ../src/ideas/Genres.cpp
    ../src/ideas/Prompt.cpp
    ../src/ideas/Theory.cpp)
target_compile_features(whynot_ideas_tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(whynot_ideas_tests PRIVATE /W4 /utf-8)
else()
    target_compile_options(whynot_ideas_tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME ideas COMMAND whynot_ideas_tests)
# WHYNOT Synth: motor, presets e nivelamento, também sem JUCE.
# "whynot_synth_tests --levels <arquivo>" mede os presets e regrava src/synth/PresetLevels.inc.
add_executable(whynot_synth_tests
    synth/test_synth.cpp
    ../src/synth/Presets3Osc.cpp
    ../src/synth/Presets808Bass.cpp
    ../src/synth/PresetsKeys.cpp
    ../src/synth/PresetsFinal.cpp
    ../src/synth/PresetsGenres.cpp
    ../src/synth/PresetsLeads.cpp
    ../src/synth/PresetsMore.cpp
    ../src/synth/PresetsPads.cpp
    ../src/synth/Synth.cpp
    ../src/synth/SynthMeasure.cpp
    ../src/synth/SynthParams.cpp
    ../src/synth/SynthPresets.cpp
    ../src/synth/Wavetables.cpp
    ../src/dsp/Color.cpp
    ../src/dsp/EqProcessor.cpp
    ../src/dsp/FxHost.cpp
    ../src/dsp/LoudnessMeter.cpp
    ../src/dsp/Master.cpp
    ../src/dsp/Space.cpp)
target_compile_features(whynot_synth_tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(whynot_synth_tests PRIVATE /W4 /utf-8 /O2)
else()
    target_compile_options(whynot_synth_tests PRIVATE -Wall -Wextra -O2)
endif()

add_test(NAME synth COMMAND whynot_synth_tests)

# Gravação (3.5): as contas do take (relógio, latência, punch, notas), sem JUCE.
add_executable(whynot_record_tests record/test_record.cpp)
target_compile_features(whynot_record_tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(whynot_record_tests PRIVATE /W4 /utf-8)
else()
    target_compile_options(whynot_record_tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME record COMMAND whynot_record_tests)

# Automação (3.5): posição do knob e o valor da curva no tick, sem JUCE.
add_executable(whynot_automation_tests automation/test_automation.cpp)
target_compile_features(whynot_automation_tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(whynot_automation_tests PRIVATE /W4 /utf-8)
else()
    target_compile_options(whynot_automation_tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME automation COMMAND whynot_automation_tests)

# Desempenho (3.5): medidor de CPU e de falhas de buffer, sem JUCE.
add_executable(whynot_perf_tests perf/test_perf.cpp)
target_compile_features(whynot_perf_tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(whynot_perf_tests PRIVATE /W4 /utf-8)
else()
    target_compile_options(whynot_perf_tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME perf COMMAND whynot_perf_tests)

# Tom do beat (21/10, noite): cromagrama, tom e acordes por compasso de um áudio, sem JUCE.
# "whynot_key_tests --wav <arquivo.wav> <bpm>" analisa um WAV e imprime o tom e os acordes.
add_executable(whynot_key_tests ideas/test_keydetect.cpp ../src/ideas/KeyDetect.cpp ../src/ideas/Theory.cpp)
target_compile_features(whynot_key_tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(whynot_key_tests PRIVATE /W4 /utf-8 /O2)
else()
    target_compile_options(whynot_key_tests PRIVATE -Wall -Wextra -O2)
endif()

add_test(NAME key COMMAND whynot_key_tests)

# Ponte de controle da Fase 4: WebSocket, Host/Origin/token, caminhos, limites, a tabela de risco
# dos comandos e fuzz, sem JUCE.
add_executable(whynot_control_tests
    control/test_control_core.cpp
    ../src/control/CommandPolicy.cpp
    ../src/control/ControlPlatform.cpp
    ../src/control/ControlPolicy.cpp
    ../src/control/WsProtocol.cpp)
target_compile_features(whynot_control_tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(whynot_control_tests PRIVATE /W4 /utf-8 /O2)
else()
    target_compile_options(whynot_control_tests PRIVATE -Wall -Wextra -O2)
endif()

add_test(NAME control COMMAND whynot_control_tests)

# Afinação nota a nota (frente C de 27/10): análise da voz em notas, edições do modelo e render com o PSOLA, sem JUCE.
add_executable(whynot_notetune_tests
    notetune/test_notetune.cpp
    ../src/dsp/NoteTune.cpp
    ../src/dsp/Tune.cpp
    ../src/dsp/VoicePitch.cpp)
target_compile_features(whynot_notetune_tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(whynot_notetune_tests PRIVATE /W4 /utf-8 /O2)
else()
    target_compile_options(whynot_notetune_tests PRIVATE -Wall -Wextra -O2)
endif()

add_test(NAME notetune COMMAND whynot_notetune_tests)
