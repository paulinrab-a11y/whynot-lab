// Testes da política do autosave e dos suspeitos de queda (src/app/AutosavePolicy.h), sem JUCE:
// adiar no gesto, na gravação e no export; o teto de adiamentos; o conjunto de plugins sujos;
// o registro de janela aberta e de plugins adicionados; a mensagem da abertura depois de uma queda.

#include "../../src/app/AutosavePolicy.h"

#include <cstdio>

using namespace whynot::autosave;

static int failures = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (! (cond)) { ++failures; std::printf ("  FALHOU: " __VA_ARGS__); std::printf ("\n"); } \
        else          { std::printf ("  ok: " __VA_ARGS__); std::printf ("\n"); } \
    } while (false)

static bool contains (const std::string& text, const std::string& part)   { return text.find (part) != std::string::npos; }

/** Roda n ticks e conta o que saiu. */
struct Tally { int waits = 0, saves = 0, lights = 0; };

static Tally run (Schedule& s, const Busy& busy, int n)
{
    Tally t;

    for (int i = 0; i < n; ++i)
    {
        switch (s.tick (busy))
        {
            case Action::wait:       ++t.waits; break;
            case Action::save:       ++t.saves; break;
            case Action::saveLight:  ++t.lights; break;
        }
    }

    return t;
}

static void testSchedule()
{
    std::printf ("Relogio do autosave\n");

    {
        Schedule s;
        CHECK (s.intervalTicks == 1200 && s.retryTicks == 30 && s.maxDeferrals == 10, "padrao: 2 min, nova tentativa em 3 s, teto de 10 adiamentos");

        const auto before = run (s, {}, 1199);
        CHECK (before.saves == 0 && before.lights == 0, "livre: nada antes dos 2 minutos");
        CHECK (s.tick ({}) == Action::save, "livre: salva no tick 1200");
        const auto next = run (s, {}, 1200);
        CHECK (next.saves == 1 && next.waits == 1199, "e de novo 2 minutos depois");
    }

    {
        // gesto aberto na hora: adia; fechou, salva na tentativa seguinte
        Schedule s (100, 30, 10);
        Busy gesture;
        gesture.gesture = true;

        const auto due = run (s, gesture, 100);
        CHECK (due.saves == 0 && s.deferrals == 1, "gesto aberto quando vence: adia (1 adiamento)");

        const auto waiting = run (s, {}, 29);
        CHECK (waiting.saves == 0, "gesto fechou: espera a nova tentativa (3 s), nao salva no tick seguinte");
        CHECK (s.tick ({}) == Action::save && s.deferrals == 0, "na nova tentativa, livre: salva e zera os adiamentos");
        CHECK (s.ticksLeft == 100, "o intervalo recomeca do zero");
    }

    {
        // gravando: mesma coisa
        Schedule s (50, 10, 3);
        Busy rec;
        rec.recording = true;
        run (s, rec, 50);
        CHECK (s.deferrals == 1, "gravando quando vence: adia");
        Busy exp;
        exp.exporting = true;
        run (s, exp, 10);
        CHECK (s.deferrals == 2, "exportando na nova tentativa: adia de novo");
        run (s, {}, 9);
        CHECK (s.tick ({}) == Action::save, "livre na terceira: salva");
    }

    {
        // teto com gesto: nunca salva no meio do gesto, e salva no primeiro tick depois que ele fecha
        Schedule s (50, 10, 3);
        Busy gesture;
        gesture.gesture = true;

        const auto t = run (s, gesture, 50 + 3 * 10 + 500);
        CHECK (t.saves == 0 && t.lights == 0, "gesto que nao acaba: nenhum autosave no meio dele");
        CHECK (s.deferrals == 3 && s.atLimit(), "os adiamentos param no teto (3)");
        CHECK (s.tick ({}) == Action::save, "teto atingido: salva no primeiro tick depois que o gesto fecha");
        CHECK (s.deferrals == 0 && s.ticksLeft == 50, "e o relogio volta ao normal");
    }

    {
        // teto com gravação longa: salva o modelo sem ler os plugins, uma vez por intervalo
        Schedule s (50, 10, 3);
        Busy rec;
        rec.recording = true;

        const auto first = run (s, rec, 50 + 3 * 10 - 1);
        CHECK (first.saves == 0 && first.lights == 0 && s.atLimit(), "gravacao longa: adia ate o teto");
        CHECK (s.tick (rec) == Action::saveLight, "passou do teto ainda gravando: salva sem ler os plugins");
        const auto second = run (s, rec, 50 + 3 * 10 - 1);
        CHECK (second.lights == 0 && second.saves == 0, "depois disso, um intervalo inteiro e os adiamentos de novo");
        CHECK (s.tick (rec) == Action::saveLight, "e outro salvar leve");
    }

    {
        // teto com gesto E gravação: o gesto manda (espera fechar); fechou e ainda grava, salvar leve
        Schedule s (20, 5, 2);
        Busy both;
        both.gesture = both.recording = true;
        const auto t = run (s, both, 20 + 2 * 5 + 100);
        CHECK (t.saves == 0 && t.lights == 0, "gesto durante a gravacao, no teto: espera o gesto");
        Busy rec;
        rec.recording = true;
        CHECK (s.tick (rec) == Action::saveLight, "gesto fechou, gravacao continua: salvar leve na hora");
    }

    {
        Schedule s (1000, 30, 10);
        s.makeDue();
        CHECK (s.tick ({}) == Action::save, "makeDue faz vencer no proximo tick");
    }
}

static void testDirty()
{
    std::printf ("Plugins sujos\n");

    DirtyPlugins d;
    const auto a = DirtyPlugins::channelKey ("ch1"), b = DirtyPlugins::insertKey ("m2", "ins3");

    CHECK (a == "c:ch1" && b == "i:m2/ins3", "chaves de canal e de insert");
    CHECK (d.isEmpty() && ! d.shouldCapture (a, false, false), "limpo: plugin sem janela e ja lido nao e relido");
    CHECK (d.shouldCapture (a, true, false), "janela aberta agora: le");
    CHECK (d.shouldCapture (a, false, true), "nunca lido (sem estado no modelo): le");

    d.mark (a);
    CHECK (d.shouldCapture (a, false, false) && ! d.shouldCapture (b, false, false), "marcado: le so ele");

    d.captured (a, true);
    CHECK (d.isMarked (a), "lido com a janela aberta: continua sujo");
    d.captured (a, false);
    CHECK (! d.isMarked (a) && d.isEmpty(), "lido com a janela fechada: limpo");

    d.markAll();
    CHECK (d.isAll() && d.shouldCapture (a, false, false) && d.shouldCapture (b, false, false), "na duvida (markAll): le todos");
    d.captured (a, false);
    CHECK (d.isMarked (b), "ler um nao limpa o 'todos'");
    d.allCaptured();
    CHECK (d.isEmpty(), "fim da leitura parcial com 'todos': limpo");

    d.mark (a);
    d.markAll();
    d.clear();
    CHECK (d.isEmpty() && ! d.isMarked (a), "leitura completa (salvar) limpa tudo");
}

static void testSuspects()
{
    std::printf ("Suspeitos de uma queda\n");

    SuspectLog log;
    CHECK (log.isEmpty() && log.toText().empty(), "vazio: nada a gravar");
    CHECK (describeCrash (log).status.empty() && describeCrash (log).suspect.file.empty(), "vazio: nenhum aviso");

    log.windowOpened ("C:\\VST3\\Serum Falso.vst3", "Sintetizador Ação");
    CHECK (log.hasWindow ("C:\\VST3\\Serum Falso.vst3") && log.windows.size() == 1, "janela aberta entra no registro");
    log.windowOpened ("C:\\VST3\\Serum Falso.vst3", "Sintetizador Ação");
    CHECK (log.windows.size() == 1, "abrir de novo a mesma janela nao duplica");

    const auto text = log.toText();
    CHECK (text == "janela\tC:\\VST3\\Serum Falso.vst3\tSintetizador Ação\n", "formato da linha de janela");

    const auto back = SuspectLog::fromText (text);
    CHECK (back.windows == log.windows && back.recent.empty(), "ida e volta pelo texto");

    {
        const auto m = describeCrash (back);
        CHECK (contains (m.status, "com a janela do plugin Sintetizador Ação aberta"), "aviso cita o plugin da janela: %s", m.status.c_str());
        CHECK (contains (m.status, "Nada foi bloqueado"), "e diz que nada foi bloqueado");
        CHECK (m.suspect.file == "C:\\VST3\\Serum Falso.vst3" && contains (m.question, "Bloquear o Sintetizador Ação agora?"),
               "oferece bloquear o da janela");
    }

    CHECK (log.windowClosed ("C:\\VST3\\Serum Falso.vst3") && log.isEmpty(), "fechar a janela tira do registro");
    CHECK (! log.windowClosed ("C:\\VST3\\Serum Falso.vst3"), "fechar duas vezes nao faz nada");

    // últimos adicionados: até 5, sem repetir, o mais novo por último
    for (int i = 1; i <= 7; ++i)
        log.inserted ("C:\\VST3\\P" + std::to_string (i) + ".vst3", "P" + std::to_string (i), "14:0" + std::to_string (i));

    CHECK (log.recent.size() == SuspectLog::maxRecent && log.recent.front().name == "P3" && log.recent.back().name == "P7",
           "guarda os 5 ultimos adicionados");
    log.inserted ("C:\\VST3\\P4.vst3", "P4", "14:09");
    CHECK (log.recent.size() == 5 && log.recent.back().name == "P4" && log.recent.back().when == "14:09", "adicionar de novo move para o fim, sem repetir");

    {
        const auto m = describeCrash (SuspectLog::fromText (log.toText()));
        CHECK (contains (m.status, "Últimos plugins adicionados: P4 (14:09), P7 (14:07), P6 (14:06), P5 (14:05) e P3 (14:03)."),
               "sem janela: lista os adicionados, o mais novo primeiro: %s", m.status.c_str());
        CHECK (! contains (m.status, "janela"), "e nao fala de janela");
        CHECK (m.suspect.name == "P4", "oferece bloquear o mais novo");
    }

    log.windowOpened ("C:\\VST3\\A.vst3", "Alfa");
    log.windowOpened ("C:\\VST3\\B.vst3", "Beta");

    {
        const auto m = describeCrash (log);
        CHECK (contains (m.status, "com as janelas dos plugins Alfa e Beta abertas"), "duas janelas: cita as duas");
        CHECK (m.suspect.name == "Beta", "suspeito: a janela aberta por ultimo");

        const auto blamed = describeCrash (log, "p6.VST3");
        CHECK (blamed.suspect.name == "P6" && contains (blamed.status, "O relatório da queda aponta o P6."),
               "modulo do relatorio da queda passa na frente da janela");

        const auto unknown = describeCrash (log, "ntdll.dll");
        CHECK (unknown.suspect.name == "Beta" && ! contains (unknown.status, "relatório"), "modulo que nao e de plugin anotado: ignora");
    }

    {
        // arquivo cortado pela queda ou com lixo: aproveita o que der, sem cair
        const auto broken = SuspectLog::fromText ("janela\tC:\\x\\Só Arquivo.vst3\t\r\nlixo\n\ninserido\t10:00\njanela\t\tSem arquivo\ninserido\t11:11\tC:\\y.vst3\tYpsilon\njanela\tC:\\cortad");
        CHECK (broken.windows.size() == 1 && broken.windows[0].name == "Só Arquivo.vst3", "linha sem nome usa o nome do arquivo");
        CHECK (broken.recent.size() == 1 && broken.recent[0].name == "Ypsilon" && broken.recent[0].when == "11:11", "linhas estranhas sao ignoradas");
    }

    {
        // tab e quebra de linha dentro de um nome não quebram o formato
        SuspectLog odd;
        odd.windowOpened ("C:\\a\tb.vst3", "Nome\ncom quebra");
        const auto again = SuspectLog::fromText (odd.toText());
        CHECK (again.windows.size() == 1 && again.windows[0].name == "Nome com quebra", "tab e quebra de linha viram espaco");
    }

    {
        SuspectLog one, two;
        one.windowOpened ("C:\\a.vst3", "A");
        two.inserted ("C:\\b.vst3", "B", "09:00");
        one.merge (two);
        CHECK (one.windows.size() == 1 && one.recent.size() == 1, "junta o que duas sessoes que cairam deixaram");
    }
}

int main()
{
    testSchedule();
    testDirty();
    testSuspects();

    std::printf (failures == 0 ? "\nTudo ok.\n" : "\n%d FALHA(S).\n", failures);
    return failures == 0 ? 0 : 1;
}
