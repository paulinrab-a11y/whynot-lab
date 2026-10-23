#pragma once

// O registro do que a IA fez pela ponte (Fase 4, fatia 4.5; docs/fase-4-plano.md, seção 5.7): uma linha por
// pedido em <dados do app>\control\ia.log, com a hora, o método, o comando, o resultado, a duração e os
// parâmetros cortados em 200 caracteres. Nunca o token (ele só viaja no cabeçalho do aperto de mão).
// Com 1 MB, o arquivo vira ia.anterior.log e começa de novo. Escreve na thread de mensagem.

#include <JuceHeader.h>

namespace whynot::control
{

class ActivityLog
{
public:
    explicit ActivityLog (juce::File file);

    /** Uma linha: "2026-10-23 10:15:02  command.run  transport.setBpm  ok  12 ms  {"bpm":150}". */
    void write (const juce::String& method, const juce::String& command, const juce::String& outcome,
                double milliseconds, const juce::var& params = {});

    /** Um acontecimento da ponte, sem pedido: "conectou", "a pessoa desligou a IA"... */
    void event (const juce::String& what);

    const juce::File& getFile() const noexcept   { return file; }

    /** Para o autoteste: a linha como ela vai para o arquivo, sem a quebra. */
    static juce::String formatLine (juce::Time, const juce::String& method, const juce::String& command,
                                    const juce::String& outcome, double milliseconds, const juce::var& params);

    /** JSON compacto, numa linha, cortado em `maxChars` com "…". */
    static juce::String shortParams (const juce::var& params, int maxChars = 200);

    static constexpr juce::int64 rotateBytes = 1024 * 1024;

private:
    void append (const juce::String& line);

    juce::File file;
};

} // namespace whynot::control
