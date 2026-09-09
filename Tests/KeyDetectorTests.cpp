#include "KeyDetector.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{
int failures = 0;

int chord(std::string_view name)
{
    for (int id = 0; id < ChordMatcher::chordCount; ++id)
        if (ChordMatcher::name(id) == name)
            return id;
    std::cerr << "Unknown test chord: " << name << '\n';
    std::exit(2);
}

int key(std::string_view name)
{
    for (int id = 0; id < KeyDetector::keyCount; ++id)
        if (KeyDetector::name(id) == name)
            return id;
    std::cerr << "Unknown test key: " << name << '\n';
    std::exit(2);
}

std::vector<KeyObservation> progression(std::initializer_list<std::string_view> names)
{
    std::vector<KeyObservation> result;
    for (const auto name : names)
        result.push_back({ chord(name), 82.0f });
    return result;
}

void expectKey(const char* label, std::initializer_list<std::string_view> chords,
               std::string_view expected)
{
    const auto result = KeyDetector().analyse(progression(chords));
    if (result.key != key(expected))
    {
        ++failures;
        std::cerr << label << ": expected " << expected << ", got "
                  << KeyDetector::name(result.key) << " (confidence " << result.confidence
                  << ", score " << result.score << ", margin " << result.margin << ")\n";
    }
}

void expectKeyIds(const std::string& label, const std::vector<int>& chords, int expected)
{
    std::vector<KeyObservation> observations;
    for (const int id : chords)
        observations.push_back({ id, 82.0f });
    const auto result = KeyDetector().analyse(observations);
    if (result.key != expected)
    {
        ++failures;
        std::cerr << label << ": expected " << KeyDetector::name(expected) << ", got "
                  << KeyDetector::name(result.key) << " (confidence " << result.confidence
                  << ", score " << result.score << ", margin " << result.margin << ")\n";
    }
}

void expectDegree(std::string_view keyName, std::string_view chordName,
                  const std::string& expected)
{
    const auto actual = KeyDetector::degreeName(key(keyName), chord(chordName));
    if (actual != expected)
    {
        ++failures;
        std::cerr << "Degree " << keyName << " / " << chordName << ": expected "
                  << expected << ", got " << actual << '\n';
    }
}
}

int main()
{
    expectKey("C major cadence", { "C", "F", "G", "C" }, "C major");
    expectKey("A minor loop", { "Am", "F", "C", "G", "Am" }, "A minor");
    expectKey("User guitar progression",
              { "G#m", "F#", "E", "D#", "G#m" }, "G# minor");
    expectKey("Full user guitar loop",
              { "G#m", "F#", "E", "D#", "G#m", "F#", "E", "D#",
                "G#m", "F#", "E", "D#", "G#m", "F#", "E", "D#",
                "G#m", "F#", "E", "D#", "G#m", "F#", "E", "D#",
                "G#m", "F#", "E", "D#" },
              "G# minor");
    expectKey("Jazz ii V I", { "Dm7", "G7", "Cmaj7", "Cmaj7" }, "C major");
    expectKey("E major dominant", { "E", "A", "B7", "E" }, "E major");
    expectKey("D minor cadence", { "Dm", "A7", "Dm", "Gm", "A7", "Dm" }, "D minor");
    expectKey("Suspended major progression",
              { "C", "Fsus2", "Gsus4", "C" }, "C major");
    expectKey("Leading-tone diminished",
              { "C", "F", "Bdim", "G7", "C" }, "C major");

    for (int root = 0; root < 12; ++root)
    {
        expectKeyIds("Transposed major " + std::to_string(root),
                     { root, (root + 5) % 12, (root + 7) % 12, root }, root);
        expectKeyIds("Transposed natural minor " + std::to_string(root),
                     { 12 + root, (root + 8) % 12, (root + 3) % 12,
                       (root + 10) % 12, 12 + root },
                     12 + root);
        expectKeyIds("Transposed harmonic minor " + std::to_string(root),
                     { 12 + root, (root + 8) % 12, 12 + (root + 5) % 12,
                       (root + 7) % 12, 12 + root },
                     12 + root);
    }

    const auto insufficient = KeyDetector().analyse(progression({ "C", "G" }));
    if (insufficient.key != -1)
    {
        ++failures;
        std::cerr << "Two roots must stay in learning mode\n";
    }

    auto invalidConfidence = progression({ "C", "F", "G", "C" });
    invalidConfidence[1].confidence = std::numeric_limits<float>::quiet_NaN();
    if (KeyDetector().analyse(invalidConfidence).key != key("C major"))
    {
        ++failures;
        std::cerr << "NaN confidence must not break key detection\n";
    }

    expectDegree("G# minor", "G#m", "i");
    expectDegree("G# minor", "F#", "VII");
    expectDegree("G# minor", "E", "VI");
    expectDegree("G# minor", "D#7", "V7");
    expectDegree("C major", "Dm7", "ii7");
    expectDegree("C major", "G7", "V7");
    expectDegree("C major", "Cmaj7", "Imaj7");
    expectDegree("C major", "Gsus4", "Vsus4");
    expectDegree("C major", "Bdim", "viidim");

    if (failures != 0)
        return 1;
    std::cout << "Key detector tests passed\n";
    return 0;
}
