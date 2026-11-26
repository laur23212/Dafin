#pragma once

// Dafin: Smart Keycaps, the keyboard-shortcut generator of Scene Title v1 (docs/scene-keycaps.md).
// A value-for-value mirror of tools/scene-ref/keycaps.py: the same key dictionary (read from the Qt
// resources, :/keycaps/key-dictionary.json), the same grammar, the same two phases.
//
//   Phase A, expandKeycaps(): at load, no fonts. Which keys, legends, actors, ids and every timing.
//                            The actor list never depends on text widths (a "+" that would start a
//                            wrapped row is hidden, not removed).
//   Phase B, layoutKeycaps(): once text can be measured. Key widths, rows, x/y and the geometry of
//                            each keycap's children. The painter runs it next to resolveConstraints.
//
// The only thing phase B needs from a renderer is the ink box of a string at an exact pixel size,
// relative to the baseline origin (PIL's getbbox(text, anchor="ls")). No pixels and no Skia here.

#include "core/scene/SceneTitle.h"

#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace drift::scene {

// Ink box relative to the pen at the baseline, whole pixels, right/bottom exclusive.
struct KeycapInk
{
    int l = 0, t = 0, r = 0, b = 0;
};

// font key of the scene, pixel size, text -> ink box; nullopt when the font cannot be measured.
using KeycapMeasure = std::function<std::optional<KeycapInk>(const QString &font, int px, const QString &text)>;

namespace keycaps {

// One resolved key: identity + what it prints on this platform with this label policy.
struct Key
{
    QString ident, primary, secondary;
    bool glyph = false, wide = false, custom = false;
};

// The key dictionary (format "dafin.keycap-dictionary", version 1). Empty with `error` set when the
// resource is missing or malformed.
const QJsonObject &dictionary(QString *error = nullptr);

// "Ctrl+K THEN Ctrl+C OR Alt+X" -> alternatives (OR) of sequences (THEN) of chords (+) of raw tokens.
std::optional<QList<QList<QStringList>>> parseShortcut(const QString &text, QString *error = nullptr);

std::optional<Key> resolveKey(const QString &token, const QString &platform, const QString &policy,
                              const QJsonObject &legends, QString *error = nullptr);

// The skin's width model (ink, inkMac, semantic) for one key, an even number of px.
std::optional<int> keyWidth(const Key &key, const QJsonObject &skin, const KeycapMeasure &measure,
                            QString *error = nullptr);

} // namespace keycaps

// What phase A leaves for phase B: per block, the generator actor and the ids it made.
struct KeycapsPlan
{
    struct KeyRef
    {
        QString gid;
        keycaps::Key key;
        QString plate, shine, text, text2; // child ids; shine / text2 empty when absent
    };
    struct Chord
    {
        QList<KeyRef> keys;
        QStringList seps; // seps[i] stands before keys[i + 1]
    };
    struct Joiner
    {
        QString id, kind; // kind: "or" or "then"
        int alt = 0, chord = -1;
    };
    struct Block
    {
        QString id;
        QJsonObject a;    // the generator actor, expressions and text slots evaluated
        QJsonObject skin; // the skin of the dictionary
        QJsonObject sep;  // skin.sep with the block's own "sep" over it
        QString label;    // label actor id, empty when none
        QPointF labelSpriteAt;
        QList<QList<Chord>> alts;
        QList<Joiner> joiners;
    };
    QList<Block> blocks;
};

// Phase A for one generator actor `a` (expressions and text slots already evaluated). Appends the
// expanded actors, as actor JSON the loader parses ("_parent" names a child's flattened group,
// "_kc" the block), to `out` and the block to `plan`. False with the reference's message on error.
bool expandKeycaps(const QJsonObject &a, QList<QJsonObject> &out, KeycapsPlan &plan, QString *error = nullptr);

// Phase B for every block of the scene. Idempotent: run it again after any text change.
bool layoutKeycaps(Scene &scene, const KeycapMeasure &measure, QString *error = nullptr);

} // namespace drift::scene
