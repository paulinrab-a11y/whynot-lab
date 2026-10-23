// Comandos da gravação (entrega 3.5): armar, REC, punch, latência, notas com tempo e o take.

#include "CommandRegistry.h"
#include "../engine/SampleAnalysis.h"
#include "../record/BeatTempo.h"
#include "../record/RecordEngine.h"
#include "../record/Takes.h"

namespace whynot
{

namespace
{
    using params::schema;

    /** Teto da linha do tempo (10.000 compassos). */
    constexpr double maxTicks = (double) maxTimelineTicks;

    /** Converte um valor na unidade pedida ("ticks", "steps", "beats" ou "bars") para ticks, dentro do teto. */
    int toTicks (const juce::var& args, double value)
    {
        const auto unit = params::string (args, "unit", "ticks");
        const double scale = unit == "steps" ? ticksPerStep : unit == "beats" ? ticksPerBeat : unit == "bars" ? ticksPerBar4 : 1.0;
        const double ticks = value * scale;
        return std::isfinite (ticks) ? juce::roundToInt (juce::jlimit (-maxTicks, maxTicks, ticks)) : 0;
    }

    double field (const juce::var& o, const char* key, double fallback)
    {
        if (! o.isObject() || ! o.hasProperty (key))
            return fallback;

        const auto& v = o[key];
        const double d = (v.isDouble() || v.isInt() || v.isInt64()) ? (double) v : v.toString().getDoubleValue();
        return std::isfinite (d) ? d : fallback;
    }

    /** Um campo em ticks, dentro da linha do tempo (sem estourar o int com valores absurdos). */
    int tickField (const juce::var& o, const char* key, int fallback)
    {
        return juce::roundToInt (juce::jlimit (0.0, maxTicks, field (o, key, fallback)));
    }

    int nextNoteId (const std::vector<Note>& notes)
    {
        int maxId = 0;

        for (auto& n : notes)
            maxId = juce::jmax (maxId, n.id);

        return maxId + 1;
    }

    /** A primeira raia da playlist sem nada entre start e end; se todas estiverem ocupadas, uma nova. */
    juce::String freeLane (Project& p, int start, int end)
    {
        for (auto& lane : p.playlistTracks)
        {
            const bool busy = std::any_of (p.playlistItems.begin(), p.playlistItems.end(), [&] (const PlaylistItem& i)
                                           { return i.track == lane.id && i.start < end && start < i.end(); });

            if (! busy)
                return lane.id;
        }

        PlaylistTrack lane;
        lane.id = "pl_" + juce::String ((int) p.playlistTracks.size() + 1).paddedLeft ('0', 2);

        while (p.findPlaylistTrack (lane.id) != nullptr)
            lane.id += "b";

        lane.name = "Raia "_t + juce::String ((int) p.playlistTracks.size() + 1);   // o nome que a playlist usa (pedido do Design, 21/10)
        p.playlistTracks.push_back (lane);
        return lane.id;
    }

    CommandResult needRecorder()
    {
        return CommandResult::fail ("Gravação indisponível neste modo."_t);
    }

    //==========================================================================
    // Edição do clipe de voz (gravar voz, fatia 4)

    /** O pico do trecho do arquivo que o bloco toca (0 a 1+); -1 se o arquivo não abre. */
    float peakOf (const juce::File& file, double fromSeconds, double seconds)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

        if (reader == nullptr || reader->sampleRate <= 0.0)
            return -1.0f;

        const auto first = (juce::int64) std::floor (juce::jmax (0.0, fromSeconds) * reader->sampleRate);
        const auto last = juce::jmin (reader->lengthInSamples, (juce::int64) std::ceil ((fromSeconds + seconds) * reader->sampleRate));
        juce::AudioBuffer<float> block ((int) juce::jmax (1u, reader->numChannels), 65536);
        float peak = 0.0f;

        for (auto pos = first; pos < last; pos += block.getNumSamples())
        {
            const int n = (int) juce::jmin ((juce::int64) block.getNumSamples(), last - pos);
            reader->read (&block, 0, n, pos, true, true);

            for (int ch = 0; ch < block.getNumChannels(); ++ch)
                peak = juce::jmax (peak, block.getMagnitude (ch, 0, n));
        }

        return peak;
    }

    /** Escreve o arquivo de trás para frente, em WAV 32 bits float (igual à entrada, amostra por amostra). */
    juce::Result writeReversed (const juce::File& source, const juce::File& target, double& seconds)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (source));

        if (reader == nullptr || reader->lengthInSamples <= 0 || reader->lengthInSamples > (juce::int64) 48000 * 60 * 60)
            return juce::Result::fail ("Não consegui ler o áudio do bloco: "_t + source.getFullPathName());

        const int channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
        juce::AudioBuffer<float> all (channels, (int) reader->lengthInSamples);
        reader->read (&all, 0, all.getNumSamples(), 0, true, channels > 1);
        all.reverse (0, all.getNumSamples());
        seconds = all.getNumSamples() / reader->sampleRate;

        target.getParentDirectory().createDirectory();
        target.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (target.createOutputStream().release());

        if (stream == nullptr)
            return juce::Result::fail ("Não consegui criar "_t + target.getFullPathName());

        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                     .withSampleRate (reader->sampleRate)
                                                     .withNumChannels (channels)
                                                     .withBitsPerSample (32)
                                                     .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));

        if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (all, 0, all.getNumSamples()))
            return juce::Result::fail ("Não consegui escrever "_t + target.getFullPathName());

        return juce::Result::ok();
    }

    /** O andamento de um áudio (o beat em um gesto, fatia 6): pelo nome do arquivo ou do canal, senão pela
        análise de até um minuto dele. bpm 0 = não deu. */
    struct Detected
    {
        double bpm = 0.0;
        juce::String source;   // "nome" ou "análise"
    };

    Detected detectBpm (const juce::File& file, const juce::String& channelName)
    {
        for (auto& name : { file.getFileName(), channelName })
            if (const double fromName = beat::bpmFromName (name.toStdString()); fromName > 0.0)
                return { fromName, "nome" };

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

        if (reader == nullptr || reader->sampleRate <= 0.0)
            return {};

        const int n = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (62.0 * reader->sampleRate));
        juce::AudioBuffer<float> audio ((int) juce::jlimit (1u, 2u, reader->numChannels), juce::jmax (1, n));
        reader->read (&audio, 0, n, 0, true, audio.getNumChannels() > 1);

        if (audio.getNumChannels() > 1)   // a soma dos dois lados (os ataques ficam iguais)
            audio.addFrom (0, 0, audio, 1, 0, n);

        return { beat::bpmFromAudio (audio.getReadPointer (0), n, reader->sampleRate), "análise"_t };
    }

    double fileSecondsOf (const juce::File& file)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        return reader != nullptr && reader->sampleRate > 0.0 ? (double) reader->lengthInSamples / reader->sampleRate : 0.0;
    }
}

void registerRecordCommands (CommandRegistry& r)
{
    r.add ({ "record.state", "Estado da gravação"_t, schema ({}), false,
             [] (CommandContext& c, const juce::var&)
             {
                 auto* rec = c.app.recorder();
                 return rec != nullptr ? CommandResult::success (rec->describe (c.model.get())) : needRecorder();
             } });

    r.add ({ "record.arm", "Armar para gravar",
             schema ({ { "track", "string", false, "Id da trilha do mixer que recebe a entrada de áudio (uma por vez; o master não)"_t },
                       { "channel", "string", false, "Id do canal cujas notas ao vivo viram notas do padrão (instrumento, não áudio)"_t },
                       { "on", "boolean", false, "true = arma, false = desarma; ausente = alterna"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 juce::String inputProblem;

                 if (params::has (a, "track"))
                 {
                     auto* m = p.findMixer (params::string (a, "track"));

                     if (m == nullptr || m->isMaster())
                         return CommandResult::fail ("Trilha do mixer não encontrada (o master não grava)."_t);

                     m->armed = params::boolean (a, "on", ! m->armed);

                     if (m->armed)
                     {
                         for (auto& other : p.mixer)
                             if (&other != m)
                                 other.armed = false;   // a entrada é uma só: uma trilha armada por vez

                         // abre a entrada da placa agora, não na hora de gravar (a placa reinicia)
                         if (auto* rec = c.app.recorder())
                             if (const auto opened = rec->ensureInputOpen(); opened.failed())
                                 inputProblem = opened.getErrorMessage();
                     }
                 }
                 else if (params::has (a, "channel"))
                 {
                     auto* ch = p.findChannel (params::string (a, "channel"));

                     if (ch == nullptr)
                         return CommandResult::fail ("Canal não encontrado."_t);

                     if (ch->isAudio())
                         return CommandResult::fail ("Canal de áudio não grava notas: para gravar áudio, arme a trilha do mixer dele."_t);

                     ch->armed = params::boolean (a, "on", ! ch->armed);
                 }
                 else
                 {
                     return CommandResult::fail ("Informe \"track\" (áudio) ou \"channel\" (MIDI)."_t);
                 }

                 auto o = new juce::DynamicObject();

                 if (inputProblem.isNotEmpty())
                     o->setProperty ("warning", inputProblem);

                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "record.setPunch", "Punch in/out",
             schema ({ { "on", "boolean", false, "true = grava só entre start e end; ausente = mantém (ou alterna, sem start/end)"_t },
                       { "start", "number", false, "Punch in"_t },
                       { "end", "number", false, "Punch out"_t },
                       { "unit", "string", false, "Unidade de start/end: ticks (960 por tempo, padrão), steps, beats ou bars"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 const bool hasRange = params::has (a, "start") || params::has (a, "end");
                 const int start = params::has (a, "start") ? toTicks (a, params::number (a, "start", 0)) : p.punchIn;
                 const int end = params::has (a, "end") ? toTicks (a, params::number (a, "end", 0)) : p.punchOut;

                 if (start < 0 || end <= start || end - start < ticksPerStep)
                     return CommandResult::fail ("O punch out precisa vir depois do punch in (pelo menos um passo)."_t);

                 p.punchIn = start;
                 p.punchOut = end;
                 p.punchOn = params::boolean (a, "on", hasRange ? true : ! p.punchOn);
                 return CommandResult::success();
             } });

    r.add ({ "record.setLatency", "Ajuste da latência de gravação"_t,
             schema ({ { "adjustMs", "number", true, "Ajuste manual em ms (-500 a 500), somado à latência que a placa informa; positivo = o take vai mais para trás"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* rec = c.app.recorder();

                 if (rec == nullptr)
                     return needRecorder();

                 rec->setAdjustMs (params::number (a, "adjustMs", 0.0));
                 return CommandResult::success (rec->describe (c.model.get()));
             } });

    r.add ({ "record.setInput", "Entrada de gravação"_t,
             schema ({ { "first", "integer", false, "Entrada da placa (1 = a primeira)"_t },
                       { "stereo", "boolean", false, "true = o par first e first + 1; false = mono (padrão)"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* rec = c.app.recorder();

                 if (rec == nullptr)
                     return needRecorder();

                 if (rec->isRecording())
                     return CommandResult::fail ("Pare a gravação antes de trocar a entrada."_t);

                 const int first = (int) params::number (a, "first", rec->settings().firstInput + 1);

                 if (first < 1 || first > 64)
                     return CommandResult::fail ("Entrada inválida: use 1 para a primeira."_t);

                 rec->setInput (first - 1, params::boolean (a, "stereo", rec->settings().stereo));
                 return CommandResult::success (rec->describe (c.model.get()));
             } });

    // Monitoração pelo app (gravar voz, 21/10): a entrada da placa toca pela trilha, com os efeitos dela.
    // O take continua seco: o gravador pega a entrada antes de tudo.
    r.add ({ "record.setMonitor", "Monitorar a entrada"_t,
             schema ({ { "track", "string", true, "Id da trilha do mixer (o master não)"_t },
                       { "mode", "string", true, "off = nunca; armed = quando armada (o padrão); on = sempre"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* m = c.model.data().findMixer (params::string (a, "track"));

                 if (m == nullptr || m->isMaster())
                     return CommandResult::fail ("Trilha do mixer não encontrada (o master não monitora)."_t);

                 const auto mode = params::string (a, "mode");

                 if (mode != "off" && mode != "armed" && mode != "on")
                     return CommandResult::fail ("Use off, armed ou on."_t);

                 m->monitor = mode == "armed" ? juce::String() : mode;
                 return CommandResult::success();
             } });

    r.add ({ "record.setLowLatency", "Baixa latência ao gravar"_t,
             schema ({ { "on", "boolean", true, "true = gravando com a monitoração, os efeitos com atraso da trilha e do master saem do que se ouve (o take é seco de qualquer jeito)"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* rec = c.app.recorder();

                 if (rec == nullptr)
                     return needRecorder();

                 rec->setLowLatency (params::boolean (a, "on", ! rec->settings().lowLatency));
                 return CommandResult::success (rec->describe (c.model.get()));
             } });

    // O assistente de gravação (gravar voz, fatia 7; pedido do usuário de 21/10, 16:44): reconhece a placa pelo
    // nome que o driver dá e, com o tipo de microfone, diz os ajustes (o nome da placa vem dela, nunca nosso).
    r.add ({ "record.guide", "Assistente de gravação"_t,
             schema ({ { "driver", "string", false, "Simular outro driver (\"ASIO\", \"Windows Audio\"); ausente = o aberto agora"_t },
                       { "device", "string", false, "Simular outro aparelho pelo nome; ausente = o aberto agora"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* rec = c.app.recorder();
                 return rec != nullptr ? CommandResult::success (rec->guide (params::string (a, "driver"), params::string (a, "device")))
                                       : needRecorder();
             } });

    r.add ({ "record.setMic", "Tipo de microfone"_t,
             schema ({ { "type", "string", true, "condenser (condensador com XLR, precisa de 48V), dynamic (dinâmico com XLR), usb ou \"\" (não sei)"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* rec = c.app.recorder();

                 if (rec == nullptr)
                     return needRecorder();

                 const auto type = params::string (a, "type");

                 if (type.isNotEmpty() && type != "condenser" && type != "dynamic" && type != "usb")
                     return CommandResult::fail ("Use condenser, dynamic, usb ou vazio."_t);

                 rec->setMic (type);
                 return CommandResult::success (rec->guide());
             } });

    //==========================================================================
    // Fatia 8 (revisão de iniciante de 22/10): o botão "Gravar voz", o microfone (o aparelho de entrada) e o fone.

    // "Gravar voz": acha a trilha da voz (a pedida, a armada, a que se chama "Voz" ou uma nova), arma, liga a contagem
    // se estava desligada e o REC. O play vem depois, pela tela (transport.play): ele pode passar para o SONG, que é
    // outro comando.
    r.add ({ "record.voice", "Gravar voz"_t,
             schema ({ { "track", "string", false, "Trilha do mixer da voz (ausente = a armada, a \"Voz\" ou uma nova)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* rec = c.app.recorder();

                 if (rec == nullptr)
                     return needRecorder();

                 if (rec->isRecording())
                     return CommandResult::fail ("Já está gravando: o stop guarda o take."_t);

                 auto& p = c.model.data();
                 MixerTrack* voice = nullptr;

                 if (params::has (a, "track"))
                 {
                     voice = p.findMixer (params::string (a, "track"));

                     if (voice == nullptr || voice->isMaster())
                         return CommandResult::fail ("Trilha do mixer não encontrada (o master não grava)."_t);
                 }

                 for (auto& m : p.mixer)
                     if (voice == nullptr && m.armed && ! m.isMaster())
                         voice = &m;

                 for (auto& m : p.mixer)
                     if (voice == nullptr && ! m.isMaster() && m.name.trim().equalsIgnoreCase ("Voz"))
                         voice = &m;

                 const bool created = voice == nullptr;

                 if (created)
                 {
                     MixerTrack m;
                     m.id = makeId ("mx");
                     m.name = "Voz";
                     m.colour = channelColourForIndex ((int) p.mixer.size());
                     m.role = "vocal";
                     p.mixer.push_back (m);
                     voice = &p.mixer.back();
                 }

                 for (auto& m : p.mixer)
                     m.armed = &m == voice;   // a entrada é uma só: uma trilha armada por vez

                 c.app.stop();

                 if (rec->settings().countInBars == 0)
                     rec->setCountIn (rec->settings().countInLength);   // o último tamanho (1 compasso, no começo)

                 rec->setRecordMode (true);

                 auto o = new juce::DynamicObject();
                 o->setProperty ("track", voice->id);
                 o->setProperty ("created", created);
                 o->setProperty ("countInBars", rec->settings().countInBars);

                 if (const auto opened = rec->ensureInputOpen(); opened.failed())
                     o->setProperty ("warning", opened.getErrorMessage());

                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "record.inputDevices", "Microfones (aparelhos de entrada)"_t, schema ({}), false,
             [] (CommandContext& c, const juce::var&)
             {
                 auto* rec = c.app.recorder();

                 if (rec == nullptr)
                     return needRecorder();

                 const auto d = rec->inputDevices();
                 juce::Array<juce::var> names;

                 for (auto& n : d.names)
                     names.add (n);

                 auto o = new juce::DynamicObject();
                 o->setProperty ("separate", d.separate);
                 o->setProperty ("devices", names);
                 o->setProperty ("default", d.defaultName);
                 o->setProperty ("chosen", rec->settings().inputDevice);
                 o->setProperty ("current", d.current);
                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "record.setInputDevice", "Microfone (aparelho de entrada)"_t,
             schema ({ { "name", "string", true, "O aparelho de entrada, como record.inputDevices lista; \"\" = o padrão do Windows. "
                                                 "No ASIO não vale: a entrada é a própria placa"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* rec = c.app.recorder();

                 if (rec == nullptr)
                     return needRecorder();

                 const auto name = params::string (a, "name").trim();

                 if (const auto d = rec->inputDevices(); name.isNotEmpty() && d.separate && ! d.names.contains (name))
                     return CommandResult::fail ("Esse microfone não está na lista: "_t + name);

                 if (const auto result = rec->setInputDevice (name); result.failed())
                     return CommandResult::fail (result.getErrorMessage());

                 c.app.refreshMonitoring();
                 return CommandResult::success (rec->guide());
             } });

    r.add ({ "record.setHeadphones", "Escuta: de fone ou nas caixas"_t,
             schema ({ { "answer", "string", true, "yes = de fone (a escuta \"Quando armada\" vale); no = nas caixas (a escuta pelo app "
                                                   "fica desligada); \"\" = perguntar de novo"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* rec = c.app.recorder();

                 if (rec == nullptr)
                     return needRecorder();

                 const auto answer = params::string (a, "answer");

                 if (answer.isNotEmpty() && answer != "yes" && answer != "no")
                     return CommandResult::fail ("Use yes, no ou vazio."_t);

                 rec->setHeadphones (answer == "yes" ? 1 : answer == "no" ? 2 : 0);
                 c.app.refreshMonitoring();
                 return CommandResult::success (rec->describe (c.model.get()));
             } });

    r.add ({ "record.cancel", "Cancelar o take"_t,
             schema ({}), false,
             [] (CommandContext& c, const juce::var&)
             {
                 auto* rec = c.app.recorder();

                 if (rec == nullptr)
                     return needRecorder();

                 if (! rec->isRecording())
                     return CommandResult::fail ("Não está gravando."_t);

                 c.app.cancelRecording();
                 return CommandResult::success (rec->describe (c.model.get()));
             } });

    // A entrada pela playlist (gravar voz, fatia 5): escolher a entrada no cabeçalho da raia já arma, como no
    // FL. A raia ganha uma trilha do mixer própria (com o nome dela), e os takes dessa trilha caem nela.
    r.add ({ "playlist.setLaneInput", "Gravar nesta raia"_t,
             schema ({ { "track", "string", true, "Raia da playlist: id ou número (1 = primeira)"_t },
                       { "first", "integer", true, "Entrada da placa (1 = a primeira); 0 = a raia para de gravar"_t },
                       { "stereo", "boolean", false, "true = o par first e first + 1; false = mono (padrão, a voz)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto key = params::string (a, "track");
                 PlaylistTrack* lane = nullptr;

                 for (auto& t : p.playlistTracks)
                     if (t.id == key)
                         lane = &t;

                 if (lane == nullptr && key.isNotEmpty() && key.containsOnly ("0123456789")
                     && juce::isPositiveAndBelow (key.getIntValue() - 1, (int) p.playlistTracks.size()))
                     lane = &p.playlistTracks[(size_t) key.getIntValue() - 1];

                 if (lane == nullptr)
                     return CommandResult::fail ("Raia da playlist não encontrada."_t);

                 const int first = (int) params::number (a, "first", 1);

                 if (first == 0)
                 {
                     if (auto* m = p.findMixer (lane->recordTrack))
                         m->armed = false;

                     lane->recordTrack = {};
                     return CommandResult::success();
                 }

                 auto* rec = c.app.recorder();

                 if (rec == nullptr)
                     return needRecorder();

                 if (rec->isRecording())
                     return CommandResult::fail ("Pare a gravação antes de trocar a entrada."_t);

                 if (first < 1 || first > 64)
                     return CommandResult::fail ("Entrada inválida: use 1 para a primeira."_t);

                 // a trilha do mixer da raia: a que ela já tem, ou uma nova com o nome dela
                 if (p.findMixer (lane->recordTrack) == nullptr)
                 {
                     MixerTrack m;
                     m.id = makeId ("mx");
                     m.name = lane->name;
                     m.colour = channelColourForIndex ((int) p.mixer.size());
                     m.role = "vocal";
                     p.mixer.push_back (m);
                     lane->recordTrack = m.id;
                 }

                 rec->setInput (first - 1, params::boolean (a, "stereo", false));

                 for (auto& m : p.mixer)
                     m.armed = m.id == lane->recordTrack;   // a entrada é uma só: uma trilha armada por vez

                 auto o = new juce::DynamicObject();
                 o->setProperty ("mixerTrack", lane->recordTrack);

                 if (const auto opened = rec->ensureInputOpen(); opened.failed())
                     o->setProperty ("warning", opened.getErrorMessage());

                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "record.setCountIn", "Contagem antes de gravar"_t,
             schema ({ { "bars", "integer", false, "0 = sem contagem; 1 ou 2 compassos de clique antes do take. Ausente = liga e desliga (Ctrl+P), com o último tamanho"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* rec = c.app.recorder();

                 if (rec == nullptr)
                     return needRecorder();

                 const int bars = params::has (a, "bars") ? (int) params::number (a, "bars", 0)
                                                          : (rec->settings().countInBars > 0 ? 0 : rec->settings().countInLength);

                 if (bars < 0 || bars > 2)
                     return CommandResult::fail ("A contagem é de 0, 1 ou 2 compassos."_t);

                 rec->setCountIn (bars);
                 return CommandResult::success (rec->describe (c.model.get()));
             } });

    // A agulha (gravar voz, 21/10): no modo SONG, o play e a gravação começam dela, e o stop volta para
    // ela, como a marca de posição do FL. Com uma seleção de tempo na playlist, vale o começo da seleção.
    r.add ({ "transport.setPosition", "Mover a agulha"_t,
             schema ({ { "at", "number", true, "Onde, na linha do tempo (0 = o começo)"_t },
                       { "unit", "string", false, "Unidade: ticks (padrão), steps, beats ou bars"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 c.app.setPlayStart (juce::jmax (0, toTicks (a, params::number (a, "at", 0))));
                 auto o = new juce::DynamicObject();
                 o->setProperty ("at", c.app.playStart());
                 return CommandResult::success (juce::var (o));
             } });

    //==========================================================================
    // Edição do clipe de voz (gravar voz, fatia 4): ganho e fades nas alças do bloco, normalizar, inverter e
    // consolidar num WAV. O ganho e os fades são campos do bloco (o arquivo não muda); inverter e consolidar
    // escrevem um WAV novo na pasta das gravações, e o bloco passa a tocar ele (o desfazer volta para o antigo).
    r.add ({ "audio.setGain", "Ganho do bloco"_t,
             schema ({ { "item", "string", true, "Id do bloco de áudio"_t },
                       { "db", "number", true, "Ganho em dB, de -60 a +24 (0 = o arquivo como está)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* item = c.model.data().findItem (params::string (a, "item"));

                 if (item == nullptr || item->kind != "audio")
                     return CommandResult::fail ("Bloco de áudio não encontrado."_t);

                 item->gainDb = clampClipGain ((float) params::number (a, "db", 0.0));
                 auto o = new juce::DynamicObject();
                 o->setProperty ("db", item->gainDb);
                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "audio.setFades", "Fades do bloco"_t,
             schema ({ { "item", "string", true, "Id do bloco de áudio"_t },
                       { "in", "number", false, "Fade de entrada, em segundos (0 = nenhum)"_t },
                       { "out", "number", false, "Fade de saída, em segundos (0 = nenhum)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto* item = p.findItem (params::string (a, "item"));

                 if (item == nullptr || item->kind != "audio")
                     return CommandResult::fail ("Bloco de áudio não encontrado."_t);

                 // os dois cabem no bloco (o normalise confere de novo)
                 const double length = ticksToSeconds (item->length, p.bpm);

                 if (params::has (a, "in"))
                     item->fadeIn = roundToMicroseconds (juce::jlimit (0.0, length, params::number (a, "in", 0.0)));

                 if (params::has (a, "out"))
                     item->fadeOut = roundToMicroseconds (juce::jlimit (0.0, juce::jmax (0.0, length - item->fadeIn), params::number (a, "out", 0.0)));

                 auto o = new juce::DynamicObject();
                 o->setProperty ("in", item->fadeIn);
                 o->setProperty ("out", item->fadeOut);
                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "audio.normalize", "Normalizar"_t,
             schema ({ { "item", "string", true, "Id do bloco de áudio"_t },
                       { "peakDb", "number", false, "Onde o pico do trecho fica, em dBFS (padrão -1)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto* item = p.findItem (params::string (a, "item"));
                 const auto* channel = item != nullptr ? p.findChannel (item->ref) : nullptr;

                 if (item == nullptr || item->kind != "audio" || channel == nullptr)
                     return CommandResult::fail ("Bloco de áudio não encontrado."_t);

                 // o pico só do trecho que o bloco toca, no arquivo (o ganho é do bloco: o arquivo não muda)
                 const float peak = peakOf (c.model.resolveSample (channel->samplePath), item->offsetSeconds,
                                            item->fileSeconds (ticksToSeconds (item->length, p.bpm)));

                 if (peak < 0.0f)
                     return CommandResult::fail ("Não consegui ler o áudio do bloco."_t);

                 if (peak < 1.0e-6f)
                     return CommandResult::fail ("O trecho do bloco está em silêncio."_t);

                 const double target = juce::jlimit (-30.0, 0.0, params::number (a, "peakDb", -1.0));
                 item->gainDb = clampClipGain ((float) (target - 20.0 * std::log10 ((double) peak)));

                 auto o = new juce::DynamicObject();
                 o->setProperty ("db", item->gainDb);
                 o->setProperty ("peakDb", std::round (200.0 * std::log10 ((double) peak)) / 10.0);
                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "audio.reverse", "Inverter o áudio"_t,
             schema ({ { "item", "string", true, "Id do bloco de áudio: ele passa a tocar o mesmo trecho, de trás para frente"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto* item = p.findItem (params::string (a, "item"));
                 const auto* channel = item != nullptr ? p.findChannel (item->ref) : nullptr;

                 if (item == nullptr || item->kind != "audio" || channel == nullptr)
                     return CommandResult::fail ("Bloco de áudio não encontrado."_t);

                 // O arquivo invertido fica ao lado do original ("Voz_take03_invertido.wav"); inverter de novo
                 // volta para o original. Outro canal toca ele, pela mesma trilha do mixer.
                 const auto source = c.model.resolveSample (channel->samplePath);
                 const auto name = source.getFileNameWithoutExtension();
                 const bool back = name.endsWith ("_invertido") && source.getSiblingFile (name.dropLastCharacters (10) + ".wav").existsAsFile();
                 const auto target = back ? source.getSiblingFile (name.dropLastCharacters (10) + ".wav")
                                          : source.getSiblingFile (name + "_invertido.wav");
                 double seconds = fileSecondsOf (source);

                 if (! back && ! target.existsAsFile())
                     if (const auto written = writeReversed (source, target, seconds); written.failed())
                         return CommandResult::fail (written.getErrorMessage());

                 if (seconds <= 0.0)
                     return CommandResult::fail ("Não consegui ler o áudio do bloco."_t);

                 const auto targetPath = target.getFullPathName().replaceCharacter ('\\', '/');
                 juce::String channelId;

                 for (auto& ch : p.channels)
                     if (ch.isAudio() && ch.output == channel->output && c.model.resolveSample (ch.samplePath) == target)
                         channelId = ch.id;

                 if (channelId.isEmpty())
                 {
                     Channel reversed = *channel;
                     reversed.id = makeId ("ch");
                     reversed.name = back ? channel->name.upToLastOccurrenceOf (" (invertido)", false, false) : channel->name + " (invertido)";
                     reversed.samplePath = targetPath;
                     reversed.originalPath = {};
                     channelId = reversed.id;
                     p.channels.push_back (reversed);
                     item = p.findItem (params::string (a, "item"));   // o vetor de canais cresceu; o bloco continua no mesmo lugar
                 }

                 // o trecho espelha: o que começava em offset agora começa em (duração - fim do trecho)
                 const double segment = item->fileSeconds (ticksToSeconds (item->length, p.bpm));
                 item->offsetSeconds = roundToMicroseconds (juce::jmax (0.0, seconds - (item->offsetSeconds + segment)));
                 item->ref = channelId;
                 std::swap (item->fadeIn, item->fadeOut);

                 auto o = new juce::DynamicObject();
                 o->setProperty ("channel", channelId);
                 o->setProperty ("path", targetPath);
                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "audio.consolidate", "Consolidar num WAV"_t,
             schema ({ { "items", "array", true, "Ids dos blocos de áudio (por exemplo, os pedaços do comp): viram um WAV só, com o ganho, "
                                                 "os fades e os crossfades, sem os efeitos do mixer"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 std::vector<PlaylistItem> chosen;

                 if (auto* list = a["items"].getArray())
                     for (auto& id : *list)
                         if (const auto* item = p.findItem (id.toString()); item != nullptr && item->kind == "audio"
                             && std::none_of (chosen.begin(), chosen.end(), [item] (const PlaylistItem& x) { return x.id == item->id; }))
                             chosen.push_back (*item);   // um id repetido entra uma vez só (senão soaria dobrado)

                 if (chosen.empty())
                     return CommandResult::fail ("Escolha os blocos de áudio que viram um WAV só."_t);

                 int from = maxTimelineTicks, to = 0;

                 for (auto& i : chosen)
                     from = juce::jmin (from, i.start), to = juce::jmax (to, i.end());

                 // Um projeto só para o render: os blocos escolhidos, os canais deles saindo direto no master,
                 // e o master sem efeitos nem volume. O que sai é a soma seca deles, como tocam.
                 Project solo = p;
                 solo.songMode = true;
                 solo.automation.clear();
                 solo.playlistItems = chosen;
                 solo.channels.clear();

                 for (auto& ch : p.channels)
                 {
                     if (std::any_of (chosen.begin(), chosen.end(), [&ch] (const PlaylistItem& i) { return i.ref == ch.id; }))
                     {
                         auto copy = ch;
                         copy.output = masterTrackId;
                         copy.mute = false;
                         solo.channels.push_back (copy);
                     }
                 }

                 MixerTrack master;
                 master.id = masterTrackId;
                 master.name = "Master";
                 solo.mixer = { master };

                 const auto* first = p.findChannel (chosen.front().ref);
                 const auto base = juce::File::createLegalFileName ((first != nullptr ? first->name : juce::String ("Voz")).replace (" ", "_") + "_consolidado");
                 const auto out = c.app.recordingsFolder().getNonexistentChildFile (base, ".wav", false);
                 out.getParentDirectory().createDirectory();

                 // o render começa na amostra de antes (a grade de 48 kHz do motor), e a fração vai para o corte do bloco
                 const double fromSeconds = ticksToSeconds (from, p.bpm);
                 const double alignedFrom = std::floor (fromSeconds * 48000.0) / 48000.0;

                 if (const auto rendered = c.app.renderRange (solo, alignedFrom, ticksToSeconds (to, p.bpm), out); rendered.failed())
                     return CommandResult::fail (rendered.getErrorMessage());

                 Channel ch;
                 ch.id = makeId ("ch");
                 ch.kind = "audio";
                 ch.name = (first != nullptr ? first->name : juce::String ("Voz")) + " (consolidado)";
                 ch.colour = first != nullptr ? first->colour : channelColourForIndex ((int) p.channels.size());
                 ch.samplePath = out.getFullPathName().replaceCharacter ('\\', '/');
                 ch.output = first != nullptr ? first->output : masterTrackId;
                 p.channels.push_back (ch);

                 PlaylistItem item;
                 item.id = makeId ("it");
                 item.kind = "audio";
                 item.ref = ch.id;
                 item.start = from;
                 item.length = to - from;
                 item.offsetSeconds = roundToMicroseconds (fromSeconds - alignedFrom);
                 item.track = chosen.front().track;

                 std::erase_if (p.playlistItems, [&chosen] (const PlaylistItem& i)
                                { return std::any_of (chosen.begin(), chosen.end(), [&i] (const PlaylistItem& x) { return x.id == i.id; }); });
                 p.playlistItems.push_back (item);

                 auto o = new juce::DynamicObject();
                 o->setProperty ("item", item.id);
                 o->setProperty ("channel", ch.id);
                 o->setProperty ("path", ch.samplePath);
                 return CommandResult::success (juce::var (o));
             } });

    //==========================================================================
    // O beat em um gesto (gravar voz, fatia 6): arrastar o WAV ou o MP3 do beat para a playlist acerta o BPM do
    // projeto pelo beat (o nome do arquivo, "Trap 140bpm.wav", ou a análise), e o bloco cobre o beat inteiro.
    r.add ({ "audio.detectBpm", "BPM do áudio"_t,
             schema ({ { "item", "string", true, "Id do bloco de áudio"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 const auto& p = c.model.get();
                 const auto* item = p.findItem (params::string (a, "item"));
                 const auto* channel = item != nullptr ? p.findChannel (item->ref) : nullptr;

                 if (item == nullptr || item->kind != "audio" || channel == nullptr)
                     return CommandResult::fail ("Bloco de áudio não encontrado."_t);

                 const auto found = detectBpm (c.model.resolveSample (channel->samplePath), channel->name);

                 if (found.bpm <= 0.0)
                     return CommandResult::fail ("Não achei o andamento deste áudio (curto demais, ou sem batida clara)."_t);

                 auto o = new juce::DynamicObject();
                 o->setProperty ("bpm", found.bpm);
                 o->setProperty ("source", found.source);
                 o->setProperty ("double", found.bpm * 2.0);
                 o->setProperty ("half", found.bpm * 0.5);
                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "audio.matchBpm", "Acertar o BPM pelo áudio"_t,
             schema ({ { "item", "string", true, "Id do bloco de áudio (o beat)"_t },
                       { "bpm", "number", false, "O andamento a usar (ausente = o do áudio: o nome do arquivo ou a análise)"_t },
                       { "auto", "boolean", false, "true = só se a playlist não tiver mais nada (o beat chegando num projeto vazio); "
                                                   "senão só diz o andamento"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 const auto itemId = params::string (a, "item");
                 const auto* item = p.findItem (itemId);
                 const auto* channel = item != nullptr ? p.findChannel (item->ref) : nullptr;

                 if (item == nullptr || item->kind != "audio" || channel == nullptr)
                     return CommandResult::fail ("Bloco de áudio não encontrado."_t);

                 const auto file = c.model.resolveSample (channel->samplePath);
                 const double fileSeconds = fileSecondsOf (file);
                 Detected found { params::number (a, "bpm", 0.0), "pedido"_t };

                 if (found.bpm <= 0.0)
                     found = detectBpm (file, channel->name);

                 if (found.bpm < 20.0 || found.bpm > 999.0)
                     return CommandResult::fail ("Não achei o andamento deste áudio (curto demais, ou sem batida clara)."_t);

                 found.bpm = std::round (found.bpm * 100.0) / 100.0;
                 auto o = new juce::DynamicObject();
                 o->setProperty ("bpm", found.bpm);
                 o->setProperty ("source", found.source);

                 const bool alone = std::none_of (p.playlistItems.begin(), p.playlistItems.end(),
                                                  [&itemId] (const PlaylistItem& i) { return i.id != itemId; });

                 if (params::boolean (a, "auto", false) && (! alone || fileSeconds < 8.0))
                 {
                     o->setProperty ("applied", false);   // já tem coisa na playlist (ou é curto): só diz
                     return CommandResult::success (juce::var (o));
                 }

                 // o áudio toca no tempo dele: com o BPM novo, o bloco passa a cobrir o arquivo inteiro de novo
                 p.bpm = found.bpm;

                 if (auto* it = p.findItem (itemId); it != nullptr && fileSeconds > it->offsetSeconds)
                     it->length = juce::jlimit (ticksPerStep, maxTimelineTicks,
                                                (int) std::ceil (secondsToTicks ((fileSeconds - it->offsetSeconds) * it->stretch, p.bpm) - 1.0e-6));

                 o->setProperty ("applied", true);
                 return CommandResult::success (juce::var (o));
             } });

    //==========================================================================
    // Warp (bloco 3, 23/10; o elogio número um do Ableton): o loop entra no BPM do projeto sem mudar o tom, e os
    // compassos dele continuam os mesmos. O andamento vem do nome do arquivo ("Loop 90bpm.wav"), que vale como está,
    // ou da análise, que às vezes acha a metade ou o dobro: ali fica o mais perto do projeto.
    r.add ({ "audio.warpToProject", "Esticar o loop para o BPM do projeto"_t,
             schema ({ { "item", "string", true, "Id do bloco de áudio (o loop)"_t },
                       { "bpm", "number", false, "O andamento do loop (ausente = o do nome do arquivo ou o da análise)"_t },
                       { "auto", "boolean", false, "true = só se for um loop (até 32 s) fora do tempo do projeto; senão só diz"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto* item = p.findItem (params::string (a, "item"));
                 const auto* channel = item != nullptr ? p.findChannel (item->ref) : nullptr;

                 if (item == nullptr || item->kind != "audio" || channel == nullptr)
                     return CommandResult::fail ("Bloco de áudio não encontrado."_t);

                 const auto file = c.model.resolveSample (channel->samplePath);
                 const double fileSeconds = fileSecondsOf (file);
                 Detected found { params::number (a, "bpm", 0.0), "pedido"_t };

                 if (found.bpm <= 0.0)
                     found = detectBpm (file, channel->name);

                 if (found.bpm < 20.0 || found.bpm > 999.0)
                     return CommandResult::fail ("Não achei o andamento deste áudio (curto demais, ou sem batida clara)."_t);

                 double loopBpm = found.bpm;

                 if (found.source != "nome" && found.source != "pedido"_t)
                     for (double candidate : { found.bpm * 0.5, found.bpm * 2.0 })
                         if (candidate >= 20.0 && candidate <= 999.0
                             && std::abs (std::log (candidate / p.bpm)) < std::abs (std::log (loopBpm / p.bpm)) - 0.05)
                             loopBpm = candidate;

                 loopBpm = std::round (loopBpm * 100.0) / 100.0;
                 const double stretch = clampStretch (loopBpm / p.bpm);
                 auto o = new juce::DynamicObject();
                 o->setProperty ("bpm", loopBpm);
                 o->setProperty ("source", found.source);
                 o->setProperty ("stretch", stretch);

                 const bool offTempo = std::abs (stretch - item->stretch) > 0.005;

                 // um loop: até 32 s e um número inteiro de compassos no andamento dele (1, 2, 4, 8 ou 16, com 2% de folga)
                 const double bars = fileSeconds * loopBpm / 60.0 / juce::jmax (1, p.timeSigNum) * (juce::jmax (1, p.timeSigDen) / 4.0);
                 static constexpr int loopBars[] { 1, 2, 4, 8, 16 };
                 const bool isLoop = fileSeconds > 0.0 && fileSeconds <= 32.0
                                     && std::any_of (std::begin (loopBars), std::end (loopBars), [bars] (int n)
                                                     { return std::abs (bars - n) <= 0.02 * n; });

                 if (params::boolean (a, "auto", false) && (! isLoop || ! offTempo))
                 {
                     o->setProperty ("applied", false);   // um beat inteiro, ou já no tempo: só diz
                     return CommandResult::success (juce::var (o));
                 }

                 item->stretch = stretch;
                 item->tape = false;   // o tom fica

                 // o bloco cobre o loop inteiro no tempo novo, arredondado no passo (o loop de 2 compassos fica com 2)
                 if (fileSeconds > item->offsetSeconds)
                 {
                     const double ticks = secondsToTicks ((fileSeconds - item->offsetSeconds) * stretch, p.bpm);
                     const double steps = ticks / ticksPerStep;
                     item->length = juce::jlimit (ticksPerStep, maxTimelineTicks,
                                                  std::abs (steps - std::round (steps)) < 0.02 ? (int) std::round (steps) * ticksPerStep
                                                                                               : (int) std::ceil (ticks - 1.0e-6));
                 }

                 o->setProperty ("applied", true);
                 o->setProperty ("length", item->length);
                 return CommandResult::success (juce::var (o));
             } });

    // Capture MIDI (bloco 3, 23/10): a última frase tocada ao vivo, gravando ou não, vira notas do padrão atual.
    r.add ({ "record.captureMidi", "Capturar o que foi tocado"_t,
             schema ({ { "channel", "string", false, "Canal que recebe as notas; ausente = o canal ao vivo (o escolhido no rack)"_t },
                       { "replace", "boolean", false, "true: troca as notas do canal no padrão; padrão false: soma"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto* pat = p.currentPatternPtr();
                 const auto id = params::string (a, "channel", c.app.liveChannel());
                 const auto* ch = p.findChannel (id);

                 if (pat == nullptr)
                     return CommandResult::fail ("Padrão não encontrado."_t);

                 if (ch == nullptr || ch->isAudio())
                     return CommandResult::fail ("Escolha um canal de instrumento para receber as notas."_t);

                 bool inTime = false;
                 auto captured = c.app.capturedPhrase (p.bpm, inTime);

                 if (captured.empty())
                     return CommandResult::fail ("Nada para capturar: toque algumas notas no teclado-piano (Ctrl+T) ou no controlador primeiro."_t);

                 auto& notes = pat->notes[id];

                 if (params::boolean (a, "replace", false))
                     notes.clear();

                 int nextId = 1;

                 for (auto& n : notes)
                     nextId = juce::jmax (nextId, n.id + 1);

                 int kept = 0;

                 for (auto n : captured)
                 {
                     if (n.start >= 256 * ticksPerStep)   // o padrão vai até 16 compassos
                         continue;

                     n.id = nextId++;
                     n.length = juce::jmin (n.length, 256 * ticksPerStep - n.start);
                     notes.push_back (n);
                     ++kept;
                 }

                 std::stable_sort (notes.begin(), notes.end(), [] (const Note& x, const Note& y) { return x.start < y.start; });

                 // o padrão cresce de compasso em compasso até caber a última nota
                 int end = 0;

                 for (auto& [laneId, lane] : pat->notes)
                     for (auto& n : lane)
                         end = juce::jmax (end, n.end());

                 if (const int stepsNeeded = (end + ticksPerStep - 1) / ticksPerStep; stepsNeeded > pat->lengthSteps)
                 {
                     pat->lengthSteps = juce::jmin (256, ((stepsNeeded + 15) / 16) * 16);

                     for (auto& [laneId, steps] : pat->steps)
                         steps.resize ((size_t) pat->lengthSteps);
                 }

                 c.app.clearCapture();   // a mesma frase não entra duas vezes

                 auto o = new juce::DynamicObject();
                 o->setProperty ("notes", kept);
                 o->setProperty ("inTime", inTime);
                 o->setProperty ("channel", id);
                 return CommandResult::success (juce::var (o));
             } });

    // Groove (bloco 3, 23/10; o Groove Pool do Ableton, na medida do channel rack): o atraso dos contratempos de um
    // loop vira o swing dos canais. Mede os ataques do loop no tempo do projeto (o loop precisa estar no tempo: o
    // warp antes) e compara os das semicolcheias ímpares com a grade.
    r.add ({ "audio.extractGroove", "Pegar o swing do loop"_t,
             schema ({ { "item", "string", true, "Id do bloco de áudio (o loop, já no BPM do projeto)"_t },
                       { "channels", "array", false, "Ids dos canais que ganham o swing; ausente = só medir"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 const auto* item = p.findItem (params::string (a, "item"));
                 const auto* channel = item != nullptr ? p.findChannel (item->ref) : nullptr;

                 if (item == nullptr || item->kind != "audio" || channel == nullptr)
                     return CommandResult::fail ("Bloco de áudio não encontrado."_t);

                 // os ataques no trecho que o bloco toca, em segundos do arquivo, levados para ticks do bloco
                 const double blockSeconds = ticksToSeconds (item->length, p.bpm);
                 const auto onsets = onsetsIn (c.model.resolveSample (channel->samplePath), item->offsetSeconds,
                                               item->offsetSeconds + item->fileSeconds (blockSeconds), audio::Sensitivity::medium);
                 const double third = ticksPerStep / 3.0;   // 100% de swing = o contratempo atrasado um terço de passo
                 double sum = 0.0, onSum = 0.0;
                 int offbeats = 0, onbeats = 0;

                 for (double at : onsets)
                 {
                     const double tick = secondsToTicks ((at - item->offsetSeconds) * item->stretch, p.bpm);
                     const double step = tick / ticksPerStep;
                     double fromEven = (step - 2.0 * std::floor (step / 2.0)) * ticksPerStep;   // de 0 a 2 passos depois do par

                     if (fromEven > ticksPerStep * 1.75)
                         fromEven -= 2.0 * ticksPerStep;   // logo antes do par seguinte: um ataque no tempo, adiantado

                     // perto de um passo par: no tempo; entre o ímpar e o meio caminho do par seguinte: um contratempo atrasado
                     if (std::abs (fromEven) < ticksPerStep * 0.25)
                     {
                         onSum += fromEven;   // o atraso do próprio detector, igual nos dois: sai da conta
                         ++onbeats;
                     }
                     else if (fromEven >= ticksPerStep * 0.75 && fromEven <= ticksPerStep * 1.5)
                     {
                         sum += fromEven - ticksPerStep;
                         ++offbeats;
                     }
                 }

                 if (offbeats < 2 || onbeats < 2)
                     return CommandResult::fail ("O loop não tem contratempos claros para medir o swing (use um loop de bateria no tempo do projeto)."_t);

                 const double delay = sum / offbeats - onSum / onbeats;
                 const float swing = (float) juce::jlimit (0.0, 100.0, std::round (juce::jmax (0.0, delay) / third * 100.0));
                 juce::Array<juce::var> applied;

                 if (auto* list = a["channels"].getArray())
                 {
                     for (auto& id : *list)
                     {
                         if (auto* target = p.findChannel (id.toString()); target != nullptr && ! target->isAudio())
                         {
                             target->swing = swing;
                             applied.add (target->id);
                         }
                     }
                 }

                 auto o = new juce::DynamicObject();
                 o->setProperty ("swing", swing);
                 o->setProperty ("offbeats", offbeats);
                 o->setProperty ("channels", applied);
                 return CommandResult::success (juce::var (o));
             } });

    //==========================================================================
    // Takes e comping (gravar voz, 21/10): o grupo de takes de um trecho, cortar todos juntos e escolher
    // o take de cada pedaço. O crossfade no corte é do motor (takes::edges).
    r.add ({ "takes.choose", "Escolher take"_t,
             schema ({ { "item", "string", true, "Id do pedaço de um take (bloco de um grupo de takes): ele toca, e os outros takes calam onde ele está"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 const auto* item = p.findItem (params::string (a, "item"));

                 if (item == nullptr || item->takeGroup.isEmpty())
                     return CommandResult::fail ("Esse bloco não é de um grupo de takes."_t);

                 const auto group = item->takeGroup;
                 takes::choose (p, item->id);
                 return CommandResult::success (takes::describe (p, group));
             } });

    r.add ({ "takes.cut", "Cortar takes"_t,
             schema ({ { "group", "string", true, "Id do grupo de takes (takeGroup de um bloco)"_t },
                       { "at", "number", true, "Onde cortar todos os takes, na linha do tempo"_t },
                       { "unit", "string", false, "Unidade de at: ticks (padrão), steps, beats ou bars"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 const auto group = params::string (a, "group");

                 if (takes::itemsOf (p, group).empty())
                     return CommandResult::fail ("Grupo de takes não encontrado."_t);

                 if (takes::cut (p, group, toTicks (a, params::number (a, "at", 0))) == 0)
                     return CommandResult::fail ("O corte precisa cair dentro dos takes, a pelo menos um passo das pontas dos pedaços."_t);

                 return CommandResult::success (takes::describe (p, group));
             } });

    r.add ({ "takes.list", "Takes de um grupo"_t,
             schema ({ { "group", "string", false, "Id do grupo; ausente = todos os grupos do projeto"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 const auto& p = c.model.get();

                 if (params::has (a, "group"))
                 {
                     const auto group = params::string (a, "group");
                     return takes::itemsOf (p, group).empty() ? CommandResult::fail ("Grupo de takes não encontrado."_t)
                                                              : CommandResult::success (takes::describe (p, group));
                 }

                 juce::StringArray groups;

                 for (auto& i : p.playlistItems)
                     if (i.takeGroup.isNotEmpty())
                         groups.addIfNotAlreadyThere (i.takeGroup);

                 juce::Array<juce::var> list;

                 for (auto& g : groups)
                     list.add (takes::describe (p, g));

                 auto o = new juce::DynamicObject();
                 o->setProperty ("groups", list);
                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "transport.record", "Gravar"_t,
             schema ({ { "on", "boolean", false, "true = liga o REC (o próximo play grava; tocando, grava a partir de agora); false = desliga e guarda o take; ausente = alterna"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* rec = c.app.recorder();

                 if (rec == nullptr)
                     return needRecorder();

                 const bool on = params::boolean (a, "on", ! rec->isRecordMode());
                 rec->setRecordMode (on);

                 // REC sem nada armado grava só as notas do canal escolhido (como no FL): avisa na hora, e não no fim
                 const auto& p = c.model.get();
                 const bool nothingArmed = std::none_of (p.mixer.begin(), p.mixer.end(), [] (const MixerTrack& m) { return m.armed && ! m.isMaster(); })
                                           && std::none_of (p.channels.begin(), p.channels.end(), [] (const Channel& ch) { return ch.armed && ! ch.isAudio(); });

                 if (on && c.app.isPlaying() && ! rec->isRecording())
                 {
                     if (const auto started = c.app.startRecordingNow(); started.failed())
                         return CommandResult::fail (started.getErrorMessage());
                 }
                 else if (! on && rec->isRecording())
                 {
                     c.app.finishRecording();
                 }

                 if (on && nothingArmed)
                     rec->setMessage ("Nada armado: o REC vai gravar só as notas do canal escolhido. Para gravar voz, clique em Gravar voz "
                                      "(na playlist) ou arme uma trilha (o ponto no mixer)."_t);

                 return CommandResult::success (rec->describe (c.model.get()));
             } });

    r.add ({ "record.note", "Nota gravada com tempo"_t,
             schema ({ { "key", "integer", true, "Nota MIDI (60 = Dó central)"_t },
                       { "on", "boolean", false, "true = aperta (padrão), false = solta"_t },
                       { "velocity", "integer", false, "Força 1-127 (padrão 100)"_t },
                       { "channel", "string", false, "Id do canal; padrão = o canal ao vivo"_t },
                       { "at", "number", false, "Quando, em segundos no relógio da placa (padrão = agora). Só entra no take; não toca o som (para tocar, live.note)"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* rec = c.app.recorder();

                 if (rec == nullptr)
                     return needRecorder();

                 const int key = (int) params::number (a, "key", -1);

                 if (! juce::isPositiveAndBelow (key, 128))
                     return CommandResult::fail ("A nota precisa estar entre 0 e 127."_t);

                 const auto channel = params::string (a, "channel", c.app.liveChannel());
                 const bool taken = rec->noteEvent (channel, key, juce::jlimit (1, 127, (int) params::number (a, "velocity", 100)),
                                                    params::boolean (a, "on", true), params::number (a, "at", -1.0));

                 return taken ? CommandResult::success()
                              : CommandResult::fail ("Esse canal não está gravando agora."_t);
             } });

    r.add ({ "record.commitTake", "Gravação"_t,
             schema ({ { "audio", "array", false, "Takes de áudio: { track, path, name, start, length, offsetSeconds, passes?, song? } (ticks; corte em segundos "
                                                  "do arquivo; passes = [{ start, length, offsetSeconds }]: em loop, cada volta vira um take do mesmo grupo; "
                                                  "song = { pattern, length }: o padrão vai junto para a playlist, do zero, e o modo passa para SONG). "
                                                  "Gravado no mesmo lugar pela mesma trilha do mixer, o take entra no grupo de takes de lá, e o mais novo toca"_t },
                       { "midi", "array", false, "Takes MIDI: { channel, pattern, notes: [{ t, len, key, vel }] } (ticks do padrão)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 juce::Array<juce::var> channels, items;
                 juce::StringArray groups;
                 int noteCount = 0;

                 if (auto* list = a["audio"].getArray())
                 {
                     for (auto& t : *list)
                     {
                         const juce::File file (t["path"].toString());

                         if (! file.existsAsFile())
                             return CommandResult::fail ("Arquivo do take não encontrado: "_t + file.getFullPathName());

                         const auto* track = p.findMixer (t["track"].toString());
                         const int start = tickField (t, "start", 0);
                         const int length = juce::jmax (ticksPerStep, tickField (t, "length", ticksPerBar4));

                         // Gravou no PAT com a playlist vazia: o padrão que tocava vai para a playlist,
                         // do zero até cobrir o take, e o modo passa para SONG. O take se ouve com ele.
                         if (const auto& song = t["song"]; song.isObject())
                         {
                             if (const auto* pattern = p.findPattern (song["pattern"].toString()))
                             {
                                 PlaylistItem block;
                                 block.id = makeId ("it");
                                 block.kind = "pattern";
                                 block.ref = pattern->id;
                                 block.start = 0;
                                 block.length = juce::jlimit (pattern->lengthTicks(), maxTimelineTicks, tickField (song, "length", pattern->lengthTicks()));
                                 block.track = freeLane (p, block.start, block.end());
                                 p.playlistItems.push_back (block);
                                 p.songMode = true;
                                 items.add (block.id);
                             }
                         }

                         // o canal do take: um arquivo, saindo pela trilha armada (sem trilha nova no mixer)
                         Channel ch;
                         ch.id = makeId ("ch");
                         ch.kind = "audio";
                         ch.name = t["name"].toString().isNotEmpty() ? t["name"].toString() : file.getFileNameWithoutExtension();
                         ch.colour = track != nullptr ? track->colour : channelColourForIndex ((int) p.channels.size());
                         ch.samplePath = file.getFullPathName().replaceCharacter ('\\', '/');
                         ch.output = track != nullptr ? track->id : masterTrackId;
                         p.channels.push_back (ch);

                         // um bloco por volta do loop (comping), ou o take inteiro
                         juce::Array<juce::var> places;

                         if (auto* passes = t["passes"].getArray(); passes != nullptr && ! passes->isEmpty())
                             places = *passes;
                         else
                             places.add (t);

                         juce::StringArray takeItems;
                         juce::String lane;

                         for (auto& place : places)
                         {
                             PlaylistItem item;
                             item.id = makeId ("it");
                             item.kind = "audio";
                             item.ref = ch.id;
                             item.start = place.getDynamicObject() == t.getDynamicObject() ? start : tickField (place, "start", start);
                             item.length = place.getDynamicObject() == t.getDynamicObject() ? length
                                                                                             : juce::jmax (ticksPerStep, tickField (place, "length", length));
                             item.offsetSeconds = roundToMicroseconds (juce::jlimit (0.0, 24.0 * 3600.0, field (place, "offsetSeconds", 0.0)));
                             // a raia que grava por esta trilha (a entrada escolhida no cabeçalho), senão a primeira livre
                             if (lane.isEmpty() && track != nullptr)
                                 for (auto& laneTrack : p.playlistTracks)
                                     if (laneTrack.recordTrack == track->id)
                                         lane = laneTrack.id;

                             item.track = lane.isNotEmpty() ? lane : freeLane (p, item.start, item.end());
                             lane = item.track;
                             p.playlistItems.push_back (item);
                             takeItems.add (item.id);
                             items.add (item.id);
                         }

                         // takes e comping: no mesmo lugar e pela mesma trilha, o take entra no grupo de lá
                         if (const auto group = takes::adopt (p, takeItems); group.isNotEmpty())
                             groups.addIfNotAlreadyThere (group);

                         channels.add (ch.id);
                     }
                 }

                 if (auto* list = a["midi"].getArray())
                 {
                     for (auto& t : *list)
                     {
                         const auto channelId = t["channel"].toString();
                         const auto* ch = p.findChannel (channelId);
                         auto* pattern = p.findPattern (t["pattern"].toString());

                         if (pattern == nullptr)
                             pattern = p.currentPatternPtr();

                         if (ch == nullptr || ch->isAudio() || pattern == nullptr)
                             return CommandResult::fail ("Canal ou padrão do take MIDI não encontrado."_t);

                         auto& notes = pattern->notes[channelId];

                         if (auto* recorded = t["notes"].getArray())
                         {
                             for (auto& no : *recorded)
                             {
                                 Note n;
                                 n.id = nextNoteId (notes);
                                 n.start = juce::jlimit (0, pattern->lengthTicks() - 1, tickField (no, "t", 0));
                                 n.length = juce::jlimit (1, pattern->lengthTicks(), tickField (no, "len", ticksPerStep));
                                 n.key = (int) juce::jlimit (0.0, 127.0, field (no, "key", 60));
                                 n.velocity = (int) juce::jlimit (1.0, 127.0, field (no, "vel", 100));
                                 notes.push_back (n);
                                 ++noteCount;
                             }
                         }
                     }
                 }

                 if (channels.isEmpty() && noteCount == 0)
                     return CommandResult::fail ("O take está vazio."_t);

                 auto o = new juce::DynamicObject();
                 o->setProperty ("channels", channels);
                 o->setProperty ("items", items);
                 o->setProperty ("notes", noteCount);
                 o->setProperty ("groups", juce::Array<juce::var> (groups.begin(), groups.size()));
                 return CommandResult::success (juce::var (o));
             } });
}

} // namespace whynot
