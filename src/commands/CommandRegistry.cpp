#include "CommandRegistry.h"
#include "../model/WnlFormat.h"

#include <cstring>

namespace whynot
{

/** Um passo de desfazer: o projeto inteiro antes e depois, em JSON. */
class SnapshotAction : public juce::UndoableAction
{
public:
    SnapshotAction (CommandRegistry& r, juce::String b, juce::String a, bool alreadyApplied)
        : registry (r), before (std::move (b)), after (std::move (a)), skipNextPerform (alreadyApplied) {}

    // As duas devolvem sempre true: com false, o juce::UndoManager apaga o histórico inteiro (e ainda devolve
    // true para quem chamou). A foto que não abre fica anotada no registro, que devolve o passo ao lugar e
    // avisa quem pediu (CommandRegistry::stepHistory).
    bool perform() override
    {
        // Na primeira vez o comando já mudou o modelo; só no "refazer" aplicamos o depois.
        if (skipNextPerform)
            skipNextPerform = false;
        else
            registry.applyHistoryStep (after);

        return true;
    }

    bool undo() override
    {
        registry.applyHistoryStep (before);
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
    auto fields = compileSchema (info.params);
    const auto id = info.id;
    commands[id] = Entry { std::move (info), std::move (fields) };
}

const CommandRegistry::Entry* CommandRegistry::findEntry (const juce::String& id) const
{
    auto it = commands.find (id);
    return it == commands.end() ? nullptr : &it->second;
}

const CommandInfo* CommandRegistry::find (const juce::String& id) const
{
    const auto* entry = findEntry (id);
    return entry == nullptr ? nullptr : &entry->info;
}

//==============================================================================
// Conferência dos parâmetros contra o schema do comando. A conta fica em Params.h (sem JUCE, testada com o
// cl); aqui o schema é lido uma vez do juce::var, e cada valor que chega é traduzido para ela.
std::vector<CommandRegistry::Field> CommandRegistry::compileSchema (const juce::var& schema)
{
    std::vector<Field> fields;
    const auto* properties = schema["properties"].getDynamicObject();

    if (properties == nullptr)
        return fields;

    const auto* required = schema["required"].getArray();
    const auto isNumber = [] (const juce::var& v)   { return v.isInt() || v.isInt64() || v.isDouble(); };

    for (const auto& property : properties->getProperties())
    {
        const auto& spec = property.value;
        Field field;
        field.name = property.name;
        field.rule.type = params::typeFromName (spec["type"].toString().toStdString());
        field.rule.required = required != nullptr && required->contains (juce::var (property.name.toString()));

        if (const auto& minimum = spec["minimum"]; isNumber (minimum) && std::isfinite ((double) minimum))
        {
            field.rule.hasMinimum = true;
            field.rule.minimum = (double) minimum;
        }

        if (const auto& maximum = spec["maximum"]; isNumber (maximum) && std::isfinite ((double) maximum))
        {
            field.rule.hasMaximum = true;
            field.rule.maximum = (double) maximum;
        }

        if (const auto* allowed = spec["enum"].getArray())
            for (const auto& option : *allowed)
            {
                if (option.isString())
                    field.rule.allowedText.push_back (option.toString().toStdString());
                else if (isNumber (option))
                    field.rule.allowedNumbers.push_back ((double) option);
            }

        fields.push_back (std::move (field));
    }

    return fields;
}

namespace
{
    // Número que não é finito em qualquer nível de uma lista ou de um objeto (as notas de um pianoroll.addNotes,
    // os pontos de uma curva). O limite de profundidade é só para nunca recursar sem fim.
    bool allFinite (const juce::var& v, int depth)
    {
        if (v.isDouble())
            return std::isfinite ((double) v);

        if (depth >= 16)
            return true;

        if (const auto* list = v.getArray())
        {
            for (const auto& item : *list)
                if (! allFinite (item, depth + 1))
                    return false;

            return true;
        }

        if (const auto* object = v.getDynamicObject())
            for (const auto& property : object->getProperties())
                if (! allFinite (property.value, depth + 1))
                    return false;

        return true;
    }

    juce::String typeLabel (params::Type t)
    {
        switch (t)
        {
            case params::Type::boolean:   return "sim ou não (true/false)"_t;
            case params::Type::integer:   return "um número inteiro"_t;
            case params::Type::number:    return "um número"_t;
            case params::Type::text:      return "um texto";
            case params::Type::list:      return "uma lista";
            case params::Type::object:    return "um objeto { nome: valor }";
            case params::Type::any:       break;
        }

        return "outro tipo";
    }

    juce::String kindLabel (params::Kind k)
    {
        switch (k)
        {
            case params::Kind::boolean:   return "sim ou não"_t;
            case params::Kind::integer:
            case params::Kind::number:    return "um número"_t;
            case params::Kind::text:      return "um texto";
            case params::Kind::list:      return "uma lista";
            case params::Kind::object:    return "um objeto";
            case params::Kind::other:     break;
        }

        return "um valor que não é JSON"_t;
    }
}

juce::String CommandRegistry::problemIn (const Entry& entry, const juce::var& p)
{
    const auto missing = [&entry] (const Field& field)
    {
        const auto& spec = entry.info.params["properties"][field.name];
        const auto description = spec["description"].toString();
        return "Falta o parâmetro \""_t + field.name.toString() + "\" (" + spec["type"].toString() + ")"
               + (description.isNotEmpty() ? ": " + description : juce::String ("."));
    };

    const bool none = p.isVoid() || p.isUndefined();
    const auto* object = p.getDynamicObject();

    if (! none && object == nullptr)
        return "Os parâmetros precisam ser um objeto { nome: valor }."_t;

    for (const auto& field : entry.fields)
        if (field.rule.required)
        {
            const auto* value = none ? nullptr : object->getProperties().getVarPointer (field.name);

            if (value == nullptr || value->isVoid() || value->isUndefined())
                return missing (field);
        }

    if (none)
        return {};

    static const params::Rule anything;   // chave fora do schema: só o número não finito é recusado

    for (const auto& given : object->getProperties())
    {
        const auto& v = given.value;

        if (v.isVoid() || v.isUndefined())   // o null do JSON: é como se não viesse
            continue;

        const params::Rule* rule = &anything;

        for (const auto& field : entry.fields)
            if (field.name == given.name)   // dois Identifier: compara ponteiros
            {
                rule = &field.rule;
                break;
            }

        params::Value value;
        juce::String text;   // segura o texto enquanto o string_view aponta para ele (cópia sem alocar)

        if (v.isBool())
        {
            value.kind = params::Kind::boolean;
            value.number = (bool) v ? 1.0 : 0.0;
        }
        else if (v.isInt() || v.isInt64())
        {
            value.kind = params::Kind::integer;
            value.number = (double) v;
        }
        else if (v.isDouble())
        {
            value.kind = params::Kind::number;
            value.number = (double) v;
        }
        else if (v.isString())
        {
            value.kind = params::Kind::text;
            text = v.toString();

            if (rule->type == params::Type::integer || rule->type == params::Type::number)
                value.number = text.getDoubleValue();

            if (rule->hasEnum())
                value.text = std::string_view (text.toRawUTF8(), text.getNumBytesAsUTF8());
        }
        else if (v.isArray())
        {
            value.kind = params::Kind::list;
        }
        else if (v.getDynamicObject() != nullptr)
        {
            value.kind = params::Kind::object;
        }

        auto problem = params::check (*rule, value);

        if (problem == params::Problem::none && (value.kind == params::Kind::list || value.kind == params::Kind::object)
            && ! allFinite (v, 0))
            problem = params::Problem::notFinite;

        if (problem == params::Problem::none)
            continue;

        const auto name = "O parâmetro \""_t + given.name.toString() + "\" ";

        switch (problem)
        {
            case params::Problem::wrongType:
                return name + "precisa ser " + typeLabel (rule->type) + ", e veio " + kindLabel (value.kind) + ".";

            case params::Problem::notFinite:
                return name + "tem um número que não é finito (NaN ou infinito)."_t;

            case params::Problem::belowMinimum:
                return name + "precisa ser no mínimo "_t + juce::String (rule->minimum) + " (veio " + juce::String (value.number) + ").";

            case params::Problem::aboveMaximum:
                return name + "precisa ser no máximo "_t + juce::String (rule->maximum) + " (veio " + juce::String (value.number) + ").";

            case params::Problem::notAllowed:
            {
                juce::StringArray options;

                for (const auto& option : rule->allowedText)
                    options.add (juce::String::fromUTF8 (option.c_str()));

                for (const double option : rule->allowedNumbers)
                    options.add (juce::String (option));

                return name + "precisa ser um destes: " + options.joinIntoString (", ") + ".";
            }

            case params::Problem::none:
                break;
        }
    }

    return {};
}

juce::String CommandRegistry::checkParams (const juce::String& id, const juce::var& p) const
{
    const auto* entry = findEntry (id);
    return entry == nullptr ? "Comando desconhecido: " + id : problemIn (*entry, p);
}

juce::var CommandRegistry::describe() const
{
    juce::Array<juce::var> list;

    for (auto& [id, entry] : commands)
    {
        const auto& info = entry.info;
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

    const auto* entry = findEntry (id);

    if (entry == nullptr)
        return CommandResult::fail ("Comando desconhecido: " + id);

    // Antes de tocar em qualquer coisa: o que o schema do comando não aceita nem chega ao comando. Custa
    // dezenas de nanossegundos por parâmetro e não aloca quando está certo (cabe num passo de arraste).
    if (const auto problem = problemIn (*entry, p); problem.isNotEmpty())
        return CommandResult::fail (problem);

    const auto* cmd = &entry->info;
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
        // Comando falhou no meio: volta ao estado anterior. A foto acabou de sair do toJson; se mesmo assim não
        // abrir, vale a cópia (quando há) e, sem ela, o erro diz que o projeto ficou pela metade.
        if (before != after)
            if (const auto back = applySnapshot (before); back.failed())
            {
                if (previous.has_value())
                    model.replace (std::move (*previous));
                else
                    result.error += " (e o projeto não voltou ao estado de antes: "_t + back.getErrorMessage() + ")";
            }

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
        const auto* entry = findEntry (id);
        const auto where = "Passo "_t + juce::String (i + 1) + " (" + id + "): ";

        if (entry == nullptr)
            return CommandResult::fail (where + "comando desconhecido.");

        const auto* cmd = &entry->info;

        if (id == "project.new" || id == "project.open")
            return CommandResult::fail (where + "não entra num lote, porque limpa o histórico de desfazer."_t);

        if (! cmd->changesModel)
            return CommandResult::fail (where + "só comandos que mudam o projeto entram num lote."_t);

        // os parâmetros de todos os passos, antes de rodar o primeiro
        if (const auto problem = problemIn (*entry, steps.getReference (i)["params"]); problem.isNotEmpty())
            return CommandResult::fail (where + problem);
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
                auto error = "Passo "_t + juce::String (i + 1) + " (" + step["id"].toString() + "): " + result.error;

                // o passo que falhou já voltou sozinho; os anteriores voltam com a foto de antes
                if (wnl::toJson (model.get()) != before)
                    if (const auto back = applySnapshot (before); back.failed())
                        error += " (e o projeto não voltou ao estado de antes do lote: "_t + back.getErrorMessage() + ")";

                return CommandResult::fail (error);
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

juce::Result CommandRegistry::applySnapshot (const juce::String& json)
{
    Project p;

    // texto do próprio app (desfazer): sem os tetos e as regras de caminho do arquivo de fora
    if (const auto read = readSnapshot ? readSnapshot (json, p) : wnl::fromSnapshot (json, p); read.failed())
        return read;

    model.replace (std::move (p));
    return juce::Result::ok();
}

void CommandRegistry::applyHistoryStep (const juce::String& json)
{
    // devolvendo um passo ao lugar, ou uma foto deste mesmo passo já falhou: o projeto não muda
    if (replayingHistory || historyError.isNotEmpty())
        return;

    if (const auto applied = applySnapshot (json); applied.failed())
        historyError = "A foto deste passo do histórico não abriu, e o projeto ficou como estava: "_t + applied.getErrorMessage();
}

bool CommandRegistry::stepHistory (bool forward)
{
    if (beforeAnything)
        beforeAnything();

    historyError.clear();

    if (! (forward ? undoManager.redo() : undoManager.undo()))
        return false;   // não havia passo

    if (historyError.isEmpty())
        return true;

    // A foto não abriu e o projeto ficou como estava, mas o UndoManager já levou o passo para o outro lado do
    // histórico: desfeito sem desfazer, o próximo Ctrl+Z pularia um passo. Devolve o passo ao lugar, sem aplicar
    // foto nenhuma (cada passo tem uma ação só: as SnapshotAction de uma transação se fundem).
    const juce::ScopedValueSetter<bool> replay (replayingHistory, true);

    if (forward)
        undoManager.undo();
    else
        undoManager.redo();

    return false;
}

bool CommandRegistry::undo()   { return stepHistory (false); }
bool CommandRegistry::redo()   { return stepHistory (true); }

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
        const double raw = (v.isDouble() || v.isInt() || v.isInt64()) ? (double) v : v.toString().getDoubleValue();

        // Quem chama converte para int ou float na mesma linha (são dezenas de `(int) params::number (...)` nos
        // comandos), e NaN, infinito ou 1e300 em int é comportamento indefinido: o número já sai daqui finito e
        // dentro da faixa do int. Só a semente passa de 32 bits (synth.vary devolve e aceita uma de 32 bits sem
        // sinal; pianoroll.transform lê em 64): para ela, a faixa do int64.
        return std::strcmp (key, "seed") == 0 ? saturatedWide (raw, fallback) : saturated (raw, fallback);
    }

    int integer (const juce::var& p, const char* key, int fallback)
    {
        return has (p, key) ? toInt (number (p, key, (double) fallback), fallback) : fallback;
    }

    juce::int64 integer64 (const juce::var& p, const char* key, juce::int64 fallback)
    {
        if (! has (p, key))
            return fallback;

        const auto& v = p[key];

        if (v.isInt() || v.isInt64())
            return (juce::int64) v;   // sem passar por double: acima de 2^53 perderia os bits de baixo

        return (juce::int64) toInt64 (v.isDouble() ? (double) v : v.toString().getDoubleValue(), (std::int64_t) fallback);
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
