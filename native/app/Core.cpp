#include "Core.h"
#include <numeric>
#include <tuple>
namespace kn {
J readJson(const File &f) {
    require(f.existsAsFile() && f.getSize() <= 30000000,
            "JSON absent ou trop volumineux : " + f.getFileName());
    return J::parse(utf(f.loadFileAsString()));
}
void writeJson(const File &f, const J &j) {
    require(f.replaceWithText(str(j.dump(2))),
            juce::String::fromUTF8("Écriture impossible : ") + f.getFullPathName());
}
File resources() {
    static File result = [] {
        auto env = juce::SystemStats::getEnvironmentVariable("KITHARA_RESOURCES", {});
        if (env.isNotEmpty()) {
            File f(env);
            require(f.isDirectory(), "Dossier de ressources absent");
            return f;
        }
        auto folder = File::getSpecialLocation(File::currentExecutableFile).getParentDirectory();
        for (int i = 0; i < 9; ++i) {
            auto f = folder.getChildFile("resources");
            if (f.getChildFile("independent/theory/chord-types.json").existsAsFile())
                return f;
            folder = folder.getParentDirectory();
        }
        throw std::runtime_error("Ressources locales introuvables");
    }();
    return result;
}
File resource(const String &name) {
    auto r = resources(), f = r.getChildFile(name);
    require(f.isAChildOf(r), "Chemin de ressource invalide");
    return f;
}
File profileLocation(const File &applicationFolder, const String &overridePath) {
    if (overridePath.isNotEmpty())
        return File(overridePath);
    if (applicationFolder.getChildFile("portable.flag").existsAsFile())
        return applicationFolder.getChildFile("data");
    return File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("kithara-nea");
}
String defaultUiLanguage(const File &applicationFolder) {
    try {
        const auto file = applicationFolder.getChildFile("installation-language.json");
        if (file.existsAsFile() && text(readJson(file), "uiLanguage") == "en")
            return "en";
    } catch (const std::exception &) {
        // An invalid installer hint must not prevent opening the application.
    }
    return "fr";
}
File dataFolder() {
    auto f = profileLocation(File::getSpecialLocation(File::currentExecutableFile).getParentDirectory(),
                            juce::SystemStats::getEnvironmentVariable("KITHARA_PROFILE", {}));
    require(f.createDirectory().wasOk(), "Dossier utilisateur inaccessible");
    return f;
}
String sha256(const File &f) {
    juce::FileInputStream in(f);
    require(in.openedOk(), "Lecture impossible : " + f.getFileName());
    return juce::SHA256(in).toHexString();
}
void checkFile(const File &f, const String &expected) {
    require(f.existsAsFile() && sha256(f) == expected,
            juce::String::fromUTF8("Ressource absente ou altérée : ") + f.getFileName());
}
TempFolder::TempFolder() {
    folder =
        File::getSpecialLocation(File::tempDirectory).getChildFile("kithara-nea-" + juce::Uuid().toString());
    require(folder.createDirectory().wasOk(), "Dossier temporaire inaccessible");
}
TempFolder::~TempFolder() {
    auto parent = File::getSpecialLocation(File::tempDirectory);
    if (folder.getParentDirectory() == parent && folder.getFileName().startsWith("kithara-nea-"))
        folder.deleteRecursively();
}
namespace {
constexpr int64_t maxAudio = 16ll * 1024 * 1024 * 1024;
uint32_t crcUpdate(uint32_t crc, const void *p, size_t n) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            auto c = i;
            for (int k = 0; k < 8; ++k)
                c = (c >> 1) ^ ((c & 1) ? 0xedb88320u : 0);
            t[i] = c;
        }
        return t;
    }();
    auto bytes = static_cast<const uint8_t *>(p);
    while (n--)
        crc = table[(crc ^ *bytes++) & 255] ^ (crc >> 8);
    return crc;
}
uint32_t fileCrc(const File &f) {
    juce::FileInputStream in(f);
    require(in.openedOk(), "Audio illisible");
    std::vector<char> b(1024 * 1024);
    uint32_t crc = ~0u;
    for (int n; (n = in.read(b.data(), (int)b.size())) > 0;)
        crc = crcUpdate(crc, b.data(), n);
    return ~crc;
}
uint16_t u16(const uint8_t *p) { return p[0] | uint16_t(p[1]) << 8; }
uint32_t u32(const uint8_t *p) { return u16(p) | uint32_t(u16(p + 2)) << 16; }
uint64_t u64(const uint8_t *p) { return u32(p) | uint64_t(u32(p + 4)) << 32; }
struct Entry {
    std::string name;
    uint16_t method = 0;
    uint32_t crc = 0;
    uint64_t packed = 0, size = 0, offset = 0;
};
std::vector<Entry> zipEntries(const File &file) {
    juce::FileInputStream in(file);
    require(in.openedOk() && file.getSize() >= 22, "Conteneur invalide");
    auto tailSize = (int)std::min<int64_t>(file.getSize(), 65557);
    std::vector<uint8_t> tail(tailSize);
    in.setPosition(file.getSize() - tailSize);
    require(in.read(tail.data(), tailSize) == tailSize, juce::String::fromUTF8("Conteneur tronqué"));
    int end = -1;
    for (int k = tailSize - 22; k >= 0; --k)
        if (u32(tail.data() + k) == 0x06054b50 && k + 22 + u16(tail.data() + k + 20) == tailSize) {
            end = k;
            break;
        }
    require(end >= 0, "Fin de conteneur absente");
    auto e = tail.data() + end;
    require(u16(e + 4) == 0 && u16(e + 6) == 0, juce::String::fromUTF8("Archive multi-disque refusée"));
    uint64_t count = u16(e + 10), offset = u32(e + 16);
    if (count == 65535 || offset == 0xffffffff) {
        auto absolute = file.getSize() - tailSize + end;
        require(absolute >= 20, "ZIP64 invalide");
        uint8_t locator[20];
        in.setPosition(absolute - 20);
        require(in.read(locator, 20) == 20 && u32(locator) == 0x07064b50, "Localisateur ZIP64 invalide");
        uint8_t header[56];
        in.setPosition((int64_t)u64(locator + 8));
        require(in.read(header, 56) == 56 && u32(header) == 0x06064b50,
                juce::String::fromUTF8("En-tête ZIP64 invalide"));
        require(u32(header + 16) == 0 && u32(header + 20) == 0,
                juce::String::fromUTF8("Archive multi-disque refusée"));
        count = u64(header + 32);
        offset = u64(header + 48);
    }
    require(count > 0 && count <= 64 && offset < (uint64_t)file.getSize(),
            juce::String::fromUTF8("Répertoire ZIP invalide"));
    in.setPosition((int64_t)offset);
    std::vector<Entry> entries;
    std::set<std::string> names;
    for (uint64_t k = 0; k < count; ++k) {
        uint8_t h[46];
        require(in.read(h, 46) == 46 && u32(h) == 0x02014b50, juce::String::fromUTF8("Entrée ZIP invalide"));
        require((u16(h + 8) & 1) == 0 && u16(h + 34) == 0,
                juce::String::fromUTF8("Entrée chiffrée ou multi-disque refusée"));
        Entry r;
        r.method = u16(h + 10);
        r.crc = u32(h + 16);
        r.packed = u32(h + 20);
        r.size = u32(h + 24);
        r.offset = u32(h + 42);
        auto n = u16(h + 28), x = u16(h + 30), c = u16(h + 32);
        require(n > 0 && n <= 512, "Nom ZIP invalide");
        std::vector<char> name(n);
        require(in.read(name.data(), n) == n, juce::String::fromUTF8("Nom ZIP tronqué"));
        r.name.assign(name.begin(), name.end());
        std::vector<uint8_t> extra(x);
        require(in.read(extra.data(), x) == x, juce::String::fromUTF8("ZIP tronqué"));
        in.skipNextBytes(c);
        for (size_t p = 0; p + 4 <= extra.size();) {
            auto id = u16(extra.data() + p), len = u16(extra.data() + p + 2);
            p += 4;
            require(p + len <= extra.size(), "Extension ZIP invalide");
            if (id == 1) {
                auto q = p;
                auto take = [&]() {
                    require(q + 8 <= p + len, juce::String::fromUTF8("Extension ZIP64 tronquée"));
                    auto v = u64(extra.data() + q);
                    q += 8;
                    return v;
                };
                if (r.size == 0xffffffff)
                    r.size = take();
                if (r.packed == 0xffffffff)
                    r.packed = take();
                if (r.offset == 0xffffffff)
                    r.offset = take();
            }
            p += len;
        }
        require(names.insert(r.name).second, juce::String::fromUTF8("Entrée ZIP dupliquée"));
        require(r.method == 0 || r.method == 8, "Compression ZIP non prise en charge");
        require(r.size <= maxAudio && r.offset < (uint64_t)file.getSize() &&
                    r.packed <= (uint64_t)file.getSize(),
                "Taille ZIP invalide");
        entries.push_back(r);
    }
    return entries;
}
void extractEntry(const File &zip, const Entry &entry, juce::OutputStream &out, uint64_t limit) {
    require(entry.size <= limit, juce::String::fromUTF8("Entrée trop volumineuse"));
    auto input = std::make_unique<juce::FileInputStream>(zip);
    require(input->openedOk(), "Archive inaccessible");
    input->setPosition((int64_t)entry.offset);
    uint8_t h[30];
    require(input->read(h, 30) == 30 && u32(h) == 0x04034b50,
            juce::String::fromUTF8("En-tête local invalide"));
    auto start = (int64_t)entry.offset + 30 + u16(h + 26) + u16(h + 28);
    require(start >= 0 && (uint64_t)start + entry.packed <= (uint64_t)zip.getSize(),
            juce::String::fromUTF8("Données ZIP hors archive"));
    std::unique_ptr<juce::InputStream> stream =
        std::make_unique<juce::SubregionStream>(input.release(), start, (int64_t)entry.packed, true);
    if (entry.method == 8)
        stream = std::make_unique<juce::GZIPDecompressorInputStream>(
            stream.release(), true, juce::GZIPDecompressorInputStream::deflateFormat, (int64_t)entry.size);
    std::vector<char> block(1024 * 1024);
    uint64_t size = 0;
    uint32_t crc = ~0u;
    for (int n; (n = stream->read(block.data(), (int)block.size())) > 0;) {
        size += n;
        require(size <= entry.size && size <= limit,
                juce::String::fromUTF8("Décompression trop volumineuse"));
        crc = crcUpdate(crc, block.data(), n);
        require(out.write(block.data(), (size_t)n), juce::String::fromUTF8("Écriture temporaire impossible"));
    }
    require(size == entry.size && ~crc == entry.crc, juce::String::fromUTF8("Entrée ZIP endommagée"));
}
void little16(juce::OutputStream &o, uint16_t x) { o.writeShort((short)x); }
void little32(juce::OutputStream &o, uint32_t x) { o.writeInt((int)x); }
void little64(juce::OutputStream &o, uint64_t x) { o.writeInt64((juce::int64)x); }
void atomicReplace(const File &temp, const File &target) {
    auto h = CreateFileW(temp.getFullPathName().toWideCharPointer(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(h != INVALID_HANDLE_VALUE, "Projet temporaire inaccessible");
    auto ok = FlushFileBuffers(h);
    CloseHandle(h);
    require(ok && MoveFileExW(temp.getFullPathName().toWideCharPointer(),
                              target.getFullPathName().toWideCharPointer(),
                              MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH),
            "Remplacement du projet impossible");
}
void writeZip(const File &file, const std::string &manifest, const File &audio, const std::string &member) {
    juce::FileOutputStream out(file);
    require(out.openedOk(), juce::String::fromUTF8("Création du conteneur impossible"));
    std::vector<Entry> entries;
    auto add = [&](std::string name, const File &source, const std::string *content) {
        Entry e;
        e.name = std::move(name);
        e.size = content ? content->size() : (uint64_t)source.getSize();
        e.packed = e.size;
        e.crc = content ? ~crcUpdate(~0u, content->data(), content->size()) : fileCrc(source);
        e.offset = (uint64_t)out.getPosition();
        little32(out, 0x04034b50);
        little16(out, 45);
        little16(out, 0x800);
        little16(out, 0);
        little16(out, 0);
        little16(out, 0);
        little32(out, e.crc);
        little32(out, 0xffffffff);
        little32(out, 0xffffffff);
        little16(out, (uint16_t)e.name.size());
        little16(out, 20);
        out.write(e.name.data(), e.name.size());
        little16(out, 1);
        little16(out, 16);
        little64(out, e.size);
        little64(out, e.size);
        if (content)
            out.write(content->data(), content->size());
        else {
            juce::FileInputStream in(source);
            require(in.openedOk(), "Audio source inaccessible");
            std::vector<char> buffer(1024 * 1024);
            uint64_t size = 0;
            uint32_t crc = ~0u;
            for (int n; (n = in.read(buffer.data(), (int)buffer.size())) > 0;) {
                size += n;
                crc = crcUpdate(crc, buffer.data(), n);
                require(out.write(buffer.data(), n),
                        juce::String::fromUTF8("Écriture de l’audio impossible"));
            }
            require(size == e.size && ~crc == e.crc,
                    juce::String::fromUTF8("L’audio a changé pendant la sauvegarde"));
        }
        entries.push_back(e);
    };
    if (audio.existsAsFile())
        add(member, audio, nullptr);
    add("project.json", {}, &manifest);
    auto central = (uint64_t)out.getPosition();
    for (auto &e : entries) {
        little32(out, 0x02014b50);
        little16(out, 45);
        little16(out, 45);
        little16(out, 0x800);
        little16(out, 0);
        little16(out, 0);
        little16(out, 0);
        little32(out, e.crc);
        little32(out, 0xffffffff);
        little32(out, 0xffffffff);
        little16(out, (uint16_t)e.name.size());
        little16(out, 28);
        little16(out, 0);
        little16(out, 0);
        little16(out, 0);
        little32(out, 0);
        little32(out, 0xffffffff);
        out.write(e.name.data(), e.name.size());
        little16(out, 1);
        little16(out, 24);
        little64(out, e.size);
        little64(out, e.packed);
        little64(out, e.offset);
    }
    auto size = (uint64_t)out.getPosition() - central, end = (uint64_t)out.getPosition();
    little32(out, 0x06064b50);
    little64(out, 44);
    little16(out, 45);
    little16(out, 45);
    little32(out, 0);
    little32(out, 0);
    little64(out, entries.size());
    little64(out, entries.size());
    little64(out, size);
    little64(out, central);
    little32(out, 0x07064b50);
    little32(out, 0);
    little64(out, end);
    little32(out, 1);
    little32(out, 0x06054b50);
    little16(out, 0);
    little16(out, 0);
    little16(out, 0xffff);
    little16(out, 0xffff);
    little32(out, 0xffffffff);
    little32(out, 0xffffffff);
    little16(out, 0);
    out.flush();
    require(out.getStatus().wasOk(), juce::String::fromUTF8("Sauvegarde incomplète"));
}
} // namespace
Document::Document() {
    j = {{"audio", ""},
         {"duration", 0.},
         {"sample_rate", 16000},
         {"chords", J::array()},
         {"beats", J::array()},
         {"downbeats", J::array()},
         {"bpm", 0.},
         {"key", ""},
         {"tags", J::array()},
         {"markers", J::array()},
         {"transpose", 0},
         {"capo", 0},
         {"meter", 0},
         {"meter_denominator", 4},
         {"meter_source", "unknown"},
         {"model", "LVChordia"},
         {"provenance", juce::String::fromUTF8("Projet KITHÁRA NÉA.")},
         {"version", 1},
         {"lyrics", J::array()},
         {"lyrics_language", ""},
         {"guitar_shapes", J::object()},
         {"song_sheet", ""},
         {"guitar_tuning", "E2,A2,D3,G3,B3,E4"},
         {"reference_text", ""},
         {"reference_matches", J::array()},
         {"audition_timbre", "upright"},
         {"instrument_family", "guitar"}};
}
void Document::validate() const {
    require(j.is_object() && j.value("version", 0) == 1, "Version de document inconnue");
    auto d = duration();
    require(std::isfinite(d) && d >= 0 && d <= 10830, juce::String::fromUTF8("Durée invalide"));
    require(chords().is_array() && lyrics().is_array() && lyrics().size() <= 100000,
            juce::String::fromUTF8("Événements invalides"));
    for (auto &c : chords()) {
        double a = c.at("start"), b = c.at("end");
        require(std::isfinite(a) && std::isfinite(b) && a >= 0 && b > a,
                juce::String::fromUTF8("Horodatage d’accord invalide"));
        require(text(c, "name").length() <= 100, juce::String::fromUTF8("Nom d’accord invalide"));
        for (auto &n : c.value("notes", J::array()))
            require(n.is_number_integer() && n.get<int>() >= 0 && n.get<int>() <= 127, "Note MIDI invalide");
    }
    for (auto &cue : lyrics()) {
        double a = cue.at("start"), b = cue.at("end");
        auto t = text(cue, "text");
        require(std::isfinite(a) && std::isfinite(b) && a >= 0 && b > a && b <= 10830 && t.isNotEmpty() &&
                    t.length() <= 10000,
                "Paroles invalides");
        int offset = -1;
        double previous = a;
        for (auto &w : cue.value("words", J::array())) {
            int at = w.at("offset");
            double from = w.at("start"), to = w.at("end");
            auto word = text(w, "text");
            require(std::isfinite(from) && std::isfinite(to) && from >= previous && from >= a && to >= from &&
                        to <= b && at > offset && word.isNotEmpty() &&
                        t.substring(at, at + word.length()) == word,
                    juce::String::fromUTF8("Mot horodaté invalide"));
            previous = from;
            offset = at + word.length() - 1;
        }
    }
    require(j.at("guitar_shapes").is_object() && j.at("guitar_shapes").size() <= 10000,
            juce::String::fromUTF8("Doigtés invalides"));
    for (auto &item : j.at("guitar_shapes").items()) {
        require(item.key().size() <= 1200 && item.value().is_array() && item.value().size() >= 4 &&
                    item.value().size() <= 12,
                juce::String::fromUTF8("Doigté invalide"));
        for (auto &fret : item.value())
            require(fret.is_number_integer() && fret.get<int>() >= -1 && fret.get<int>() <= 30,
                    "Case invalide");
    }
    require(text(j, "song_sheet").length() <= 1000000 &&
                text(j, "song_sheet_generated").length() <= 1000000 &&
                text(j, "reference_text").length() <= 1000000,
            "Texte trop long");
    auto ref = text(j, "reference_text");
    int prev = -1;
    double when = -1;
    for (auto &match : j.at("reference_matches")) {
        int at = match.at("offset");
        auto word = text(match, "text"), kind = text(match, "kind");
        double a = match.at("start"), b = match.at("end");
        require(at > prev && ref.substring(at, at + word.length()) == word && word.isNotEmpty() &&
                    std::isfinite(a) && std::isfinite(b) && a >= when && a >= 0 && b >= a && b <= 10830 &&
                    (kind == "exact" || kind == "near" || kind == "grouped"),
                "Alignement invalide");
        prev = at + word.length() - 1;
        when = a;
    }
}
Document Document::load(const File &file) {
    Document d;
    J payload;
    if (!file.hasFileExtension("knea")) {
        payload = readJson(file);
        require(payload.is_object(), "Document invalide");
        for (auto &v : payload.items())
            d.j[v.key()] = v.value();
        auto path = text(d.j, "audio");
        if (path.isNotEmpty() && !File::isAbsolutePath(path))
            d.j["audio"] = utf(file.getParentDirectory().getChildFile(path).getFullPathName());
        d.validate();
        return d;
    }
    auto entries = zipEntries(file);
    auto find = [&](const std::string &n) -> Entry {
        for (auto &e : entries)
            if (e.name == n)
                return e;
        throw std::runtime_error(utf(juce::String::fromUTF8("Entrée du conteneur absente : ")) + n);
    };
    juce::MemoryOutputStream manifest;
    extractEntry(file, find("project.json"), manifest, 30000000);
    payload = J::parse(static_cast<const char *>(manifest.getData()),
                       static_cast<const char *>(manifest.getData()) + manifest.getDataSize());
    require(payload.value("format", "") == "kithara-nea" && payload.value("schemaVersion", 0) == 1,
            "Version de conteneur inconnue");
    require(payload.at("document").is_object(), "Document invalide");
    for (auto &v : payload.at("document").items())
        d.j[v.key()] = v.value();
    d.validate();
    auto media = payload.at("media");
    d.mediaMode = text(media, "mode");
    if (d.mediaMode == "embedded") {
        auto path = text(media, "path");
        require(std::regex_match(utf(path), std::regex("audio/source(?:\\.[A-Za-z0-9]{1,10})?")) &&
                    text(d.j, "audio") == path,
                juce::String::fromUTF8("Chemin audio intégré invalide"));
        auto entry = find(utf(path));
        require(media.at("size").is_number_integer() && entry.size == media.at("size").get<uint64_t>() &&
                    entry.size > 0,
                juce::String::fromUTF8("Taille audio incohérente"));
        d.media = std::make_shared<TempFolder>();
        auto target = d.media->folder.getChildFile(path.fromLastOccurrenceOf("/", false, false));
        {
            juce::FileOutputStream out(target);
            require(out.openedOk(), "Extraction audio impossible");
            extractEntry(file, entry, out, maxAudio);
            out.flush();
        }
        checkFile(target, text(media, "sha256"));
        d.j["audio"] = utf(target.getFullPathName());
        d.audioName = text(media, "sourceName", utf(target.getFileName()).c_str());
    } else if (d.mediaMode == "external") {
        auto path = text(d.j, "audio");
        require(path.isNotEmpty(), juce::String::fromUTF8("Média externe absent"));
        if (!File::isAbsolutePath(path))
            d.j["audio"] = utf(file.getParentDirectory().getChildFile(path).getFullPathName());
    } else
        require(d.mediaMode == "none" && text(d.j, "audio").isEmpty(),
                juce::String::fromUTF8("Référence audio incohérente"));
    return d;
}
bool Document::ownsAudio() const {
    return j.value("audio_origin", "") == "recording" ||
           (text(j, "audio").isNotEmpty() && audio().getParentDirectory() == dataFolder() &&
            audio().getFileName().startsWith("Prise-") && audio().hasFileExtension("wav"));
}
void Document::save(const File &target, bool embedded) const {
    validate();
    auto data = j;
    if (ownsAudio()) {
        embedded = true;
        data["audio_origin"] = "recording";
    }
    J description = {{"mode", "none"}};
    File source;
    std::string member;
    auto path = text(j, "audio");
    if (path.isNotEmpty()) {
        source = audio();
        require(source != target, "Le projet ne peut pas remplacer son audio");
        if (embedded) {
            require(source.existsAsFile() && source.getSize() > 0 && source.getSize() <= maxAudio,
                    "Audio source absent ou trop volumineux");
            auto ext = source.getFileExtension();
            if (!std::regex_match(utf(ext), std::regex("\\.[A-Za-z0-9]{1,10}")))
                ext = ".bin";
            member = utf("audio/source" + ext);
            data["audio"] = member;
            description = {{"mode", "embedded"},
                           {"path", member},
                           {"sourceName", utf(audioName.isNotEmpty() ? audioName : source.getFileName())},
                           {"size", source.getSize()},
                           {"sha256", utf(sha256(source))}};
        } else {
            require(!media,
                    juce::String::fromUTF8("Choisir le média externe original avant une sauvegarde légère"));
            description = {{"mode", "external"}};
            data["audio"] = utf(source.getRelativePathFrom(target.getParentDirectory()));
            source = {};
        }
    }
    J payload = {{"format", "kithara-nea"}, {"schemaVersion", 1}, {"document", data}, {"media", description}};
    auto manifest = payload.dump(2);
    require(manifest.size() <= 30000000, "Projet trop volumineux");
    target.getParentDirectory().createDirectory();
    auto temp = target.getSiblingFile(".knea-" + juce::Uuid().toString() + ".tmp");
    try {
        if (target.hasFileExtension("knea"))
            writeZip(temp, manifest, source, member);
        else {
            data["audio"] =
                path.isEmpty() ? "" : utf(audio().getRelativePathFrom(target.getParentDirectory()));
            writeJson(temp, data);
        }
        atomicReplace(temp, target);
    } catch (...) {
        temp.deleteFile();
        throw;
    }
}
void History::push(const Document &d) {
    back.push_back(d);
    if (back.size() > 50)
        back.erase(back.begin());
    forward.clear();
}
Document History::undo(const Document &d) {
    if (back.empty())
        return d;
    forward.push_back(d);
    auto result = back.back();
    back.pop_back();
    return result;
}
Document History::redo(const Document &d) {
    if (forward.empty())
        return d;
    back.push_back(d);
    auto result = forward.back();
    forward.pop_back();
    return result;
}
static const std::array<std::string, 12> names = {"C",  "C#", "D",  "Eb", "E",  "F",
                                                  "F#", "G",  "Ab", "A",  "Bb", "B"};
int pitch(String s) {
    if (s.isEmpty() || s == "N")
        return -1;
    int n = -1;
    switch (s[0]) {
    case 'C':
        n = 0;
        break;
    case 'D':
        n = 2;
        break;
    case 'E':
        n = 4;
        break;
    case 'F':
        n = 5;
        break;
    case 'G':
        n = 7;
        break;
    case 'A':
        n = 9;
        break;
    case 'B':
        n = 11;
        break;
    }
    if (n < 0)
        return -1;
    if (s.length() > 1) {
        if (s[1] == '#')
            ++n;
        else if (s[1] == 'b')
            --n;
    }
    return (n + 12) % 12;
}
String transpose(String name, int amount) {
    if (name == "N" || name.isEmpty() || name == juce::String::fromUTF8("—"))
        return name;
    auto parts = juce::StringArray::fromTokens(name, "/", "");
    for (auto &p : parts) {
        auto n = pitch(p);
        if (n >= 0) {
            auto length = p.length() > 1 && (p[1] == '#' || p[1] == 'b') ? 2 : 1;
            p = str(names[(n + amount % 12 + 12) % 12]) + p.substring(length);
        }
    }
    return parts.joinIntoString("/");
}
String midiName(int n) { return str(names[(n % 12 + 12) % 12]) + String(n / 12 - 1); }
int noteMidi(String s) {
    std::smatch m;
    auto v = utf(s);
    require(std::regex_match(v, m, std::regex("([A-G](?:#|b)?)(-?[0-9]+)")), "Note invalide : " + s);
    return (std::stoi(m[2]) + 1) * 12 + pitch(str(m[1]));
}
String label(const J &chord, const Document &d) {
    auto n = transpose(text(chord, "name"), d.j.value("transpose", 0));
    auto bass = transpose(text(chord, "bass"), d.j.value("transpose", 0));
    return n + (bass.isNotEmpty() ? "/" + bass : String());
}
String shapeKey(const String &n, const String &tuning, int capo) {
    return str(J::array({utf(n), utf(tuning), capo}).dump());
}
int degree(const std::string &s) {
    auto at = s.find_first_of("0123456789");
    require(at != std::string::npos, "Intervalle invalide");
    return std::stoi(s.substr(at));
}
int semitone(const std::string &s) {
    static int major[] = {0, 2, 4, 5, 7, 9, 11};
    int d = degree(s) - 1;
    return major[d % 7] + 12 * (d / 7) + (int)std::count(s.begin(), s.end(), '#') -
           (int)std::count(s.begin(), s.end(), 'b');
}
Theory::Theory() {
    auto data = readJson(resource("independent/theory/chord-types.json"));
    std::map<std::string, std::set<std::string>> types;
    for (auto &row : data.at("types")) {
        std::set<std::string> formula = row.at("intervals").get<std::set<std::string>>();
        types[row.at("id")] = formula;
        for (auto &a : row.at("aliases"))
            aliases[a] = formula;
    }
    for (auto &row : data.at("composites")) {
        auto formula = types[row.at("base")];
        for (auto &a : row.value("alter", J::array())) {
            auto v = a.get<std::string>();
            for (auto i = formula.begin(); i != formula.end();)
                if (degree(*i) == degree(v))
                    i = formula.erase(i);
                else
                    ++i;
            formula.insert(v);
        }
        for (auto &a : row.value("add", J::array()))
            formula.insert(a.get<std::string>());
        aliases[row.at("alias")] = formula;
    }
    tunings = readJson(resource("independent/theory/tunings.json")).at("tunings");
}
std::vector<std::string> Theory::intervals(String input) const {
    input = input.replace(juce::String::fromUTF8("Δ"), "maj")
                .replace(juce::String::fromUTF8("°"), "dim")
                .replace(juce::String::fromUTF8("ø"), "m7b5")
                .removeCharacters("(), ");
    if (input == "6/9")
        input = "69";
    auto suffix = utf(input);
    static std::string modifier =
        "sus[24]|add[b#]*(?:13|11|[1-9])|[b#]+(?:13|11|[1-9])|(?:omit|no)(?:13|11|[1-9])";
    std::string base;
    bool found = false;
    for (auto &a : aliases)
        if (suffix.rfind(a.first, 0) == 0 && (!found || a.first.size() > base.size()) &&
            std::regex_match(suffix.substr(a.first.size()), std::regex("(?:" + modifier + ")*"))) {
            base = a.first;
            found = true;
        }
    require(found, juce::String::fromUTF8("Qualité d’accord inconnue : ") + input);
    auto formula = aliases.at(base);
    auto tail = suffix.substr(base.size());
    std::smatch m;
    while (!tail.empty()) {
        require(std::regex_search(tail, m, std::regex("^(?:" + modifier + ")")),
                juce::String::fromUTF8("Altération inconnue"));
        auto token = m.str();
        tail = tail.substr(token.size());
        if (token.rfind("sus", 0) == 0) {
            for (auto i = formula.begin(); i != formula.end();)
                if (degree(*i) == 3)
                    i = formula.erase(i);
                else
                    ++i;
            formula.insert(token.substr(3));
        } else if (token.rfind("add", 0) == 0)
            formula.insert(token.substr(3));
        else if (token.rfind("omit", 0) == 0 || token.rfind("no", 0) == 0) {
            auto d = degree(token);
            for (auto i = formula.begin(); i != formula.end();)
                if (degree(*i) == d)
                    i = formula.erase(i);
                else
                    ++i;
        } else {
            auto d = degree(token);
            for (auto i = formula.begin(); i != formula.end();)
                if (degree(*i) == d)
                    i = formula.erase(i);
                else
                    ++i;
            formula.insert(token);
        }
    }
    std::vector<std::string> result(formula.begin(), formula.end());
    std::sort(result.begin(), result.end(), [](auto &a, auto &b) {
        return std::make_pair(degree(a), semitone(a)) < std::make_pair(degree(b), semitone(b));
    });
    return result;
}
std::vector<int> Theory::notes(String name, String bass) const {
    name = name.replace("6/9", "69").upToFirstOccurrenceOf("/", false, false);
    auto root = pitch(name);
    if (root < 0)
        return {};
    int length = name.length() > 1 && (name[1] == '#' || name[1] == 'b') ? 2 : 1;
    std::set<int> pcs;
    try {
        for (auto &i : intervals(name.substring(length)))
            pcs.insert((root + semitone(i)) % 12);
    } catch (const std::exception &) {
        pcs = {
            root,
            (root +
             (name.substring(length).startsWith("m") && !name.substring(length).startsWith("maj") ? 3 : 4)) %
                12,
            (root + 7) % 12};
    }
    std::vector<int> result = {48 + root};
    for (auto pc : pcs) {
        int n = 48 + pc;
        while (n < result.front())
            n += 12;
        if (std::find(result.begin(), result.end(), n) == result.end())
            result.push_back(n);
    }
    std::sort(result.begin(), result.end());
    auto bp = pitch(bass);
    if (bp >= 0 && bp != root)
        result.insert(result.begin(), 36 + bp);
    return result;
}
std::vector<int> stringVoicing(const Document &document, const std::vector<int> &shape) {
    auto tuning = juce::StringArray::fromTokens(text(document.j, "guitar_tuning"), ",", "");
    if ((size_t)tuning.size() != shape.size())
        return {};
    std::vector<int> notes;
    for (size_t i = 0; i < shape.size(); ++i) {
        if (shape[i] < 0)
            continue; // Muted strings never sound; open strings include the capo.
        int note = noteMidi(tuning[(int)i]) + document.j.value("capo", 0) + shape[i];
        if (note >= 0 && note <= 127)
            notes.push_back(note);
    }
    return notes; // Keep unisons, octave doublings and reentrant string order.
}
String stringTimbre(const Document &document) {
    auto family = text(document.j, "instrument_family", "guitar");
    if (family == "ukulele" || family == "mandolin" || family == "banjo")
        return family;
    return "nylon"; // Guitar; explicit temporary guitar substitute for bass.
}
PianoVoicing pianoVoicing(const J &chord, const Document &document, const Theory &theory) {
    PianoVoicing result;
    auto name = text(chord, "name").replace("6/9", "69");
    auto bass = text(chord, "bass");
    if (bass.isEmpty() && name.containsChar('/'))
        bass = name.fromFirstOccurrenceOf("/", false, false);
    name = transpose(name, document.j.value("transpose", 0));
    if (name == "N" || pitch(name) < 0)
        return result;
    // Piano realization independent of string instrument, tuning and capo:
    // one bass note in C2-B2 and the complete chord above it in the medium register.
    result.right = theory.notes(name);
    if (result.right.empty())
        return result;
    int low = bass.isNotEmpty() ? pitch(transpose(bass, document.j.value("transpose", 0))) : pitch(name);
    if (low < 0)
        low = pitch(name);
    result.left.push_back(36 + low);
    return result;
}
std::vector<std::vector<int>> Theory::positions(String name, String tuning, int capo, int limit) {
    auto key = utf(shapeKey(name, tuning, capo)) + ":" + std::to_string(limit);
    if (auto i = cache.find(key); i != cache.end())
        return i->second;
    std::set<int> wanted;
    for (auto n : notes(name))
        wanted.insert(n % 12);
    if (wanted.empty())
        return {};
    std::vector<int> strings;
    for (auto &n : juce::StringArray::fromTokens(tuning, ",", ""))
        strings.push_back(noteMidi(n) + capo);
    require(strings.size() >= 4 && strings.size() <= 12, "Accordage invalide");
    std::vector<std::pair<double, std::vector<int>>> candidates;
    for (int low = 0; low <= 10; ++low) {
        std::vector<std::vector<int>> options;
        for (int n : strings) {
            std::vector<int> f = {-1};
            if (low && wanted.count(n % 12))
                f.push_back(0);
            for (int k = std::max(0, low); k < std::min(15, low + 5); ++k)
                if (wanted.count((n + k) % 12))
                    f.push_back(k);
            options.push_back(f);
        }
        std::vector<int> shape(strings.size());
        std::function<void(int)> visit = [&](int index) {
            if (index < (int)strings.size()) {
                for (int f : options[index]) {
                    shape[index] = f;
                    visit(index + 1);
                }
                return;
            }
            std::set<int> sounding, fingers, classes;
            int count = 0, mutes = 0, first = -1, span = 0, minimum = 100, maximum = 0;
            for (size_t i = 0; i < strings.size(); ++i) {
                auto f = shape[i];
                if (f < 0) {
                    ++mutes;
                    continue;
                }
                if (first < 0)
                    first = (int)i;
                ++count;
                auto n = strings[i] + f;
                sounding.insert(n);
                classes.insert(n % 12);
                if (f > 0) {
                    fingers.insert(f);
                    minimum = std::min(minimum, f);
                    maximum = std::max(maximum, f);
                }
            }
            if (count < std::min(3, (int)strings.size()) ||
                !std::includes(classes.begin(), classes.end(), wanted.begin(), wanted.end()) ||
                fingers.size() > 4)
                return;
            if (minimum != 100)
                span = maximum - minimum;
            if (span > 4)
                return;
            int inner = 0, stopped = 0;
            for (size_t i = 0; i < shape.size(); ++i) {
                if ((int)i >= first && shape[i] < 0)
                    ++inner;
                if (shape[i] > 0)
                    ++stopped;
            }
            double wrong = *sounding.begin() % 12 == pitch(name) ? 0 : 2.5;
            double score =
                stopped + mutes * 1.3 + inner * 3 + wrong + span * .8 + (minimum == 100 ? 0 : minimum) * .2;
            candidates.push_back({score, shape});
        };
        visit(0);
    }
    std::sort(candidates.begin(), candidates.end());
    std::set<std::vector<int>> seen;
    std::vector<std::vector<int>> result;
    for (auto &c : candidates)
        if (seen.insert(c.second).second) {
            result.push_back(c.second);
            if ((int)result.size() >= limit)
                break;
        }
    if (cache.size() > 512)
        cache.clear();
    cache[key] = result;
    return result;
}
std::vector<int> Theory::chosen(const J &c, const Document &d) {
    auto name = transpose(text(c, "name"), d.j.value("transpose", 0)), tuning = text(d.j, "guitar_tuning");
    auto shapes = positions(name, tuning, d.j.value("capo", 0));
    auto k = utf(shapeKey(label(c, d), tuning, d.j.value("capo", 0)));
    if (d.j.at("guitar_shapes").contains(k)) {
        auto saved = d.j.at("guitar_shapes").at(k).get<std::vector<int>>();
        if (std::find(shapes.begin(), shapes.end(), saved) != shapes.end())
            return saved;
    }
    return shapes.empty() ? std::vector<int>() : shapes.front();
}
String Theory::key(const Document &d) const {
    std::array<double, 12> h{};
    for (auto &c : d.chords()) {
        auto duration = std::max(.05, c.at("end").get<double>() - c.at("start").get<double>());
        for (auto n : notes(text(c, "name"), text(c, "bass")))
            h[n % 12] += duration;
        auto root = pitch(text(c, "name"));
        if (root >= 0)
            h[root] += duration;
    }
    double sum = std::accumulate(h.begin(), h.end(), 0.);
    if (!sum)
        return {};
    for (auto &v : h)
        v -= sum / 12;
    double profiles[2][12] = {{6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88},
                              {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17}};
    double best = -1;
    String result;
    for (int root = 0; root < 12; ++root)
        for (int mode = 0; mode < 2; ++mode) {
            double mean = std::accumulate(profiles[mode], profiles[mode] + 12, 0.) / 12, dot = 0, hh = 0,
                   pp = 0;
            for (int i = 0; i < 12; ++i) {
                double p = profiles[mode][(i - root + 12) % 12] - mean;
                dot += h[i] * p;
                hh += h[i] * h[i];
                pp += p * p;
            }
            double score = dot / (std::sqrt(hh * pp) + 1e-12);
            if (score > best) {
                best = score;
                result = str(names[root]) + (mode ? " mineur" : " majeur");
            }
        }
    return result;
}
} // namespace kn
