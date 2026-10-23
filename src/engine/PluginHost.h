#pragma once

#include <JuceHeader.h>

#include "../model/Project.h"

namespace whynot
{

namespace te = tracktion;

class PluginCatalog;

/** Ponto único para criar e ler plugins externos (VST3) a partir do modelo.
    Usado pelo projetor para o instrumento de um canal e, pelos inserts do mixer, para o tipo "vst3".
    Thread de mensagem.
*/
namespace plugin_host
{
    PluginRef refFrom (const juce::PluginDescription&);
    juce::PluginDescription descriptionOf (const PluginRef&);

    /** ExternalPlugin do Tracktion com o estado salvo já restaurado. Não insere em nenhuma trilha. */
    te::Plugin::Ptr create (te::Edit&, const PluginRef&);

    /** A instância carregada, ou nullptr (plugin ausente, bloqueado ou com erro). */
    juce::AudioPluginInstance* instanceOf (te::Plugin*);

    /** Estado atual do plugin vivo, em base64; vazio se não carregou. */
    juce::String readState (te::Plugin&);

    /** "" se o plugin está tocando; senão o motivo, em português, para mostrar ao usuário. */
    juce::String problemFor (te::Plugin*, const PluginRef&, const PluginCatalog*);
}

} // namespace whynot
