// Testes do miolo da paleta de comandos (src/ui/CommandSearch.h), sem JUCE: dobrar o texto, pontuar e ordenar.

#include "../../src/ui/CommandSearch.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace whynot::cmdsearch;

static int failures = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (! (cond)) { ++failures; std::printf ("  FALHOU: " __VA_ARGS__); std::printf ("\n"); } \
        else          { std::printf ("  ok: " __VA_ARGS__); std::printf ("\n"); } \
    } while (false)

// a posição do id na ordem (-1 = não entrou)
static int placeOf (const std::vector<Entry>& entries, const std::vector<int>& order, const char* id)
{
    for (std::size_t i = 0; i < order.size(); ++i)
        if (entries[(std::size_t) order[i]].id == id)
            return (int) i;

    return -1;
}

static std::string firstId (const std::vector<Entry>& entries, const std::vector<int>& order)
{
    return order.empty() ? std::string() : entries[(std::size_t) order[0]].id;
}

int main()
{
    std::printf ("Paleta de comandos: busca\n");

    // um recorte da lista de verdade (ações do KeyMap e comandos), na ordem em que a paleta monta
    const std::vector<Entry> entries {
        { "transport.togglePlay",  "Tocar ou parar (volta à agulha)" },
        { "transport.metronome",   "Metrônomo" },
        { "transport.tapTempo",    "Tap tempo: bata no ritmo para achar o BPM" },
        { "file.saveNewVersion",   "Salvar nova versão" },
        { "pianoroll.transpose",   "Transpor" },
        { "pattern.tempoExact",    "Tempo exato do padrão" },
        { "export.wav",            "Exportar WAV" },
        { "export.mp3",            "Exportar MP3" },
        { "export.midi",           "Exportar MIDI" },
        { "pattern.new",           "Próximo padrão vazio (cria um se não houver)" },
        { "pattern.next",          "Próximo padrão" },
        { "view.mixer",            "Mixer" },
        { "view.settings",         "Áudio e MIDI" },
        { "ui.zoomIn",             "Aumentar interface" },
        { "playlist.clear",        "Limpar playlist" },
        { "steps.clear",           "Limpar passos" },
    };

    //--------------------------------------------------------------------------
    // dobrar
    CHECK (fold ("Padrão") == "padrao" && fold ("PADRÃO") == "padrao", "fold tira o acento e a caixa");
    CHECK (fold ("Áudio e MIDI") == "audio e midi" && fold ("Metrônomo") == "metronomo" && fold ("ação") == "acao",
           "fold nas letras do português");
    CHECK (fold ("Padrão", false) == "Padrao" && fold ("ÁUDIO", false) == "AUDIO", "fold sem mexer na caixa");
    CHECK (fold ("a\xCC\x83o") == "ao", "acento solto (texto decomposto) some");
    CHECK (fold ("stra\xC3\x9F" "e") == "strasse" && fold ("\xC3\x86on") == "aeon", "eszett e ae viram duas letras");
    CHECK (fold ("5 \xC3\x97 3") == "5 \xC3\x97 3" && fold ("\xE2\x86\x92 ok") == "\xE2\x86\x92 ok", "o que não é letra acentuada passa igual");
    CHECK (fold ("ab\xC3") == "ab\xC3" && fold ("\xFFz") == "\xFFz", "UTF-8 quebrado não trava nem some");
    CHECK (fold ("").empty(), "texto vazio");

    {
        const auto f = prepare ("transport.togglePlay");
        CHECK (f.text == "transport.toggleplay" && f.starts[0] == 1 && f.starts[10] == 1 && f.starts[16] == 1
                   && f.starts[1] == 0 && f.starts[9] == 0 && f.starts[11] == 0,
               "começo de palavra: depois do ponto e na maiúscula do meio do id");
    }

    CHECK (tokens ("  Exportar   MP3 ") == (std::vector<std::string> { "exportar", "mp3" }) && tokens ("   ").empty(), "palavras da busca");

    //--------------------------------------------------------------------------
    // pontuar
    CHECK (scoreToken ("xyz", prepare ("Exportar WAV")) == detail::noMatch, "letras que não estão não casam");
    CHECK (scoreToken ("wave", prepare ("WAV")) == detail::noMatch, "busca maior que o texto não casa");
    CHECK (scoreToken ("exp", prepare ("Exportar WAV")) > scoreToken ("exp", prepare ("Reexportar")), "começo de palavra vale mais que o meio");
    CHECK (scoreToken ("port", prepare ("Exportar")) > scoreToken ("port", prepare ("Parar o transporte"))
               && scoreToken ("mix", prepare ("Mixer")) > scoreToken ("mix", prepare ("Medir o pico e o x")),
           "letras coladas valem mais que espalhadas");
    CHECK (scoreToken ("ew", prepare ("Exportar WAV")) > scoreToken ("ew", prepare ("Rever")), "iniciais das palavras (EW) casam bem");

    //--------------------------------------------------------------------------
    // ordenar
    {
        const auto order = rank (entries, "expo");
        const int wav = placeOf (entries, order, "export.wav"), transpose = placeOf (entries, order, "pianoroll.transpose");
        const int scattered = placeOf (entries, order, "pattern.tempoExact");
        CHECK (firstId (entries, order) == "export.wav", "\"expo\" acha Exportar WAV primeiro (veio %s)", firstId (entries, order).c_str());
        CHECK (wav == 0 && (transpose < 0 || transpose > wav), "\"expo\": Exportar WAV antes de Transpor (%d e %d)", wav, transpose);
        CHECK (scattered > placeOf (entries, order, "export.midi"), "\"expo\": as letras espalhadas (Tempo exato do padrão) ficam depois dos três Exportar");
    }

    {
        const auto a = rank (entries, "padrao"), b = rank (entries, "PADRÃO"), c = rank (entries, "Padrão");
        CHECK (! a.empty() && a == b && a == c, "acento e caixa ignorados na busca (%d achados)", (int) a.size());
        CHECK (firstId (entries, a) == "pattern.next", "\"padrao\": o título mais curto na frente (veio %s)", firstId (entries, a).c_str());
        CHECK (firstId (entries, rank (entries, "audio")) == "view.settings" && firstId (entries, rank (entries, "METRONOMO")) == "transport.metronome",
               "\"audio\" acha Áudio e MIDI; \"METRONOMO\" acha Metrônomo");
    }

    CHECK (firstId (entries, rank (entries, "bpm")) == "transport.tapTempo", "\"bpm\" acha o tap tempo");
    CHECK (firstId (entries, rank (entries, "mixer")) == "view.mixer", "\"mixer\" acha o Mixer");
    CHECK (firstId (entries, rank (entries, "zoomin")) == "ui.zoomIn" && firstId (entries, rank (entries, "view.mix")) == "view.mixer",
           "a busca também vale pelo id");
    CHECK (firstId (entries, rank (entries, "exportar mp3")) == "export.mp3" && rank (entries, "exportar mp3").size() == 1,
           "duas palavras: as duas têm de casar");
    CHECK (firstId (entries, rank (entries, "mp3 exportar")) == "export.mp3", "a ordem das palavras não importa");
    CHECK (rank (entries, "qqqq").empty(), "nada casa: lista vazia");

    {
        // empate: "Limpar playlist" e "Limpar passos" casam igual com "limpar" e têm títulos de tamanho parecido
        const std::vector<Entry> tie { { "b.one", "Limpar tudo" }, { "a.two", "Limpar tudo" }, { "c.three", "Limpar tudo" } };
        const auto order = rank (tie, "limpar");
        CHECK (order == (std::vector<int> { 0, 1, 2 }), "empate estável: fica a ordem em que veio");

        std::vector<Entry> reversed (tie.rbegin(), tie.rend());
        CHECK (rank (reversed, "limpar") == (std::vector<int> { 0, 1, 2 }), "empate estável com a lista ao contrário");
    }

    //--------------------------------------------------------------------------
    // recentes
    {
        std::vector<int> all;
        for (int i = 0; i < (int) entries.size(); ++i) all.push_back (i);

        CHECK (rank (entries, "") == all && rank (entries, "   ") == all, "vazio sem recentes: tudo, na ordem em que veio");

        std::vector<std::string> recent;
        touchRecent (recent, "export.mp3");
        touchRecent (recent, "view.mixer");
        touchRecent (recent, "export.mp3");   // de novo: sobe, sem repetir
        touchRecent (recent, "nao.existe");
        CHECK (recent == (std::vector<std::string> { "nao.existe", "export.mp3", "view.mixer" }), "recentes: o mais novo na frente, sem repetir");

        const auto order = rank (entries, "", recent);
        CHECK (order.size() == entries.size() && firstId (entries, order) == "export.mp3" && entries[(std::size_t) order[1]].id == "view.mixer"
                   && entries[(std::size_t) order[2]].id == "transport.togglePlay",
               "vazio devolve os recentes primeiro (o id que não existe some) e depois o resto");
        CHECK (placeOf (entries, order, "export.mp3") == 0 && std::count (order.begin(), order.end(), 7) == 1, "um recente não aparece duas vezes");
        CHECK (firstId (entries, rank (entries, "mixer", recent)) == "view.mixer" && rank (entries, "expo", recent) == rank (entries, "expo"),
               "com texto, quem manda é a busca, não os recentes");

        for (int i = 0; i < 20; ++i)
            touchRecent (recent, "id" + std::to_string (i));

        CHECK (recent.size() == 8 && recent.front() == "id19", "os recentes guardam os 8 últimos");
    }

    std::printf (failures == 0 ? "\nTudo certo.\n" : "\n%d falha(s).\n", failures);
    return failures == 0 ? 0 : 1;
}
