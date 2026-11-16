// Autoteste da frente avisos-tela (16/11): o que a rodada anterior deixou pronto por baixo e agora aparece na tela.
// - o .wnl: projeto que declara recursos desconhecidos (app.features) avisa ao abrir e pergunta antes do primeiro
//   salvar por cima (o original fica em history/); caminho de rede ou que sai da pasta avisa quantos e por quê;
//   o salvar lista o que não foi para samples/ por não ser áudio.
// Nenhuma janela nem caixa aparece: o aviso é lido do estado (AppController::takeProjectNotices e os textos das
// perguntas), e a resposta da caixa é simulada chamando o que o botão chama (como o AutosaveGuard.cpp faz com o
// blockSuspect). Dono: avisos-tela.

#include "SelfTestKit.h"

#include <cmath>
#include <cstring>

namespace whynot::selftest
{

namespace
{
    void set (juce::var node, const char* key, const juce::var& value)
    {
        if (auto* o = node.getDynamicObject())
            o->setProperty (key, value);
    }

    juce::String slash (const juce::File& f)   { return f.getFullPathName().replaceCharacter ('\\', '/'); }

    /** Os textos dos avisos numa linha, para o relatório (a coordenação revisa a redação por aqui). */
    juce::String texts (const std::vector<AppController::ProjectNotice>& notices)
    {
        juce::StringArray list;

        for (auto& n : notices)
            list.add ((n.warning ? "[aviso] " : "[nota] ") + n.text);

        return list.isEmpty() ? juce::String ("(nenhum)") : list.joinIntoString (" | ");
    }

    /** Um take que o app deixou sem fechar ao cair: o WAV (32 bits float, mono, 48 kHz) com os tamanhos do
        cabeçalho zerados, como fica antes do primeiro flush, e o marcador "<nome>.gravando.json" ao lado
        (os campos do InputRecorder::start). Devolve o WAV. */
    juce::File orphanTake (const juce::File& folder, const juce::String& stem, double seconds, double playFrom, double recordFrom,
                           const juce::String& takeName, const juce::String& trackName)
    {
        constexpr double rate = 48000.0;
        folder.createDirectory();
        const auto wav = folder.getChildFile (stem + ".wav");
        wav.deleteFile();

        juce::AudioBuffer<float> b (1, juce::roundToInt (seconds * rate));

        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * i / rate));

        {
            std::unique_ptr<juce::OutputStream> stream (wav.createOutputStream().release());
            juce::WavAudioFormat format;

            if (auto w = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (1).withBitsPerSample (32)
                                                            .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint)))
                w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        }

        // o cabeçalho de quem não fechou: o tamanho do RIFF e o do bloco "data" em zero
        juce::MemoryBlock bytes;

        if (wav.loadFileAsData (bytes) && bytes.getSize() > 64)
        {
            auto* d = static_cast<char*> (bytes.getData());
            std::memset (d + 4, 0, 4);

            for (size_t i = 12; i + 8 < juce::jmin<size_t> (bytes.getSize(), 4096); ++i)
            {
                if (std::memcmp (d + i, "data", 4) == 0)
                {
                    std::memset (d + i + 4, 0, 4);
                    break;
                }
            }

            wav.replaceWithData (bytes.getData(), bytes.getSize());
        }

        auto* o = new juce::DynamicObject();
        const juce::var json (o);
        o->setProperty ("version", 1);
        o->setProperty ("file", wav.getFileName());
        o->setProperty ("path", wav.getFullPathName());
        o->setProperty ("sampleRate", rate);
        o->setProperty ("channels", 1);
        o->setProperty ("bitsPerSample", 32);
        o->setProperty ("format", "float");
        o->setProperty ("playFromSeconds", playFrom);
        o->setProperty ("recordFromSeconds", recordFrom);
        o->setProperty ("takeName", takeName);
        o->setProperty ("track", trackName);
        o->setProperty ("startedAt", "2026-11-16T14:32:00.000-03:00");
        record::InputRecorder::markerFileFor (wav).replaceWithText (juce::JSON::toString (json), false, false, "\n");
        return wav;
    }

    bool anyContains (const std::vector<AppController::ProjectNotice>& notices, const juce::String& a, const juce::String& b, bool warning)
    {
        for (auto& n : notices)
            if (n.warning == warning && n.text.contains (a) && n.text.contains (b))
                return true;

        return false;
    }
}

void runScreenNoticesSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;

    const auto root = ctx.work.getChildFile ("avisos-tela");
    root.deleteRecursively();
    root.createDirectory();

    //--------------------------------------------------------------------------
    rep.section ("Avisos na tela: o .wnl (versão mais nova, caminhos recusados, o que não é áudio)"_t);
    {
        app.run ("project.new");

        const auto folder = root.getChildFile ("Futuro");
        const auto file = folder.getChildFile ("Futuro.wnl");
        folder.createDirectory();

        const bool saved = app.run ("project.save", args ({ { "path", file.getFullPathName() } })).ok;
        const auto afterPlainSave = app.takeProjectNotices();
        const bool reopened = app.openProject (file).wasOk();
        const auto afterPlainOpen = app.takeProjectNotices();
        rep.check (saved && reopened && afterPlainSave.empty() && afterPlainOpen.empty() && app.newerAppSaveQuestion().isEmpty(),
                   "projeto desta versão: salva e abre sem aviso, e salvar não pergunta nada ("_t + texts (afterPlainOpen) + ")");

        // salvo por uma versão mais nova: o arquivo declara um recurso que este app não conhece
        auto doc = juce::JSON::parse (file.loadFileAsString());
        set (doc["app"], "features", juce::Array<juce::var> { "recurso-do-futuro", "outro" });
        const bool written = file.replaceWithText (juce::JSON::toString (doc));

        const bool opened = app.openProject (file).wasOk();
        const auto onOpen = app.takeProjectNotices();
        rep.check (written && opened && onOpen.size() == 1 && anyContains (onOpen, "versão mais nova"_t, "recurso-do-futuro", true),
                   "features desconhecida: avisa ao abrir: "_t + texts (onOpen));

        const auto question = app.newerAppSaveQuestion();
        rep.check (question.contains ("Salvar por cima apaga") && question.contains ("recurso-do-futuro") && question.contains ("history"),
                   "e pergunta antes do primeiro salvar por cima, dizendo que o original fica em history: "_t
                       + question.replace ("\n", " "));
        rep.check (app.newerAppSaveQuestion (folder.getChildFile ("Outro nome.wnl")).isEmpty() && app.newerAppSaveQuestion (file).isNotEmpty(),
                   "\"Salvar como\" em outro arquivo não pergunta (o original não é tocado); em cima do mesmo, pergunta"_t);

        // a resposta "Salvar por cima" da caixa é o próprio project.save
        const bool over = app.run ("project.save").ok;
        const auto kept = folder.getChildFile ("history").findChildFiles (juce::File::findFiles, false, "*(app mais novo)*.wnl");
        const auto onSave = app.takeProjectNotices();
        rep.check (over && kept.size() == 1 && kept[0].loadFileAsString().contains ("recurso-do-futuro")
                       && onSave.size() == 1 && anyContains (onSave, "original", "history", false),
                   "salvando por cima, o original fica em history/ e o aviso diz onde: "_t + texts (onSave));

        const bool again = app.run ("project.save").ok;
        const auto onSecondSave = app.takeProjectNotices();
        rep.check (again && app.newerAppSaveQuestion().isEmpty() && onSecondSave.empty()
                       && folder.getChildFile ("history").findChildFiles (juce::File::findFiles, false, "*.wnl").size() == 1,
                   "depois do primeiro salvar o arquivo é desta versão: não pergunta nem avisa de novo, e não há segunda cópia"_t);

        // caminho de rede: recusado ao abrir (ficou só o nome do arquivo), e a tela diz quantos e por quê
        doc = juce::JSON::parse (file.loadFileAsString());
        set (doc["channels"][0]["sample"], "path", "\\\\servidor\\kit\\kick.wav");
        set (doc["channels"][0]["sample"], "originalPath", "");
        const auto network = folder.getChildFile ("Rede.wnl");
        const bool networkOpened = network.replaceWithText (juce::JSON::toString (doc)) && app.openProject (network).wasOk();
        const auto onNetwork = app.takeProjectNotices();
        rep.check (networkOpened && onNetwork.size() == 1 && anyContains (onNetwork, "1 caminho", "de rede", true)
                       && app.newerAppSaveQuestion().isEmpty(),
                   "caminho de rede: avisa ao abrir: "_t + texts (onNetwork));

        // caminho que sai da pasta do projeto: o outro motivo
        set (doc["channels"][0]["sample"], "path", "../../fora/kick.wav");
        const auto outside = folder.getChildFile ("Fora.wnl");
        const bool outsideOpened = outside.replaceWithText (juce::JSON::toString (doc)) && app.openProject (outside).wasOk();
        const auto onOutside = app.takeProjectNotices();
        rep.check (outsideOpened && onOutside.size() == 1 && anyContains (onOutside, "1 caminho", "sai da pasta", true),
                   "caminho com \"..\" para fora da pasta: avisa ao abrir: "_t + texts (onOutside));

        // o que não é áudio não entra em samples/ ao salvar, e a tela lista
        const auto notes = root.getChildFile ("fora").getChildFile ("notas.txt");
        notes.getParentDirectory().createDirectory();
        notes.replaceWithText ("um documento, não um sample");
        set (doc["channels"][0]["sample"], "path", slash (notes));
        const auto withDocument = folder.getChildFile ("Documento.wnl");
        const bool documentOpened = withDocument.replaceWithText (juce::JSON::toString (doc)) && app.openProject (withDocument).wasOk();
        const auto onDocumentOpen = app.takeProjectNotices();
        const bool documentSaved = app.run ("project.save").ok;
        const auto onDocumentSave = app.takeProjectNotices();
        rep.check (documentOpened && onDocumentOpen.empty() && documentSaved && onDocumentSave.size() == 1
                       && anyContains (onDocumentSave, "notas.txt", "não é áudio"_t, false)
                       && ! folder.getChildFile ("samples").getChildFile ("notas.txt").exists(),
                   "salvar com um arquivo que não é áudio: não vai para samples/ e o aviso lista: "_t + texts (onDocumentSave));

        app.run ("project.new");
        rep.check (app.takeProjectNotices().empty() && app.newerAppSaveQuestion().isEmpty(), "projeto novo começa sem aviso pendente"_t);
    }

    //--------------------------------------------------------------------------
    rep.section ("Avisos na tela: o take que uma queda deixou sem guardar vai para a playlist"_t);
    {
        app.run ("project.new");
        app.recoveredTakes.clear();

        const auto folder = root.getChildFile ("Take");
        const auto file = folder.getChildFile ("Take.wnl");
        folder.createDirectory();

        const auto voiceTrack = app.run ("mixer.addTrack", args ({ { "name", "Voz" } })).value["track"].toString();
        const bool saved = app.run ("project.save", args ({ { "path", file.getFullPathName() } })).ok;

        // a queda fabricada: 7 tempos a 140 BPM (3 s) num WAV com o cabeçalho de quem não fechou, e o marcador ao
        // lado dizendo que o transporte começou no tempo 4 (um compasso de contagem) e o take vale do tempo 8
        const auto takes = folder.getChildFile ("gravacoes");
        const auto wav = orphanTake (takes, "Voz_take01", 7.0 * beat140, 4.0 * beat140, 8.0 * beat140, "Voz take 1", "Voz");
        const auto marker = record::InputRecorder::markerFileFor (wav);
        rep.check (saved && voiceTrack.isNotEmpty() && wav.existsAsFile() && marker.existsAsFile(),
                   "o take fabricado: WAV com cabeçalho de tamanho zero e o marcador .gravando.json ao lado"_t);

        const bool opened = app.openProject (file).wasOk();
        const bool found = app.recoveredTakes.size() == 1;
        const auto take = found ? app.recoveredTakes.front() : AppController::RecoveredTake();
        app.recoveredTakes.clear();   // a tela lê e esvazia
        const auto fixed = readWav (wav);
        rep.check (opened && found && take.file == wav && take.repaired && take.numSamples == 144000 && ! marker.exists()
                       && fixed.valid && fixed.buffer.getNumSamples() == 144000,
                   "abrir o projeto acha o take, conserta o cabeçalho (144000 amostras) e apaga o marcador"_t);

        if (! found)
        {
            app.run ("project.new");
            return;
        }

        const auto question = app.recoveredTakeQuestion (take);
        rep.check (question.contains ("Voz take 1") && question.contains ("compasso 3") && question.contains ("trilha \"Voz\"")
                       && question.contains ("aproximada") && question.contains (wav.getFullPathName()),
                   "a caixa oferece o take, diz onde ele entra, que a posição é aproximada e onde fica o arquivo: "_t + question.replace ("\n", " "));

        const auto leftText = app.recoveredTakeLeftText (take);
        rep.check (leftText.contains (wav.getFullPathName()), "\"Deixar na pasta\": o aviso diz onde o WAV ficou: "_t + leftText);

        // "Pôr na playlist": o mesmo comando da gravação normal, num passo de desfazer
        const auto channelsBefore = ctx.model.get().channels.size();
        const auto itemsBefore = ctx.model.get().playlistItems.size();
        const auto placed = app.placeRecoveredTake (take);

        const Channel* channel = nullptr;
        const PlaylistItem* item = nullptr;

        for (auto& c : ctx.model.get().channels)
            if (c.isAudio() && c.name == "Voz take 1")
                channel = &c;

        if (channel != nullptr)
            for (auto& i : ctx.model.get().playlistItems)
                if (i.kind == "audio" && i.ref == channel->id)
                    item = &i;

        rep.check (placed.ok && ctx.model.get().channels.size() == channelsBefore + 1 && ctx.model.get().playlistItems.size() == itemsBefore + 1
                       && channel != nullptr && item != nullptr,
                   "\"Pôr na playlist\" cria o canal de áudio e o bloco ("_t + (placed.ok ? juce::String ("ok") : placed.error) + ")");

        if (channel != nullptr && item != nullptr)
        {
            rep.check (item->start == 8 * ticksPerBeat && item->length == 3 * ticksPerBeat && std::abs (item->offsetSeconds - 4.0 * beat140) < 1.0e-4,
                       "o bloco começa onde o marcador diz (tempo 8), sem a contagem (corte de "_t
                           + juce::String (item->offsetSeconds, 3).replaceCharacter ('.', ',') + " s) e dura os 3 tempos que sobram");
            rep.check (channel->output == voiceTrack && ctx.model.resolveSample (channel->samplePath) == wav,
                       "o canal sai pela trilha do marcador (Voz) e toca o WAV de gravacoes/"_t);
        }

        const auto placedText = app.recoveredTakePlacedText (take);
        rep.check (placedText.contains ("Voz take 1") && placedText.contains ("compasso 3") && placedText.contains ("Ctrl+Z"),
                   "o pé conta o que foi feito: "_t + placedText);

        const bool undone = app.run ("edit.undo").ok;
        rep.check (undone && ctx.model.get().channels.size() == channelsBefore && ctx.model.get().playlistItems.size() == itemsBefore && wav.existsAsFile(),
                   "um Ctrl+Z tira o canal e o bloco; o WAV continua na pasta"_t);

        const bool redone = app.run ("edit.redo").ok;
        rep.check (redone && ctx.model.get().playlistItems.size() == itemsBefore + 1, "e o refazer traz de volta"_t);
        app.run ("edit.undo");

        // a trilha do marcador não existe mais: o take sai pelo master, e a caixa avisa
        auto orphanOfNoTrack = take;
        orphanOfNoTrack.info.trackName = "Trilha que sumiu";
        rep.check (app.recoveredTakeQuestion (orphanOfNoTrack).contains ("pelo master (a trilha \"Trilha que sumiu\""),
                   "trilha do marcador que não existe no projeto: a caixa diz que o take sai pelo master"_t);

        // arquivo apagado entre a oferta e o clique: recusa com o motivo, sem mexer no projeto
        auto gone = take;
        gone.file = takes.getChildFile ("nao-existe.wav");
        const auto refused = app.placeRecoveredTake (gone);
        rep.check (! refused.ok && refused.error.contains ("nao-existe.wav") && ctx.model.get().playlistItems.size() == itemsBefore,
                   "arquivo que sumiu: recusa e diz qual: "_t + refused.error);

        // projeto nunca salvo: os takes vão para Gravacoes na pasta de dados, varrida na partida do app
        const auto unsavedWav = orphanTake (ctx.data.getChildFile ("Gravacoes"), "avisos-tela_take01", 2.0 * beat140, 0.0, 0.0, "Voz take 9", "");
        size_t atStartup = 0;
        bool startupFound = false;
        {
            AppController restarted (true, ctx.data);
            atStartup = restarted.recoveredTakes.size();

            for (auto& t : restarted.recoveredTakes)
                startupFound = startupFound || (t.file == unsavedWav && t.info.takeName == "Voz take 9" && t.repaired);
        }
        rep.check (startupFound && readWav (unsavedWav).valid
                       && ! record::InputRecorder::markerFileFor (unsavedWav).exists(),
                   "projeto nunca salvo: a partida do app acha o take em Gravacoes, na pasta de dados ("_t + juce::String ((int) atStartup) + ")");
        unsavedWav.deleteFile();

        app.run ("project.new");
    }
}

} // namespace whynot::selftest
