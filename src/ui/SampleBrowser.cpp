#include "SampleBrowser.h"
#include "Theme.h"
#include "fx/FxWidgets.h"
#include "../app/AppController.h"

namespace whynot
{

namespace
{
    juce::var args (std::initializer_list<std::pair<const char*, juce::var>> kv)
    {
        auto o = new juce::DynamicObject();
        for (auto& [k, v] : kv) o->setProperty (k, v);
        return juce::var (o);
    }

    bool byName (const juce::File& a, const juce::File& b)
    {
        return a.getFileName().compareNatural (b.getFileName()) < 0;
    }
}

//==============================================================================
/** Lê todas as pastas em segundo plano e devolve a lista de sons para a busca. */
class SampleBrowser::Indexer : public juce::Thread
{
public:
    Indexer (juce::Array<juce::File> foldersToScan, juce::Component::SafePointer<SampleBrowser> ownerToNotify)
        : juce::Thread ("WHYNOT indice de sons"), folders (std::move (foldersToScan)), owner (ownerToNotify)
    {
    }

    void run() override
    {
        juce::Array<juce::File> found;

        for (auto& folder : folders)
        {
            for (const auto& entry : juce::RangedDirectoryIterator (folder, true, audioWildcard, juce::File::findFiles))
            {
                if (threadShouldExit() || found.size() >= maxIndexed)
                    break;

                if (! entry.isHidden())
                    found.add (entry.getFile());
            }
        }

        if (threadShouldExit())
            return;

        juce::MessageManager::callAsync ([target = owner, files = std::move (found)]() mutable
        {
            if (auto* browser = target.getComponent())
                browser->indexReady (std::move (files));
        });
    }

private:
    juce::Array<juce::File> folders;
    juce::Component::SafePointer<SampleBrowser> owner;
};

//==============================================================================
SampleBrowser::SampleBrowser (AppController& a) : app (a)
{
    juce::PropertiesFile::Options options;
    options.applicationName = "Navegador";
    options.filenameSuffix = ".settings";
    options.folderName = "WHYNOT LAB";
    options.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile> (options);

    getLibraryFolder().createDirectory();
    loadSettings();

    openButton.setTooltip ("Abrir a Biblioteca no Explorer: coloque ali seus drum kits, loops e one-shots"_t);
    openButton.onClick = [this] { getLibraryFolder().startAsProcess(); };

    addButton.setTooltip ("Adicionar outra pasta de sons à lista"_t);
    addButton.onClick = [this] { chooseFolderToAdd(); };

    filterButton.setTooltip ("Filtrar pelo tom e pelo BPM do projeto, ou só loops ou só one-shots"_t);
    filterButton.setNeutralWhenOn (true);
    filterButton.onClick = [this] { showFilterMenu(); };
    updateFilterButton();

    // a leitura dos sons (BPM e tom) termina em segundo plano: a lista acende as etiquetas (ou refaz o filtro)
    if (auto* cache = app.soundInfo())
        cache->onAnalysed = [safe = juce::Component::SafePointer<SampleBrowser> (this)] { if (safe != nullptr) safe->soundsAnalysed(); };

    favouritesButton.setTooltip ("Só os favoritos: as pastas de kit e os sons marcados com a estrela do fim da linha"_t);
    favouritesButton.setClickingTogglesState (true);
    favouritesButton.onClick = [this] { setFavouritesOnly (favouritesButton.getToggleState()); };

    search.setTextToShowWhenEmpty ("Buscar som", theme::colour::textFaint);
    search.setFont (theme::font (theme::type::body));
    search.setIndents (32, 8);
    search.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    search.onTextChange = [this] { rebuildRows(); };
    search.onEscapeKey = [this] { search.clear(); rebuildRows(); };
    search.onReturnKey = [this]
    {
        // Enter na busca leva para a lista, já no primeiro som achado
        list.grabKeyboardFocus();

        for (int i = 0; i < (int) rows.size(); ++i)
            if (rows[(size_t) i].kind == Kind::file) { list.selectRow (i); break; }
    };

    list.setRowHeight (rowHeight);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setMultipleSelectionEnabled (false);

    addAndMakeVisible (favouritesButton);
    addAndMakeVisible (filterButton);
    addAndMakeVisible (openButton);
    addAndMakeVisible (addButton);
    addAndMakeVisible (search);
    addAndMakeVisible (list);
    Favorites::get().addListener (this);

    rebuildRoots();
    rebuildRows();
}

SampleBrowser::~SampleBrowser()
{
    Favorites::get().removeListener (this);
    stopTimer();

    if (auto* cache = app.soundInfo())
        cache->onAnalysed = nullptr;

    if (indexer != nullptr)
        indexer->stopThread (3000);

    list.setModel (nullptr);
}

juce::File SampleBrowser::getLibraryFolder() const
{
    return app.getDefaultProjectsFolder().getChildFile ("Biblioteca");
}

//==============================================================================
void SampleBrowser::loadSettings()
{
    extraFolders = juce::StringArray::fromLines (settings->getValue ("extraFolders"));
    extraFolders.removeEmptyStrings();
    keyFilter = (soundinfo::KeyFilter) juce::jlimit (0, 2, settings->getIntValue ("keyFilter", 0));
    bpmFilter = (soundinfo::BpmFilter) juce::jlimit (0, 1, settings->getIntValue ("bpmFilter", 0));
    kindFilter = (soundinfo::KindFilter) juce::jlimit (0, 2, settings->getIntValue ("kindFilter", 0));

    if (settings->containsKey ("expanded"))
    {
        for (auto& path : juce::StringArray::fromLines (settings->getValue ("expanded")))
            if (path.isNotEmpty())
                expanded.insert (path);
    }
    else
    {
        // primeira vez: kit e biblioteca já abertos
        expanded.insert (app.getKitFolder().getFullPathName());
        expanded.insert (getLibraryFolder().getFullPathName());
    }
}

void SampleBrowser::saveSettings()
{
    juce::StringArray open;
    for (auto& path : expanded)
        open.add (path);

    settings->setValue ("extraFolders", extraFolders.joinIntoString ("\n"));
    settings->setValue ("expanded", open.joinIntoString ("\n"));
    settings->setValue ("keyFilter", (int) keyFilter);
    settings->setValue ("bpmFilter", (int) bpmFilter);
    settings->setValue ("kindFilter", (int) kindFilter);
    settings->saveIfNeeded();
}

void SampleBrowser::rebuildRoots()
{
    roots.clear();

    if (const auto kit = app.getKitFolder(); kit.isDirectory())
        roots.push_back ({ kit, "Kit WHYNOT", false });

    roots.push_back ({ getLibraryFolder(), "Biblioteca", false });

    for (auto& path : extraFolders)
        if (const juce::File folder (path); folder.isDirectory())
            roots.push_back ({ folder, folder.getFileName(), true });
}

bool SampleBrowser::isSearching() const
{
    return search.getText().trim().isNotEmpty();
}

void SampleBrowser::rebuildRows()
{
    // a linha escolhida segue o arquivo: a lista do filtro cresce enquanto a leitura corre
    const int selectedRow = list.getSelectedRow();
    const auto selectedFile = juce::isPositiveAndBelow (selectedRow, (int) rows.size()) ? rows[(size_t) selectedRow].file : juce::File();

    rows.clear();
    filterPending = filterMatched = 0;

    if (isFiltering() && ! isFavouritesOnly())
    {
        // o filtro do tom e do BPM: todas as pastas (e as palavras da busca, se houver); o que ainda não foi lido entra
        // na fila, na ordem do índice, e aparece quando a leitura disser que combina
        const auto words = juce::StringArray::fromTokens (search.getText().trim(), true);
        auto* cache = app.soundInfo();
        const auto status = cache != nullptr ? cache->statusOf (index) : std::vector<SoundInfoCache::Status>();   // uma trava só
        std::vector<juce::File> wanted;

        for (int i = 0; i < index.size() && cache != nullptr; ++i)
        {
            const auto& f = index.getReference (i);
            const auto& st = status[(size_t) i];
            bool matches = true;

            if (! words.isEmpty())
            {
                const auto name = f.getFileNameWithoutExtension();

                for (auto& w : words)
                    if (! name.containsIgnoreCase (w)) { matches = false; break; }
            }

            if (! matches)
                continue;

            const bool read = st.info.has_value() && st.info->analysed;

            if (! read || ! st.settled)
            {
                if ((int) wanted.size() < SoundInfoCache::maxQueue)
                    wanted.push_back (f);

                if (! read)
                {
                    ++filterPending;
                    continue;
                }
            }

            if (filterPasses (*st.info) && ++filterMatched <= maxResults)
                rows.push_back ({ Kind::file, f, f.getFileNameWithoutExtension(), f.getParentDirectory().getFileName(), 0 });
        }

        if (cache != nullptr)
            cache->requestMany (wanted);   // o resto entra quando a fila esvaziar (o aviso refaz a lista)

        juce::String caption = filterMatched == 0 ? (indexing || filterPending > 0 ? "Lendo os sons..."_t : "Nada combina com o filtro"_t)
                                                  : juce::String (filterMatched) + (filterMatched == 1 ? " som combina" : " sons combinam");
        caption << ": " << filterCaption();

        if (filterPending > 0)
            caption << " (lendo mais " << juce::String (filterPending) << ")";

        rows.insert (rows.begin(), Node { Kind::caption, {}, caption });
    }
    else if (isSearching())
    {
        const auto words = juce::StringArray::fromTokens (search.getText().trim(), true);
        int found = 0;

        // só os favoritos: um conjunto (o índice pode ter 50 mil sons), em minúsculas como o Windows compara
        const bool onlyFavourites = isFavouritesOnly();
        std::set<juce::String> favourites;

        for (auto& path : Favorites::get().list (Favorites::Kind::sample))
            favourites.insert (path.toLowerCase());

        for (auto& f : index)
        {
            if (onlyFavourites && favourites.count (f.getFullPathName().toLowerCase()) == 0)
                continue;

            const auto name = f.getFileNameWithoutExtension();
            bool matches = true;

            for (auto& w : words)
                if (! name.containsIgnoreCase (w)) { matches = false; break; }

            if (! matches)
                continue;

            if (++found <= maxResults)
                rows.push_back ({ Kind::file, f, name, f.getParentDirectory().getFileName(), 0 });
        }

        juce::String caption;
        if (found == 0)               caption = indexing ? "Lendo as pastas..."_t : "Nada com esse nome"_t;
        else if (found > maxResults)  caption = juce::String (found) + " sons (mostrando " + juce::String (maxResults) + ")";
        else                          caption = juce::String (found) + (found == 1 ? " som" : " sons");

        rows.insert (rows.begin(), Node { Kind::caption, {}, caption });
    }
    else if (isFavouritesOnly())
    {
        appendFavourites();
    }
    else
    {
        for (auto& r : roots)
        {
            rows.push_back ({ Kind::root, r.folder, r.label, {}, 0, r.removable });

            if (expanded.count (r.folder.getFullPathName()) > 0)
                appendChildren (r.folder, 1);
        }
    }

    list.updateContent();

    if (selectedFile != juce::File())
    {
        const juce::ScopedValueSetter<bool> quiet (restoringSelection, true);   // sem tocar a prévia de novo
        const int now = rowOf (selectedFile);

        if (now >= 0 && now != selectedRow)
            list.selectRow (now, true);
        else if (now < 0)
            list.deselectAllRows();
    }

    list.repaint();
}

void SampleBrowser::appendChildren (const juce::File& folder, int depth)
{
    auto folders = folder.findChildFiles (juce::File::findDirectories, false);
    auto files = folder.findChildFiles (juce::File::findFiles, false, audioWildcard);
    std::sort (folders.begin(), folders.end(), byName);
    std::sort (files.begin(), files.end(), byName);

    if (folders.isEmpty() && files.isEmpty())
    {
        rows.push_back ({ Kind::hint, {}, folder == getLibraryFolder()
                                              ? "Pasta vazia. Abra com o botão da pasta, lá em cima, e coloque seus kits aqui."_t
                                              : "Nenhum som nesta pasta"_t, {}, depth });
        return;
    }

    for (auto& f : folders)
    {
        if (f.isHidden())
            continue;

        rows.push_back ({ Kind::folder, f, f.getFileName(), {}, depth });

        if (depth < 12 && expanded.count (f.getFullPathName()) > 0)
            appendChildren (f, depth + 1);
    }

    for (auto& f : files)
        if (! f.isHidden())
            rows.push_back ({ Kind::file, f, f.getFileNameWithoutExtension(), f.getFileExtension().substring (1).toUpperCase(), depth });
}

void SampleBrowser::appendFavourites()
{
    // Pastas (kits) primeiro, abrindo como na árvore; depois os sons, com a pasta de cada um à direita.
    // O que foi apagado ou desconectado do disco não aparece, mas continua marcado (pode voltar).
    auto& favourites = Favorites::get();
    juce::Array<juce::File> folders, files;

    for (auto& path : favourites.list (Favorites::Kind::folder))
        if (const juce::File f (path); f.isDirectory())
            folders.add (f);

    for (auto& path : favourites.list (Favorites::Kind::sample))
        if (const juce::File f (path); f.existsAsFile())
            files.add (f);

    if (folders.isEmpty() && files.isEmpty())
    {
        rows.push_back ({ Kind::hint, {}, "Nenhum favorito ainda. Passe o mouse num som ou numa pasta e clique na estrela do fim da linha."_t,
                          {}, 0 });
        return;
    }

    std::sort (folders.begin(), folders.end(), byName);
    std::sort (files.begin(), files.end(), byName);

    if (! folders.isEmpty())
    {
        rows.push_back (Node { Kind::caption, {}, "Pastas" });

        for (auto& f : folders)
        {
            rows.push_back ({ Kind::folder, f, f.getFileName(), f.getParentDirectory().getFileName(), 0 });

            if (expanded.count (f.getFullPathName()) > 0)
                appendChildren (f, 1);
        }
    }

    if (! files.isEmpty())
    {
        rows.push_back (Node { Kind::caption, {}, "Sons" });

        for (auto& f : files)
            rows.push_back ({ Kind::file, f, f.getFileNameWithoutExtension(), f.getParentDirectory().getFileName(), 0 });
    }
}

void SampleBrowser::setFavouritesOnly (bool on)
{
    favouritesButton.setToggleState (on, juce::dontSendNotification);
    favouritesButton.setIcon (on ? Icon::starFilled : Icon::star);
    rebuildRows();
    repaint();
}

void SampleBrowser::favoritesChanged()
{
    // Só o filtro muda de linhas; na árvore nada sai do lugar ao marcar.
    if (isFavouritesOnly())
        rebuildRows();
    else
        list.repaint();

    repaint (getLocalBounds().removeFromBottom (footerHeight));
}

bool SampleBrowser::canStar (const Node& n) const
{
    // as raízes fixas (Kit WHYNOT, Biblioteca) já estão sempre ali; pasta extra e subpastas são kits
    return n.kind == Kind::file || n.kind == Kind::folder || (n.kind == Kind::root && n.removable);
}

bool SampleBrowser::isFavourite (const Node& n) const
{
    return canStar (n) && Favorites::get().contains (n.kind == Kind::file ? Favorites::Kind::sample : Favorites::Kind::folder,
                                                     n.file.getFullPathName());
}

void SampleBrowser::toggleFavourite (const Node& n)
{
    if (! canStar (n))
        return;

    const auto kind = n.kind == Kind::file ? Favorites::Kind::sample : Favorites::Kind::folder;
    const auto path = n.file.getFullPathName();
    const bool on = Favorites::get().toggle (kind, path);

    if (onStatus)
        onStatus (juce::File (path).getFileName() + (on ? " nos favoritos" : " saiu dos favoritos"), false);
}

juce::Rectangle<int> SampleBrowser::starColumn (int width, int height) const
{
    // os 26 px da estrela e a margem da direita (a linha tem 16 px de recuo), para o clique não exigir mira
    return { width - 16 - favui::starColumn, 0, 16 + favui::starColumn, height };
}

int SampleBrowser::rowOf (const juce::File& f) const
{
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].kind != Kind::caption && rows[i].kind != Kind::hint && rows[i].file == f)
            return (int) i;

    return -1;
}

void SampleBrowser::toggle (int row)
{
    const auto key = rows[(size_t) row].file.getFullPathName();

    if (expanded.count (key) > 0)
        expanded.erase (key);
    else
        expanded.insert (key);

    saveSettings();
    rebuildRows();
}

//==============================================================================
void SampleBrowser::startIndexing()
{
    if (indexer != nullptr)
        indexer->stopThread (3000);

    juce::Array<juce::File> folders;
    for (auto& r : roots)
        folders.add (r.folder);

    indexing = true;
    indexer = std::make_unique<Indexer> (folders, juce::Component::SafePointer<SampleBrowser> (this));
    indexer->startThread (juce::Thread::Priority::low);
    repaint();
}

void SampleBrowser::indexReady (juce::Array<juce::File> files)
{
    index = std::move (files);
    indexing = false;

    if (isSearching() || isFiltering())   // com o filtro salvo ligado, a lista vem do índice
        rebuildRows();

    repaint();
}

//==============================================================================
// O filtro por tom e BPM (bloco 4b, 23/10)
bool SampleBrowser::isFiltering() const noexcept
{
    return keyFilter != soundinfo::KeyFilter::any || bpmFilter != soundinfo::BpmFilter::any || kindFilter != soundinfo::KindFilter::any;
}

bool SampleBrowser::filterPasses (const SoundInfo& info) const
{
    const auto& p = app.getModel().get();
    return soundinfo::keyFits (info, p.keyRoot, Project::scaleDegrees (p.scale), keyFilter) && soundinfo::bpmFits (info.bpm, p.bpm, bpmFilter)
        && soundinfo::kindFits (info, kindFilter);   // com a escala do projeto (dórico, frígio...); na cromática, todo tom
}

namespace
{
    juce::String keyLabel (int root, bool major)
    {
        SoundInfo k;
        k.keyRoot = ((root % 12) + 12) % 12;
        k.keyMajor = major;
        return k.keyText();
    }
}

juce::String SampleBrowser::filterCaption() const
{
    const auto& p = app.getModel().get();
    juce::StringArray parts;

    if (keyFilter != soundinfo::KeyFilter::any)
        parts.add ((keyFilter == soundinfo::KeyFilter::project ? "no tom "_t : "combina com "_t) + soundinfo::projectKeyText (p.keyRoot, p.scale));

    if (bpmFilter != soundinfo::BpmFilter::any)
        parts.add ("perto de "_t + juce::String (juce::roundToInt (p.bpm)) + " BPM");

    if (kindFilter != soundinfo::KindFilter::any)
        parts.add (kindFilter == soundinfo::KindFilter::loops ? "só loops"_t : "só one-shots"_t);

    return parts.joinIntoString (", ");
}

void SampleBrowser::updateFilterButton()
{
    filterButton.setToggleState (isFiltering(), juce::dontSendNotification);
    filterButton.setTooltip (isFiltering() ? "Filtro: "_t + filterCaption() + ". Clique para mudar ou tirar."_t
                                           : juce::String ("Filtrar pelo tom e pelo BPM do projeto, ou só loops ou só one-shots"_t));
}

void SampleBrowser::setIndexForTest (juce::Array<juce::File> files)
{
    if (indexer != nullptr)
        indexer->stopThread (3000);   // o índice das pastas de verdade não chega por cima

    indexReady (std::move (files));
    rebuildRows();
}

void SampleBrowser::setFilters (soundinfo::KeyFilter key, soundinfo::BpmFilter bpm, soundinfo::KindFilter kind)
{
    keyFilter = key;
    bpmFilter = bpm;
    kindFilter = kind;
    saveSettings();
    updateFilterButton();

    if (isFiltering() && index.isEmpty() && ! indexing)
        startIndexing();   // o filtro procura em todas as pastas

    rebuildRows();
    repaint();
}

void SampleBrowser::soundsAnalysed()
{
    // com o filtro, os sons que acabaram de ser lidos podem entrar na lista (no máximo a cada 250 ms, para a lista
    // não pular enquanto a leitura corre); sem ele, só as etiquetas acendem
    if (isFiltering() && ! isFavouritesOnly())
    {
        if (! rebuildQueued)
        {
            rebuildQueued = true;
            juce::Timer::callAfterDelay (index.size() > 5000 ? 1000 : 250, [safe = juce::Component::SafePointer<SampleBrowser> (this)]
            {
                if (safe == nullptr)
                    return;

                safe->rebuildQueued = false;
                safe->rebuildRows();
                safe->repaint (safe->getLocalBounds().removeFromBottom (footerHeight));
            });
        }

        return;
    }

    list.repaint();
    repaint (getLocalBounds().removeFromBottom (footerHeight));
}

void SampleBrowser::showFilterMenu()
{
    const auto& p = app.getModel().get();
    const bool chromatic = p.scale == "chromatic";
    const auto degrees = Project::scaleDegrees (p.scale);
    const auto projectKey = soundinfo::projectKeyText (p.keyRoot, p.scale);
    const int bpm = juce::roundToInt (p.bpm);

    // os tons que passam em cada filtro, pela escala do projeto (Ré dórico: Dó maior e Lá menor)
    auto names = [] (const std::vector<std::pair<int, bool>>& keys, const std::vector<std::pair<int, bool>>& skip)
    {
        juce::StringArray out;

        for (auto& k : keys)
            if (std::find (skip.begin(), skip.end(), k) == skip.end())
                out.add (keyLabel (k.first, k.second));

        return out.joinIntoString (", ");
    };
    const auto same = soundinfo::keysThatFit (p.keyRoot, degrees, soundinfo::KeyFilter::project);
    const auto neighbours = soundinfo::keysThatFit (p.keyRoot, degrees, soundinfo::KeyFilter::compatible);

    juce::PopupMenu menu;
    menu.addSectionHeader (chromatic ? "Tom (o projeto está na escala cromática: todo tom serve)"_t : "Tom (o do projeto: "_t + projectKey + ")");
    menu.addItem (1, "Qualquer tom"_t, true, keyFilter == soundinfo::KeyFilter::any);
    menu.addItem (2, "As mesmas notas do projeto ("_t + names (same, {}) + ")", ! chromatic, keyFilter == soundinfo::KeyFilter::project);
    menu.addItem (3, "Combina: também os vizinhos ("_t + names (neighbours, same) + ")", ! chromatic, keyFilter == soundinfo::KeyFilter::compatible);
    menu.addSectionHeader ("Andamento"_t);
    menu.addItem (10, "Qualquer BPM"_t, true, bpmFilter == soundinfo::BpmFilter::any);
    menu.addItem (11, "Perto do projeto: "_t + juce::String (bpm) + " BPM (4% para mais ou para menos, ou "_t
                      + juce::String (juce::roundToInt (p.bpm / 2.0)) + " e " + juce::String (bpm * 2) + ")",
                  true, bpmFilter == soundinfo::BpmFilter::project);
    menu.addSectionHeader ("Tipo"_t);
    menu.addItem (20, "Loops e one-shots"_t, true, kindFilter == soundinfo::KindFilter::any);
    menu.addItem (21, "Só loops"_t, true, kindFilter == soundinfo::KindFilter::loops);
    menu.addItem (22, "Só one-shots"_t, true, kindFilter == soundinfo::KindFilter::oneShots);
    menu.addSeparator();
    menu.addItem (30, "Tirar os filtros"_t, isFiltering());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&filterButton),
                        [safe = juce::Component::SafePointer<SampleBrowser> (this)] (int choice)
    {
        if (safe == nullptr || choice == 0)
            return;

        auto key = safe->keyFilter;
        auto bpm = safe->bpmFilter;
        auto kind = safe->kindFilter;

        if (choice >= 1 && choice <= 3)    key = (soundinfo::KeyFilter) (choice - 1);
        if (choice == 10 || choice == 11)  bpm = (soundinfo::BpmFilter) (choice - 10);
        if (choice >= 20 && choice <= 22)  kind = (soundinfo::KindFilter) (choice - 20);

        if (choice == 30)
        {
            key = soundinfo::KeyFilter::any;
            bpm = soundinfo::BpmFilter::any;
            kind = soundinfo::KindFilter::any;
        }

        safe->setFilters (key, bpm, kind);
    });
}

//==============================================================================
void SampleBrowser::preview (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) rows.size()) || rows[(size_t) row].kind != Kind::file)
        return;

    const auto& file = rows[(size_t) row].file;

    if (app.previewFile (file))
    {
        previewPath = file.getFullPathName();
        previewing = true;
        startTimerHz (15);
    }
    else if (onStatus)
    {
        onStatus ("Não deu para abrir "_t + file.getFileName(), true);
    }

    list.repaint();
}

void SampleBrowser::timerCallback()
{
    // o fim da prévia é detectado na thread de áudio: aqui só se lê o estado
    const bool now = app.isPreviewing();

    if (now != previewing)
    {
        previewing = now;
        list.repaint();
    }

    if (! previewing)
        stopTimer();
}

void SampleBrowser::addToRack (const juce::File& file)
{
    const auto result = app.run ("channel.addSampler", args ({ { "path", file.getFullPathName() } }));

    const bool is808 = result.ok && result.value["role"].toString() == "808";

    if (onStatus)
        onStatus (result.ok ? file.getFileNameWithoutExtension() + (is808 ? " no channel rack como 808: mono, com glide e respeitando a nota"_t
                                                                          : juce::String (" no channel rack"))
                            : result.error,
                  ! result.ok);
}

void SampleBrowser::addKitToRack (const juce::File& folder)
{
    const auto result = app.run ("channel.addKit", args ({ { "folder", folder.getFullPathName() } }));

    if (onStatus)
        onStatus (result.ok ? "Kit "_t + folder.getFileName() + ": " + juce::String (result.value["channels"].size()) + " canais no channel rack"
                            : result.error,
                  ! result.ok);
}

void SampleBrowser::sliceToRack (const juce::File& file)
{
    const auto result = app.run ("channel.addSlicer", args ({ { "path", file.getFullPathName() } }));

    if (onStatus)
        onStatus (result.ok ? file.getFileNameWithoutExtension() + ": "_t + juce::String ((int) result.value["slices"])
                                  + " fatias no channel rack, com as notas no piano roll"_t
                            : result.error,
                  ! result.ok);
}

void SampleBrowser::chooseFolderToAdd()
{
    chooser = std::make_unique<juce::FileChooser> ("Escolha uma pasta de sons"_t,
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory));

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto folder = fc.getResult();

                              if (! folder.isDirectory())
                                  return;

                              for (auto& r : roots)
                              {
                                  if (r.folder == folder)
                                  {
                                      if (onStatus) onStatus ("Essa pasta já está na lista"_t, false);
                                      return;
                                  }
                              }

                              extraFolders.addIfNotAlreadyThere (folder.getFullPathName());
                              expanded.insert (folder.getFullPathName());
                              saveSettings();
                              rebuildRoots();
                              rebuildRows();
                              startIndexing();

                              if (onStatus) onStatus (folder.getFileName() + " entrou nos Sons", false);
                          });
}

void SampleBrowser::showRowMenu (int row)
{
    const auto node = rows[(size_t) row];
    juce::PopupMenu menu;

    if (node.kind == Kind::file)
    {
        menu.addItem (1, "Ouvir");
        menu.addItem (2, "Adicionar ao channel rack");
        menu.addItem (6, "Fatiar no channel rack (uma fatia por tecla)"_t);
        menu.addSeparator();
        menu.addItem (8, isFavourite (node) ? "Tirar dos favoritos" : "Marcar como favorito");   // o 6 é o fatiar
        menu.addItem (3, "Mostrar no Explorer");
    }
    else if (node.kind == Kind::root || node.kind == Kind::folder)
    {
        menu.addItem (7, "Adicionar o kit ao channel rack (um canal por som)"_t);

        if (canStar (node))
            menu.addItem (8, isFavourite (node) ? "Tirar dos favoritos" : "Marcar como favorito (kit)");

        menu.addItem (4, "Abrir no Explorer");

        if (node.removable)
            menu.addItem (5, "Tirar da lista");
    }
    else
    {
        return;
    }

    juce::Component::SafePointer<SampleBrowser> safe (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&list), [safe, node, row] (int choice)
    {
        if (safe == nullptr || choice == 0)
            return;

        switch (choice)
        {
            case 1: safe->preview (safe->rowOf (node.file)); break;   // a linha pode ter mudado com o filtro lendo
            case 2: safe->addToRack (node.file); break;
            case 6: safe->sliceToRack (node.file); break;
            case 7: safe->addKitToRack (node.file); break;
            case 3: node.file.revealToUser(); break;
            case 4: node.file.startAsProcess(); break;
            case 5:
                safe->extraFolders.removeString (node.file.getFullPathName());
                safe->saveSettings();
                safe->rebuildRoots();
                safe->rebuildRows();
                safe->startIndexing();
                break;
            case 8: safe->toggleFavourite (node); break;
            default: break;
        }
    });
}

//==============================================================================
int SampleBrowser::getNumRows()
{
    return (int) rows.size();
}

void SampleBrowser::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    using namespace theme;

    if (! juce::isPositiveAndBelow (row, (int) rows.size()))
        return;

    const auto& n = rows[(size_t) row];
    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (8, 0);

    if (n.kind == Kind::caption)
    {
        g.setColour (colour::textFaint);
        g.setFont (captionFont());
        g.drawText (fxui::caps (n.label), area.withTrimmedTop (6), juce::Justification::centredLeft);   // "LÁ MENOR", com o acento
        return;
    }

    if (n.kind == Kind::hint)
    {
        g.setColour (colour::textFaint);
        g.setFont (font (type::body));
        g.drawFittedText (n.label, area.withTrimmedLeft (8 + n.depth * indent + 18), juce::Justification::centredLeft, 2, 0.9f);
        return;
    }

    if (selected)
    {
        g.setColour (colour::raisedHigh);
        g.fillRoundedRectangle (area.toFloat().reduced (0.0f, 1.0f), metric::radius);
    }

    area.reduce (8, 0);

    // estrela no fim da linha (som, pasta de kit): cheia se é favorito; o contorno aparece com o mouse na linha
    const auto starBox = area.removeFromRight (favui::starColumn).withTrimmedLeft (6);

    if (canStar (n))
        favui::drawStar (g, starBox.toFloat(), isFavourite (n), selected || hover.getRow() == row,
                         hover.isOver (row, starColumn (width, height)));

    area.removeFromLeft (n.depth * indent);

    const bool isFolder = n.kind == Kind::root || n.kind == Kind::folder;
    const auto chevron = area.removeFromLeft (14).toFloat();

    if (isFolder)
        icons::draw (g, expanded.count (n.file.getFullPathName()) > 0 ? Icon::chevronDown : Icon::chevronRight,
                     chevron.withSizeKeepingCentre (12.0f, 12.0f), colour::textFaint);

    area.removeFromLeft (4);

    // o som que está tocando na prévia acende em laranja
    const bool sounding = n.kind == Kind::file && previewing && n.file.getFullPathName() == previewPath;
    const auto ink = sounding ? colour::signal : (selected ? colour::text : colour::textDim);
    icons::draw (g, isFolder ? Icon::folder : Icon::wave, area.removeFromLeft (16).toFloat().withSizeKeepingCentre (16.0f, 16.0f), ink);
    area.removeFromLeft (8);

    // o BPM e o tom (bloco 4b), pelo nome ou pela leitura em segundo plano; sem eles, a extensão ou a pasta
    auto detail = n.detail;

    if (n.kind == Kind::file)
    {
        if (auto* cache = app.soundInfo())
        {
            const auto info = cache->get (n.file);

            if (! cache->isSettled (n.file))
                cache->request (n.file);   // o que está na tela é lido primeiro

            if (const auto tags = info.tags(); tags.isNotEmpty())
                detail = tags;
        }
    }

    if (detail.isNotEmpty())
    {
        const auto detailFont = font (type::small);
        const int w = juce::jmin (area.getWidth() / 2, juce::roundToInt (juce::GlyphArrangement::getStringWidth (detailFont, detail)) + 4);
        g.setColour (colour::textFaint);
        g.setFont (detailFont);
        g.drawText (detail, area.removeFromRight (w), juce::Justification::centredRight, true);
        area.removeFromRight (8);
    }

    g.setColour (sounding ? colour::signal : colour::text);
    g.setFont (font (type::body, n.kind == Kind::root ? Weight::semibold : Weight::medium));
    g.drawText (n.label, area, juce::Justification::centredLeft, true);
}

void SampleBrowser::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow (row, (int) rows.size()))
        return;

    if (e.mods.isPopupMenu())
    {
        showRowMenu (row);
        return;
    }

    const auto n = rows[(size_t) row];

    // clique na estrela: marca ou desmarca, sem abrir a pasta nem tocar o som
    if (canStar (n) && starColumn (list.getVisibleRowWidth(), list.getRowHeight()).contains (e.getPosition()))
    {
        toggleFavourite (n);
        return;
    }

    if ((n.kind == Kind::root || n.kind == Kind::folder) && e.getNumberOfClicks() == 1)
        toggle (row);
    else if (n.kind == Kind::file && juce::Time::getMillisecondCounter() - lastSelectionChange > 120)
        preview (row);   // clicar de novo no mesmo som repete a prévia
}

void SampleBrowser::selectedRowsChanged (int lastRowSelected)
{
    // clique ou setas num som: ouve (o botão direito só abre o menu)
    lastSelectionChange = juce::Time::getMillisecondCounter();

    // a ListBox escolhe a linha antes de avisar o clique: na estrela, só marca (não toca)
    if (hover.isPressingIn (lastRowSelected, starColumn (list.getVisibleRowWidth(), list.getRowHeight())))
        return;

    if (! juce::ModifierKeys::currentModifiers.isPopupMenu() && ! restoringSelection)
        preview (lastRowSelected);
}

void SampleBrowser::listBoxItemDoubleClicked (int row, const juce::MouseEvent& e)
{
    // dois cliques rápidos na estrela marcam e desmarcam: não criam canal
    if (starColumn (list.getVisibleRowWidth(), list.getRowHeight()).contains (e.getPosition()))
        return;

    if (juce::isPositiveAndBelow (row, (int) rows.size()) && rows[(size_t) row].kind == Kind::file)
        addToRack (rows[(size_t) row].file);
}

void SampleBrowser::returnKeyPressed (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) rows.size()))
        return;

    const auto kind = rows[(size_t) row].kind;

    if (kind == Kind::file)
        addToRack (rows[(size_t) row].file);
    else if (kind == Kind::root || kind == Kind::folder)
        toggle (row);
}

juce::var SampleBrowser::getDragSourceDescription (const juce::SparseSet<int>& selectedRows)
{
    if (selectedRows.size() != 1)
        return {};

    const int row = selectedRows[0];

    // um som, ou uma pasta (o kit inteiro vai para o channel rack)
    if (! juce::isPositiveAndBelow (row, (int) rows.size())
        || (rows[(size_t) row].kind != Kind::file && rows[(size_t) row].kind != Kind::folder))
        return {};

    return juce::String (dragPrefix) + rows[(size_t) row].file.getFullPathName();
}

juce::String SampleBrowser::getTooltipForRow (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) rows.size()))
        return {};

    const auto& n = rows[(size_t) row];

    switch (n.kind)
    {
        case Kind::file:
        {
            // a pasta e o que se sabe do som (o BPM e o tom: de onde vieram)
            juce::String about;

            if (auto* cache = app.soundInfo())
            {
                const auto info = cache->get (n.file);

                if (info.bpm > 0.0)
                    about << juce::String (info.bpm, 1).replace (".", ",") << " BPM (" << (info.bpmSource == "nome" ? "pelo nome"_t : "pela leitura do áudio"_t) << ")\n";

                if (info.hasKey())
                    about << (info.isNote ? "Nota "_t : "Tom "_t) << info.keyText() << " (" << (info.keySource == "nome" ? "pelo nome"_t : "pela leitura do áudio"_t) << ")\n";

                if (info.seconds > 0.0)
                    about << juce::String (info.seconds, 2).replace (".", ",") << " s" << (info.isLoop() ? ", loop" : ", one-shot") << "\n";
            }

            return n.file.getFileName() + "  (" + n.file.getParentDirectory().getFileName() + ")\n" + about
                   + "Clique ouve. Arraste para o rack ou a playlist; duplo clique cria um canal."_t
                   + "\n" + "A estrela no fim da linha marca como favorito."_t;
        }

        case Kind::root:
        case Kind::folder:
            return n.file.getFullPathName() + (n.removable ? "\nBotão direito: tirar da lista"_t : juce::String())
                   + (canStar (n) ? "\n" + "A estrela no fim da linha marca o kit como favorito."_t : juce::String());

        case Kind::hint:
        case Kind::caption:
            break;
    }

    return {};
}

//==============================================================================
juce::Rectangle<int> SampleBrowser::searchArea() const
{
    return { theme::metric::pad, headerHeight, getWidth() - 2 * theme::metric::pad, theme::metric::control };
}

void SampleBrowser::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (colour::panel);

    g.setColour (colour::text);
    g.setFont (font (type::title, Weight::semibold));
    g.drawText ("Sons", getLocalBounds().removeFromTop (headerHeight).reduced (metric::pad, 0), juce::Justification::centredLeft);

    // busca: fundo de controle com a lupa à esquerda (o TextEditor fica transparente por cima)
    const auto box = searchArea().toFloat();
    g.setColour (colour::raised);
    g.fillRoundedRectangle (box, metric::radius);
    icons::draw (g, Icon::search, box.withWidth (32.0f).withSizeKeepingCentre (16.0f, 16.0f), colour::textDim);

    // rodapé: quantos sons há e como usar
    auto footer = getLocalBounds().removeFromBottom (footerHeight).reduced (metric::pad, 0);
    g.setColour (colour::line);
    g.fillRect (footer.removeFromTop (1));
    footer.removeFromTop (6);

    g.setColour (colour::textDim);
    g.setFont (font (type::small, Weight::medium));
    const int favourites = Favorites::get().list (Favorites::Kind::sample).size() + Favorites::get().list (Favorites::Kind::folder).size();
    g.drawText (isFavouritesOnly() ? juce::String (favourites) + (favourites == 1 ? " favorito" : " favoritos")
                : isFiltering() ? juce::String (filterMatched) + (filterMatched == 1 ? " som combina" : " sons combinam")
                                      + (filterPending > 0 ? " · lendo mais "_t + juce::String (filterPending) : juce::String())
                : indexing ? "Lendo as pastas..."_t
                           : juce::String (index.size()) + (index.size() == 1 ? " som nas pastas" : " sons nas pastas"),
                footer.removeFromTop (18), juce::Justification::centredLeft, true);

    g.setColour (colour::textFaint);
    g.setFont (font (type::body));
    g.drawFittedText ("Clique ouve. Arraste para o rack ou a playlist; duplo clique cria um canal."_t,
                      footer, juce::Justification::topLeft, 2, 0.9f);
}

void SampleBrowser::resized()
{
    using namespace theme;
    auto r = getLocalBounds();
    auto header = r.removeFromTop (headerHeight).reduced (metric::pad - 4, (headerHeight - metric::control) / 2);
    addButton.setBounds (header.removeFromRight (metric::control));
    header.removeFromRight (2);
    openButton.setBounds (header.removeFromRight (metric::control));
    header.removeFromRight (2);
    favouritesButton.setBounds (header.removeFromRight (metric::control));
    header.removeFromRight (2);
    filterButton.setBounds (header.removeFromRight (metric::control));

    search.setBounds (searchArea());
    r.removeFromBottom (footerHeight);
    r.removeFromTop (metric::control + metric::gap);
    list.setBounds (r.reduced (metric::gap - 4, 0));
}

void SampleBrowser::visibilityChanged()
{
    if (isVisible())
    {
        // a pasta pode ter mudado enquanto o painel estava fechado
        rebuildRoots();
        rebuildRows();
        startIndexing();
    }
    else if (previewing)
    {
        app.stopPreview();
        previewing = false;
    }
}

} // namespace whynot
