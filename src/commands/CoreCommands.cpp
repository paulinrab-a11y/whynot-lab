#include "CommandRegistry.h"
#include "../model/WnlFormat.h"
#include "../engine/SampleAnalysis.h"

namespace whynot
{

namespace
{
    using params::schema;

    // Resolve o padrão alvo: o informado, ou o atual.
    Pattern* targetPattern (Project& p, const juce::var& args)
    {
        const auto id = params::string (args, "pattern");
        return id.isEmpty() ? p.currentPatternPtr() : p.findPattern (id);
    }

    int clampRoll (int roll)
    {
        int best = 1;

        for (int allowed : { 1, 2, 3, 4, 6, 8 })
            if (roll >= allowed)
                best = allowed;

        return best;
    }

    /** true se rotear `from` para `to` criaria um laço (to já chega em from, por saída,
        envio ou sidechain). */
    bool wouldLoop (const Project& p, const juce::String& from, const juce::String& to)
    {
        return to == from || p.reaches (to, from);
    }

    juce::var idResult (const char* key, const juce::String& id)
    {
        auto o = new juce::DynamicObject();
        o->setProperty (key, id);
        return juce::var (o);
    }

    void insertBeforeMaster (Project& p, MixerTrack t)
    {
        auto it = std::find_if (p.mixer.begin(), p.mixer.end(), [] (const MixerTrack& m) { return m.isMaster(); });
        p.mixer.insert (it, std::move (t));
    }
}

void registerCoreCommands (CommandRegistry& r)
{
    //==========================================================================
    // Transporte
    r.add ({ "transport.play", "Tocar", schema ({}), false,
             [] (CommandContext& c, const juce::var&) { c.app.play(); return CommandResult::success(); } });

    r.add ({ "transport.stop", "Parar", schema ({}), false,
             [] (CommandContext& c, const juce::var&) { c.app.stop(); return CommandResult::success(); } });

    r.add ({ "transport.togglePlay", "Tocar ou parar", schema ({}), false,
             [] (CommandContext& c, const juce::var&)
             {
                 if (c.app.isPlaying()) c.app.stop(); else c.app.play();
                 return CommandResult::success();
             } });

    r.add ({ "transport.togglePause", "Tocar ou pausar", schema ({}), false,
             [] (CommandContext& c, const juce::var&) { c.app.togglePause(); return CommandResult::success(); } });

    r.add ({ "transport.setBpm", "Mudar BPM",
             schema ({ { "bpm", "number", true, "Andamento entre 20 e 999" } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 const double bpm = params::number (a, "bpm", 0.0);

                 if (bpm < 20.0 || bpm > 999.0)
                     return CommandResult::fail ("BPM precisa estar entre 20 e 999.");

                 c.model.data().bpm = bpm;
                 return CommandResult::success();
             } });

    r.add ({ "transport.setTimeSignature", "Mudar compasso",
             schema ({ { "numerator", "integer", true, "Tempos por compasso (1 a 32)" },
                       { "denominator", "integer", true, "Figura do tempo: 2, 4, 8 ou 16" } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 const int num = (int) params::number (a, "numerator", 0);
                 const int den = (int) params::number (a, "denominator", 0);

                 if (num < 1 || num > 32 || (den != 2 && den != 4 && den != 8 && den != 16))
                     return CommandResult::fail ("Compasso inválido. Use numerador de 1 a 32 e denominador 2, 4, 8 ou 16."_t);

                 c.model.data().timeSigNum = num;
                 c.model.data().timeSigDen = den;
                 return CommandResult::success();
             } });

    r.add ({ "transport.setMetronome", "Metrônomo"_t,
             schema ({ { "on", "boolean", true, "Liga ou desliga o metrônomo"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 c.model.data().metronome = params::boolean (a, "on", ! c.model.get().metronome);
                 return CommandResult::success();
             } });

    r.add ({ "transport.setLoop", "Loop",
             schema ({ { "on", "boolean", true, "Liga ou desliga o loop do padrão"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 c.model.data().loop = params::boolean (a, "on", ! c.model.get().loop);
                 return CommandResult::success();
             } });

    //==========================================================================
    // Projeto e arquivos
    r.add ({ "project.new", "Novo projeto", schema ({}), false,
             [] (CommandContext& c, const juce::var&)
             {
                 auto res = c.app.newProject();
                 return res.wasOk() ? CommandResult::success() : CommandResult::fail (res.getErrorMessage());
             } });

    r.add ({ "project.open", "Abrir projeto",
             schema ({ { "path", "string", true, "Caminho do arquivo .wnl" } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto res = c.app.openProject (juce::File (params::string (a, "path")));
                 return res.wasOk() ? CommandResult::success() : CommandResult::fail (res.getErrorMessage());
             } });

    r.add ({ "project.save", "Salvar projeto",
             schema ({ { "path", "string", false, "Caminho do .wnl; vazio = arquivo atual" } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 const auto path = params::string (a, "path");
                 auto res = c.app.saveProject (path.isEmpty() ? juce::File() : juce::File (path));
                 return res.wasOk() ? CommandResult::success() : CommandResult::fail (res.getErrorMessage());
             } });

    r.add ({ "project.exportWav", "Exportar WAV",
             schema ({ { "path", "string", true, "Arquivo .wav de saída"_t },
                       { "loops", "integer", false, "Padrão: quantas vezes repetir (padrão 4)"_t },
                       { "what", "string", false, "pattern (padrão atual) ou song (playlist inteira); padrão = o modo PAT/SONG atual"_t },
                       { "background", "boolean", false, "true = começa e volta na hora; o andamento sai no export.status"_t },
                       { "from", "number", false, "Só um trecho da música (SONG): início em ticks; precisa de background"_t },
                       { "to", "number", false, "Fim do trecho, em ticks"_t },
                       { "sampleRate", "number", false, "44100, 48000 (padrão), 88200 ou 96000; precisa de background"_t },
                       { "bitDepth", "integer", false, "16 (com dither), 24 (padrão) ou 32 (float); precisa de background"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 const int loops = juce::jlimit (1, 64, (int) params::number (a, "loops", 4));
                 const auto what = params::string (a, "what", c.model.get().songMode ? "song" : "pattern");
                 const double rate = params::number (a, "sampleRate", 48000.0);
                 const int bits = (int) params::number (a, "bitDepth", 24);

                 if (rate != 44100.0 && rate != 48000.0 && rate != 88200.0 && rate != 96000.0)
                     return CommandResult::fail ("Taxa de amostragem inválida: use 44100, 48000, 88200 ou 96000."_t);

                 if (bits != 16 && bits != 24 && bits != 32)
                     return CommandResult::fail ("Bits inválidos: use 16, 24 ou 32."_t);

                 if (params::boolean (a, "background", false))
                 {
                     AppServices::BackgroundExport request;
                     request.kind = "wav";
                     request.sampleRate = rate;
                     request.bitDepth = bits;
                     request.target = juce::File (params::string (a, "path")).withFileExtension ("wav");
                     request.loops = loops;
                     request.song = what == "song";
                     request.fromTicks = juce::jmax (0, (int) params::number (a, "from", 0));
                     request.toTicks = juce::jmax (0, (int) params::number (a, "to", 0));
                     const auto r = c.app.startBackgroundExport (request);
                     return r.wasOk() ? CommandResult::success (c.app.backgroundExportStatus()) : CommandResult::fail (r.getErrorMessage());
                 }

                 if (params::has (a, "from") || params::has (a, "to"))
                     return CommandResult::fail ("Exportar um trecho precisa de \"background\": true."_t);

                 auto res = c.app.exportWav (juce::File (params::string (a, "path")), loops, what == "song");
                 return res.wasOk() ? CommandResult::success() : CommandResult::fail (res.getErrorMessage());
             } });

    r.add ({ "project.rename", "Renomear projeto",
             schema ({ { "name", "string", true, "Nome novo" } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 const auto name = params::string (a, "name").trim();

                 if (name.isEmpty())
                     return CommandResult::fail ("O nome não pode ficar vazio."_t);

                 c.model.data().name = name;
                 return CommandResult::success();
             } });

    //==========================================================================
    // Canais
    r.add ({ "channel.addSampler", "Adicionar canal",
             schema ({ { "path", "string", true, "Arquivo de áudio (WAV, AIFF, FLAC, OGG, MP3)"_t },
                       { "name", "string", false, "Nome do canal; padrão = nome do arquivo"_t },
                       { "rootKey", "integer", false, "Nota em que o sample soa no tom original; ausente = detectada no arquivo (ou 60)"_t },
                       { "detectRoot", "boolean", false, "Achar a nota do sample sozinho quando rootKey não vem (padrão true)"_t },
                       { "oneShot", "boolean", false, "Toca o sample inteiro (padrão true; false no 808)"_t },
                       { "cutItself", "boolean", false, "Nota nova corta a anterior do mesmo canal (padrão false)"_t },
                       { "mono", "boolean", false, "Uma nota por vez; notas com slide deslizam (padrão false; true no 808)"_t },
                       { "detectRole", "boolean", false, "Pelo nome e pelo som, o 808 e o baixo nascem mono, com glide e respeitando a duração da nota (padrão true)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 const juce::File file (params::string (a, "path"));

                 if (! file.existsAsFile())
                     return CommandResult::fail ("Arquivo não encontrado: "_t + file.getFullPathName());

                 auto& p = c.model.data();
                 Channel ch;
                 ch.id = makeId ("ch");
                 ch.name = params::string (a, "name", file.getFileNameWithoutExtension()).trim();
                 ch.colour = channelColourForIndex ((int) p.channels.size());
                 ch.samplePath = file.getFullPathName().replaceCharacter ('\\', '/');
                 ch.rootKey = juce::jlimit (0, 127, (int) params::number (a, "rootKey", 60));
                 bool rootDetected = false;

                 // Uma leitura só do arquivo serve à nota e ao papel (808 ou não).
                 const bool wantRoot = ! params::has (a, "rootKey") && params::boolean (a, "detectRoot", true);
                 const bool wantRole = params::boolean (a, "detectRole", true);
                 const auto profile = wantRoot || wantRole ? analyseSample (file) : SampleProfile {};

                 if (wantRoot && rootIsReliable (profile.pitch))
                 {
                     ch.rootKey = profile.pitch.key;
                     ch.rootCents = (float) profile.pitch.cents;
                     rootDetected = true;
                 }

                 // O 808 de um kit nasce como o nativo: mono, glide e a nota inteira manda.
                 const auto role = wantRole ? guessSampleRole (file, profile, params::string (a, "name")) : SampleRole::unknown;
                 const bool bass = role == SampleRole::bass;
                 ch.oneShot = params::boolean (a, "oneShot", ! bass);
                 ch.cutItself = params::boolean (a, "cutItself", false);
                 ch.mono = params::boolean (a, "mono", bass);

                 // Bateria sem a rampa de 1 ms de fábrica: o clique do kick é o ataque dele (revisão de 22/10).
                 if (isDrumRole (role))
                     ch.attackMs = 0.0f;

                 // Regra de roteamento: todo canal novo ganha a própria trilha de mixer.
                 MixerTrack mx;
                 mx.id = makeId ("mx");
                 mx.name = ch.name;
                 mx.colour = ch.colour;
                 mx.autoFor = ch.id;
                 mx.volumeDb = -6.0f;   // headroom: kick e 808 juntos não clipam o master
                 ch.output = mx.id;

                 p.channels.push_back (ch);
                 insertBeforeMaster (p, mx);

                 auto o = new juce::DynamicObject();
                 o->setProperty ("channel", ch.id);
                 o->setProperty ("mixerTrack", mx.id);
                 o->setProperty ("rootKey", ch.rootKey);
                 o->setProperty ("rootCents", ch.rootCents);
                 o->setProperty ("rootDetected", rootDetected);
                 o->setProperty ("role", juce::String (roleId (role)));
                 o->setProperty ("mono", ch.mono);
                 o->setProperty ("oneShot", ch.oneShot);
                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "channel.remove", "Remover canal",
             schema ({ { "channel", "string", true, "Id do canal" } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 const auto id = params::string (a, "channel");
                 auto* ch = p.findChannel (id);

                 if (ch == nullptr)
                     return CommandResult::fail ("Canal não encontrado: "_t + id);

                 const auto output = ch->output;
                 std::erase_if (p.channels, [&] (const Channel& x) { return x.id == id; });

                 // A trilha criada para este canal sai junto, se mais nada mandar som para ela.
                 if (auto* mx = p.findMixer (output); mx != nullptr && mx->autoFor == id)
                 {
                     const bool usedByChannel = std::any_of (p.channels.begin(), p.channels.end(), [&] (auto& x) { return x.output == output; });
                     const bool usedByMixer = std::any_of (p.mixer.begin(), p.mixer.end(), [&] (auto& m) { return m.output == output; });

                     if (! usedByChannel && ! usedByMixer)
                         std::erase_if (p.mixer, [&] (const MixerTrack& m) { return m.id == output; });
                 }

                 return CommandResult::success();
             } });

    r.add ({ "channel.rename", "Renomear canal",
             schema ({ { "channel", "string", true, "Id do canal" }, { "name", "string", true, "Nome novo" } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto* ch = p.findChannel (params::string (a, "channel"));
                 const auto name = params::string (a, "name").trim();

                 if (ch == nullptr)
                     return CommandResult::fail ("Canal não encontrado."_t);

                 if (name.isEmpty())
                     return CommandResult::fail ("O nome não pode ficar vazio."_t);

                 // A trilha automática acompanha o nome do canal.
                 if (auto* mx = p.findMixer (ch->output); mx != nullptr && mx->autoFor == ch->id && mx->name == ch->name)
                     mx->name = name;

                 ch->name = name;
                 return CommandResult::success();
             } });

    r.add ({ "channel.setSample", "Trocar sample",
             schema ({ { "channel", "string", true, "Id do canal" }, { "path", "string", true, "Arquivo de áudio"_t },
                       { "detectRoot", "boolean", false, "Achar a nota do sample novo sozinho (padrão true)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* ch = c.model.data().findChannel (params::string (a, "channel"));
                 const juce::File file (params::string (a, "path"));

                 if (ch == nullptr)
                     return CommandResult::fail ("Canal não encontrado."_t);

                 if (! file.existsAsFile())
                     return CommandResult::fail ("Arquivo não encontrado: "_t + file.getFullPathName());

                 ch->samplePath = file.getFullPathName().replaceCharacter ('\\', '/');
                 ch->originalPath = {};
                 ch->instrument = {};   // um sample no canal do WHYNOT Piano: ele volta a ser sampler
                 ch->tunedNotes.clear();   // afinação nota a nota (27/10): as notas eram do áudio antigo
                 ch->tuneFormantFollows = false;
                 ch->instrumentPreset = {};

                 // O desvio em cents era do sample antigo. Sem nota no novo (bateria), a raiz fica.
                 ch->rootCents = 0.0f;
                 bool rootDetected = false;

                 if (params::boolean (a, "detectRoot", true))
                 {
                     if (const auto e = detectRootOf (file); rootIsReliable (e))
                     {
                         ch->rootKey = e.key;
                         ch->rootCents = (float) e.cents;
                         rootDetected = true;
                     }
                 }

                 auto o = new juce::DynamicObject();
                 o->setProperty ("rootKey", ch->rootKey);
                 o->setProperty ("rootCents", ch->rootCents);
                 o->setProperty ("rootDetected", rootDetected);
                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "channel.setMute", "Silenciar canal",
             schema ({ { "channel", "string", true, "Id do canal" }, { "on", "boolean", false, "true = mudo; ausente = alterna" } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* ch = c.model.data().findChannel (params::string (a, "channel"));

                 if (ch == nullptr)
                     return CommandResult::fail ("Canal não encontrado."_t);

                 ch->mute = params::boolean (a, "on", ! ch->mute);
                 return CommandResult::success();
             } });

    r.add ({ "channel.setCutItself", "Cortar a si mesmo",
             schema ({ { "channel", "string", true, "Id do canal" }, { "on", "boolean", false, "true = nota nova corta a anterior; ausente = alterna" } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* ch = c.model.data().findChannel (params::string (a, "channel"));

                 if (ch == nullptr)
                     return CommandResult::fail ("Canal não encontrado."_t);

                 ch->cutItself = params::boolean (a, "on", ! ch->cutItself);
                 return CommandResult::success();
             } });

    r.add ({ "channel.setOutput", "Mudar saída do canal"_t,
             schema ({ { "channel", "string", true, "Id do canal" }, { "track", "string", true, "Id da trilha de mixer" } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto* ch = p.findChannel (params::string (a, "channel"));
                 const auto track = params::string (a, "track");

                 if (ch == nullptr)
                     return CommandResult::fail ("Canal não encontrado."_t);

                 if (p.findMixer (track) == nullptr)
                     return CommandResult::fail ("Trilha de mixer não encontrada: "_t + track);

                 ch->output = track;
                 return CommandResult::success();
             } });

    //==========================================================================
    // Padrões
    r.add ({ "pattern.add", "Novo padrão"_t,
             schema ({ { "name", "string", false, "Nome do padrão"_t }, { "steps", "integer", false, "Número de passos (padrão 16)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 Pattern pat;
                 pat.id = makeId ("pt");
                 pat.name = params::string (a, "name", "Padrão "_t + juce::String ((int) p.patterns.size() + 1));
                 pat.lengthSteps = juce::jlimit (1, 256, (int) params::number (a, "steps", 16));
                 p.patterns.push_back (pat);
                 p.currentPattern = pat.id;
                 return CommandResult::success (idResult ("pattern", pat.id));
             } });

    // Fluidez (21/10): o beat de 1 minuto vive de copiar o padrão e mudar um detalhe (a virada, o refrão)
    r.add ({ "pattern.clone", "Clonar padrão"_t,
             schema ({ { "pattern", "string", false, "Id do padrão; padrão = atual"_t },
                       { "name", "string", false, "Nome do clone; padrão = o nome com \"(cópia)\""_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 const auto* source = targetPattern (p, a);

                 if (source == nullptr)
                     return CommandResult::fail ("Padrão não encontrado."_t);

                 Pattern copy = *source;   // passos, notas e tamanho
                 copy.id = makeId ("pt");
                 copy.name = params::string (a, "name", source->name + " (cópia)"_t);

                 const auto at = std::find_if (p.patterns.begin(), p.patterns.end(), [&] (const Pattern& q) { return q.id == source->id; });
                 p.patterns.insert (at == p.patterns.end() ? p.patterns.end() : std::next (at), copy);   // logo depois do original
                 p.currentPattern = copy.id;
                 return CommandResult::success (idResult ("pattern", copy.id));
             } });

    r.add ({ "pattern.select", "Escolher padrão"_t,
             schema ({ { "pattern", "string", true, "Id do padrão"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 const auto id = params::string (a, "pattern");

                 if (p.findPattern (id) == nullptr)
                     return CommandResult::fail ("Padrão não encontrado: "_t + id);

                 p.currentPattern = id;
                 return CommandResult::success();
             } });

    r.add ({ "pattern.rename", "Renomear padrão"_t,
             schema ({ { "pattern", "string", false, "Id do padrão; padrão = atual"_t }, { "name", "string", true, "Nome novo" } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* pat = targetPattern (c.model.data(), a);
                 const auto name = params::string (a, "name").trim();

                 if (pat == nullptr)
                     return CommandResult::fail ("Padrão não encontrado."_t);

                 if (name.isEmpty())
                     return CommandResult::fail ("O nome não pode ficar vazio."_t);

                 pat->name = name;
                 return CommandResult::success();
             } });

    r.add ({ "pattern.setLength", "Tamanho do padrão"_t,
             schema ({ { "pattern", "string", false, "Id do padrão; padrão = atual"_t }, { "steps", "integer", true, "Passos: 16 = 1 compasso em 4/4" } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* pat = targetPattern (c.model.data(), a);
                 const int steps = (int) params::number (a, "steps", 0);

                 if (pat == nullptr)
                     return CommandResult::fail ("Padrão não encontrado."_t);

                 if (steps < 1 || steps > 256)
                     return CommandResult::fail ("O padrão precisa ter de 1 a 256 passos."_t);

                 pat->lengthSteps = steps;   // normalise() ajusta o tamanho das raias
                 return CommandResult::success();
             } });

    //==========================================================================
    // Passos
    r.add ({ "steps.set", "Editar passo",
             schema ({ { "channel", "string", true, "Id do canal" },
                       { "index", "integer", true, "Passo, a partir de 0" },
                       { "on", "boolean", false, "Liga ou desliga; ausente = mantém"_t },
                       { "velocity", "integer", false, "1 a 127" },
                       { "roll", "integer", false, "Repetições dentro do passo: 1, 2, 3, 4, 6 ou 8"_t },
                       { "chance", "integer", false, "Chance de tocar em cada volta, 1 a 100 (%)"_t },
                       { "pitch", "integer", false, "Tom do passo em semitons, -24 a 24, sobre a nota do canal"_t },
                       { "pan", "integer", false, "Pan do passo, -100 (esquerda) a 100 (direita); só em canal de sample"_t },
                       { "pattern", "string", false, "Id do padrão; padrão = atual"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto* pat = targetPattern (p, a);
                 const auto channel = params::string (a, "channel");
                 const int index = (int) params::number (a, "index", -1);

                 if (pat == nullptr)
                     return CommandResult::fail ("Padrão não encontrado."_t);

                 if (p.findChannel (channel) == nullptr)
                     return CommandResult::fail ("Canal não encontrado: "_t + channel);

                 if (index < 0 || index >= pat->lengthSteps)
                     return CommandResult::fail ("Passo fora do padrão: "_t + juce::String (index));

                 if (params::has (a, "pan") && p.findChannel (channel)->kind != "sampler")
                     return CommandResult::fail ("O pan do passo vale só nos canais de sample (bateria e one-shots)."_t);

                 auto& steps = pat->steps[channel];
                 steps.resize ((size_t) pat->lengthSteps);
                 auto& s = steps[(size_t) index];

                 if (params::has (a, "on"))        s.on = params::boolean (a, "on", s.on);
                 if (params::has (a, "velocity"))  s.velocity = juce::jlimit (1, 127, (int) params::number (a, "velocity", s.velocity));
                 if (params::has (a, "roll"))      s.roll = clampRoll ((int) params::number (a, "roll", s.roll));
                 if (params::has (a, "chance"))    s.chance = juce::jlimit (1, 100, (int) params::number (a, "chance", s.chance));
                 if (params::has (a, "pitch"))     s.pitch = juce::jlimit (-24, 24, (int) params::number (a, "pitch", s.pitch));
                 if (params::has (a, "pan"))       s.pan = juce::jlimit (-100, 100, (int) params::number (a, "pan", s.pan));

                 return CommandResult::success();
             } });

    r.add ({ "steps.setGrid", "Escrever passos",
             schema ({ { "channel", "string", true, "Id do canal" },
                       { "grid", "string", true, "Um caractere por passo: x = ligado, . = desligado, 2/3/4/6/8 = ligado com roll" },
                       { "velocity", "integer", false, "Velocity dos passos ligados (padrão 100)"_t },
                       { "pattern", "string", false, "Id do padrão; padrão = atual"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto* pat = targetPattern (p, a);
                 const auto channel = params::string (a, "channel");
                 const auto grid = params::string (a, "grid").removeCharacters (" |");
                 const int velocity = juce::jlimit (1, 127, (int) params::number (a, "velocity", 100));

                 if (pat == nullptr)
                     return CommandResult::fail ("Padrão não encontrado."_t);

                 if (p.findChannel (channel) == nullptr)
                     return CommandResult::fail ("Canal não encontrado: "_t + channel);

                 auto& steps = pat->steps[channel];
                 steps.assign ((size_t) pat->lengthSteps, Step{});

                 for (int i = 0; i < juce::jmin (grid.length(), pat->lengthSteps); ++i)
                 {
                     const auto ch = grid[i];
                     auto& s = steps[(size_t) i];

                     if (ch == 'x' || ch == 'X')
                     {
                         s.on = true;
                         s.velocity = ch == 'X' ? 127 : velocity;
                     }
                     else if (juce::CharacterFunctions::isDigit (ch))
                     {
                         s.on = true;
                         s.velocity = velocity;
                         s.roll = clampRoll (ch - '0');
                     }
                 }

                 return CommandResult::success();
             } });

    r.add ({ "steps.clear", "Limpar passos",
             schema ({ { "channel", "string", false, "Id do canal; ausente = todos" }, { "pattern", "string", false, "Id do padrão; padrão = atual"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* pat = targetPattern (c.model.data(), a);

                 if (pat == nullptr)
                     return CommandResult::fail ("Padrão não encontrado."_t);

                 const auto channel = params::string (a, "channel");

                 for (auto& [id, steps] : pat->steps)
                     if (channel.isEmpty() || id == channel)
                         std::fill (steps.begin(), steps.end(), Step{});

                 return CommandResult::success();
             } });

    //==========================================================================
    // Mixer
    auto findTrack = [] (Project& p, const juce::var& a) { return p.findMixer (params::string (a, "track")); };

    r.add ({ "mixer.setVolume", "Volume",
             schema ({ { "track", "string", true, "Id da trilha de mixer" }, { "db", "number", true, "Volume em dB (-100 a +6)" } }), true,
             [findTrack] (CommandContext& c, const juce::var& a)
             {
                 auto* m = findTrack (c.model.data(), a);

                 if (m == nullptr)
                     return CommandResult::fail ("Trilha de mixer não encontrada."_t);

                 m->volumeDb = (float) juce::jlimit (-100.0, 6.0, params::number (a, "db", m->volumeDb));
                 return CommandResult::success();
             } });

    r.add ({ "mixer.setPan", "Pan",
             schema ({ { "track", "string", true, "Id da trilha de mixer" }, { "pan", "number", true, "-1 = esquerda, 0 = centro, 1 = direita" } }), true,
             [findTrack] (CommandContext& c, const juce::var& a)
             {
                 auto* m = findTrack (c.model.data(), a);

                 if (m == nullptr)
                     return CommandResult::fail ("Trilha de mixer não encontrada."_t);

                 m->pan = (float) juce::jlimit (-1.0, 1.0, params::number (a, "pan", m->pan));
                 return CommandResult::success();
             } });

    r.add ({ "mixer.setMute", "Mute",
             schema ({ { "track", "string", true, "Id da trilha" }, { "on", "boolean", false, "true = mudo; ausente = alterna" } }), true,
             [findTrack] (CommandContext& c, const juce::var& a)
             {
                 auto* m = findTrack (c.model.data(), a);

                 if (m == nullptr)
                     return CommandResult::fail ("Trilha de mixer não encontrada."_t);

                 m->mute = params::boolean (a, "on", ! m->mute);
                 return CommandResult::success();
             } });

    r.add ({ "mixer.setSolo", "Solo",
             schema ({ { "track", "string", true, "Id da trilha" }, { "on", "boolean", false, "true = solo; ausente = alterna" } }), true,
             [findTrack] (CommandContext& c, const juce::var& a)
             {
                 auto* m = findTrack (c.model.data(), a);

                 if (m == nullptr || m->isMaster())
                     return CommandResult::fail ("Trilha de mixer não encontrada (o master não tem solo)."_t);

                 m->solo = params::boolean (a, "on", ! m->solo);
                 return CommandResult::success();
             } });

    r.add ({ "mixer.setOutput", "Mudar saída da trilha"_t,
             schema ({ { "track", "string", true, "Id da trilha" }, { "output", "string", true, "Id da trilha de destino (mx_master = master)" } }), true,
             [findTrack] (CommandContext& c, const juce::var& a)
             {
                 auto& p = c.model.data();
                 auto* m = findTrack (p, a);
                 const auto output = params::string (a, "output");

                 if (m == nullptr || m->isMaster())
                     return CommandResult::fail ("Trilha de mixer não encontrada (a saída do master é a placa de som)."_t);

                 if (p.findMixer (output) == nullptr)
                     return CommandResult::fail ("Trilha de destino não encontrada: "_t + output);

                 if (wouldLoop (p, m->id, output))
                     return CommandResult::fail ("Essa saída criaria um laço: o som voltaria para a própria trilha."_t);

                 m->output = output == masterTrackId ? juce::String() : output;
                 return CommandResult::success();
             } });

    r.add ({ "mixer.rename", "Renomear trilha",
             schema ({ { "track", "string", true, "Id da trilha" }, { "name", "string", true, "Nome novo" } }), true,
             [findTrack] (CommandContext& c, const juce::var& a)
             {
                 auto* m = findTrack (c.model.data(), a);
                 const auto name = params::string (a, "name").trim();

                 if (m == nullptr)
                     return CommandResult::fail ("Trilha de mixer não encontrada."_t);

                 if (name.isEmpty())
                     return CommandResult::fail ("O nome não pode ficar vazio."_t);

                 m->name = name;
                 return CommandResult::success();
             } });

    //==========================================================================
    // Edição e telas
    r.add ({ "edit.undo", "Desfazer", schema ({}), false,
             [&r] (CommandContext&, const juce::var&)
             {
                 return r.undo() ? CommandResult::success() : CommandResult::fail ("Nada para desfazer.");
             } });

    r.add ({ "edit.redo", "Refazer", schema ({}), false,
             [&r] (CommandContext&, const juce::var&)
             {
                 return r.redo() ? CommandResult::success() : CommandResult::fail ("Nada para refazer.");
             } });

    // Ctrl+Z do FL: desfaz a última edição; apertado de novo, refaz (alterna entre os dois estados).
    r.add ({ "edit.undoToggle", "Desfazer ou refazer a última edição"_t, schema ({}), false,
             [&r, undid = std::make_shared<bool> (false)] (CommandContext&, const juce::var&)
             {
                 if (*undid && r.canRedo())
                 {
                     *undid = false;
                     return r.redo() ? CommandResult::success() : CommandResult::fail ("Nada para refazer.");
                 }

                 *undid = r.undo();
                 return *undid ? CommandResult::success() : CommandResult::fail ("Nada para desfazer.");
             } });

    r.add ({ "view.toggle", "Mostrar ou esconder painel",
             schema ({ { "panel", "string", true, "playlist, channelRack, pianoRoll, projects (tela Projetos), mixer, sounds (navegador de sons) ou plugins"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 c.app.togglePanel (params::string (a, "panel"));
                 return CommandResult::success();
             } });
}

} // namespace whynot
