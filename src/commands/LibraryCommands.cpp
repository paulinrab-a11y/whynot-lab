#include "CommandRegistry.h"
#include "../library/ProjectImport.h"
#include "../library/ProjectLibrary.h"

namespace whynot
{

namespace
{
    using params::schema;
    using library::ProjectLibrary;

    CommandResult noLibrary()
    {
        return CommandResult::fail ("A tela Projetos não está disponível aqui."_t);
    }

    // "path" de um .wnl que existe; vazio = erro com a explicação
    juce::File projectFrom (const juce::var& a, juce::String& error)
    {
        const juce::File file (params::string (a, "path"));

        if (! file.existsAsFile() || ! file.hasFileExtension ("wnl"))
        {
            error = "Projeto não encontrado: "_t + file.getFullPathName();
            return {};
        }

        return file;
    }

    // "A"/"B" ou 0/1; -1 = inválido
    int deckFrom (const juce::var& a, const char* key, int fallback)
    {
        if (! params::has (a, key))
            return fallback;

        const auto v = a[key];
        const auto s = v.toString().trim().toUpperCase();

        if (s == "A" || s == "0")  return 0;
        if (s == "B" || s == "1")  return 1;
        return -1;
    }
}

void registerLibraryCommands (CommandRegistry& r)
{
    r.add ({ "library.list", "Listar os projetos"_t,
             schema ({}), false,
             [] (CommandContext& c, const juce::var&)
             {
                 auto* lib = c.app.library();
                 return lib != nullptr ? CommandResult::success (lib->describe()) : noLibrary();
             } });

    r.add ({ "library.refresh", "Procurar projetos nas pastas"_t,
             schema ({ { "folders", "array", false, "Pastas onde procurar os .wnl (troca as atuais nesta sessão); padrão: as de agora"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* lib = c.app.library();

                 if (lib == nullptr)
                     return noLibrary();

                 if (auto* folders = a["folders"].getArray())
                 {
                     juce::Array<juce::File> roots;

                     for (auto& f : *folders)
                         if (juce::File::isAbsolutePath (f.toString()))
                             roots.add (juce::File (f.toString()));

                     lib->setRoots (roots);
                 }

                 lib->refresh();
                 return CommandResult::success (lib->describe());
             } });

    r.add ({ "library.renderPreview", "Renderizar a prévia de um projeto"_t,
             schema ({ { "path", "string", true, "O .wnl do projeto (não precisa estar aberto)"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* lib = c.app.library();

                 if (lib == nullptr)
                     return noLibrary();

                 juce::String error;
                 const auto file = projectFrom (a, error);

                 if (file == juce::File())
                     return CommandResult::fail (error);

                 lib->renderPreview (file);
                 return CommandResult::success (lib->describe());
             } });

    r.add ({ "library.renderPending", "Renderizar as prévias pendentes"_t,
             schema ({}), false,
             [] (CommandContext& c, const juce::var&)
             {
                 auto* lib = c.app.library();

                 if (lib == nullptr)
                     return noLibrary();

                 auto o = new juce::DynamicObject();
                 o->setProperty ("queued", lib->renderPending());
                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "library.play", "Ouvir a prévia de um projeto"_t,
             schema ({ { "path", "string", true, "O .wnl do projeto"_t },
                       { "from", "number", false, "Começar deste ponto, em segundos; sem ele, alinhado com o outro deck se ele toca, senão do começo"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* lib = c.app.library();

                 if (lib == nullptr)
                     return noLibrary();

                 juce::String error;
                 const auto file = projectFrom (a, error);

                 if (file == juce::File())
                     return CommandResult::fail (error);

                 const auto result = lib->playPreview (file, params::has (a, "from") ? juce::jmax (0.0, params::number (a, "from", 0.0)) : -1.0);
                 return result.wasOk() ? CommandResult::success (lib->deckStatus()) : CommandResult::fail (result.getErrorMessage());
             } });

    r.add ({ "library.pause", "Pausar ou continuar a prévia"_t,
             schema ({}), false,
             [] (CommandContext& c, const juce::var&)
             {
                 auto* lib = c.app.library();

                 if (lib == nullptr)
                     return noLibrary();

                 lib->toggleDeck();
                 return CommandResult::success (lib->deckStatus());
             } });

    r.add ({ "library.stop", "Parar a prévia"_t,
             schema ({}), false,
             [] (CommandContext& c, const juce::var&)
             {
                 auto* lib = c.app.library();

                 if (lib == nullptr)
                     return noLibrary();

                 lib->stopDeck();
                 return CommandResult::success (lib->deckStatus());
             } });

    r.add ({ "library.seek", "Levar a prévia a um ponto"_t,
             schema ({ { "seconds", "number", true, "Ponto da prévia, em segundos"_t },
                       { "deck", "string", false, "A ou B (padrão: o que soa); o outro vai junto, pela sincronia"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* lib = c.app.library();

                 if (lib == nullptr)
                     return noLibrary();

                 const int deck = deckFrom (a, "deck", lib->getActiveDeck());

                 if (deck < 0)
                     return CommandResult::fail ("Deck: A ou B."_t);

                 lib->seekDeck (deck, params::number (a, "seconds", 0.0));
                 return CommandResult::success (lib->deckStatus());
             } });

    r.add ({ "library.loadDeck", "Carregar um deck"_t,
             schema ({ { "deck", "string", true, "A ou B"_t },
                       { "path", "string", true, "O .wnl de um projeto (a prévia dele) ou um arquivo de áudio de referência"_t },
                       { "from", "number", false, "Ponto, em segundos; sem ele, alinhado com o outro deck se ele toca"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* lib = c.app.library();

                 if (lib == nullptr)
                     return noLibrary();

                 const int deck = deckFrom (a, "deck", -1);
                 const juce::File file (params::string (a, "path"));

                 if (deck < 0)
                     return CommandResult::fail ("Deck: A ou B."_t);

                 if (! file.existsAsFile())
                     return CommandResult::fail ("Arquivo não encontrado: "_t + file.getFullPathName());

                 const auto result = lib->loadDeck (deck, file, params::has (a, "from") ? juce::jmax (0.0, params::number (a, "from", 0.0)) : -1.0);
                 return result.wasOk() ? CommandResult::success (lib->deckStatus()) : CommandResult::fail (result.getErrorMessage());
             } });

    r.add ({ "library.setActive", "Ouvir o deck A ou o B"_t,
             schema ({ { "deck", "string", true, "A ou B: o que passa a soar (a troca é só de volume, em 20 ms)"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* lib = c.app.library();

                 if (lib == nullptr)
                     return noLibrary();

                 const int deck = deckFrom (a, "deck", -1);

                 if (deck < 0)
                     return CommandResult::fail ("Deck: A ou B."_t);

                 lib->setActiveDeck (deck);
                 return CommandResult::success (lib->deckStatus());
             } });

    r.add ({ "library.switchDeck", "Trocar entre A e B"_t,
             schema ({}), false,
             [] (CommandContext& c, const juce::var&)
             {
                 auto* lib = c.app.library();

                 if (lib == nullptr)
                     return noLibrary();

                 lib->toggleActiveDeck();
                 return CommandResult::success (lib->deckStatus());
             } });

    r.add ({ "library.setSync", "Sincronia dos decks"_t,
             schema ({ { "mode", "string", true, "bar (mesmo compasso, pelo BPM de cada projeto), second (mesmo segundo) ou free (livre)"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* lib = c.app.library();

                 if (lib == nullptr)
                     return noLibrary();

                 const auto mode = params::string (a, "mode");

                 if (mode == "bar")          lib->setSync (ProjectLibrary::Sync::bar);
                 else if (mode == "second")  lib->setSync (ProjectLibrary::Sync::second);
                 else if (mode == "free")    lib->setSync (ProjectLibrary::Sync::free);
                 else                        return CommandResult::fail ("Modo: bar, second ou free."_t);

                 return CommandResult::success (lib->deckStatus());
             } });

    r.add ({ "library.matchLoudness", "Igualar o volume dos decks"_t,
             schema ({ { "on", "boolean", true, "true = os dois no mesmo LUFS (o mais alto desce até o outro)"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* lib = c.app.library();

                 if (lib == nullptr)
                     return noLibrary();

                 lib->setMatchLoudness (params::boolean (a, "on", true));
                 return CommandResult::success (lib->deckStatus());
             } });

    r.add ({ "library.setMeta", "Organizar um projeto"_t,
             schema ({ { "path", "string", true, "O .wnl do projeto"_t },
                       { "favorite", "boolean", false, "Estrela"_t },
                       { "colour", "string", false, "Cor: #RRGGBB (as da paleta dos canais) ou \"\" para tirar"_t },
                       { "tags", "string", false, "Tags separadas por vírgula (troca todas; \"\" tira)"_t },
                       { "status", "string", false, "ideia, producao, mix, pronto ou \"\" (sem status)"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* lib = c.app.library();

                 if (lib == nullptr)
                     return noLibrary();

                 juce::String error;
                 const auto file = projectFrom (a, error);

                 if (file == juce::File())
                     return CommandResult::fail (error);

                 if (params::has (a, "status"))
                 {
                     const auto text = params::string (a, "status");
                     const auto s = ProjectLibrary::statusFromText (text);

                     if (s == ProjectLibrary::Status::none && text.trim().isNotEmpty())
                         return CommandResult::fail ("Status: ideia, producao, mix ou pronto."_t);

                     lib->setStatus (file, s);
                 }

                 if (params::has (a, "favorite"))
                     lib->setFavorite (file, params::boolean (a, "favorite", true));

                 if (params::has (a, "colour"))
                     lib->setColour (file, params::string (a, "colour"));

                 if (params::has (a, "tags"))
                     lib->setTags (file, juce::StringArray::fromTokens (params::string (a, "tags"), ",", "\""));

                 const auto m = lib->metaOf (file);
                 auto o = new juce::DynamicObject();
                 o->setProperty ("favorite", m.favorite);
                 o->setProperty ("colour", m.colour);
                 o->setProperty ("tags", m.tags.joinIntoString (","));
                 o->setProperty ("status", ProjectLibrary::statusId (m.status));
                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "library.search", "Filtrar a lista de projetos"_t,
             schema ({ { "query", "string", false, "Texto livre e bpm:140-150 (acha também 70-75), tom:Am, tag:x, status:mix, fav"_t },
                       { "view", "string", false, "all, recent, favorites, status:<s>, tag:<t>, folder:<caminho> ou combo:<nome> (padrão: all)"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* lib = c.app.library();

                 if (lib == nullptr)
                     return noLibrary();

                 using V = ProjectLibrary::Filter::View;
                 ProjectLibrary::Filter f;
                 f.query = params::string (a, "query");
                 const auto view = params::string (a, "view", "all");

                 if (view == "recent")                    f.view = V::recent;
                 else if (view == "favorites")            f.view = V::favorites;
                 else if (view.startsWith ("status:"))    { f.view = V::status; f.status = ProjectLibrary::statusFromText (view.fromFirstOccurrenceOf (":", false, false)); }
                 else if (view.startsWith ("tag:"))       { f.view = V::tag; f.tag = view.fromFirstOccurrenceOf (":", false, false).trim().toLowerCase(); }
                 else if (view.startsWith ("folder:"))    { f.view = V::folder; f.folder = juce::File (view.fromFirstOccurrenceOf (":", false, false)); }
                 else if (view.startsWith ("combo:"))     { f.view = V::combo; f.combo = view.fromFirstOccurrenceOf (":", false, false).trim(); }
                 else if (view != "all")                  return CommandResult::fail ("Vista: all, recent, favorites, status:<s>, tag:<t>, folder:<caminho> ou combo:<nome>."_t);

                 lib->setFilter (f);
                 return CommandResult::success (lib->describeRows());
             } });

    r.add ({ "library.toggleVersions", "Abrir ou fechar as versões de um projeto"_t,
             schema ({ { "path", "string", true, "O .wnl de qualquer versão do projeto"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* lib = c.app.library();

                 if (lib == nullptr)
                     return noLibrary();

                 lib->toggleStack (ProjectLibrary::stackKey (juce::File (params::string (a, "path"))));
                 return CommandResult::success (lib->describeRows());
             } });

    r.add ({ "library.addFolder", "Acrescentar uma pasta de projetos"_t,
             schema ({ { "path", "string", true, "Pasta com projetos (.wnl), até três níveis abaixo"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* lib = c.app.library();

                 if (lib == nullptr)
                     return noLibrary();

                 const juce::File folder (params::string (a, "path"));

                 if (! folder.isDirectory())
                     return CommandResult::fail ("Pasta não encontrada: "_t + folder.getFullPathName());

                 lib->addFolder (folder);
                 return CommandResult::success (lib->describe());
             } });

    r.add ({ "library.removeFolder", "Tirar uma pasta da lista"_t,
             schema ({ { "path", "string", true, "Uma pasta acrescentada antes"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* lib = c.app.library();

                 if (lib == nullptr)
                     return noLibrary();

                 lib->removeFolder (juce::File (params::string (a, "path")));
                 return CommandResult::success (lib->describe());
             } });

    //--------------------------------------------------------------------------
    // Combos (fatia 4)
    auto withCombo = [] (CommandContext& c, const juce::var& a, auto&& action) -> CommandResult
    {
        auto* lib = c.app.library();

        if (lib == nullptr)
            return noLibrary();

        const auto result = action (*lib, params::string (a, "combo"));
        return result.wasOk() ? CommandResult::success (lib->describeCombos()) : CommandResult::fail (result.getErrorMessage());
    };

    r.add ({ "library.combos", "Listar os combos"_t, schema ({}), false,
             [] (CommandContext& c, const juce::var&)
             {
                 auto* lib = c.app.library();
                 return lib != nullptr ? CommandResult::success (lib->describeCombos()) : noLibrary();
             } });

    r.add ({ "library.comboCreate", "Criar um combo"_t,
             schema ({ { "combo", "string", true, "Nome do combo"_t } }), false,
             [withCombo] (CommandContext& c, const juce::var& a)
             { return withCombo (c, a, [] (ProjectLibrary& lib, const juce::String& name) { return lib.createCombo (name); }); } });

    r.add ({ "library.comboRename", "Renomear um combo"_t,
             schema ({ { "combo", "string", true, "Nome de agora"_t }, { "name", "string", true, "Nome novo"_t } }), false,
             [withCombo] (CommandContext& c, const juce::var& a)
             {
                 const auto to = params::string (a, "name");
                 return withCombo (c, a, [to] (ProjectLibrary& lib, const juce::String& name) { return lib.renameCombo (name, to); });
             } });

    r.add ({ "library.comboDelete", "Apagar um combo"_t,
             schema ({ { "combo", "string", true, "Nome do combo (o arquivo vai para a lixeira)"_t } }), false,
             [withCombo] (CommandContext& c, const juce::var& a)
             { return withCombo (c, a, [] (ProjectLibrary& lib, const juce::String& name) { return lib.deleteCombo (name); }); } });

    r.add ({ "library.comboAdd", "Pôr no combo"_t,
             schema ({ { "combo", "string", true, "Nome do combo"_t },
                       { "path", "string", true, "Um .wnl ou um arquivo de áudio de referência"_t },
                       { "latest", "boolean", false, "Seguir a versão mais nova da pilha (Beat → Beat_3)"_t },
                       { "note", "string", false, "Uma nota sobre o item"_t },
                       { "index", "integer", false, "Posição (1 = primeiro); padrão: no fim"_t } }), false,
             [withCombo] (CommandContext& c, const juce::var& a)
             {
                 const juce::File file (params::string (a, "path"));
                 const bool latest = params::boolean (a, "latest", false);
                 const auto note = params::string (a, "note");
                 const int index = params::has (a, "index") ? (int) params::number (a, "index", 0) - 1 : -1;
                 return withCombo (c, a, [&] (ProjectLibrary& lib, const juce::String& name) { return lib.addToCombo (name, file, latest, note, index); });
             } });

    r.add ({ "library.comboRemove", "Tirar do combo"_t,
             schema ({ { "combo", "string", true, "Nome do combo"_t }, { "index", "integer", true, "Posição (1 = primeiro)"_t } }), false,
             [withCombo] (CommandContext& c, const juce::var& a)
             {
                 const int index = (int) params::number (a, "index", 0) - 1;
                 return withCombo (c, a, [index] (ProjectLibrary& lib, const juce::String& name) { return lib.removeFromCombo (name, index); });
             } });

    r.add ({ "library.comboMove", "Mudar a ordem no combo"_t,
             schema ({ { "combo", "string", true, "Nome do combo"_t },
                       { "from", "integer", true, "Posição de agora (1 = primeiro)"_t },
                       { "to", "integer", true, "Posição nova (1 = primeiro)"_t } }), false,
             [withCombo] (CommandContext& c, const juce::var& a)
             {
                 const int from = (int) params::number (a, "from", 0) - 1, to = (int) params::number (a, "to", 0) - 1;
                 return withCombo (c, a, [from, to] (ProjectLibrary& lib, const juce::String& name) { return lib.moveInCombo (name, from, to); });
             } });

    r.add ({ "library.comboNote", "Nota de um item do combo"_t,
             schema ({ { "combo", "string", true, "Nome do combo"_t }, { "index", "integer", true, "Posição (1 = primeiro)"_t },
                       { "note", "string", true, "A nota (\"\" apaga)"_t } }), false,
             [withCombo] (CommandContext& c, const juce::var& a)
             {
                 const int index = (int) params::number (a, "index", 0) - 1;
                 const auto note = params::string (a, "note");
                 return withCombo (c, a, [index, note] (ProjectLibrary& lib, const juce::String& name) { return lib.setComboNote (name, index, note); });
             } });

    r.add ({ "library.comboLatest", "Seguir a versão mais nova"_t,
             schema ({ { "combo", "string", true, "Nome do combo"_t }, { "index", "integer", true, "Posição (1 = primeiro)"_t },
                       { "on", "boolean", true, "true = toca sempre a versão mais nova da pilha"_t } }), false,
             [withCombo] (CommandContext& c, const juce::var& a)
             {
                 const int index = (int) params::number (a, "index", 0) - 1;
                 const bool on = params::boolean (a, "on", true);
                 return withCombo (c, a, [index, on] (ProjectLibrary& lib, const juce::String& name) { return lib.setComboLatest (name, index, on); });
             } });

    r.add ({ "library.comboPlay", "Tocar o combo em sequência"_t,
             schema ({ { "combo", "string", true, "Nome do combo"_t }, { "from", "integer", false, "Começar deste item (1 = primeiro)"_t } }), false,
             [withCombo] (CommandContext& c, const juce::var& a)
             {
                 const int from = params::has (a, "from") ? (int) params::number (a, "from", 1) - 1 : 0;
                 return withCombo (c, a, [from] (ProjectLibrary& lib, const juce::String& name) { return lib.playCombo (name, from); });
             } });

    //--------------------------------------------------------------------------
    // Importar de outro projeto (fatia 5): muda o projeto aberto, então é um passo de desfazer.
    r.add ({ "project.importFrom", "Importar de outro projeto"_t,
             schema ({ { "path", "string", true, "O .wnl de onde trazer (lido só como dado; não precisa estar aberto)"_t },
                       { "patterns", "string", false, "Padrões: ids ou nomes separados por vírgula; padrão = todos os que têm alguma coisa"_t },
                       { "mixer", "boolean", false, "A trilha de mixer de cada canal vem com os efeitos (padrão true); false = só o volume e o pan"_t },
                       { "transpose", "boolean", false, "As notas vão para o tom do projeto aberto (padrão false)"_t } }), true,
             [] (CommandContext& c, const juce::var& a)
             {
                 juce::String error;
                 const auto file = projectFrom (a, error);

                 if (file == juce::File())
                     return CommandResult::fail (error);

                 library::ImportSource source;

                 if (const auto r = library::readImportSource (file, source); r.failed())
                     return CommandResult::fail (r.getErrorMessage());

                 library::ImportOptions options;
                 options.mixer = params::boolean (a, "mixer", true);
                 options.transpose = params::boolean (a, "transpose", false);

                 if (params::has (a, "patterns"))
                 {
                     for (auto name : juce::StringArray::fromTokens (params::string (a, "patterns"), ",", "\""))
                         if (name.trim().isNotEmpty())
                             options.patterns.add (name.trim());
                 }
                 else
                 {
                     for (auto& info : source.patterns)
                         options.patterns.add (info.id);
                 }

                 library::ImportReport report;

                 if (const auto r = library::importInto (c.model.data(), source, options, report); r.failed())
                     return CommandResult::fail (r.getErrorMessage());

                 auto toVar = [] (const juce::StringArray& ids)
                 {
                     juce::Array<juce::var> out;

                     for (auto& id : ids)
                         out.add (id);

                     return out;
                 };

                 auto o = new juce::DynamicObject();
                 o->setProperty ("patterns", toVar (report.patterns));
                 o->setProperty ("channels", toVar (report.channels));
                 o->setProperty ("mixerTracks", toVar (report.tracks));
                 o->setProperty ("semitones", report.semitones);
                 return CommandResult::success (juce::var (o));
             } });

    r.add ({ "library.loop", "Loop nos decks"_t,
             schema ({ { "bars", "integer", true, "Compassos do loop, a partir do compasso de agora (0 = desliga)"_t } }), false,
             [] (CommandContext& c, const juce::var& a)
             {
                 auto* lib = c.app.library();

                 if (lib == nullptr)
                     return noLibrary();

                 lib->setLoopBars ((int) params::number (a, "bars", 0));
                 return CommandResult::success (lib->deckStatus());
             } });

    r.add ({ "library.deck", "Estado do deck"_t,
             schema ({}), false,
             [] (CommandContext& c, const juce::var&)
             {
                 auto* lib = c.app.library();
                 return lib != nullptr ? CommandResult::success (lib->deckStatus()) : noLibrary();
             } });
}

} // namespace whynot
