#include "IdeasPanel.h"

#include "Theme.h"
#include "../app/AppController.h"

namespace whynot
{

namespace
{
    juce::var args (std::initializer_list<std::pair<const char*, juce::var>> kv)
    {
        auto o = new juce::DynamicObject();

        for (auto& [k, v] : kv)
            o->setProperty (k, v);

        return juce::var (o);
    }

    juce::String scaleLabelPt (const juce::String& s)
    {
        if (s == "major")           return "maior";
        if (s == "phrygian")        return "frígio"_t;
        if (s == "dorian")          return "dórico"_t;
        if (s == "harmonicMinor")   return "menor harmônica"_t;
        if (s == "pentatonicMinor") return "pentatônica menor"_t;
        if (s == "chromatic")       return "cromática"_t;
        return "menor";
    }

    juce::String keyLabelPt (int root, const juce::String& scale)
    {
        static const char* sharps[] = { "Dó", "Dó#", "Ré", "Ré#", "Mi", "Fá", "Fá#", "Sol", "Sol#", "Lá", "Lá#", "Si" };
        return juce::String::fromUTF8 (sharps[((root % 12) + 12) % 12]) + " " + scaleLabelPt (scale);
    }

    const int barChoices[] = { 1, 2, 4, 8 };
}


//==============================================================================
class IdeasPanel::Body : public juce::Component
{
public:
    explicit Body (IdeasPanel& o) : owner (o)
    {
        prompt.setTextToShowWhenEmpty ("Descreva: trap triste em Fá menor"_t, theme::colour::textFaint);
        prompt.setFont (theme::font (theme::type::body));
        prompt.setIndents (12, 8);
        prompt.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        prompt.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        prompt.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        prompt.setColour (juce::TextEditor::textColourId, theme::colour::text);
        prompt.setTooltip ("Escreva o que quer, em português, e aperte Enter: gênero, clima, tom, parte e compassos. "
                           "Ex.: \"melodia de drill sombria em F#m\", \"808 de funk, 2 compassos\"."_t);
        prompt.onReturnKey = [this] { owner.applyPrompt(); };
        prompt.onEscapeKey = [this] { prompt.clear(); unfocusAllComponents(); };

        theme::styleComboBox (progression);
        progression.setTooltip ("Sequência de acordes, em graus. Automática = o gerador escolhe uma que combina com o gênero e o clima."_t);
        theme::styleComboBox (follow);
        follow.setTooltip ("Usa os acordes que outro canal já toca (ex.: o baixo segue o piano) em vez de uma progressão."_t);

        complexity.setTooltip ("Complexidade: sétimas e nonas nos acordes, notas de passagem. Duplo clique volta a 50%."_t);
        density.setTooltip ("Densidade: quantas notas. Duplo clique volta a 50%."_t);

        useSound.setTooltip ("Cria um canal do WHYNOT Synth com o som sugerido e gera esta parte nele"_t);
        analyse.setTooltip ("Lê as notas do canal aberto: acordes e o tom provável"_t);
        useKey.setTooltip ("Muda o tom do projeto para o tom detectado"_t);
        useKey.setEnabled (false);

        useSound.onClick = [this] { owner.createSoundChannel(); };
        analyse.onClick = [this] { owner.analyse(); };

        for (juce::Component* c : { (juce::Component*) &parts, (juce::Component*) &genres, (juce::Component*) &moods,
                                    (juce::Component*) &bars, (juce::Component*) &progression, (juce::Component*) &follow,
                                    (juce::Component*) &complexity, (juce::Component*) &density, (juce::Component*) &useSound,
                                    (juce::Component*) &analyse, (juce::Component*) &useKey })
        {
            // Clicar num controle não pode levar o teclado para o campo de pedido (o "filho
            // padrão" do painel): espaço, Ctrl+Z e os outros atalhos têm que continuar no app.
            c->setWantsKeyboardFocus (false);
            c->setMouseClickGrabsKeyboardFocus (false);
            addAndMakeVisible (c);
        }

        useSound.setVisible (false);
        setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (prompt);
    }

    int layoutFor (int width, bool apply)
    {
        const int pad = theme::metric::pad, w = width - 2 * pad, c = theme::metric::control;
        int y = pad;
        captions.clear();

        auto place = [&] (juce::Component& comp, int h, int x = 0, int cw = -1)
        {
            if (apply)
                comp.setBounds (pad + x, y, cw < 0 ? w : cw, h);
        };

        auto caption = [&] (const juce::String& text)
        {
            captions.push_back ({ text, y });
            y += 18;
        };

        titleY = y;
        y += 28;
        place (prompt, c);
        promptBox = juce::Rectangle<int> (pad, y, w, c);
        y += c + 4;
        understoodY = y;
        y += 30;

        caption ("PARTE");
        place (parts, parts.getHeightForWidth (w));
        y += parts.getHeightForWidth (w) + 12;
        caption ("GÊNERO"_t);
        place (genres, genres.getHeightForWidth (w));
        y += genres.getHeightForWidth (w) + 12;
        caption ("CLIMA");
        place (moods, moods.getHeightForWidth (w));
        y += moods.getHeightForWidth (w) + 12;
        caption ("PROGRESSÃO"_t);
        place (progression, c);
        y += c + 12;
        caption ("COMPASSOS");
        place (bars, bars.getHeightForWidth (w));
        y += bars.getHeightForWidth (w) + 10;
        place (complexity, 26);
        y += 28;
        place (density, 26);
        y += 26 + 12;
        caption ("SEGUIR ACORDES DE");
        place (follow, c);
        y += c + 16;

        resultY = y;
        y += 58;
        soundY = -1;

        if (useSound.isVisible())
        {
            caption ("SOM SUGERIDO");
            soundY = y;
            y += 20;
            place (useSound, c);
            y += c + 12;
        }

        dividerY = y;
        y += 12;

        const int half = (w - 6) / 2;
        place (analyse, c, 0, half);
        place (useKey, c, half + 6, w - half - 6);
        y += c + 8;
        analysisY = y;
        y += 58 + pad;
        return y;
    }

    void resized() override   { layoutFor (getWidth(), true); }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        const int pad = metric::pad, w = getWidth() - 2 * pad;

        g.setColour (colour::text);
        g.setFont (font (type::title, Weight::semibold));
        g.drawText ("Ideias", pad, titleY, w, 20, juce::Justification::centredLeft, false);

        g.setColour (colour::textDim);
        g.setFont (font (type::small));
        g.drawText (header, pad + 60, titleY, w - 60, 20, juce::Justification::centredRight, true);

        g.setColour (colour::raised);
        g.fillRoundedRectangle (promptBox.toFloat(), metric::radius);

        g.setColour (colour::textFaint);
        g.setFont (font (type::small));
        g.drawFittedText (understood.isNotEmpty() ? understood : keyLine, pad, understoodY, w, 28,
                          juce::Justification::topLeft, 2, 1.0f);

        g.setFont (captionFont());

        for (auto& [text, y] : captions)
            g.drawText (text, pad, y, w, 14, juce::Justification::centredLeft, false);

        g.setColour (resultText.isNotEmpty() ? colour::textDim : colour::textFaint);
        g.setFont (font (type::small));
        g.drawFittedText (resultText.isNotEmpty() ? resultText
                                                  : "Gerar escreve a parte no canal aberto. Troque o canal e a parte e gere de novo: tudo sai no mesmo tom, progressão e batida. Outra ideia sorteia acordes e batida novos."_t,
                          pad, resultY, w, 54, juce::Justification::topLeft, 3, 1.0f);

        if (soundY >= 0)
        {
            g.setColour (colour::text);
            g.drawText (soundText, pad, soundY, w, 16, juce::Justification::centredLeft, true);
        }

        g.setColour (colour::line);
        g.fillRect (pad, dividerY, w, 1);

        g.setColour (analysisText.isNotEmpty() ? colour::textDim : colour::textFaint);
        g.drawFittedText (analysisText.isNotEmpty() ? analysisText : "Analisar mostra os acordes e o tom das notas do canal aberto."_t,
                          pad, analysisY, w, 54, juce::Justification::topLeft, 3, 1.0f);
    }

    IdeasPanel& owner;
    juce::TextEditor prompt;
    ChoiceChips parts, genres, moods, bars;
    juce::ComboBox progression, follow;
    AmountSlider complexity { "Complexidade" }, density { "Densidade" };
    IconButton useSound { "Novo canal", Icon::plus, "Novo canal com este som" };
    IconButton analyse { "Analisar", Icon::search, "Analisar" };
    IconButton useKey { "Usar tom", Icon::check, "Usar tom" };

    juce::String header, keyLine, understood, resultText, analysisText, soundText;
    std::vector<std::pair<juce::String, int>> captions;
    juce::Rectangle<int> promptBox;
    int titleY = 0, understoodY = 0, resultY = 0, soundY = -1, dividerY = 0, analysisY = 0;
};

//==============================================================================
IdeasPanel::IdeasPanel (AppController& a) : app (a)
{
    body = std::make_unique<Body> (*this);

    partIds = { "chords", "melody", "arp", "bass", "808", "drums" };
    body->parts.setItems ({ "Acordes", "Melodia", "Arpejo", "Baixo", "808", "Bateria" });

    const auto options = app.run ("ideas.options");
    juce::StringArray genreNames, moodNames;

    if (auto* list = options.value["genres"].getArray())
        for (auto& gv : *list)
        {
            genreIds.add (gv["id"].toString());
            genreNames.add (gv["name"].toString());
        }

    if (auto* list = options.value["moods"].getArray())
        for (auto& mv : *list)
        {
            moodIds.add (mv["id"].toString());
            moodNames.add (mv["name"].toString());
        }

    body->genres.setItems (genreNames);
    body->moods.setItems (moodNames);
    body->bars.setItems ({ "1", "2", "4", "8" });
    body->bars.setSelected (2);

    body->genres.onChange = [this] (int) { reloadProgressions(); };
    body->moods.onChange = [this] (int) { reloadProgressions(); };
    body->useKey.onClick = [this]
    {
        if (detectedRoot < 0)
            return;

        app.run ("project.setKey", args ({ { "root", detectedRoot }, { "scale", detectedScale } }));
        status ("Tom do projeto: "_t + keyLabelPt (detectedRoot, detectedScale));
    };

    // Rodapé fixo, sempre à vista: uma ação principal (Gerar) e duas secundárias.
    generateButton.setTooltip ("Gerar esta parte na ideia atual: acordes, melodia, 808 e bateria saem no mesmo tom, "
                               "na mesma progressão e na mesma batida"_t);
    otherButton.setTooltip ("Outra ideia: sorteia outra progressão e outra batida e gera esta parte. "
                            "As partes já geradas não mudam: gere de novo para acompanhar."_t);
    varyButton.setTooltip ("Variar: outra versão só desta parte, com os mesmos acordes e a mesma batida"_t);
    generateButton.onClick = [this] { generate (Mode::same); };
    otherButton.onClick = [this] { generate (Mode::newIdea); };
    varyButton.onClick = [this] { generate (Mode::vary); };

    for (auto* b : { &generateButton, &otherButton, &varyButton })
    {
        b->setWantsKeyboardFocus (false);
        b->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    setMouseClickGrabsKeyboardFocus (false);
    viewport.setMouseClickGrabsKeyboardFocus (false);
    viewport.setViewedComponent (body.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (6);
    addAndMakeVisible (viewport);
    reloadProgressions();
}

IdeasPanel::~IdeasPanel()
{
    viewport.setViewedComponent (nullptr, false);
}

juce::String IdeasPanel::genreId() const   { return genreIds[body->genres.getSelected()]; }
juce::String IdeasPanel::moodId() const    { return moodIds[body->moods.getSelected()]; }
juce::String IdeasPanel::partId() const    { return partIds[body->parts.getSelected()]; }

void IdeasPanel::status (const juce::String& m, bool isError)
{
    if (onStatus)
        onStatus (m, isError);
}

void IdeasPanel::refresh (const Project& p, const juce::String& ch)
{
    channel = ch;
    const auto* c = p.findChannel (ch);
    channelName = c != nullptr ? c->name : juce::String();
    body->header = channelName.isNotEmpty() ? "no canal "_t + channelName : "abra um canal"_t;
    body->keyLine = "Tom do projeto: "_t + keyLabelPt (p.keyRoot, p.scale) + " (mude na barra do piano roll)"_t;

    juce::StringArray ids, names;

    for (auto& other : p.channels)
        if (! other.isAudio() && other.id != ch)
        {
            ids.add (other.id);
            names.add (other.name);
        }

    if (ids != followIds)
    {
        const auto keep = juce::isPositiveAndBelow (body->follow.getSelectedItemIndex() - 1, followIds.size())
                              ? followIds[body->follow.getSelectedItemIndex() - 1] : juce::String();
        followIds = ids;
        body->follow.clear (juce::dontSendNotification);
        body->follow.addItem ("Nenhum: usar a progressão"_t, 1);

        for (int i = 0; i < names.size(); ++i)
            body->follow.addItem (names[i], i + 2);

        body->follow.setSelectedItemIndex (keep.isNotEmpty() && followIds.contains (keep) ? followIds.indexOf (keep) + 1 : 0,
                                           juce::dontSendNotification);
    }

    const auto signature = juce::String (p.keyRoot) + p.scale;

    if (signature != lastKeySignature)
    {
        lastKeySignature = signature;
        reloadProgressions();
    }

    body->repaint();
}

void IdeasPanel::reloadProgressions()
{
    const auto previous = juce::isPositiveAndBelow (body->progression.getSelectedItemIndex() - 1, progressionTexts.size())
                              ? progressionTexts[body->progression.getSelectedItemIndex() - 1] : juce::String();
    const auto res = app.run ("ideas.progressions", args ({ { "genre", genreId() }, { "mood", moodId() } }));
    progressionTexts.clear();
    body->progression.clear (juce::dontSendNotification);
    body->progression.addItem ("Automática"_t, 1);

    if (auto* list = res.value["progressions"].getArray())
        for (auto& pv : *list)
        {
            progressionTexts.add (pv["text"].toString());
            body->progression.addItem (pv["text"].toString() + "  ·  "_t + pv["names"].toString(), progressionTexts.size() + 1);
        }

    const int keep = previous.isNotEmpty() ? progressionTexts.indexOf (previous) : -1;
    body->progression.setSelectedItemIndex (keep >= 0 ? keep + 1 : 0, juce::dontSendNotification);
}

void IdeasPanel::generate (Mode mode)
{
    const auto part = partId();

    if (part != "drums" && channel.isEmpty())
    {
        status ("Abra um canal de instrumento no piano roll para receber as notas."_t, true);
        return;
    }

    if (mode == Mode::newIdea || seed == 0)
    {
        seed = (uint32_t) juce::Random::getSystemRandom().nextInt (1 << 30) + 1;
        variation = 0;
    }
    else if (mode == Mode::vary)
    {
        variation = part == lastPart ? variation + 1 : 1;
    }
    else
    {
        variation = 0;
    }

    lastPart = part;

    auto* o = new juce::DynamicObject();
    o->setProperty ("part", part);
    o->setProperty ("genre", genreId());
    o->setProperty ("mood", moodId());
    o->setProperty ("bars", barChoices[juce::jlimit (0, 3, body->bars.getSelected())]);
    o->setProperty ("complexity", body->complexity.getValue());
    o->setProperty ("density", body->density.getValue());
    o->setProperty ("seed", (int) seed);
    o->setProperty ("variation", (int) variation);

    if (part != "drums")
        o->setProperty ("channel", channel);

    const int prog = body->progression.getSelectedItemIndex() - 1;

    if (juce::isPositiveAndBelow (prog, progressionTexts.size()))
        o->setProperty ("progression", progressionTexts[prog]);

    const int fol = body->follow.getSelectedItemIndex() - 1;

    if (part != "drums" && juce::isPositiveAndBelow (fol, followIds.size()))
        o->setProperty ("follow", followIds[fol]);

    const auto res = app.run ("ideas.generate", juce::var (o));

    if (! res.ok)
    {
        status (res.error, true);
        return;
    }

    auto summary = res.value["summary"].toString();

    if (auto* missing = res.value["missing"].getArray(); missing != nullptr && ! missing->isEmpty())
    {
        juce::StringArray roles;

        for (auto& m : *missing)
            roles.add (m.toString());

        summary << "\n" << "Sem canal para: "_t << roles.joinIntoString (", ")
                << ". Dê nomes como Kick, Caixa, Chimbal, Open hat, Perc."_t;
    }

    if ((bool) res.value["mono"])
        summary << "\n" << "Canal em mono para o 808 deslizar."_t;

    body->resultText = summary;
    suggestSound (part != "drums" ? res.value["sound"] : juce::var());

    if (part != "drums" && onGenerated)
        onGenerated();

    const auto prefix = mode == Mode::newIdea ? "Outra ideia: "_t : (mode == Mode::vary ? "Variação: "_t : "Gerado: ");
    status (prefix + res.value["summary"].toString());
}

void IdeasPanel::suggestSound (const juce::var& categories)
{
    // O WHYNOT Synth é outra frente: sem os comandos synth.* no app, o bloco fica escondido.
    soundId.clear();
    body->soundText.clear();
    const auto* cats = categories.getArray();

    if (synthAvailable && cats != nullptr)
    {
        for (auto& cat : *cats)
        {
            const auto res = app.run ("synth.listPresets", args ({ { "genre", genreId() }, { "category", cat } }));

            if (! res.ok)
            {
                synthAvailable = false;
                break;
            }

            const auto* presets = res.value["presets"].getArray();

            if (presets == nullptr || presets->isEmpty())
                continue;

            // A ideia escolhe entre os primeiros do gênero: outra ideia, outro som.
            const auto& p = presets->getReference ((int) (seed % (uint32_t) juce::jmin (6, presets->size())));
            soundId = p["id"].toString();
            body->soundText = p["name"].toString() + "  ·  "_t + p["categoryName"].toString();
            break;
        }
    }

    const bool show = soundId.isNotEmpty();

    if (show != body->useSound.isVisible())
    {
        body->useSound.setVisible (show);
        resized();
    }

    body->repaint();
}

void IdeasPanel::createSoundChannel()
{
    if (soundId.isEmpty())
        return;

    // Canal novo e as notas nele: um passo só de desfazer.
    CommandRegistry::ScopedGesture gesture (app.getCommands(), "Novo canal com o som sugerido"_t);
    const auto res = app.run ("channel.addSynth", args ({ { "preset", soundId } }));

    if (! res.ok)
    {
        status (res.error, true);
        return;
    }

    channel = res.value["channel"].toString();

    if (onChannelCreated)
        onChannelCreated (channel);

    generate (Mode::same);
}

void IdeasPanel::applyPrompt()
{
    const auto text = body->prompt.getText().trim();

    if (text.isEmpty())
        return;

    const auto res = app.run ("ideas.parse", args ({ { "text", text } }));
    const auto& v = res.value;
    juce::StringArray understood;

    if (auto* list = v["understood"].getArray())
        for (auto& u : *list)
            understood.add (u.toString());

    if (understood.isEmpty())
    {
        body->understood = "Não entendi. Tente: \"melodia de trap triste em Fá menor\" ou \"808 de drill, 2 compassos\"."_t;
        body->repaint();
        status ("Não entendi o pedido."_t, true);
        return;
    }

    if (v.hasProperty ("genre"))  body->genres.setSelected (genreIds.indexOf (v["genre"].toString()));
    if (v.hasProperty ("mood"))   body->moods.setSelected (moodIds.indexOf (v["mood"].toString()));
    if (v.hasProperty ("part"))   body->parts.setSelected (partIds.indexOf (v["part"].toString()));
    if (v.hasProperty ("complexity")) body->complexity.setValue ((double) v["complexity"]);
    if (v.hasProperty ("density"))    body->density.setValue ((double) v["density"]);

    if (v.hasProperty ("bars"))
    {
        const int wanted = (int) v["bars"];
        int best = 0;

        for (int i = 0; i < 4; ++i)
            if (std::abs (barChoices[i] - wanted) < std::abs (barChoices[best] - wanted))
                best = i;

        body->bars.setSelected (best);
    }

    if (v.hasProperty ("root"))
    {
        auto* key = new juce::DynamicObject();
        key->setProperty ("root", v["root"]);

        if (v.hasProperty ("scale"))
            key->setProperty ("scale", v["scale"]);

        app.run ("project.setKey", juce::var (key));
    }

    if (v.hasProperty ("bpm"))
        app.run ("transport.setBpm", args ({ { "bpm", v["bpm"] } }));

    body->understood = "Entendi: "_t + understood.joinIntoString (" · "_t);
    reloadProgressions();
    unfocusAllComponents();
    generate (Mode::newIdea);
}

void IdeasPanel::analyse()
{
    if (channel.isEmpty())
    {
        status ("Abra um canal no piano roll para analisar."_t, true);
        return;
    }

    const auto res = app.run ("ideas.detectChords", args ({ { "channel", channel } }));

    if (! res.ok)
    {
        body->analysisText = res.error;
        body->useKey.setEnabled (false);
        detectedRoot = -1;
        body->repaint();
        status (res.error, true);
        return;
    }

    detectedRoot = (int) res.value["root"];
    detectedScale = res.value["scale"].toString();
    body->analysisText = "Acordes: "_t + res.value["chordNames"].toString() + "\n" + "Tom provável: "_t + res.value["key"].toString();
    body->useKey.setEnabled (true);
    body->repaint();
    status (channelName + ": " + res.value["chordNames"].toString() + " (tom provável: "_t + res.value["key"].toString() + ")");
}

void IdeasPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::colour::panel);
    g.setColour (theme::colour::line);
    g.fillRect (0, 0, 1, getHeight());
    g.fillRect (1, getHeight() - footerHeight, getWidth() - 1, 1);   // fio em cima do rodapé
}

void IdeasPanel::resized()
{
    auto area = getLocalBounds().withTrimmedLeft (1);
    auto footer = area.removeFromBottom (footerHeight)
                      .reduced (theme::metric::pad, (footerHeight - theme::metric::control) / 2);
    // os dois secundários com o texto inteiro (sem toda a folga do ghost); o Gerar fica com o resto
    const int gap = 4, c = footer.getHeight();
    varyButton.setBounds (footer.removeFromRight (varyButton.getIdealWidth (c) - 8));
    footer.removeFromRight (gap);
    otherButton.setBounds (footer.removeFromRight (otherButton.getIdealWidth (c) - 8));
    footer.removeFromRight (gap);
    generateButton.setBounds (footer);

    viewport.setBounds (area);
    const int w = viewport.getWidth() - (viewport.getHeight() < body->layoutFor (viewport.getWidth(), false) ? viewport.getScrollBarThickness() : 0);
    body->setSize (w, body->layoutFor (w, false));
}

} // namespace whynot
