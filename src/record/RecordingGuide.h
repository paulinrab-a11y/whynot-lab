#pragma once

// Assistente de gravação (gravar voz, fatia 7; pedido do usuário de 21/10, 16:44): reconhece a placa pelo nome que
// o driver informa e, com o tipo de microfone, diz os ajustes. Sem JUCE (testado em tests/record).
//
// As marcas aparecem só aqui, para reconhecer o nome que a placa informa. A tela mostra o nome que o driver dá,
// nunca um nome nosso de produto (regra do projeto: nomes comerciais não entram nos textos da interface).

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace whynot::guide
{

/** O que é a placa escolhida. */
enum class Kind
{
    unknown,        // não reconhecida: os conselhos gerais valem
    usbInterface,   // interface de áudio USB com entrada XLR (a placa do estúdio caseiro)
    usbMic,         // microfone USB: ele mesmo é a placa
    asioWrapper,    // um ASIO genérico por cima de outra placa (melhor o ASIO da própria placa, se houver)
    builtIn,        // a placa do computador (a entrada de microfone do notebook)
};

struct Device
{
    Kind kind = Kind::unknown;
    bool directMonitor = false;   // a placa devolve a voz no fone sozinha (botão ou knob de monitoração direta)
    bool phantom = false;         // tem 48V para microfone condensador
    int inputs = 0;               // entradas de microfone, quando se sabe (0 = não sei)
};

enum class Mic
{
    unknown,
    condenserXlr,   // condensador com cabo XLR: precisa de 48V
    dynamicXlr,     // dinâmico com cabo XLR: sem 48V, pede mais ganho
    usb,            // microfone USB: é a própria placa
};

namespace detail
{
    inline std::string lower (const std::string& s)
    {
        std::string out;

        for (char c : s)
            out += (char) std::tolower ((unsigned char) c);

        return out;
    }

    inline bool hasAny (const std::string& text, std::initializer_list<const char*> words)
    {
        return std::any_of (words.begin(), words.end(), [&text] (const char* w) { return text.find (w) != std::string::npos; });
    }
}

/** Reconhece a placa pelo tipo do driver ("ASIO", "Windows Audio"...) e pelo nome do aparelho. */
inline Device recognise (const std::string& driverType, const std::string& deviceName)
{
    using detail::hasAny;
    const auto name = detail::lower (deviceName);
    const auto type = detail::lower (driverType);
    Device d;

    if (hasAny (name, { "asio4all", "flexasio", "generic low latency", "asio generic" }))
    {
        d.kind = Kind::asioWrapper;
        return d;
    }

    // microfones USB: aparecem como placa própria, com o nome do microfone
    if (hasAny (name, { "at2020usb", "at2020 usb", "atr2100", "fifine", "yeti", "quadcast", "hyperx", "q2u", "nt-usb", "podmic usb",
                        "usb pnp", "usb microphone", "microfone usb", "usb mic", "snowball", "razer seiren", "elgato wave" }))
    {
        d.kind = Kind::usbMic;
        d.directMonitor = hasAny (name, { "at2020usb", "yeti", "quadcast", "nt-usb", "q2u", "elgato wave" });   // têm saída de fone
        d.inputs = 1;
        return d;
    }

    struct Family { std::initializer_list<const char*> names; bool directMonitor; int inputs; };
    const Family interfaces[] {
        // os nomes específicos antes dos da marca: o primeiro que bate vale
        { { "scarlett solo" }, true, 1 },
        { { "scarlett 2i2", "2i2" }, true, 2 },
        { { "scarlett 4i4", "4i4" }, true, 2 },
        { { "focusrite", "scarlett" }, true, 0 },
        { { "umc22", "umc 22" }, false, 1 },                      // a menor não tem knob de monitoração direta
        { { "umc202", "umc204", "umc404", "umc1820", "umc asio", "umc" }, true, 2 },
        { { "u-phoria", "uphoria", "behringer" }, true, 2 },
        { { "m-audio", "m-track", "air 192", "air192" }, true, 2 },
        { { "presonus", "audiobox", "studio 24c", "studio 26c" }, true, 2 },
        { { "minifuse", "arturia" }, true, 2 },
        { { "steinberg", "yamaha steinberg", "ur22", "ur12", "ur-c" }, true, 2 },
        { { "motu m2", "motu m4", "motu" }, true, 2 },
        { { "audient", "evo 4", "evo4", "id4", "id14" }, true, 2 },
        { { "ssl 2", "ssl2" }, true, 2 },
        { { "zoom uac", "tascam", "us-1x2", "roland rubix", "rubix" }, true, 2 },
    };

    for (auto& f : interfaces)
    {
        if (hasAny (name, f.names))
        {
            d.kind = Kind::usbInterface;
            d.directMonitor = f.directMonitor;
            d.phantom = true;
            d.inputs = f.inputs;
            return d;
        }
    }

    if (hasAny (name, { "realtek", "high definition audio", "conexant", "intel smart sound", "microfone (", "microphone array",
                        "conjunto de microfones", "fone de ouvido", "headset", "alto-falantes", "speakers (" })
        || (type.find ("asio") == std::string::npos && hasAny (name, { "microfone", "microphone" })))
    {
        d.kind = Kind::builtIn;
        return d;
    }

    return d;
}

/** Os ajustes recomendados. */
struct Advice
{
    bool preferAsio = true;       // escolher o driver ASIO da própria placa (no Windows)
    int bufferMin = 128, bufferMax = 256;
    double sampleRate = 48000.0;  // 44,1 kHz também serve; 48 é o padrão do vídeo e do celular
    int input = 1;                // a entrada do microfone (1 = a primeira)
    bool mono = true;             // a voz é mono: estéreo tocaria só num lado
    bool needsPhantom = false;    // ligar o 48V na placa
    bool moreGain = false;        // microfone dinâmico: precisa de mais ganho na placa
    bool warnDirectMonitor = false;   // a placa também devolve a voz: escolher uma das duas monitorações
    std::vector<std::string> steps;   // os passos, em português, para a tela e a IA
};

/** Só a placa do computador pelo nome que não deixa dúvida (o chip da placa-mãe, o conjunto de microfones do
    notebook). O recognise também chama de placa do computador o "Microfone (...)" genérico, que no Windows pode ser
    uma interface sem marca no nome: bom para os passos, mas não para desligar a escuta (gravar voz, fatia 8). */
inline bool surelyBuiltIn (const std::string& deviceName)
{
    return detail::hasAny (detail::lower (deviceName), { "realtek", "high definition audio", "conexant", "intel smart sound",
                                                          "microphone array", "conjunto de microfones" });
}

inline Advice advise (const Device& d, Mic mic, bool appMonitoring)
{
    Advice a;

    switch (d.kind)
    {
        case Kind::usbInterface:
            a.steps.push_back ("Driver: o ASIO da própria placa (instale o driver do fabricante, se ainda não tem).");
            break;
        case Kind::usbMic:
            a.preferAsio = false;
            a.steps.push_back ("O microfone USB é a própria placa: no Windows Audio (WASAPI), escolha ele em \"Microfone (aparelho)\", aqui na Gravação.");
            break;
        case Kind::asioWrapper:
            a.steps.push_back ("Este ASIO genérico funciona, mas o ASIO da própria placa costuma ter menos atraso. Se a placa tem um, prefira ele.");
            break;
        case Kind::builtIn:
            a.preferAsio = false;
            a.steps.push_back ("Esta é a placa do computador: dá para gravar, mas com mais ruído e atraso. Uma interface USB ou um microfone USB resolvem.");
            break;
        case Kind::unknown:
        default:
            a.steps.push_back ("Driver: ASIO, se a placa tiver; senão, o Windows Audio (WASAPI).");
            break;
    }

    a.steps.push_back ("Buffer de 128 a 256 amostras, em 48 kHz (44,1 também serve). Se estalar, suba o buffer.");

    if (mic == Mic::usb && d.kind != Kind::usbMic)
        a.steps.push_back ("Microfone USB: ele aparece como um aparelho à parte. No Windows Audio, escolha ele em \"Microfone (aparelho)\", aqui na Gravação.");

    if (mic == Mic::condenserXlr)
    {
        a.needsPhantom = true;
        a.steps.push_back ("Condensador: ligue o 48V (phantom) na placa, com o volume baixo, e espere uns segundos.");
    }

    if (mic == Mic::dynamicXlr)
    {
        a.moreGain = true;
        a.steps.push_back ("Dinâmico: deixe o 48V desligado e suba bem o ganho da placa (ele pede mais que um condensador).");
    }

    a.steps.push_back ("Entrada 1, em mono: a voz é mono; um par estéreo a deixaria num lado só.");
    a.steps.push_back ("Ganho: cante o trecho mais alto e ajuste até o pico ficar entre -18 e -10 dBFS, sem passar de -6.");

    if (d.directMonitor && appMonitoring)
    {
        a.warnDirectMonitor = true;
        a.steps.push_back ("A placa tem monitoração direta, e o app também está devolvendo a voz: use uma só, senão vem eco.");
    }

    return a;
}

/** O nível da voz para o medidor com alvo: o pico de uma frase, em dBFS. */
enum class Level { silent, low, good, hot, clip };

inline Level judge (double peakDb)
{
    if (peakDb < -50.0)  return Level::silent;   // nada chega: entrada errada, 48V desligado, cabo
    if (peakDb < -18.0)  return Level::low;      // baixo: suba o ganho na placa
    if (peakDb <= -10.0) return Level::good;     // o alvo
    if (peakDb < -1.0)   return Level::hot;      // alto: sobra pouco para o grito
    return Level::clip;                          // estourou: abaixe o ganho na placa (o fader do app não conserta)
}

} // namespace whynot::guide
