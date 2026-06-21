#include "persistence/ProjectSerializer.h"

#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace composer::persistence {
namespace {

using application::Error;
using application::ErrorCode;

// --- Writing -------------------------------------------------------------------------------

void appendEscaped(std::string& out, const std::string& value) {
    out.push_back('"');
    for (const char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    std::array<char, 8> buffer{};
                    std::snprintf(buffer.data(), buffer.size(), "\\u%04x",
                        static_cast<unsigned int>(static_cast<unsigned char>(c)));
                    out += buffer.data();
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
}

std::string formatDouble(const double value) {
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return std::string(buffer.data(), result.ptr);
}

class Writer final {
public:
    [[nodiscard]] std::string build(const domain::Project& project) {
        out_.clear();
        writeProject(project);
        out_.push_back('\n');
        return out_;
    }

private:
    void indent() {
        for (int i = 0; i < depth_; ++i) {
            out_ += "  ";
        }
    }

    void key(const std::string_view name) {
        indent();
        out_.push_back('"');
        out_ += name;
        out_ += "\": ";
    }

    void writeProject(const domain::Project& project) {
        out_ += "{\n";
        ++depth_;
        key("schemaVersion");
        out_ += std::to_string(domain::Project::kSchemaVersion);
        out_ += ",\n";
        key("id");
        appendEscaped(out_, project.id.value);
        out_ += ",\n";
        key("name");
        appendEscaped(out_, project.name);
        out_ += ",\n";
        key("bpm");
        out_ += formatDouble(project.tempoMap.beatsPerMinute());
        out_ += ",\n";
        key("sampleRate");
        out_ += formatDouble(project.tempoMap.sampleRate());
        out_ += ",\n";
        key("timeSignatureNumerator");
        out_ += std::to_string(project.timeSignatureNumerator);
        out_ += ",\n";
        key("timeSignatureDenominator");
        out_ += std::to_string(project.timeSignatureDenominator);
        out_ += ",\n";
        key("tracks");
        writeTracks(project.tracks);
        out_.push_back('\n');
        --depth_;
        indent();
        out_.push_back('}');
    }

    void writeTracks(const std::vector<domain::InstrumentTrack>& tracks) {
        if (tracks.empty()) {
            out_ += "[]";
            return;
        }
        out_ += "[\n";
        ++depth_;
        for (std::size_t i = 0; i < tracks.size(); ++i) {
            indent();
            writeTrack(tracks[i]);
            out_ += (i + 1 < tracks.size()) ? ",\n" : "\n";
        }
        --depth_;
        indent();
        out_.push_back(']');
    }

    void writeTrack(const domain::InstrumentTrack& track) {
        out_ += "{\n";
        ++depth_;
        key("id");
        appendEscaped(out_, track.id.value);
        out_ += ",\n";
        key("name");
        appendEscaped(out_, track.name);
        out_ += ",\n";
        key("instrumentId");
        appendEscaped(out_, track.instrumentId);
        out_ += ",\n";
        key("volume");
        out_ += formatDouble(track.volume);
        out_ += ",\n";
        key("pan");
        out_ += formatDouble(track.pan);
        out_ += ",\n";
        key("muted");
        out_ += track.muted ? "true" : "false";
        out_ += ",\n";
        key("soloed");
        out_ += track.soloed ? "true" : "false";
        out_ += ",\n";
        key("clips");
        writeClips(track.clips);
        out_.push_back('\n');
        --depth_;
        indent();
        out_.push_back('}');
    }

    void writeClips(const std::vector<domain::MidiClip>& clips) {
        if (clips.empty()) {
            out_ += "[]";
            return;
        }
        out_ += "[\n";
        ++depth_;
        for (std::size_t i = 0; i < clips.size(); ++i) {
            indent();
            writeClip(clips[i]);
            out_ += (i + 1 < clips.size()) ? ",\n" : "\n";
        }
        --depth_;
        indent();
        out_.push_back(']');
    }

    void writeClip(const domain::MidiClip& clip) {
        out_ += "{\n";
        ++depth_;
        key("id");
        appendEscaped(out_, clip.id.value);
        out_ += ",\n";
        key("name");
        appendEscaped(out_, clip.name);
        out_ += ",\n";
        key("start");
        out_ += std::to_string(clip.range.start);
        out_ += ",\n";
        key("end");
        out_ += std::to_string(clip.range.end);
        out_ += ",\n";
        key("notes");
        writeNotes(clip.notes);
        out_.push_back('\n');
        --depth_;
        indent();
        out_.push_back('}');
    }

    void writeNotes(const std::vector<domain::NoteEvent>& notes) {
        if (notes.empty()) {
            out_ += "[]";
            return;
        }
        out_ += "[\n";
        ++depth_;
        for (std::size_t i = 0; i < notes.size(); ++i) {
            indent();
            out_ += "{ \"id\": ";
            appendEscaped(out_, notes[i].id.value);
            out_ += ", \"start\": ";
            out_ += std::to_string(notes[i].start);
            out_ += ", \"duration\": ";
            out_ += std::to_string(notes[i].duration);
            out_ += ", \"pitch\": ";
            out_ += std::to_string(notes[i].pitch);
            out_ += ", \"velocity\": ";
            out_ += std::to_string(notes[i].velocity);
            out_ += " }";
            out_ += (i + 1 < notes.size()) ? ",\n" : "\n";
        }
        --depth_;
        indent();
        out_.push_back(']');
    }

    std::string out_;
    int depth_{0};
};

// --- Parsing -------------------------------------------------------------------------------

// Minimal strict recursive-descent JSON parser scoped to the project schema. It accepts any
// well-formed JSON value but the schema readers below only pull the fields they expect, ignoring
// unknown ones (forward-compatibility for newer minor schema fields).

struct JsonValue;
using JsonObject = std::vector<std::pair<std::string, JsonValue>>;
using JsonArray = std::vector<JsonValue>;

struct JsonValue {
    enum class Kind { null, boolean, number, string, array, object };
    Kind kind{Kind::null};
    bool boolean{};
    double number{};
    std::string text;  // raw numeric text (for exact integer recovery) or string contents
    std::shared_ptr<JsonArray> array;
    std::shared_ptr<JsonObject> object;
};

class Parser final {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    [[nodiscard]] bool parse(JsonValue& out) {
        skipWhitespace();
        if (!parseValue(out)) {
            return false;
        }
        skipWhitespace();
        return pos_ == text_.size();
    }

    [[nodiscard]] std::string_view error() const noexcept { return error_; }

private:
    void skipWhitespace() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool fail(const std::string_view message) {
        error_ = message;
        return false;
    }

    bool parseValue(JsonValue& out) {
        skipWhitespace();
        if (pos_ >= text_.size()) {
            return fail("unexpected end of input");
        }
        const char c = text_[pos_];
        switch (c) {
            case '{': return parseObject(out);
            case '[': return parseArray(out);
            case '"': return parseString(out);
            case 't': case 'f': return parseBool(out);
            case 'n': return parseNull(out);
            default: return parseNumber(out);
        }
    }

    bool parseObject(JsonValue& out) {
        out.kind = JsonValue::Kind::object;
        out.object = std::make_shared<JsonObject>();
        ++pos_;  // consume '{'
        skipWhitespace();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return true;
        }
        while (true) {
            skipWhitespace();
            JsonValue keyValue;
            if (pos_ >= text_.size() || text_[pos_] != '"') {
                return fail("expected object key");
            }
            if (!parseString(keyValue)) {
                return false;
            }
            skipWhitespace();
            if (pos_ >= text_.size() || text_[pos_] != ':') {
                return fail("expected ':'");
            }
            ++pos_;
            JsonValue value;
            if (!parseValue(value)) {
                return false;
            }
            out.object->emplace_back(keyValue.text, std::move(value));
            skipWhitespace();
            if (pos_ >= text_.size()) {
                return fail("unterminated object");
            }
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == '}') {
                ++pos_;
                return true;
            }
            return fail("expected ',' or '}'");
        }
    }

    bool parseArray(JsonValue& out) {
        out.kind = JsonValue::Kind::array;
        out.array = std::make_shared<JsonArray>();
        ++pos_;  // consume '['
        skipWhitespace();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return true;
        }
        while (true) {
            JsonValue value;
            if (!parseValue(value)) {
                return false;
            }
            out.array->push_back(std::move(value));
            skipWhitespace();
            if (pos_ >= text_.size()) {
                return fail("unterminated array");
            }
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == ']') {
                ++pos_;
                return true;
            }
            return fail("expected ',' or ']'");
        }
    }

    bool parseString(JsonValue& out) {
        out.kind = JsonValue::Kind::string;
        out.text.clear();
        ++pos_;  // consume opening quote
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') {
                return true;
            }
            if (c == '\\') {
                if (pos_ >= text_.size()) {
                    return fail("unterminated escape");
                }
                const char esc = text_[pos_++];
                switch (esc) {
                    case '"': out.text.push_back('"'); break;
                    case '\\': out.text.push_back('\\'); break;
                    case '/': out.text.push_back('/'); break;
                    case 'n': out.text.push_back('\n'); break;
                    case 'r': out.text.push_back('\r'); break;
                    case 't': out.text.push_back('\t'); break;
                    case 'b': out.text.push_back('\b'); break;
                    case 'f': out.text.push_back('\f'); break;
                    case 'u': {
                        if (pos_ + 4 > text_.size()) {
                            return fail("truncated unicode escape");
                        }
                        unsigned int code = 0;
                        for (int i = 0; i < 4; ++i) {
                            code = code * 16 + hexDigit(text_[pos_++]);
                        }
                        appendUtf8(out.text, code);
                        break;
                    }
                    default: return fail("invalid escape");
                }
            } else {
                out.text.push_back(c);
            }
        }
        return fail("unterminated string");
    }

    static unsigned int hexDigit(const char c) {
        if (c >= '0' && c <= '9') {
            return static_cast<unsigned int>(c - '0');
        }
        if (c >= 'a' && c <= 'f') {
            return static_cast<unsigned int>(c - 'a' + 10);
        }
        if (c >= 'A' && c <= 'F') {
            return static_cast<unsigned int>(c - 'A' + 10);
        }
        return 0;
    }

    static void appendUtf8(std::string& out, const unsigned int code) {
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }

    bool parseBool(JsonValue& out) {
        if (text_.compare(pos_, 4, "true") == 0) {
            out.kind = JsonValue::Kind::boolean;
            out.boolean = true;
            pos_ += 4;
            return true;
        }
        if (text_.compare(pos_, 5, "false") == 0) {
            out.kind = JsonValue::Kind::boolean;
            out.boolean = false;
            pos_ += 5;
            return true;
        }
        return fail("invalid literal");
    }

    bool parseNull(JsonValue& out) {
        if (text_.compare(pos_, 4, "null") == 0) {
            out.kind = JsonValue::Kind::null;
            pos_ += 4;
            return true;
        }
        return fail("invalid literal");
    }

    bool parseNumber(JsonValue& out) {
        const std::size_t begin = pos_;
        if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) {
            ++pos_;
        }
        bool any = false;
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+'
                || c == '-') {
                ++pos_;
                any = true;
            } else {
                break;
            }
        }
        if (!any) {
            return fail("invalid number");
        }
        out.kind = JsonValue::Kind::number;
        out.text = std::string(text_.substr(begin, pos_ - begin));
        std::from_chars(out.text.data(), out.text.data() + out.text.size(), out.number);
        return true;
    }

    std::string_view text_;
    std::size_t pos_{0};
    std::string_view error_;
};

const JsonValue* find(const JsonValue& object, const std::string_view key) {
    if (object.kind != JsonValue::Kind::object || !object.object) {
        return nullptr;
    }
    for (const auto& [name, value] : *object.object) {
        if (name == key) {
            return &value;
        }
    }
    return nullptr;
}

std::string getString(const JsonValue& object, const std::string_view key) {
    const auto* value = find(object, key);
    return (value != nullptr && value->kind == JsonValue::Kind::string) ? value->text
                                                                        : std::string{};
}

std::int64_t getInt(const JsonValue& object, const std::string_view key,
    const std::int64_t fallback) {
    const auto* value = find(object, key);
    if (value == nullptr || value->kind != JsonValue::Kind::number) {
        return fallback;
    }
    std::int64_t result = fallback;
    std::from_chars(value->text.data(), value->text.data() + value->text.size(), result);
    return result;
}

double getDouble(const JsonValue& object, const std::string_view key, const double fallback) {
    const auto* value = find(object, key);
    return (value != nullptr && value->kind == JsonValue::Kind::number) ? value->number : fallback;
}

bool getBool(const JsonValue& object, const std::string_view key, const bool fallback) {
    const auto* value = find(object, key);
    return (value != nullptr && value->kind == JsonValue::Kind::boolean) ? value->boolean
                                                                         : fallback;
}

}  // namespace

std::string ProjectSerializer::serialize(const domain::Project& project) {
    Writer writer;
    return writer.build(project);
}

application::Result<domain::Project> ProjectSerializer::parse(const std::string& json) {
    JsonValue root;
    Parser parser{json};
    if (!parser.parse(root) || root.kind != JsonValue::Kind::object) {
        return Error{ErrorCode::invalidArgument, "project manifest is not valid JSON"};
    }

    const std::int64_t schemaVersion = getInt(root, "schemaVersion", 0);
    if (schemaVersion == 0) {
        return Error{ErrorCode::invalidArgument, "project manifest is missing schemaVersion"};
    }
    if (schemaVersion > domain::Project::kSchemaVersion) {
        return Error{ErrorCode::dependencyUnavailable,
            "project manifest is from a newer Composer version; open read-only"};
    }

    domain::Project project;
    project.id = domain::EntityId{getString(root, "id")};
    project.name = getString(root, "name");
    project.tempoMap = domain::TempoMap{
        getDouble(root, "bpm", 120.0), getDouble(root, "sampleRate", 48000.0)};
    project.timeSignatureNumerator =
        static_cast<int>(getInt(root, "timeSignatureNumerator", 4));
    project.timeSignatureDenominator =
        static_cast<int>(getInt(root, "timeSignatureDenominator", 4));

    const auto* tracks = find(root, "tracks");
    if (tracks != nullptr && tracks->kind == JsonValue::Kind::array && tracks->array) {
        for (const auto& trackValue : *tracks->array) {
            domain::InstrumentTrack track;
            track.id = domain::EntityId{getString(trackValue, "id")};
            track.name = getString(trackValue, "name");
            track.instrumentId = getString(trackValue, "instrumentId");
            track.volume = static_cast<float>(getDouble(trackValue, "volume", 1.0));
            track.pan = static_cast<float>(getDouble(trackValue, "pan", 0.0));
            track.muted = getBool(trackValue, "muted", false);
            track.soloed = getBool(trackValue, "soloed", false);

            const auto* clips = find(trackValue, "clips");
            if (clips != nullptr && clips->kind == JsonValue::Kind::array && clips->array) {
                for (const auto& clipValue : *clips->array) {
                    domain::MidiClip clip;
                    clip.id = domain::EntityId{getString(clipValue, "id")};
                    clip.name = getString(clipValue, "name");
                    clip.range = domain::HalfOpenTickRange{
                        getInt(clipValue, "start", 0), getInt(clipValue, "end", 0)};

                    const auto* notes = find(clipValue, "notes");
                    if (notes != nullptr && notes->kind == JsonValue::Kind::array
                        && notes->array) {
                        for (const auto& noteValue : *notes->array) {
                            domain::NoteEvent note;
                            note.id = domain::EntityId{getString(noteValue, "id")};
                            note.start = getInt(noteValue, "start", 0);
                            note.duration = getInt(noteValue, "duration", 0);
                            note.pitch =
                                static_cast<domain::Pitch>(getInt(noteValue, "pitch", 0));
                            note.velocity = static_cast<domain::Velocity>(
                                getInt(noteValue, "velocity", domain::kMaxVelocity));
                            clip.notes.push_back(std::move(note));
                        }
                    }
                    track.clips.push_back(std::move(clip));
                }
            }
            project.tracks.push_back(std::move(track));
        }
    }

    return project;
}

}  // namespace composer::persistence
