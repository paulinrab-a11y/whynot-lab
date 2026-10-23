// Autoteste dos favoritos (pedido do usuário de 21/10, item 3.1 do plano):
// - a migração do synth-favoritos.txt antigo para o favoritos.json;
// - marcar e desmarcar pelos comandos favorites.* (sem entrar no desfazer) e o que eles recusam;
// - o favoritos.json: formato, reler do disco e arquivo corrompido;
// - os favoritos primeiro nos menus de adicionar efeito (slot do mixer) e instrumento (o "+" do rack);
// - a estrela nas listas: clicar marca e desmarca, o filtro "Favoritos" e capturas sem janela
//   (componentes num pai fora da área de trabalho, createComponentSnapshot), em favoritos-*.png.

#include "SelfTestKit.h"
#include "../Favorites.h"
#include "../../dsp/FxCatalog.h"
#include "../../synth/SynthPresets.h"
#include "../../ui/ChannelRack.h"
#include "../../ui/FavoriteStar.h"
#include "../../ui/PluginBrowser.h"
#include "../../ui/SampleBrowser.h"
#include "../../ui/Theme.h"
#include "../../ui/fx/InsertPanel.h"
#include "../../ui/synth/SynthBrowser.h"

namespace whynot::selftest
{

namespace
{
    using Kind = Favorites::Kind;

    juce::String utf8 (const char* s)          { return juce::String::fromUTF8 (s); }
    juce::String utf8 (const std::string& s)   { return juce::String::fromUTF8 (s.c_str()); }

    bool isOrange (juce::Colour c)   { return (int) c.getRed() - (int) c.getBlue() > 60 && c.getRed() > 150; }

    bool writeWav (const juce::File& f, double hz)
    {
        const double rate = 44100.0;
        juce::AudioBuffer<float> b (1, 4410);

        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, 0.4f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / rate));

        f.getParentDirectory().createDirectory();
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (f.createOutputStream().release());

        if (stream == nullptr)
            return false;

        juce::WavAudioFormat wav;
        auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (1).withBitsPerSample (16));
        return w != nullptr && w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    /** Os itens do primeiro nível de um menu: "#" + texto = seção, "-" = separador. */
    juce::StringArray menuTexts (const juce::PopupMenu& menu)
    {
        juce::StringArray texts;

        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        {
            const auto& item = it.getItem();
            texts.add (item.isSeparator ? juce::String ("-") : item.isSectionHeader ? "#" + item.text : item.text);
        }

        return texts;
    }

    /** O n-ésimo item (0 = o primeiro) com esse texto no primeiro nível. */
    const juce::PopupMenu::Item* findItem (const juce::PopupMenu& menu, const juce::String& text, int nth = 0)
    {
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            if (it.getItem().text == text && nth-- == 0)
                return &it.getItem();

        return nullptr;
    }

    bool hasStar (const juce::PopupMenu::Item* item)
    {
        return item != nullptr && dynamic_cast<const favui::MenuItem*> (item->customComponent.get()) != nullptr;
    }

    juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> p)
    {
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                 juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                 juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                 &c, &c, now, p, now, 1, false };
    }

    /** Um clique de verdade na linha da ListBox (o componente da linha recebe o mouseDown e o mouseUp),
        `fromRight` px antes do fim da linha. */
    bool clickRow (juce::ListBox& list, int row, int fromRight)
    {
        const auto r = list.getRowPosition (row, true);
        const juce::Point<int> at (r.getRight() - fromRight, r.getCentreY());
        auto* rowComponent = list.getComponentAt (at);

        if (rowComponent == nullptr || rowComponent == &list)
            return false;

        const auto local = rowComponent->getLocalPoint (&list, at).toFloat();
        rowComponent->mouseDown (mouseAt (*rowComponent, local));
        rowComponent->mouseUp (mouseAt (*rowComponent, local));
        return true;
    }

    /** O centro da estrela da linha, em coordenadas de `host` (a estrela fica `fromRight` px antes do fim da linha). */
    juce::Point<int> starCentre (juce::Component& host, juce::ListBox& list, int row, int fromRight)
    {
        const auto r = list.getRowPosition (row, true);
        return host.getLocalPoint (&list, juce::Point<int> (r.getRight() - fromRight, r.getCentreY()));
    }

    juce::Image snapshot (juce::Component& c)
    {
        // imagem de software: ler pixel da imagem nativa (Direct2D) com um Graphics aberto já travou o autoteste
        return juce::SoftwareImageType().convert (c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f));
    }

    bool savePng (const juce::Image& image, const juce::File& file)
    {
        file.deleteFile();

        if (juce::FileOutputStream out (file); out.openedOk())
            return juce::PNGImageFormat().writeImageToStream (image, out) && image.getWidth() > 0;

        return false;
    }

    /** O cartão de fundo das capturas (a cor dos navegadores). */
    struct Stage : juce::Component
    {
        void paint (juce::Graphics& g) override   { g.fillAll (theme::colour::panel); }
    };

    /** Fundo de menu, para a captura dos itens com estrela. */
    struct MenuStage : juce::Component
    {
        void paint (juce::Graphics& g) override
        {
            g.fillAll (theme::colour::background);
            getLookAndFeel().drawPopupMenuBackground (g, getWidth(), getHeight());
        }
    };
}

void runFavoritesSelfTest (SelfTestContext& ctx)
{
    auto& rep = ctx.rep;
    auto& app = ctx.app;
    auto& model = ctx.model;
    auto& favourites = Favorites::get();

    // Uma pasta só desta seção: o favoritos.json do teste nunca encosta no do usuário (nem no ctx.data).
    const auto folder = ctx.work.getChildFile ("favoritos");
    folder.deleteRecursively();
    folder.createDirectory();

    const auto sounds = ctx.work.getChildFile ("favoritos-sons");
    const auto kit = sounds.getChildFile ("Kit Favorito");
    const auto kick = sounds.getChildFile ("Kick Favorito.wav");
    const auto snare = sounds.getChildFile ("Snare Favorito.wav");
    const bool wroteSounds = writeWav (kick, 60.0) && writeWav (snare, 200.0)
                             && writeWav (kit.getChildFile ("Hat.wav"), 800.0) && writeWav (kit.getChildFile ("Clap.wav"), 400.0);

    juce::String synthId, gainId;
    juce::PluginDescription synthDesc, gainDesc;

    for (auto& d : app.knownPlugins())
    {
        if (d.name == "WHYNOT Teste Synth") { synthId = d.createIdentifierString(); synthDesc = d; }
        if (d.name == "WHYNOT Teste Ganho") { gainId = d.createIdentifierString(); gainDesc = d; }
    }

    //==========================================================================
    rep.section ("Favoritos: migração do synth-favoritos.txt"_t);
    {
        // como a versão antiga gravava: um id por linha (com repetido e linha vazia, para ver que limpa)
        const auto old = folder.getChildFile ("synth-favoritos.txt");
        old.replaceWithText ("808-limpo\n\nkeys-ep-fm\n808-limpo\n");
        favourites.setFolder (folder);

        rep.check (favourites.list (Kind::synthPreset) == juce::StringArray { "808-limpo", "keys-ep-fm" },
                   "os presets do synth-favoritos.txt entram nos favoritos, na ordem e sem repetir: "_t
                       + favourites.list (Kind::synthPreset).joinIntoString (", "));
        rep.check (favourites.getFile() == folder.getChildFile ("favoritos.json") && favourites.getFile().existsAsFile(),
                   "o favoritos.json fica na pasta de dados (a mesma do atalhos.json)"_t);
        rep.check (! old.existsAsFile() && folder.getChildFile ("synth-favoritos.txt.migrado").existsAsFile(),
                   "o arquivo antigo vira synth-favoritos.txt.migrado e não é lido de novo"_t);

        const auto listed = app.run ("synth.listPresets", args ({ { "favorites", true } }));
        rep.check (listed.ok && listed.value["presets"].size() == 2, "o synth enxerga os favoritos migrados (synth.listPresets com favorites)"_t);

        favourites.setFolder (ctx.data);
        favourites.setFolder (folder);
        rep.check (favourites.list (Kind::synthPreset).size() == 2, "trocar de pasta e voltar relê do disco sem duplicar"_t);

        // synth.setFavorite (do synth) grava no mesmo lugar
        app.run ("synth.setFavorite", args ({ { "preset", "keys-ep-fm" }, { "on", false } }));
        app.run ("synth.setFavorite", args ({ { "preset", "keys-ep-fm" }, { "on", true } }));
        rep.check (favourites.contains (Kind::synthPreset, "keys-ep-fm") && ! folder.getChildFile ("synth-favoritos.txt").existsAsFile(),
                   "synth.setFavorite grava no favoritos.json, não recria o arquivo antigo"_t);
    }

    //==========================================================================
    rep.section ("Favoritos: marcar e desmarcar pelos comandos"_t);
    {
        auto& commands = app.getCommands();
        const bool couldUndo = commands.canUndo();
        const auto undoBefore = commands.undoDescription();

        auto set = [&app] (const char* kind, const juce::String& id, bool on)
        {
            return app.run ("favorites.set", args ({ { "kind", kind }, { "id", id }, { "on", on } }));
        };

        rep.check (wroteSounds, "sons de teste gravados em "_t + sounds.getFullPathName());

        const auto eq = set ("effect", "eq", true);
        rep.check (eq.ok && (bool) eq.value["favorite"] && (bool) eq.value["changed"] && eq.value["name"].toString().isNotEmpty(),
                   "favorites.set marca um efeito nativo (eq: "_t + eq.value["name"].toString() + ")");
        rep.check (! (bool) set ("effect", "eq", true).value["changed"], "marcar de novo não muda nada"_t);

        const auto plate = set ("effectPreset", "reverb/plate-vocal", true);
        rep.check (plate.ok && favourites.contains (Kind::effectPreset, "reverb/plate-vocal"),
                   "marca um preset de efeito (\"<efeito>/<preset>\": "_t + plate.value["name"].toString() + ")");

        rep.check (set ("sample", kick.getFullPathName(), true).ok && set ("sample", snare.getFullPathName(), true).ok
                       && favourites.contains (Kind::sample, kick.getFullPathName()),
                   "marca sons pelo caminho completo"_t);
        rep.check (favourites.contains (Kind::sample, kick.getFullPathName().toUpperCase()),
                   "o caminho vale sem diferenciar maiúsculas (como o Windows)"_t);
        rep.check (set ("folder", kit.getFullPathName(), true).ok && favourites.contains (Kind::folder, kit.getFullPathName()),
                   "marca uma pasta de kit"_t);

        rep.check (synthId.isNotEmpty() && gainId.isNotEmpty(), "os plugins de teste estão no catálogo (seção de plugins)"_t);
        const auto byName = set ("plugin", "WHYNOT Teste Synth", true);
        rep.check (byName.ok && byName.value["id"].toString() == synthId, "plugin marcado pelo nome fica gravado pelo id do catálogo"_t);
        rep.check (set ("plugin", gainId, true).ok && favourites.contains (Kind::plugin, gainId), "marca um efeito VST3 pelo id"_t);

        const auto on = app.run ("favorites.toggle", args ({ { "kind", "effect" }, { "id", "comp" } }));
        const auto off = app.run ("favorites.toggle", args ({ { "kind", "effect" }, { "id", "comp" } }));
        rep.check (on.ok && (bool) on.value["favorite"] && off.ok && ! (bool) off.value["favorite"] && ! favourites.contains (Kind::effect, "comp"),
                   "favorites.toggle marca e, de novo, desmarca"_t);

        rep.check (! set ("estrela", "eq", true).ok, "tipo desconhecido é recusado"_t);
        rep.check (! set ("effect", "nao-existe", true).ok, "efeito que não existe é recusado"_t);
        rep.check (! set ("effectPreset", "reverb/nao-existe", true).ok, "preset de efeito que não existe é recusado"_t);
        rep.check (! set ("synthPreset", "nao-existe", true).ok && ! set ("synthPreset", "init", true).ok,
                   "preset do synth que não existe (ou o Init) é recusado"_t);
        rep.check (! set ("plugin", "Plugin Que Nao Existe", true).ok, "plugin fora do catálogo é recusado"_t);
        rep.check (! set ("sample", sounds.getChildFile ("nao-existe.wav").getFullPathName(), true).ok, "som que não existe é recusado"_t);
        rep.check (! set ("sample", "Kick Favorito.wav", true).ok, "caminho relativo é recusado"_t);
        rep.check (! app.run ("favorites.set", args ({ { "kind", "effect" } })).ok, "sem id é recusado"_t);

        // desmarcar aceita o que sumiu do disco (o som apagado continua tirável)
        const auto gone = sounds.getChildFile ("Apagado.wav");
        writeWav (gone, 300.0);
        set ("sample", gone.getFullPathName(), true);
        gone.deleteFile();
        rep.check (set ("sample", gone.getFullPathName(), false).ok && ! favourites.contains (Kind::sample, gone.getFullPathName()),
                   "desmarcar um som que foi apagado funciona"_t);

        const auto all = app.run ("favorites.list");
        const auto counts = all.value["counts"];
        rep.check (all.ok && (int) counts["effect"] == 1 && (int) counts["effectPreset"] == 1 && (int) counts["sample"] == 2
                       && (int) counts["folder"] == 1 && (int) counts["plugin"] == 2 && (int) counts["synthPreset"] == 2
                       && all.value["file"].toString() == favourites.getFile().getFullPathName(),
                   "favorites.list conta cada tipo e diz onde está o arquivo: "_t + juce::JSON::toString (counts, true));

        bool namesOk = all.value["favorites"].size() == 9;

        for (int i = 0; i < all.value["favorites"].size(); ++i)
            namesOk = namesOk && all.value["favorites"][i]["name"].toString().isNotEmpty() && (bool) all.value["favorites"][i]["available"];

        rep.check (namesOk, "cada favorito vem com o nome para mostrar e se ainda existe"_t);

        const auto onlySynth = app.run ("favorites.list", args ({ { "kind", "synthPreset" } }));
        rep.check (onlySynth.ok && onlySynth.value["favorites"].size() == 2, "favorites.list filtra por tipo"_t);
        rep.check (! app.run ("favorites.list", args ({ { "kind", "x" } })).ok, "favorites.list recusa tipo desconhecido"_t);

        bool noModel = true;

        for (auto* id : { "favorites.list", "favorites.set", "favorites.toggle" })
            noModel = noModel && commands.find (id) != nullptr && ! commands.find (id)->changesModel;

        rep.check (noModel && commands.canUndo() == couldUndo && commands.undoDescription() == undoBefore,
                   "os comandos de favoritos não mexem no projeto nem entram no desfazer"_t);
    }

    //==========================================================================
    rep.section ("Favoritos: o arquivo favoritos.json"_t);
    {
        juce::var json;
        const bool parsed = juce::JSON::parse (favourites.getFile().loadFileAsString(), json).wasOk();
        const auto lists = json["favorites"];
        auto has = [&lists] (const char* kind, const juce::String& id)
        {
            if (const auto* items = lists[kind].getArray())
                return items->contains (juce::var (id));

            return false;
        };

        rep.check (parsed && (int) json["version"] == 1, "favoritos.json é JSON com version 1"_t);
        rep.check (has ("effect", "eq") && has ("effectPreset", "reverb/plate-vocal") && has ("sample", kick.getFullPathName())
                       && has ("folder", kit.getFullPathName()) && has ("plugin", synthId) && has ("synthPreset", "808-limpo"),
                   "cada tipo numa lista própria (effect, effectPreset, sample, folder, plugin, synthPreset)"_t);

        // relê do disco
        favourites.setFolder (ctx.data);
        rep.check (! favourites.contains (Kind::effect, "eq"), "outra pasta de dados, outros favoritos"_t);
        favourites.setFolder (folder);
        rep.check (favourites.contains (Kind::effect, "eq") && favourites.contains (Kind::sample, snare.getFullPathName()),
                   "de volta à pasta, os favoritos vêm do disco"_t);

        // corrompido: começa vazio, guarda o arquivo e não derruba o app
        const auto broken = ctx.work.getChildFile ("favoritos-quebrado");
        broken.deleteRecursively();
        broken.createDirectory();
        broken.getChildFile ("favoritos.json").replaceWithText ("{ \"favorites\": { \"effect\": [ \"eq\" ");
        favourites.setFolder (broken);
        rep.check (favourites.list (Kind::effect).isEmpty() && favourites.getLoadError().isNotEmpty()
                       && broken.getChildFile ("favoritos-quebrado.json").existsAsFile(),
                   "favoritos.json corrompido: começa vazio e guarda a cópia em favoritos-quebrado.json"_t);
        rep.check (favourites.set (Kind::effect, "limiter", true) && favourites.getFile().loadFileAsString().contains ("limiter"),
                   "a próxima marca grava um arquivo bom por cima"_t);

        broken.getChildFile ("favoritos.json").replaceWithText (juce::String::repeatedString ("[", 5000) + juce::String::repeatedString ("]", 5000));
        favourites.setFolder (broken);
        rep.check (favourites.list (Kind::effect).isEmpty() && favourites.getLoadError().isNotEmpty(),
                   "aninhamento demais é recusado sem estourar a pilha"_t);

        favourites.setFolder (folder);
    }

    //==========================================================================
    rep.section ("Favoritos: primeiro nos menus de adicionar"_t);
    {
        app.run ("project.new");
        theme::LookAndFeel laf;   // o visual da casa, só neste pai (nada global)
        Stage host;
        host.setLookAndFeel (&laf);
        host.setSize (900, 600);

        const auto* eqInfo = dsp::findEffect ("eq");
        const auto* reverbInfo = dsp::findEffect ("reverb");
        const auto* plateInfo = dsp::findPreset ("reverb", "plate-vocal");
        const auto eqName = eqInfo != nullptr ? utf8 (eqInfo->name) : juce::String();
        const auto reverbName = reverbInfo != nullptr ? utf8 (reverbInfo->name) : juce::String();
        const auto plateName = plateInfo != nullptr ? utf8 (plateInfo->name) : juce::String();
        const auto gainLabel = gainDesc.name + (gainDesc.manufacturerName.isNotEmpty() ? " (" + gainDesc.manufacturerName + ")" : juce::String());

        // --- o slot vazio do mixer
        InsertPanel panel (app);
        host.addAndMakeVisible (panel);
        panel.setBounds (0, 0, InsertPanel::idealWidth, 400);
        panel.setTrack (masterTrackId);

        std::vector<std::function<void()>> actions;
        const auto menu = panel.createAddMenu (0, actions);
        const auto texts = menuTexts (menu);
        const juce::StringArray expectedTop { "Só favoritos"_t, "-", "#Favoritos", eqName, reverbName + ": " + plateName, gainLabel };
        juce::StringArray top;

        for (int i = 0; i < juce::jmin (texts.size(), expectedTop.size()); ++i)
            top.add (texts[i]);

        rep.check (top == expectedTop && texts.size() > expectedTop.size() && texts[expectedTop.size()].startsWith ("#"),
                   "menu do slot: o filtro, depois os favoritos (efeito, preset, VST3) e só então as categorias: "_t
                       + top.joinIntoString (" | "));

        const auto* favouriteEq = findItem (menu, eqName, 0);
        const auto* categoryEq = findItem (menu, eqName, 1);
        const auto* categoryReverb = findItem (menu, reverbName, 0);
        rep.check (hasStar (favouriteEq) && hasStar (categoryEq) && hasStar (categoryReverb),
                   "cada efeito do menu tem a estrela (os favoritos e os das categorias)"_t);

        bool plateFirst = false, presetsStarred = categoryReverb != nullptr && categoryReverb->subMenu != nullptr;

        if (presetsStarred)
        {
            const auto presetTexts = menuTexts (*categoryReverb->subMenu);
            plateFirst = presetTexts.size() > 2 && presetTexts[0] == "Padrão"_t && presetTexts[1] == "-" && presetTexts[2] == plateName;

            for (int i = 2; i < presetTexts.size(); ++i)
                presetsStarred = presetsStarred && hasStar (findItem (*categoryReverb->subMenu, presetTexts[i]));
        }

        rep.check (plateFirst, "nos presets do efeito, o favorito vem logo depois do Padrão"_t);
        rep.check (presetsStarred, "cada preset do submenu tem a estrela"_t);

        // escolher o favorito põe o efeito no slot
        const int before = model.get().findMixer (masterTrackId) != nullptr ? (int) model.get().findMixer (masterTrackId)->inserts.size() : -1;

        if (favouriteEq != nullptr && juce::isPositiveAndBelow (favouriteEq->itemID - 1, (int) actions.size()))
            actions[(size_t) favouriteEq->itemID - 1]();

        const auto* master = model.get().findMixer (masterTrackId);
        rep.check (master != nullptr && (int) master->inserts.size() == before + 1 && master->inserts.front().type == "eq",
                   "escolher o efeito favorito no menu põe o efeito no slot"_t);

        // o filtro "Só favoritos"
        panel.favouritesOnly = true;
        actions.clear();
        const auto filtered = menuTexts (panel.createAddMenu (0, actions));
        rep.check (filtered == juce::StringArray { "Só favoritos"_t, "-", "#Favoritos", eqName, reverbName + ": " + plateName, gainLabel },
                   "com \"Só favoritos\", o menu mostra só os favoritos: "_t + filtered.joinIntoString (" | "));
        const auto filteredMenu = panel.createAddMenu (0, actions);
        const auto* filterItem = findItem (filteredMenu, "Só favoritos"_t);
        rep.check (filterItem != nullptr && filterItem->isTicked, "o filtro ligado aparece marcado"_t);

        const auto empty = ctx.work.getChildFile ("favoritos-vazio");
        empty.deleteRecursively();
        favourites.setFolder (empty);
        actions.clear();
        const auto none = menuTexts (panel.createAddMenu (0, actions));
        rep.check (none.size() == 3 && none[2].startsWith ("Nenhum favorito"), "sem favoritos, o filtro diz como marcar"_t);
        favourites.setFolder (folder);
        panel.favouritesOnly = false;

        // --- o "+" do channel rack
        ChannelRack rack (app);
        host.addAndMakeVisible (rack);
        rack.setBounds (0, 0, 800, 400);
        rack.refresh (model.get());

        const auto* p808 = synth::findPreset ("808-limpo");
        const auto* pKeys = synth::findPreset ("keys-ep-fm");
        const auto name808 = p808 != nullptr ? utf8 (p808->name) : juce::String();
        const auto nameKeys = pKeys != nullptr ? utf8 (pKeys->name) : juce::String();

        std::vector<std::function<void()>> rackActions;
        const auto rackMenu = rack.createAddMenu (rackActions);
        const auto rackTexts = menuTexts (rackMenu);
        const juce::StringArray expectedRack { "#Favoritos", "WHYNOT Teste Synth (VST3)", name808 + " (WHYNOT Synth)", nameKeys + " (WHYNOT Synth)", "-",
                                               "Sample ou loop (arquivo de áudio)..."_t, "WHYNOT Synth (melodias, baixos e 808)",
                                               "WHYNOT Piano (piano de cauda gravado)",                // onda 3 (integração de teste)
                                               "Fatiar um beat ou loop (arquivo de áudio)..."_t,       // Fatiar e 808 (integração de teste)
                                               "Instrumentos VST3" };
        rep.check (rackTexts == expectedRack, "menu do \"+\" do rack: os instrumentos favoritos primeiro: "_t + rackTexts.joinIntoString (" | "));
        rep.check (hasStar (findItem (rackMenu, "WHYNOT Teste Synth (VST3)")) && hasStar (findItem (rackMenu, name808 + " (WHYNOT Synth)")),
                   "os favoritos do rack têm a estrela"_t);

        const auto* vst = findItem (rackMenu, "Instrumentos VST3");
        rep.check (vst != nullptr && vst->subMenu != nullptr && hasStar (findItem (*vst->subMenu, synthDesc.name + (synthDesc.manufacturerName.isNotEmpty()
                                                                                                                        ? " (" + synthDesc.manufacturerName + ")" : juce::String()))),
                   "o submenu de instrumentos VST3 tem a estrela em cada um"_t);

        // escolher o preset favorito cria o canal do synth com ele
        if (const auto* item = findItem (rackMenu, name808 + " (WHYNOT Synth)"); item != nullptr && juce::isPositiveAndBelow (item->itemID - 1, (int) rackActions.size()))
            rackActions[(size_t) item->itemID - 1]();

        bool synthAdded = false;

        for (auto& ch : model.get().channels)
            synthAdded = synthAdded || (ch.isSynth() && ch.synthPreset == "808-limpo");

        rep.check (synthAdded, "escolher o preset favorito no \"+\" cria o canal do WHYNOT Synth com ele"_t);

        host.removeAllChildren();
        host.setLookAndFeel (nullptr);
    }

    //==========================================================================
    rep.section ("Favoritos: estrelas nas listas (capturas sem janela)"_t);
    {
        theme::LookAndFeel laf;
        Stage host;
        host.setLookAndFeel (&laf);

        // --- navegador de plugins (F8)
        {
            host.setSize (272, 420);
            PluginBrowser plugins (app);
            host.addAndMakeVisible (plugins);
            plugins.setBounds (host.getLocalBounds());
            plugins.refresh();

            auto& list = plugins.getList();
            const int synthRow = plugins.rowOf (synthId), gainRow = plugins.rowOf (gainId);
            rep.check (synthRow >= 0 && gainRow >= 0, "os dois plugins de teste aparecem na lista"_t);

            // o clique na estrela marca e desmarca (o 20 é o meio da coluna da estrela, a partir do fim da linha)
            const bool clicked = clickRow (list, synthRow, 26);
            const bool offNow = ! favourites.contains (Kind::plugin, synthId);
            clickRow (list, synthRow, 26);
            rep.check (clicked && offNow && favourites.contains (Kind::plugin, synthId), "clicar na estrela do plugin desmarca e, de novo, marca"_t);

            clickRow (list, synthRow, 120);   // no nome: só escolhe a linha
            rep.check (favourites.contains (Kind::plugin, synthId), "clicar no nome do plugin não mexe na estrela"_t);

            const auto image = snapshot (host);
            const auto star = starCentre (host, list, gainRow, 26);
            rep.check (isOrange (image.getPixelAt (star.x, star.y)), "a estrela do favorito é cheia e laranja"_t);
            rep.check (savePng (image, ctx.work.getChildFile ("favoritos-plugins.png")), "captura: favoritos-plugins.png"_t);

            favourites.set (Kind::plugin, synthId, false);
            plugins.setFavouritesOnly (true);
            rep.check (plugins.rowOf (gainId) >= 0 && plugins.rowOf (synthId) < 0 && list.getListBoxModel()->getNumRows() == 2,
                       "o filtro Favoritos mostra só o plugin marcado (com o título da seção)"_t);
            favourites.set (Kind::plugin, synthId, true);
            rep.check (plugins.rowOf (synthId) >= 0, "marcar com o filtro ligado põe o plugin na lista na hora"_t);
            rep.check (savePng (snapshot (host), ctx.work.getChildFile ("favoritos-plugins-filtro.png")), "captura: favoritos-plugins-filtro.png"_t);
            host.removeAllChildren();
        }

        // --- navegador de sons (Alt+F8), no filtro Favoritos: os kits e os sons marcados
        {
            host.setSize (272, 420);
            SampleBrowser browser (app);
            host.addAndMakeVisible (browser);
            browser.setBounds (host.getLocalBounds());
            browser.setFavouritesOnly (true);

            auto& list = browser.getList();
            const int kitRow = browser.rowOf (kit), kickRow = browser.rowOf (kick), snareRow = browser.rowOf (snare);
            rep.check (kitRow >= 0 && kickRow > kitRow && snareRow > kickRow, "no filtro Favoritos: a pasta do kit e depois os sons, em ordem"_t);

            const auto image = snapshot (host);
            const auto star = starCentre (host, list, kickRow, 26);
            const auto kitStar = starCentre (host, list, kitRow, 26);
            rep.check (isOrange (image.getPixelAt (star.x, star.y)) && isOrange (image.getPixelAt (kitStar.x, kitStar.y)),
                       "som e kit favoritos com a estrela cheia e laranja"_t);
            rep.check (savePng (image, ctx.work.getChildFile ("favoritos-sons.png")), "captura: favoritos-sons.png"_t);

            // a estrela desmarca; no filtro, o som sai da lista
            clickRow (list, snareRow, 26);
            rep.check (! favourites.contains (Kind::sample, snare.getFullPathName()) && browser.rowOf (snare) < 0,
                       "clicar na estrela do som desmarca e, no filtro, ele sai da lista"_t);
            favourites.set (Kind::sample, snare.getFullPathName(), true);
            rep.check (browser.rowOf (snare) >= 0, "marcado de novo (pelo comando ou outra tela), volta na hora"_t);

            browser.setFavouritesOnly (false);
            rep.check (browser.rowOf (kick) < 0, "sem o filtro, volta a árvore das pastas"_t);
            host.removeAllChildren();
        }

        // --- presets do WHYNOT Synth
        {
            host.setSize (300, 520);
            synthui::SynthBrowser presets;
            host.addAndMakeVisible (presets);
            presets.setBounds (host.getLocalBounds().reduced (12));
            int chosen = 0;
            presets.onChoose = [&chosen] (const juce::String&) { ++chosen; };
            presets.setCurrent ("808-limpo");

            auto& list = presets.getList();
            const int row808 = presets.rowOf ("808-limpo");
            list.scrollToEnsureRowIsOnscreen (row808);
            const auto image = snapshot (host);
            const auto star = starCentre (host, list, row808, 23);
            rep.check (row808 >= 0 && isOrange (image.getPixelAt (star.x, star.y)), "o preset favorito tem a estrela cheia e laranja"_t);
            rep.check (savePng (image, ctx.work.getChildFile ("favoritos-synth.png")), "captura: favoritos-synth.png"_t);

            clickRow (list, row808, 23);
            rep.check (! favourites.contains (Kind::synthPreset, "808-limpo") && chosen == 0,
                       "clicar na estrela do preset escolhido desmarca sem recarregar o preset"_t);
            clickRow (list, row808, 23);
            rep.check (favourites.contains (Kind::synthPreset, "808-limpo"), "e marca de novo"_t);

            presets.setFavouritesOnly (true);
            rep.check (list.getListBoxModel()->getNumRows() == 2 && presets.rowOf ("keys-ep-fm") >= 0, "o filtro Favoritos do synth usa o mesmo arquivo"_t);
            host.removeAllChildren();
        }

        // --- itens de menu com estrela (o desenho do menu de efeitos)
        {
            MenuStage menuHost;
            menuHost.setLookAndFeel (&laf);
            menuHost.setSize (260, 4 * 30 + 10);

            auto effectName = [] (const char* type)
            {
                const auto* info = dsp::findEffect (type);
                return info != nullptr ? utf8 (info->name) : juce::String (type);
            };

            const auto* plate = dsp::findPreset ("reverb", "plate-vocal");
            favui::MenuItem favourite (effectName ("eq"), Kind::effect, "eq", false, true, true);
            favui::MenuItem plain (effectName ("comp"), Kind::effect, "comp", false, true, true);
            favui::MenuItem preset (effectName ("reverb") + ": " + (plate != nullptr ? utf8 (plate->name) : juce::String()),
                                    Kind::effectPreset, "reverb/plate-vocal", false, false, true);
            favui::MenuItem ticked ("Preset atual (marcado no menu)", Kind::effectPreset, "eq/nao-marcado", true, false, true);
            int y = 5;

            for (auto* item : { &favourite, &plain, &preset, &ticked })
            {
                menuHost.addAndMakeVisible (item);
                item->setBounds (5, y, menuHost.getWidth() - 10, 30);
                y += 30;
            }

            plain.setHighlighted (true);   // o mouse na linha: o contorno da estrela aparece
            const auto image = snapshot (menuHost);
            const auto starPoint = favourite.starArea().getCentre() + favourite.getPosition();
            rep.check (isOrange (image.getPixelAt (starPoint.x, starPoint.y)), "no menu, o favorito tem a estrela cheia e laranja"_t);
            rep.check (savePng (image, ctx.work.getChildFile ("favoritos-menu.png")), "captura: favoritos-menu.png"_t);

            // clicar na estrela do item marca e desmarca sem precisar de menu aberto (o menu não fecha)
            const auto at = plain.starArea().getCentre().toFloat();
            plain.mouseUp (mouseAt (plain, at));
            const bool marked = favourites.contains (Kind::effect, "comp");
            plain.mouseUp (mouseAt (plain, at));
            rep.check (marked && ! favourites.contains (Kind::effect, "comp"), "clicar na estrela de um item de menu marca e desmarca"_t);

            menuHost.removeAllChildren();
            menuHost.setLookAndFeel (nullptr);
        }

        host.setLookAndFeel (nullptr);
    }

    // de volta à pasta de dados do autoteste
    favourites.setFolder (ctx.data);
}

} // namespace whynot::selftest
