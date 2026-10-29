#include "CommandRegistry.h"
#include "../model/WnlFormat.h"

namespace whynot
{

/** Um passo de desfazer: o projeto inteiro antes e depois, em JSON. */
class SnapshotAction : public juce::UndoableAction
{
public:
    SnapshotAction (CommandRegistry& r, juce::String b, juce::String a, bool alreadyApplied)
        : registry (r), before (std::move (b)), after (std::move (a)), skipNextPerform (alreadyApplied) {}

    bool perform() override
    {
        // Na primeira vez o comando já mudou o modelo; só no "refazer" aplicamos o depois.
        if (skipNextPerform)
            skipNextPerform = false;
        else
            registry.applySnapshot (after);

        return true;
    }

    bool undo() override
    {
        registry.applySnapshot (before);
        return true;
    }

    int getSizeInUnits() override { return before.length() + after.length(); }

    juce::UndoableAction* createCoalescedAction (juce::UndoableAction* next) override
    {
        if (auto* n = dynamic_cast<SnapshotAction*> (next))
            return new SnapshotAction (registry, before, n->after, false);

        return nullptr;
    }

private:
    CommandRegistry& registry;
    juce::String before, after;
    bool skipNextPerform;
};

//==============================================================================
CommandRegistry::CommandRegistry (ProjectModel& m, AppServices& a) : model (m), app (a) {}

void CommandRegistry::add (CommandInfo info)
{
    jassert (info.id.isNotEmpty() && info.run != nullptr);
    commands[info.id] = std::move (info);
}

const CommandInfo* CommandRegistry::find (const juce::String& id) const
{
    auto it = commands.find (id);
    return it == commands.end() ? nullptr : &it->second;
}

juce::var CommandRegistry::describe() const
{
    juce::Array<juce::var> list;

    for (auto& [id, info] : commands)
    {
        auto o = new juce::DynamicObject();
        o->setProperty ("id", id);
        o->setProperty ("title", info.title);
        o->setProperty ("params", info.params);
        o->setProperty ("undoable", info.changesModel);
        list.add (juce::var (o));
    }

    return list;
}

CommandResult CommandRegistry::run (const juce::String& id, const juce::var& p)
{
    if (beforeCommand)
        beforeCommand (id);

    if (beforeAnything)
        beforeAnything();

    auto* cmd = find (id);

    if (cmd == nullptr)
        return CommandResult::fail ("Comando desconhecido: " + id);

    CommandContext ctx { model, app };

    if (! cmd->changesModel)
        return cmd->run (ctx, p);

    // Num gesto (arrastar nota, bloco ou fader) cada passo do mouse é um comando. As duas fotos em
    // JSON do desfazer custavam ~15 ms por passo e deixavam o arraste aos trancos (pedido do usuário,
    // 21/10): no gesto, o passo guarda só uma cópia do projeto (para voltar se falhar), e as fotos
    // saem uma vez, no endGesture. O gesto continua sendo um passo só de desfazer.
    if (gestureDepth > 0)
    {
        Project previous = model.get();

        if (! gestureStart.has_value())
            gestureStart = previous;

        auto result = cmd->run (ctx, p);
        wnl::normalise (model.data());

        if (result.ok && vetoChange && ! (model.get() == previous))
            if (const auto why = vetoChange (previous, model.get()); why.isNotEmpty())
                result = CommandResult::fail (why);

        if (! result.ok)
        {
            if (! (model.get() == previous))
                model.replace (std::move (previous));

            return result;
        }

        if (! (model.get() == previous))
            model.changed();

        return result;
    }

    // Remover um canal, um insert ou uma trilha tira o plugin VST3 do motor. O estado dele só ia para o modelo
    // ao salvar: o Ctrl+Z trazia o plugin com o timbre velho (revisão profissional, 22/10). Lê agora, antes da foto.
    if (id == "channel.remove" || id == "mixer.removeInsert" || id == "mixer.removeTrack")
        app.captureLivePluginStates();

    const auto before = wnl::toJson (model.get());
    std::optional<Project> previous;

    if (vetoChange)
        previous = model.get();

    auto result = cmd->run (ctx, p);
    wnl::normalise (model.data());
    const auto after = wnl::toJson (model.get());

    if (result.ok && previous.has_value() && before != after)
        if (const auto why = vetoChange (*previous, model.get()); why.isNotEmpty())
            result = CommandResult::fail (why);

    if (! result.ok)
    {
        // Comando falhou no meio: volta ao estado anterior.
        if (before != after)
            applySnapshot (before);

        return result;
    }

    if (before != after)
    {
        // dentro de um lote (runBatch), o passo de desfazer é um só, o do lote inteiro
        if (batchDepth == 0)
        {
            if (gestureDepth == 0)
                undoManager.beginNewTransaction (cmd->title);

            undoManager.perform (new SnapshotAction (*this, before, after, true));
        }

        model.changed();
    }

    return result;
}

CommandResult CommandRegistry::runBatch (const juce::Array<juce::var>& steps, const juce::String& title)
{
    if (beforeAnything)
        beforeAnything();

    if (gestureDepth > 0)
        return CommandResult::fail ("Há um gesto aberto: o lote espera ele acabar."_t);

    // confere antes de rodar qualquer passo
    for (int i = 0; i < steps.size(); ++i)
    {
        const auto id = steps.getReference (i)["id"].toString();
        const auto* cmd = find (id);
        const auto where = "Passo "_t + juce::String (i + 1) + " (" + id + "): ";

        if (cmd == nullptr)
            return CommandResult::fail (where + "comando desconhecido.");

        if (id == "project.new" || id == "project.open")
            return CommandResult::fail (where + "não entra num lote, porque limpa o histórico de desfazer."_t);

        if (! cmd->changesModel)
            return CommandResult::fail (where + "só comandos que mudam o projeto entram num lote."_t);
    }

    // Cada passo roda pelo run() normal: normaliza, volta sozinho se falhar e chama o changed(), para o
    // Projector projetar no motor e o passo seguinte achar a instância viva. Com batchDepth > 0, o run() não
    // empilha desfazer (nem o de um comando chamado por dentro de outro); o lote empilha um passo só, no fim.
    // Não usa gesto com undoCurrentTransactionOnly: no JUCE ele devolve ao refazer uma reserva antiga
    // (stashedFutureTransactions), e um Ctrl+Y aplicaria uma foto velha do projeto inteiro.
    const auto before = wnl::toJson (model.get());
    juce::Array<juce::var> results;

    {
        struct Depth
        {
            explicit Depth (int& d) : depth (d)   { ++depth; }
            ~Depth()                              { --depth; }
            int& depth;
        } inBatch (batchDepth);

        for (int i = 0; i < steps.size(); ++i)
        {
            const auto& step = steps.getReference (i);
            const auto result = run (step["id"].toString(), step["params"]);

            if (! result.ok)
            {
                // o passo que falhou já voltou sozinho; os anteriores voltam com a foto de antes
                if (wnl::toJson (model.get()) != before)
                    applySnapshot (before);

                return CommandResult::fail ("Passo "_t + juce::String (i + 1) + " (" + step["id"].toString() + "): " + result.error);
            }

            results.add (result.value);
        }
    }

    if (const auto after = wnl::toJson (model.get()); after != before)
    {
        undoManager.beginNewTransaction (title);
        undoManager.perform (new SnapshotAction (*this, before, after, true));
    }

    auto* out = new juce::DynamicObject();
    out->setProperty ("results", results);
    return CommandResult::success (juce::var (out));
}

void CommandRegistry::applySnapshot (const juce::String& json)
{
    Project p;

    if (wnl::fromJson (json, p).wasOk())
        model.replace (std::move (p));
}

bool CommandRegistry::undo()
{
    if (beforeAnything)
        beforeAnything();

    return undoManager.undo();
}

bool CommandRegistry::redo()
{
    if (beforeAnything)
        beforeAnything();

    return undoManager.redo();
}

void CommandRegistry::beginGesture (const juce::String& name)
{
    if (beforeGestureBegins)
        beforeGestureBegins();

    if (beforeAnything)
        beforeAnything();

    if (gestureDepth++ == 0)
        undoManager.beginNewTransaction (name);
}

void CommandRegistry::endGesture()
{
    gestureDepth = juce::jmax (0, gestureDepth - 1);

    if (gestureDepth > 0 || ! gestureStart.has_value())
        return;

    // as fotos do gesto inteiro, num passo só (a transação abriu no beginGesture)
    const auto before = wnl::toJson (*gestureStart);
    const auto after = wnl::toJson (model.get());
    gestureStart.reset();

    if (before != after)
        undoManager.perform (new SnapshotAction (*this, before, after, true));
}

//==============================================================================
namespace params
{
    juce::var schema (std::initializer_list<std::tuple<const char*, const char*, bool, juce::String>> fields)
    {
        auto root = new juce::DynamicObject();
        root->setProperty ("type", "object");
        auto props = new juce::DynamicObject();
        juce::Array<juce::var> required;

        for (auto& [name, type, isRequired, description] : fields)
        {
            auto f = new juce::DynamicObject();
            f->setProperty ("type", type);
            f->setProperty ("description", description);
            props->setProperty (name, juce::var (f));

            if (isRequired)
                required.add (name);
        }

        root->setProperty ("properties", juce::var (props));
        root->setProperty ("required", required);
        return juce::var (root);
    }

    bool has (const juce::var& p, const char* key)
    {
        return p.isObject() && p.hasProperty (key);
    }

    juce::String string (const juce::var& p, const char* key, const juce::String& fallback)
    {
        return has (p, key) ? p[key].toString() : fallback;
    }

    double number (const juce::var& p, const char* key, double fallback)
    {
        if (! has (p, key))
            return fallback;

        const auto& v = p[key];
        return (v.isDouble() || v.isInt() || v.isInt64()) ? (double) v : v.toString().getDoubleValue();
    }

    bool boolean (const juce::var& p, const char* key, bool fallback)
    {
        if (! has (p, key))
            return fallback;

        const auto& v = p[key];
        return v.isBool() ? (bool) v : (v.isString() ? v.toString().equalsIgnoreCase ("true") : (int) v != 0);
    }
}

} // namespace whynot
