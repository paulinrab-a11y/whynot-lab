#pragma once

// A parte do autosave e da recuperação de queda que não depende do JUCE (testada em tests/app/test_autosave_policy.cpp):
//  - quando o autosave periódico pode rodar (adiar no gesto, na gravação e no export, com teto);
//  - de quais plugins VST3 vale reler o estado (os "sujos");
//  - os suspeitos de uma queda (janela de plugin aberta, últimos plugins adicionados) e a mensagem.
// Tudo na thread de mensagem. Textos em UTF-8 (std::string); o AppController converte para juce::String.

#include <algorithm>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace whynot::autosave
{

//==============================================================================
/** O que está acontecendo na hora em que o autosave vence. */
struct Busy
{
    bool gesture = false;     // arraste de nota, bloco, fader ou knob do controlador (CommandRegistry::isInGesture)
    bool recording = false;   // take em andamento
    bool exporting = false;   // export em segundo plano

    bool any() const noexcept   { return gesture || recording || exporting; }
};

enum class Action
{
    wait,        // ainda não venceu, ou foi adiado
    save,        // salva agora, lendo o estado dos plugins sujos
    saveLight    // o teto de adiamentos passou e a gravação ou o export continua: salva o modelo sem reler plugin nenhum
};

/** O relógio do autosave periódico. tick() é chamado a cada 100 ms pelo Housekeeping.

    Vencido o intervalo: livre, salva. Ocupado (gesto, gravação, export), tenta de novo em retryTicks, até
    maxDeferrals vezes. Passado o teto, não adia mais para sempre:
      - com gesto aberto, olha a cada tick e salva assim que ele fechar (nunca no meio do arraste);
      - sem gesto, mas gravando ou exportando, salva o modelo sem ler o estado dos plugins (a parte que engasga).
*/
struct Schedule
{
    int intervalTicks = 1200;   // 2 minutos
    int retryTicks = 30;        // 3 segundos
    int maxDeferrals = 10;      // 10 x 3 s = até 30 s de espera antes do teto

    int ticksLeft = 1200;
    int deferrals = 0;

    Schedule() = default;
    Schedule (int interval, int retry, int maxDefer) : intervalTicks (interval), retryTicks (retry), maxDeferrals (maxDefer), ticksLeft (interval) {}

    void restart() noexcept    { ticksLeft = intervalTicks; deferrals = 0; }
    void makeDue() noexcept    { ticksLeft = 0; }
    bool atLimit() const noexcept   { return deferrals >= maxDeferrals; }

    Action tick (const Busy& busy) noexcept
    {
        if (ticksLeft > 0 && --ticksLeft > 0)
            return Action::wait;

        if (! busy.any())
        {
            restart();
            return Action::save;
        }

        if (deferrals < maxDeferrals)
        {
            ++deferrals;
            ticksLeft = retryTicks;
            return Action::wait;
        }

        if (busy.gesture)
            return Action::wait;   // ticksLeft fica em 0: o próximo tick olha de novo

        restart();
        return Action::saveLight;
    }
};

//==============================================================================
/** Os plugins VST3 cujo estado pode ter mudado desde a última leitura.

    Chave: "c:<canal>" (instrumento do canal) ou "i:<trilha>/<insert>". Na dúvida, markAll(). */
class DirtyPlugins
{
public:
    static std::string channelKey (const std::string& channel)   { return "c:" + channel; }
    static std::string insertKey (const std::string& track, const std::string& insert)   { return "i:" + track + "/" + insert; }

    void mark (const std::string& key)   { keys.insert (key); }
    void markAll() noexcept              { all = true; }

    /** Uma leitura completa (salvar, exportar, fechar janela) limpa tudo. */
    void clear() noexcept                { keys.clear(); all = false; }

    /** O estado deste plugin acabou de ir para o modelo. Com a janela aberta ele continua sujo. */
    void captured (const std::string& key, bool windowShowing)
    {
        if (! windowShowing)
            keys.erase (key);
    }

    /** Fim de uma leitura parcial: o "todos" já foi atendido (cada plugin vivo foi lido). */
    void allCaptured() noexcept          { all = false; }

    bool isAll() const noexcept          { return all; }
    bool isMarked (const std::string& key) const   { return all || keys.count (key) > 0; }
    bool isEmpty() const noexcept        { return ! all && keys.empty(); }

    /** Ler o estado deste plugin na captura parcial?
        windowShowing: a janela dele está aberta agora (o usuário pode estar mexendo).
        neverCaptured: o modelo ainda não tem estado nenhum dele (plugin recém-inserido). */
    bool shouldCapture (const std::string& key, bool windowShowing, bool neverCaptured) const
    {
        return windowShowing || neverCaptured || isMarked (key);
    }

private:
    std::set<std::string> keys;
    bool all = false;
};

//==============================================================================
/** Um plugin suspeito de uma queda. file é o que o catálogo bloqueia (PluginDescription::fileOrIdentifier). */
struct Suspect
{
    std::string file, name;
    std::string when;   // "14:05", só nos adicionados

    bool operator== (const Suspect& o) const   { return file == o.file && name == o.name && when == o.when; }
};

/** O que uma sessão anota para a seguinte saber quem culpar: as janelas de plugin abertas agora (a mais nova
    por último) e os últimos plugins adicionados (o mais novo por último, até maxRecent). */
struct SuspectLog
{
    static constexpr std::size_t maxRecent = 5;

    std::vector<Suspect> windows, recent;

    bool isEmpty() const noexcept   { return windows.empty() && recent.empty(); }

    void windowOpened (const std::string& file, const std::string& name)
    {
        windowClosed (file);
        windows.push_back ({ file, name, {} });
    }

    bool windowClosed (const std::string& file)
    {
        const auto before = windows.size();
        windows.erase (std::remove_if (windows.begin(), windows.end(), [&] (const Suspect& s) { return s.file == file; }), windows.end());
        return windows.size() != before;
    }

    bool hasWindow (const std::string& file) const
    {
        return std::any_of (windows.begin(), windows.end(), [&] (const Suspect& s) { return s.file == file; });
    }

    void inserted (const std::string& file, const std::string& name, const std::string& when)
    {
        recent.erase (std::remove_if (recent.begin(), recent.end(), [&] (const Suspect& s) { return s.file == file; }), recent.end());
        recent.push_back ({ file, name, when });

        if (recent.size() > maxRecent)
            recent.erase (recent.begin(), recent.begin() + (std::ptrdiff_t) (recent.size() - maxRecent));
    }

    /** Junta o que outra sessão que caiu deixou. */
    void merge (const SuspectLog& other)
    {
        for (auto& w : other.windows)
            windowOpened (w.file, w.name);

        for (auto& r : other.recent)
            inserted (r.file, r.name, r.when);
    }

    /** Uma linha por item, campos separados por tab:
          janela <tab> arquivo <tab> nome
          inserido <tab> hora <tab> arquivo <tab> nome */
    std::string toText() const
    {
        std::string text;

        for (auto& w : windows)
            text += "janela\t" + clean (w.file) + "\t" + clean (w.name) + "\n";

        for (auto& r : recent)
            text += "inserido\t" + clean (r.when) + "\t" + clean (r.file) + "\t" + clean (r.name) + "\n";

        return text;
    }

    /** Lê o que toText() escreveu. Linha estranha (arquivo cortado pela queda, lixo) é ignorada. */
    static SuspectLog fromText (const std::string& text)
    {
        SuspectLog log;
        std::istringstream in (text);
        std::string line;

        while (std::getline (in, line))
        {
            if (! line.empty() && line.back() == '\r')
                line.pop_back();

            const auto fields = split (line);

            if (fields.size() == 3 && fields[0] == "janela" && ! fields[1].empty())
                log.windowOpened (fields[1], nameOr (fields[2], fields[1]));
            else if (fields.size() == 4 && fields[0] == "inserido" && ! fields[2].empty())
                log.inserted (fields[2], nameOr (fields[3], fields[2]), fields[1]);
        }

        return log;
    }

private:
    static std::string clean (std::string s)
    {
        for (auto& c : s)
            if (c == '\t' || c == '\n' || c == '\r')
                c = ' ';

        return s;
    }

    static std::string nameOr (const std::string& name, const std::string& file)
    {
        if (! name.empty())
            return name;

        const auto slash = file.find_last_of ("/\\");
        return slash == std::string::npos ? file : file.substr (slash + 1);
    }

    static std::vector<std::string> split (const std::string& line)
    {
        std::vector<std::string> fields;
        std::size_t start = 0;

        for (;;)
        {
            const auto tab = line.find ('\t', start);
            fields.push_back (line.substr (start, tab == std::string::npos ? std::string::npos : tab - start));

            if (tab == std::string::npos)
                break;

            start = tab + 1;
        }

        return fields;
    }
};

//==============================================================================
/** O aviso da abertura depois de uma queda. */
struct CrashMessage
{
    std::string status;     // uma linha, para a barra de status ("" = nada a dizer)
    std::string question;   // o texto da caixa que oferece bloquear ("" = ninguém para bloquear)
    Suspect suspect;        // quem a caixa oferece bloquear (file vazio = ninguém)
};

namespace detail
{
    inline std::string lower (std::string s)
    {
        for (auto& c : s)
            if (c >= 'A' && c <= 'Z')
                c = (char) (c - 'A' + 'a');

        return s;
    }

    inline std::string joinNames (const std::vector<std::string>& names)
    {
        std::string text;

        for (std::size_t i = 0; i < names.size(); ++i)
        {
            if (i > 0)
                text += i + 1 == names.size() ? " e " : ", ";

            text += names[i];
        }

        return text;
    }
}

/** Monta o aviso a partir do que a sessão que caiu anotou.

    crashModule: o módulo (DLL, .vst3) que o relatório da queda culpa, quando houver (frente crash-log,
    CrashReport::lastCrash()); "" = sem relatório. Se bater com um suspeito, ele passa na frente.
    Ninguém é bloqueado aqui: janela aberta e "adicionado há pouco" não provam culpa. */
inline CrashMessage describeCrash (const SuspectLog& log, const std::string& crashModule = {})
{
    CrashMessage m;

    if (log.isEmpty())
        return m;

    // quem o relatório da queda aponta, se for um dos anotados
    const Suspect* blamed = nullptr;

    if (! crashModule.empty())
    {
        const auto module = detail::lower (crashModule);
        auto matches = [&] (const Suspect& s) { return detail::lower (s.file).find (module) != std::string::npos; };

        for (auto& w : log.windows)
            if (matches (w))
                blamed = &w;

        if (blamed == nullptr)
            for (auto& r : log.recent)
                if (matches (r))
                    blamed = &r;
    }

    std::string text;

    if (! log.windows.empty())
    {
        std::vector<std::string> names;

        for (auto& w : log.windows)
            names.push_back (w.name);

        text = names.size() == 1 ? "A WHYNOT LAB fechou sem aviso na última vez, com a janela do plugin " + names[0] + " aberta."
                                 : "A WHYNOT LAB fechou sem aviso na última vez, com as janelas dos plugins " + detail::joinNames (names) + " abertas.";
    }
    else
    {
        text = "A WHYNOT LAB fechou sem aviso na última vez.";
    }

    if (! log.recent.empty())
    {
        std::vector<std::string> names;

        for (auto r = log.recent.rbegin(); r != log.recent.rend(); ++r)   // o mais novo primeiro
            names.push_back (r->when.empty() ? r->name : r->name + " (" + r->when + ")");

        text += " Últimos plugins adicionados: " + detail::joinNames (names) + ".";
    }

    if (blamed != nullptr)
        text += " O relatório da queda aponta o " + blamed->name + ".";

    m.suspect = blamed != nullptr        ? *blamed
              : ! log.windows.empty()    ? log.windows.back()
                                         : log.recent.back();

    m.status = text + " Nada foi bloqueado.";
    m.question = text + "\n\nIsso não prova que a culpa é dele, por isso nada foi bloqueado. Bloquear o " + m.suspect.name
                 + " agora? Ele deixa de abrir, também dentro dos projetos, até você desbloquear em Plugins (F8), Bloqueados.";
    return m;
}

} // namespace whynot::autosave
