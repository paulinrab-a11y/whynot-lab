#pragma once

#include <JuceHeader.h>

#include "Controls.h"
#include "../model/Project.h"

namespace whynot
{

class AppController;

/** WHYNOT Ideias no piano roll: descreve ou escolhe gênero, clima e parte, e o gerador escreve
    acordes, melodia, arpejo, baixo, 808 (no canal aberto) ou bateria (nos canais do kit).
    "Gerar" usa a ideia atual (as partes combinam: mesmo tom, progressão e batida);
    "Outra ideia" sorteia outra progressão e batida; "Variar" muda só esta parte.
    Com o WHYNOT Synth no app, sugere um som para a parte e cria o canal com ele.
    Tudo passa pelos comandos ideas.* (e synth.* para o som). */
class IdeasPanel : public juce::Component
{
public:
    explicit IdeasPanel (AppController&);
    ~IdeasPanel() override;

    void refresh (const Project&, const juce::String& channel);

    std::function<void (const juce::String&, bool isError)> onStatus;
    std::function<void()> onGenerated;                            // notas novas no canal: o piano roll rola até elas
    std::function<void (const juce::String& channel)> onChannelCreated;   // canal novo com o som sugerido

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int idealWidth = 300, footerHeight = 48;

private:
    enum class Mode { newIdea, same, vary };

    class Body;

    void generate (Mode);
    void applyPrompt();
    void analyse();
    void reloadProgressions();
    void suggestSound (const juce::var& categories);
    void createSoundChannel();
    void status (const juce::String&, bool isError = false);

    juce::String genreId() const;
    juce::String moodId() const;
    juce::String partId() const;

    AppController& app;
    juce::String channel, channelName;
    juce::StringArray genreIds, moodIds, partIds, progressionTexts, followIds;
    juce::String lastKeySignature;
    uint32_t seed = 0, variation = 0;
    juce::String lastPart;
    int detectedRoot = -1;
    juce::String detectedScale;
    bool synthAvailable = true;            // vira false se o app não tem o WHYNOT Synth
    juce::String soundId;

    std::unique_ptr<Body> body;
    juce::Viewport viewport;
    IconButton generateButton { "Gerar", Icon::sparkle, "Gerar", IconButton::Style::filled };
    IconButton otherButton { "Outra ideia", Icon::plus, "Outra ideia" };
    IconButton varyButton { "Variar", Icon::loop, "Variar" };
};

} // namespace whynot
