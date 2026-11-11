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

# Desempenho (29/10, frente Desempenho): custo por bloco dos efeitos nativos, da afinação, das vozes do sampler e do
# WHYNOT Synth, e nenhuma alocação no process. O ctest roda só as checagens (--check); sem argumento, imprime a tabela.
add_executable(whynot_rt_bench
    perf/bench_rt.cpp
    ../src/dsp/Color.cpp
    ../src/dsp/Console.cpp
    ../src/dsp/Denoise.cpp
    ../src/dsp/DynamicEq.cpp
    ../src/dsp/Dynamics.cpp
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
    ../src/dsp/NoteTune.cpp
    ../src/dsp/PitchDetect.cpp
    ../src/dsp/Space.cpp
    ../src/dsp/Tune.cpp
    ../src/dsp/Vintage.cpp
    ../src/dsp/Vocal.cpp
    ../src/dsp/VoicePitch.cpp
    ../src/synth/Presets3Osc.cpp
    ../src/synth/Presets808Bass.cpp
    ../src/synth/PresetsFinal.cpp
    ../src/synth/PresetsGenres.cpp
    ../src/synth/PresetsKeys.cpp
    ../src/synth/PresetsLeads.cpp
    ../src/synth/PresetsMore.cpp
    ../src/synth/PresetsPads.cpp
    ../src/synth/Synth.cpp
    ../src/synth/SynthMeasure.cpp
    ../src/synth/SynthParams.cpp
    ../src/synth/SynthPresets.cpp
    ../src/synth/Wavetables.cpp)
target_compile_features(whynot_rt_bench PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(whynot_rt_bench PRIVATE /W4 /utf-8 /O2)
else()
    target_compile_options(whynot_rt_bench PRIVATE -Wall -Wextra -O2)
endif()

add_test(NAME rt_bench COMMAND whynot_rt_bench --check)

# Carregamento assíncrono de samples (frente samples-async): um trabalho por arquivo, o lote de cada canal com
# geração e a entrega entre duas threads de verdade. Sem JUCE.
find_package(Threads REQUIRED)
add_executable(whynot_sample_loads_tests samples/test_sample_loads.cpp)
target_compile_features(whynot_sample_loads_tests PRIVATE cxx_std_20)
target_link_libraries(whynot_sample_loads_tests PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(whynot_sample_loads_tests PRIVATE /W4 /utf-8 /O2)
else()
    target_compile_options(whynot_sample_loads_tests PRIVATE -Wall -Wextra -O2)
endif()

add_test(NAME sample_loads COMMAND whynot_sample_loads_tests)
# Parâmetros dos comandos (10/11, frente comandos): a conversão de número que satura e recusa o não finito, e a
# conferência de um valor contra a regra do schema (src/commands/Params.h, só cabeçalho), sem JUCE.
add_executable(whynot_params_tests
    commands/test_params.cpp)
target_compile_features(whynot_params_tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(whynot_params_tests PRIVATE /W4 /utf-8 /O2)
else()
    target_compile_options(whynot_params_tests PRIVATE -Wall -Wextra -O2)
endif()

add_test(NAME params COMMAND whynot_params_tests)
# Caminhos que um .wnl guarda (frente wnl-seguro, 10/11): o que o app aceita ao abrir um projeto de terceiros e o
# que ele copia para samples/, mais um fuzz, sem JUCE. As regras de nome do Windows são as da ponte de controle.
add_executable(whynot_wnl_paths_tests
    model/test_wnl_paths.cpp
    ../src/control/ControlPolicy.cpp
    ../src/control/WsProtocol.cpp)
target_include_directories(whynot_wnl_paths_tests PRIVATE ../src)
target_compile_features(whynot_wnl_paths_tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(whynot_wnl_paths_tests PRIVATE /W4 /utf-8 /O2)
else()
    target_compile_options(whynot_wnl_paths_tests PRIVATE -Wall -Wextra -O2)
endif()

add_test(NAME wnl_paths COMMAND whynot_wnl_paths_tests)
# Registro de queda e log de sessão (frente crash-log, 10/11): o texto do .txt, o rodízio dos arquivos, o módulo de
# um endereço, o log em arquivo e, no Windows, processos filhos que caem de propósito para conferir o .dmp. Sem JUCE.
# Nenhum deles abre janela (o teste desliga as do Windows e as da biblioteca C).
add_executable(whynot_crash_tests
    app/test_crash.cpp
    ../src/app/CrashReport.cpp
    ../src/common/Log.cpp)
target_compile_features(whynot_crash_tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(whynot_crash_tests PRIVATE /W4 /utf-8 /O2)
else()
    target_compile_options(whynot_crash_tests PRIVATE -Wall -Wextra -O2)
endif()

add_test(NAME crash COMMAND whynot_crash_tests)
# Autosave e recuperação de queda (frente app-autosave, 10/11): a política de adiamento do autosave periódico, o
# conjunto de plugins sujos e os suspeitos de uma queda (janela aberta, últimos adicionados), sem JUCE.
add_executable(whynot_autosave_tests
    app/test_autosave_policy.cpp)
target_compile_features(whynot_autosave_tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(whynot_autosave_tests PRIVATE /W4 /utf-8 /O2)
else()
    target_compile_options(whynot_autosave_tests PRIVATE -Wall -Wextra -O2)
endif()

add_test(NAME autosave COMMAND whynot_autosave_tests)
# Lógica da tela sem JUCE (frente ui-atualizacao, 10/11): quais telas atualizar dada a máscara da mudança e o
# que está à vista (RefreshPlan.h), e a decisão da placa de som que some e volta (DeviceWatch.h).
add_executable(whynot_ui_tests ui/test_ui_logic.cpp)
target_compile_features(whynot_ui_tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(whynot_ui_tests PRIVATE /W4 /utf-8)
else()
    target_compile_options(whynot_ui_tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME ui_logic COMMAND whynot_ui_tests)
# Paleta de comandos (frente paleta, 10/11): dobrar o texto (sem acento, sem caixa), pontuar e ordenar, sem JUCE.
add_executable(whynot_palette_tests
    ui/test_command_search.cpp)
target_compile_features(whynot_palette_tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(whynot_palette_tests PRIVATE /W4 /utf-8 /O2)
else()
    target_compile_options(whynot_palette_tests PRIVATE -Wall -Wextra -O2)
endif()

add_test(NAME palette COMMAND whynot_palette_tests)
# CI (frente ci, 10/11): com -DWHYNOT_WERROR=ON, aviso vira erro em todos os executáveis de teste desta pasta.
# A chamada fica adiada para o fim do arquivo (cmake_language DEFER): pega também os alvos de blocos que entrarem
# depois deste. O WHYNOT_ASAN e o WHYNOT_GUARD_CF vêm do CMakeLists.txt da raiz e já valem aqui.
function(whynot_ci_test_options)
    if(NOT WHYNOT_WERROR)
        return()
    endif()

    get_property(whynot_test_targets DIRECTORY "${CMAKE_CURRENT_FUNCTION_LIST_DIR}" PROPERTY BUILDSYSTEM_TARGETS)

    foreach(whynot_test_target IN LISTS whynot_test_targets)
        get_target_property(whynot_test_type ${whynot_test_target} TYPE)

        if(whynot_test_type STREQUAL "EXECUTABLE")
            if(MSVC)
                target_compile_options(${whynot_test_target} PRIVATE /WX)
            else()
                target_compile_options(${whynot_test_target} PRIVATE -Werror)
            endif()
        endif()
    endforeach()
endfunction()

cmake_language(DEFER CALL whynot_ci_test_options)
# Editores (frente editores, 10/11): a tabela de atalhos com contexto (KeyMapCore.h: presets, trocas do usuário,
# conflito) e o seguir a reprodução por página (TimelineFollow.h). Só cabeçalhos, sem JUCE.
add_executable(whynot_editors_tests
    editors/test_editors_core.cpp)
target_compile_features(whynot_editors_tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(whynot_editors_tests PRIVATE /W4 /utf-8 /O2)
else()
    target_compile_options(whynot_editors_tests PRIVATE -Wall -Wextra -O2)
endif()

add_test(NAME editors COMMAND whynot_editors_tests)
