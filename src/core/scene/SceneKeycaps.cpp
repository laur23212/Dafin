#include "core/scene/SceneKeycaps.h"

// Mirrors tools/scene-ref/keycaps.py line for line: where the Python reads a.get(k, default), so
// does this, and the arithmetic keeps the reference's order and its int()/ceil()/"//" steps.

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

#include <cmath>
#include <cstdio>

namespace drift::scene {

namespace {

struct KFail
{
    QString message;
};

[[noreturn]] void kfail(const QString &message)
{
    throw KFail{message};
}

constexpr int kSS = 2; // the text sprites of sprite2x (a separator is a plain text actor)

const QSet<QString> &glyphs()
{
    static const QSet<QString> g{QStringLiteral("⌘"), QStringLiteral("⌥"), QStringLiteral("⇧"),
                                 QStringLiteral("⌃")};
    return g;
}

// Python's len(): code points.
int pyLen(const QString &s)
{
    return int(s.toUcs4().size());
}

// Python's repr() of a str, for the messages the reference prints with !r.
QString pyRepr(const QString &s)
{
    const QChar q = s.contains(QLatin1Char('\'')) && !s.contains(QLatin1Char('"')) ? QLatin1Char('"') : QLatin1Char('\'');
    QString out(q);
    for (const QChar c : s) {
        if (c == QLatin1Char('\\'))
            out += QStringLiteral("\\\\");
        else if (c == q)
            out += QLatin1Char('\\') + c;
        else if (c == QLatin1Char('\n'))
            out += QStringLiteral("\\n");
        else if (c == QLatin1Char('\t'))
            out += QStringLiteral("\\t");
        else
            out += c;
    }
    return out + q;
}

// Python truthiness of a JSON value.
bool truthy(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::Bool:
        return v.toBool();
    case QJsonValue::Double:
        return v.toDouble() != 0.0;
    case QJsonValue::String:
        return !v.toString().isEmpty();
    case QJsonValue::Array:
        return !v.toArray().isEmpty();
    case QJsonValue::Object:
        return !v.toObject().isEmpty();
    default:
        return false;
    }
}

// d.get(k, default) for a number; a present non-number is an error (Python would raise).
double num(const QJsonObject &o, const char *k, double def)
{
    const QString key = QLatin1String(k);
    if (!o.contains(key))
        return def;
    const QJsonValue v = o.value(key);
    if (!v.isDouble())
        kfail(QStringLiteral("keycaps: %1 must be a number").arg(key));
    return v.toDouble();
}

// d[k] for a number.
double req(const QJsonObject &o, const char *k)
{
    const QString key = QLatin1String(k);
    if (!o.contains(key) || !o.value(key).isDouble())
        kfail(QStringLiteral("keycaps: missing number %1").arg(key));
    return o.value(key).toDouble();
}

QString str(const QJsonObject &o, const char *k, const QString &def = {})
{
    const QString key = QLatin1String(k);
    return o.contains(key) ? o.value(key).toString() : def;
}

QJsonObject obj(const QJsonObject &o, const char *k)
{
    return o.value(QLatin1String(k)).toObject();
}

// round(v, 6), correctly rounded like Python (glibc / UCRT printf), whatever the C locale.
double round6(double v)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.6f", v);
    QByteArray s(buf);
    s.replace(',', '.');
    return s.toDouble();
}

int roundEven(double v)
{
    return int(std::ceil(v / 2.0) * 2.0);
}

// ---- the dictionary --------------------------------------------------------------------------

struct Dictionary
{
    QJsonObject root, keys, skins, joiners;
    QHash<QString, QString> alias; // lower-case alias or id -> id
    QString error;
};

Dictionary loadDictionary()
{
    Dictionary d;
    QFile f(QStringLiteral(":/keycaps/key-dictionary.json"));
    if (!f.open(QIODevice::ReadOnly)) {
        d.error = QStringLiteral("keycaps: the key dictionary (:/keycaps/key-dictionary.json) is missing");
        return d;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("dafin.keycap-dictionary")
        || root.value(QStringLiteral("version")).toInt() != 1) {
        d.error = QStringLiteral("keycaps: not a keycap dictionary v1");
        return d;
    }
    d.root = root;
    d.keys = obj(root, "keys");
    d.skins = obj(root, "skins");
    d.joiners = obj(root, "joiners");
    // The reference builds this in file order, later entries winning. QJsonObject has no file
    // order, so an alias that is another key's id (or two keys' alias) would make the two readers
    // differ: the file is refused instead (test_keycaps.py checks the same).
    for (auto it = d.keys.begin(); it != d.keys.end(); ++it) {
        for (const QJsonValue &a : it.value().toObject().value(QStringLiteral("aliases")).toArray()) {
            const QString lower = a.toString().toLower();
            if ((d.alias.contains(lower) && d.alias.value(lower) != it.key())
                || (d.keys.contains(lower) && lower != it.key())) {
                d.error = QStringLiteral("keycaps: alias '%1' of '%2' is ambiguous").arg(lower, it.key());
                return d;
            }
            d.alias.insert(lower, it.key());
        }
        d.alias.insert(it.key(), it.key());
    }
    return d;
}

const Dictionary &dict()
{
    static const Dictionary d = loadDictionary();
    if (!d.error.isEmpty())
        kfail(d.error);
    return d;
}

// ---- grammar and keys ------------------------------------------------------------------------

QList<QList<QStringList>> parseOrFail(const QString &input)
{
    static const QRegularExpression orRe(QStringLiteral(R"(\s+OR\s+)"),
                                         QRegularExpression::CaseInsensitiveOption
                                             | QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression thenRe(QStringLiteral(R"(\s+THEN\s+)"),
                                           QRegularExpression::CaseInsensitiveOption
                                               | QRegularExpression::UseUnicodePropertiesOption);
    const QString text = input.trimmed();
    if (text.isEmpty())
        kfail(QStringLiteral("keycaps: the shortcut is empty"));
    QList<QList<QStringList>> alts;
    for (const QString &alt : text.split(orRe)) {
        QList<QStringList> seq;
        for (const QString &chord : alt.split(thenRe)) {
            QStringList keys;
            bool empty = false;
            for (const QString &k : chord.split(QLatin1Char('+'))) {
                keys << k.trimmed();
                empty = empty || keys.constLast().isEmpty();
            }
            if (keys.isEmpty() || empty)
                kfail(QStringLiteral("keycaps: empty key in %1 (a \"+\" key is written \"plus\")").arg(pyRepr(chord)));
            seq << keys;
        }
        alts << seq;
    }
    return alts;
}

keycaps::Key makeKey(const QString &ident, const QString &primary, const QString &secondary, bool glyph, bool wide,
                     bool custom)
{
    keycaps::Key k;
    k.ident = ident;
    k.primary = primary;
    k.secondary = secondary;
    k.glyph = glyph;
    k.wide = wide;
    k.custom = custom;
    return k;
}

keycaps::Key resolveOrFail(const QString &token, const QString &platform, const QString &policy,
                           const QJsonObject &legends)
{
    const Dictionary &d = dict();
    if (token.toLower().startsWith(QLatin1String("custom:"))) {
        const QString label = token.mid(token.indexOf(QLatin1Char(':')) + 1);
        if (label.isEmpty())
            kfail(QStringLiteral("keycaps: custom: needs a legend"));
        return makeKey(QStringLiteral("custom:") + label, label, {}, false, false, true);
    }
    QString ident;
    if (pyLen(token) > 1 || glyphs().contains(token))
        ident = d.alias.value(token.toLower());
    if (ident.isEmpty() && pyLen(token) == 1)
        return makeKey(token.toLower(), token.toUpper(), {}, false, false, false);
    static const QRegularExpression fkey(QStringLiteral(R"(\A[fF]([1-9]|1[0-9]|2[0-4])\z)"));
    if (ident.isEmpty() && fkey.match(token).hasMatch())
        return makeKey(token.toLower(), token.toUpper(), {}, false, false, false);
    if (ident.isEmpty())
        kfail(QStringLiteral("keycaps: unknown key %1; use custom:%2 for a legend of your own").arg(pyRepr(token), token));
    QJsonObject spec = d.keys.value(ident).toObject();
    if (spec.contains(QStringLiteral("neutral"))) { // "mod": Ctrl on Windows, Command on macOS
        ident = obj(spec, "neutral").value(platform).toString();
        spec = d.keys.value(ident).toObject();
    }
    const bool wide = truthy(spec.value(QStringLiteral("wide")));
    if (platform == QLatin1String("windows")) {
        const QJsonValue pc = spec.value(QStringLiteral("pc"));
        if (pc.isNull() || pc.isUndefined())
            kfail(QStringLiteral("keycaps: %1 is a macOS key and has no place on a Windows block").arg(pyRepr(ident)));
        return makeKey(ident, pc.toString(), {}, false, wide, false);
    }
    const QJsonValue macValue = spec.value(QStringLiteral("mac"));
    if (macValue.isNull() || macValue.isUndefined())
        kfail(QStringLiteral("keycaps: %1 has no macOS legend").arg(pyRepr(ident)));
    const QJsonObject mac = macValue.toObject();
    if (policy == QLatin1String("physical")) {
        QString name = legends.value(ident).toString();
        if (name.isEmpty())
            name = spec.value(QStringLiteral("pc")).toString();
        if (name.isEmpty())
            name = str(mac, "plain", ident).toUpper();
        return makeKey(ident, name, {}, false, wide, false);
    }
    const QString glyph = str(mac, "glyph");
    if (!glyph.isEmpty() && policy == QLatin1String("glyph"))
        return makeKey(ident, glyph, {}, true, wide, false);
    if (!glyph.isEmpty() && policy == QLatin1String("glyphPlusNames"))
        return makeKey(ident, glyph, str(mac, "dual"), true, wide, false);
    return makeKey(ident, str(mac, "plain", ident), {}, false, wide, false);
}

// ---- phase A ---------------------------------------------------------------------------------

// keycaps._timed: the entrance and the exit of one generated actor.
QJsonObject timedIn(double start, double dur, const QJsonValue &ease, const QJsonObject &from)
{
    return QJsonObject{{QStringLiteral("start"), round6(start)},
                       {QStringLiteral("dur"), dur},
                       {QStringLiteral("ease"), truthy(ease) ? ease : QJsonValue(QStringLiteral("smoothstep"))},
                       {QStringLiteral("from"), from}};
}

QJsonObject timedOut(double start, double dur)
{
    return QJsonObject{{QStringLiteral("start"), round6(start)},
                       {QStringLiteral("dur"), dur},
                       {QStringLiteral("ease"), QStringLiteral("smoothstep")},
                       {QStringLiteral("to"), QJsonObject{{QStringLiteral("opacity"), 0}}}};
}

QJsonObject baseActor(const QString &id, const QString &role, const QString &type, const QString &block,
                      const QString &parent = {})
{
    QJsonObject d{{QStringLiteral("id"), id}, {QStringLiteral("role"), role}, {QStringLiteral("type"), type},
                  {QStringLiteral("_kc"), block}};
    if (!parent.isEmpty())
        d.insert(QStringLiteral("_parent"), parent);
    return d;
}

const QJsonObject &zeroBaseline()
{
    static const QJsonObject at{{QStringLiteral("baseline"), QJsonArray{0, 0}}};
    return at;
}

QJsonObject merged(QJsonObject base, const QJsonObject &over)
{
    for (auto it = over.begin(); it != over.end(); ++it)
        base.insert(it.key(), it.value());
    return base;
}

void expandOrFail(const QJsonObject &a, QList<QJsonObject> &out, KeycapsPlan &plan)
{
    static const QStringList platforms{QStringLiteral("windows"), QStringLiteral("macos")};
    static const QStringList policies{QStringLiteral("names"), QStringLiteral("glyph"), QStringLiteral("glyphPlusNames"),
                                      QStringLiteral("physical")};
    const Dictionary &d = dict();
    const QString bid = a.value(QStringLiteral("id")).toString();
    const QString platform = str(a, "platform", QStringLiteral("windows"));
    if (!platforms.contains(platform))
        kfail(QStringLiteral("%1: platform must be one of ('windows', 'macos')").arg(bid));
    QString skinName = a.value(QStringLiteral("skin")).toString();
    if (skinName.isEmpty())
        skinName = platform == QLatin1String("windows") ? QStringLiteral("classic-pc") : QStringLiteral("classic-mac");
    if (!d.skins.contains(skinName))
        kfail(QStringLiteral("%1: unknown skin %2").arg(bid, pyRepr(skinName)));
    const QJsonObject skin = d.skins.value(skinName).toObject();
    const QString policy = str(a, "labelPolicy", platform == QLatin1String("windows") ? QStringLiteral("names")
                                                                                       : QStringLiteral("glyphPlusNames"));
    if (!policies.contains(policy))
        kfail(QStringLiteral("%1: labelPolicy must be one of ('names', 'glyph', 'glyphPlusNames', 'physical')").arg(bid));
    const QJsonObject legends = obj(a, "legends");
    const QJsonObject sepStyle = merged(obj(skin, "sep"), obj(a, "sep")); // per block: Stage13 A signs are 43 px, Stage13C's 41
    const QList<QList<QStringList>> parsed = parseOrFail(str(a, "shortcut"));
    const QJsonObject tm = obj(a, "timing");
    const QJsonObject kt = obj(tm, "key"), st = obj(tm, "sep"), jt = obj(tm, "joiner"), lt = obj(tm, "label");
    const QJsonObject kin = obj(kt, "in"), kout = obj(kt, "out");

    KeycapsPlan::Block blk;
    blk.id = bid;
    blk.a = a;
    blk.skin = skin;
    blk.sep = sepStyle;

    const auto joinerText = [&](const char *word, const char *dictKey) {
        const QJsonValue w = a.value(QLatin1String(word));
        return truthy(w) ? w.toString() : d.joiners.value(QLatin1String(dictKey)).toString();
    };
    const auto joinerActor = [&](const QString &jid, double t, const QString &text) {
        QJsonObject j = baseActor(jid, QStringLiteral("keycapsJoiner"), QStringLiteral("text"), bid);
        j.insert(QStringLiteral("text"), text);
        j.insert(QStringLiteral("font"), str(jt, "font", QStringLiteral("semi")));
        j.insert(QStringLiteral("size"), num(jt, "size", 23));
        j.insert(QStringLiteral("color"), jt.contains(QStringLiteral("color")) ? jt.value(QStringLiteral("color"))
                                                                              : QJsonValue(QStringLiteral("AQUA")));
        j.insert(QStringLiteral("at"), zeroBaseline());
        j.insert(QStringLiteral("in"), timedIn(t, num(jt, "dur", 0.38), QJsonValue(),
                                               QJsonObject{{QStringLiteral("opacity"), 0},
                                                           {QStringLiteral("dy"), num(jt, "dy", 7)}}));
        j.insert(QStringLiteral("out"), timedOut(num(jt, "outStart", 4.86), num(jt, "outDur", 0.34)));
        return j;
    };

    if (truthy(a.value(QStringLiteral("label")))) {
        const QJsonObject lb = obj(a, "label");
        const double t = num(lt, "start", 0.7);
        QJsonObject l = baseActor(bid + QStringLiteral(".label"), QStringLiteral("keycapsLabel"), QStringLiteral("text"), bid);
        l.insert(QStringLiteral("text"), lb.value(QStringLiteral("text")).toString());
        l.insert(QStringLiteral("font"), str(lb, "font", QStringLiteral("semi")));
        l.insert(QStringLiteral("size"), num(lb, "size", 25));
        l.insert(QStringLiteral("color"), lb.contains(QStringLiteral("color")) ? lb.value(QStringLiteral("color"))
                                                                              : QJsonValue(QStringLiteral("AQUA")));
        l.insert(QStringLiteral("at"), zeroBaseline());
        l.insert(QStringLiteral("in"), timedIn(t, num(lt, "dur", 0.38), QJsonValue(),
                                               QJsonObject{{QStringLiteral("opacity"), 0},
                                                           {QStringLiteral("dy"), num(lt, "dy", 7)}}));
        l.insert(QStringLiteral("out"), timedOut(num(lt, "outStart", 4.86), num(lt, "outDur", 0.34)));
        const QJsonArray at = lb.value(QStringLiteral("at")).toArray();
        if (at.size() != 2 || !at.at(0).isDouble() || !at.at(1).isDouble())
            kfail(QStringLiteral("%1: label.at must be [x, y]").arg(bid));
        blk.labelSpriteAt = QPointF(at.at(0).toDouble(), at.at(1).toDouble());
        out << l;
        blk.label = bid + QStringLiteral(".label");
    }

    for (int ai = 0; ai < parsed.size(); ++ai) {
        const QList<QStringList> &seq = parsed.at(ai);
        QList<KeycapsPlan::Chord> altMeta;
        if (ai > 0) {
            const QString jid = QStringLiteral("%1.or%2").arg(bid).arg(ai);
            const double t = num(jt, "orStart", 1.35) + num(jt, "orStep", 0.32) * (ai - 1);
            out << joinerActor(jid, t, joinerText("orWord", "or"));
            blk.joiners << KeycapsPlan::Joiner{jid, QStringLiteral("or"), ai, -1};
        }
        for (int ci = 0; ci < seq.size(); ++ci) {
            const QStringList &chord = seq.at(ci);
            if (ci > 0) {
                const QString jid = QStringLiteral("%1.a%2then%3").arg(bid).arg(ai).arg(ci);
                const double t = num(jt, "thenStart", 1.35) + num(jt, "thenStep", 0.38) * (ci - 1);
                out << joinerActor(jid, t, joinerText("thenWord", "then"));
                blk.joiners << KeycapsPlan::Joiner{jid, QStringLiteral("then"), ai, ci};
            }
            QList<keycaps::Key> keys;
            for (const QString &tok : chord)
                keys << resolveOrFail(tok, platform, policy, legends);
            const int nk = int(keys.size());
            QList<double> keyStarts;
            for (int ki = 0; ki < nk; ++ki)
                keyStarts << num(kt, "start", 0.85) + num(kt, "altStep", 0.0) * ai + num(kt, "chordStep", 0.0) * ci
                                 + num(kt, "keyStep", 0.17) * ki;
            KeycapsPlan::Chord chordMeta;
            for (int ki = 0; ki < nk; ++ki) {
                const keycaps::Key &key = keys.at(ki);
                if (ki > 0) {
                    const QString sid = QStringLiteral("%1.a%2c%3p%4").arg(bid).arg(ai).arg(ci).arg(ki);
                    const QString rel = str(st, "rel", QStringLiteral("mid"));
                    const QJsonArray lst = st.value(QStringLiteral("list")).toArray();
                    const double left = keyStarts.at(ki - 1), right = keyStarts.at(ki);
                    double s0 = 0.0;
                    if (!lst.isEmpty() && lst.size() >= ki && !lst.at(ki - 1).isNull()
                        && nk == int(num(st, "listFor", nk))) {
                        s0 = lst.at(ki - 1).toDouble();
                    } else {
                        double base = 0.0;
                        if (rel == QLatin1String("left"))
                            base = left;
                        else if (rel == QLatin1String("right"))
                            base = right;
                        else if (rel == QLatin1String("mid"))
                            base = (left + right) / 2;
                        else
                            kfail(QStringLiteral("%1: sep.rel must be left, right or mid").arg(bid));
                        s0 = base + num(st, "off", 0.04);
                    }
                    const QJsonObject sin = obj(st, "in"), sout = obj(st, "out");
                    QJsonObject s = baseActor(sid, QStringLiteral("keycapsSeparator"), QStringLiteral("text"), bid);
                    s.insert(QStringLiteral("text"), QStringLiteral("+"));
                    s.insert(QStringLiteral("font"), sepStyle.value(QStringLiteral("font")));
                    s.insert(QStringLiteral("size"), sepStyle.value(QStringLiteral("size")));
                    s.insert(QStringLiteral("color"), sepStyle.value(QStringLiteral("color")));
                    s.insert(QStringLiteral("at"), zeroBaseline());
                    s.insert(QStringLiteral("in"), timedIn(s0, num(sin, "dur", 0.32), QJsonValue(),
                                                           QJsonObject{{QStringLiteral("opacity"), 0}}));
                    s.insert(QStringLiteral("out"), timedOut(num(sout, "start", 4.75), num(sout, "dur", 0.30)));
                    out << s;
                    chordMeta.seps << sid;
                }
                const QString gid = QStringLiteral("%1.a%2c%3k%4").arg(bid).arg(ai).arg(ci).arg(ki);
                QJsonObject from{{QStringLiteral("opacity"), 0}};
                if (truthy(kin.value(QStringLiteral("dy"))))
                    from.insert(QStringLiteral("dy"), kin.value(QStringLiteral("dy")));
                const double outStart = num(kout, "start", 4.72) + num(kout, "revStep", 0.0) * (nk - 1 - ki);
                QJsonObject grp = baseActor(gid, QStringLiteral("keycap"), QStringLiteral("group"), bid);
                grp.insert(QStringLiteral("flatten"), true);
                grp.insert(QStringLiteral("at"), QJsonArray{0, 0});
                grp.insert(QStringLiteral("size"), QJsonArray{0, 0});
                grp.insert(QStringLiteral("ss"), skin.value(QStringLiteral("ss")));
                grp.insert(QStringLiteral("in"), timedIn(keyStarts.at(ki), num(kin, "dur", 0.38),
                                                         kin.value(QStringLiteral("ease")), from));
                grp.insert(QStringLiteral("out"), timedOut(outStart, num(kout, "dur", 0.30)));
                if (truthy(kin.value(QStringLiteral("snap"))))
                    grp.insert(QStringLiteral("snap"), kin.value(QStringLiteral("snap")));
                out << grp;

                KeycapsPlan::KeyRef ref;
                ref.gid = gid;
                ref.key = key;
                QJsonObject plate = baseActor(gid + QStringLiteral(".plate"), QStringLiteral("keycapPlate"),
                                              QStringLiteral("roundRect"), bid, gid);
                plate.insert(QStringLiteral("box"), QJsonArray{0, 0, 0, 0});
                plate.insert(QStringLiteral("radius"), skin.value(QStringLiteral("radius")));
                plate.insert(QStringLiteral("fill"), skin.value(QStringLiteral("fill")));
                plate.insert(QStringLiteral("strokeColor"), skin.value(QStringLiteral("stroke")));
                plate.insert(QStringLiteral("stroke"), skin.value(QStringLiteral("strokeWidth")));
                out << plate;
                ref.plate = plate.value(QStringLiteral("id")).toString();
                if (truthy(skin.value(QStringLiteral("shine")))) {
                    const QJsonObject sh = obj(skin, "shine");
                    QJsonObject line = baseActor(gid + QStringLiteral(".shine"), QStringLiteral("keycapShine"),
                                                 QStringLiteral("line"), bid, gid);
                    line.insert(QStringLiteral("from"), QJsonArray{0, 0});
                    line.insert(QStringLiteral("to"), QJsonArray{0, 0});
                    line.insert(QStringLiteral("width"), sh.value(QStringLiteral("width")));
                    line.insert(QStringLiteral("color"), sh.value(QStringLiteral("color")));
                    out << line;
                    ref.shine = line.value(QStringLiteral("id")).toString();
                }
                const QJsonObject pm = obj(skin, "primary");
                const bool sym = glyphs().contains(key.primary);
                QJsonObject text = baseActor(gid + QStringLiteral(".text"), QStringLiteral("keycapLabel"),
                                             QStringLiteral("text"), bid, gid);
                text.insert(QStringLiteral("text"), key.primary);
                text.insert(QStringLiteral("font"), pm.value(sym ? QStringLiteral("symbolFont") : QStringLiteral("font")));
                text.insert(QStringLiteral("size"), 0);
                text.insert(QStringLiteral("color"), skin.value(QStringLiteral("textColor")));
                text.insert(QStringLiteral("at"), zeroBaseline());
                out << text;
                ref.text = text.value(QStringLiteral("id")).toString();
                if (!key.secondary.isEmpty()) {
                    const QJsonObject dual = obj(skin, "dual");
                    QJsonObject t2 = baseActor(gid + QStringLiteral(".text2"), QStringLiteral("keycapSecondary"),
                                               QStringLiteral("text"), bid, gid);
                    t2.insert(QStringLiteral("text"), key.secondary);
                    t2.insert(QStringLiteral("font"), dual.value(QStringLiteral("secondaryFont")));
                    t2.insert(QStringLiteral("size"), dual.value(QStringLiteral("secondarySize")));
                    t2.insert(QStringLiteral("color"), dual.value(QStringLiteral("secondaryColor")));
                    t2.insert(QStringLiteral("at"), zeroBaseline());
                    out << t2;
                    ref.text2 = t2.value(QStringLiteral("id")).toString();
                }
                chordMeta.keys << ref;
            }
            altMeta << chordMeta;
        }
        blk.alts << altMeta;
    }
    plan.blocks << blk;
}

// ---- phase B ---------------------------------------------------------------------------------

int keyWidthOrFail(const keycaps::Key &key, const QJsonObject &skin, const KeycapMeasure &measure);

struct Layout
{
    Scene &sc;
    const KeycapMeasure &measure;
    QHash<QString, int> index;

    Actor &actor(const QString &id)
    {
        const auto it = index.constFind(id);
        if (it == index.constEnd())
            kfail(QStringLiteral("keycaps: actor %1 is missing").arg(id));
        return sc.actors[it.value()];
    }

    KeycapInk ink(const QString &font, double px, const QString &text) const
    {
        const std::optional<KeycapInk> box = measure ? measure(font, int(px), text) : std::nullopt;
        if (!box)
            kfail(QStringLiteral("keycaps: cannot measure %1 in font %2").arg(pyRepr(text), pyRepr(font)));
        return *box;
    }

    // keycaps._sprite_wh: the 1x sprite of a 2x text.
    QPair<int, int> spriteWh(const KeycapInk &i) const
    {
        return {int(std::floor((i.r - i.l + 14 * kSS) / double(kSS))), int(std::floor((i.b - i.t + 16 * kSS) / double(kSS)))};
    }

    // keycaps._set_text_at: sprite top-left -> pen x, baseline y (docs/scene-titles.md, Text anchors).
    void setTextAt(Actor &a, double sx, double sy) const
    {
        const KeycapInk i = ink(a.font, a.size * kSS, a.text);
        a.baseline = QPointF(sx + (14 - i.l) / double(kSS), sy + (16 - i.t) / double(kSS));
        a.anchorKind = QStringLiteral("baseline");
    }

    void block(const KeycapsPlan::Block &blk)
    {
        const QJsonObject &a = blk.a;
        const QJsonObject &skin = blk.skin;
        const QJsonObject flow = obj(a, "flow");
        const QJsonArray at = a.value(QStringLiteral("at")).toArray();
        if (at.size() != 2 || !at.at(0).isDouble() || !at.at(1).isDouble())
            kfail(QStringLiteral("%1: at must be [x, y]").arg(blk.id));
        const double x0 = at.at(0).toDouble(), y0 = at.at(1).toDouble();
        const QJsonObject lock = obj(a, "lock");
        const double gap = num(flow, "gap", 32), plusDx = num(flow, "plusDx", 16);
        // None and 0 both mean "no wrap", as the reference's `if max_w`.
        const double maxW = flow.value(QStringLiteral("maxWidth")).isNull() ? 0.0 : num(flow, "maxWidth", 0);
        const double rowPitch = num(flow, "rowPitch", 160), chordPitch = num(flow, "chordPitch", 164);
        const double orPitch = num(flow, "orPitch", 74), thenPitch = num(flow, "thenPitch", 72);
        const double jx = num(flow, "joinerDx", 8), orDy = num(flow, "orDy", -7), thenDy = num(flow, "thenDy", -4);
        const QJsonValue plusDy = flow.contains(QStringLiteral("plusDy")) ? flow.value(QStringLiteral("plusDy"))
                                                                        : QJsonValue(QStringLiteral("center"));
        const bool centerSep = plusDy.toString() == QLatin1String("center");
        if (!centerSep && !plusDy.isDouble())
            kfail(QStringLiteral("%1: flow.plusDy must be \"center\" or a number").arg(blk.id));
        if (!blk.label.isEmpty())
            setTextAt(actor(blk.label), blk.labelSpriteAt.x(), blk.labelSpriteAt.y());
        const QJsonObject &sp = blk.sep;
        const QPair<int, int> plus = spriteWh(ink(sp.value(QStringLiteral("font")).toString(), req(sp, "size") * kSS,
                                                   QStringLiteral("+")));
        const double adv = gap + plus.first;
        double y = y0;
        const auto joiner = [&](const QString &kind, int alt, int chord) -> Actor & {
            for (const KeycapsPlan::Joiner &j : blk.joiners)
                if (j.kind == kind && j.alt == alt && j.chord == chord)
                    return actor(j.id);
            kfail(QStringLiteral("%1: joiner %2 missing").arg(blk.id, kind));
        };
        for (int ai = 0; ai < blk.alts.size(); ++ai) {
            const QList<KeycapsPlan::Chord> &alt = blk.alts.at(ai);
            if (ai > 0) {
                setTextAt(joiner(QStringLiteral("or"), ai, -1), x0 + jx, y + orDy);
                y += orPitch;
            }
            for (int ci = 0; ci < alt.size(); ++ci) {
                if (ci > 0) {
                    setTextAt(joiner(QStringLiteral("then"), ai, ci), x0 + jx, y + thenDy);
                    y += thenPitch;
                }
                const KeycapsPlan::Chord &chord = alt.at(ci);
                const bool locked = lockApplies(lock, chord, int(blk.alts.size()), int(alt.size()));
                int row = 0;
                double end = x0;
                for (int ki = 0; ki < chord.keys.size(); ++ki) {
                    const KeycapsPlan::KeyRef &k = chord.keys.at(ki);
                    const double w = locked ? lockNumber(lock, "w", ki) : keyWidthOrFail(k.key, skin, measure);
                    double h = num(a, "height", 0);
                    if (h == 0.0) {
                        const double dual = num(skin, "dualHeight", 0);
                        h = !k.key.secondary.isEmpty() && dual != 0.0 ? dual : req(skin, "height");
                    }
                    bool hidden = false;
                    double kx = 0.0, ky = 0.0;
                    if (locked) {
                        kx = lockNumber(lock, "x", ki);
                        ky = y;
                    } else if (ki == 0) {
                        kx = x0;
                        ky = y;
                    } else if (maxW != 0.0 && end + adv + w > x0 + maxW) {
                        ++row;
                        kx = x0;
                        hidden = true;
                        ky = y + row * rowPitch;
                    } else {
                        kx = end + adv;
                        ky = y + row * rowPitch;
                    }
                    if (ki > 0) {
                        Actor &sep = actor(chord.seps.at(ki - 1));
                        sep.hidden = hidden;
                        if (!hidden) {
                            double sx = 0.0, sy = 0.0;
                            if (locked) {
                                const QJsonArray p = lock.value(QStringLiteral("sep")).toArray().at(ki - 1).toArray();
                                sx = p.at(0).toDouble();
                                sy = p.at(1).toDouble();
                            } else {
                                sx = end + plusDx;
                                sy = ky + (centerSep ? double(int((h - plus.second) / 2.0)) : plusDy.toDouble());
                            }
                            setTextAt(sep, sx, sy);
                        }
                    }
                    placeKey(k, skin, kx, ky, w, h);
                    end = kx + w;
                }
                y += (row + 1) * chordPitch;
            }
        }
    }

    static double lockNumber(const QJsonObject &lock, const char *k, int i)
    {
        const QJsonValue v = lock.value(QLatin1String(k)).toArray().at(i);
        if (!v.isDouble())
            kfail(QStringLiteral("keycaps: lock.%1[%2] must be a number").arg(QLatin1String(k)).arg(i));
        return v.toDouble();
    }

    static bool lockApplies(const QJsonObject &lock, const KeycapsPlan::Chord &chord, int nAlts, int nChords)
    {
        if (lock.isEmpty() || nAlts != 1 || nChords != 1)
            return false;
        const QJsonArray want = lock.value(QStringLiteral("keys")).toArray();
        if (want.size() != chord.keys.size())
            return false;
        for (int i = 0; i < want.size(); ++i)
            if (!want.at(i).isString() || want.at(i).toString() != chord.keys.at(i).key.ident)
                return false;
        return true;
    }

    void placeKey(const KeycapsPlan::KeyRef &k, const QJsonObject &skin, double x, double y, double w, double h)
    {
        const keycaps::Key &key = k.key;
        const double ss = req(skin, "ss");
        Actor &g = actor(k.gid);
        g.groupAt = QPointF(x, y);
        g.groupW = w;
        g.groupH = h;
        const double pad = req(skin, "pad");
        Actor &plate = actor(k.plate);
        plate.box[0] = pad;
        plate.box[1] = pad;
        plate.box[2] = w - pad - 1;
        plate.box[3] = h - pad - 1;
        if (!k.shine.isEmpty()) {
            const QJsonObject sh = obj(skin, "shine");
            Actor &line = actor(k.shine);
            line.from = QPointF(req(sh, "inset"), req(sh, "y"));
            line.to = QPointF(w - req(sh, "inset"), req(sh, "y"));
        }
        const QJsonObject pm = obj(skin, "primary");
        Actor &text = actor(k.text);
        if (!key.secondary.isEmpty()) {
            const QJsonObject d = obj(skin, "dual");
            const double size = key.glyph ? req(d, "symbolSize") : req(d, "primarySize");
            text.size = size;
            text.centerInGroup = false;
            text.centerDy = 0.0;
            KeycapInk i = ink(text.font, size * ss, key.primary);
            text.baseline = QPointF(((w * ss - (i.r - i.l)) / 2.0 - i.l) / ss, (req(d, "top") * ss - i.t) / ss);
            Actor &t2 = actor(k.text2);
            i = ink(t2.font, t2.size * ss, key.secondary);
            t2.baseline = QPointF(((w * ss - (i.r - i.l)) / 2.0 - i.l) / ss,
                                  ((h - req(d, "secondaryBottom")) * ss - i.t) / ss);
        } else {
            double size = 0.0;
            if (key.glyph)
                size = req(pm, "symbolSize");
            else
                size = pyLen(key.primary) < num(pm, "longFrom", 99) ? req(pm, "size") : num(pm, "longSize", req(pm, "size"));
            text.size = size;
            text.baseline = QPointF(0, 0);
            text.centerInGroup = true;
            text.centerDy = req(pm, "dy");
        }
    }
};

int keyWidthOrFail(const keycaps::Key &key, const QJsonObject &skin, const KeycapMeasure &measure)
{
    const auto inkOf = [&](const QString &font, double px, const QString &text) {
        const std::optional<KeycapInk> box = measure ? measure(font, int(px), text) : std::nullopt;
        if (!box)
            kfail(QStringLiteral("keycaps: cannot measure %1 in font %2").arg(pyRepr(text), pyRepr(font)));
        return *box;
    };
    const QJsonObject w = obj(skin, "width");
    const QString model = str(w, "model");
    const QJsonObject pm = obj(skin, "primary");
    if (model == QLatin1String("ink")) {
        const KeycapInk i = inkOf(str(w, "font"), req(w, "size"), key.primary);
        double v = qMax(req(w, "min"), double(i.r - i.l) + req(w, "pad"));
        if (key.wide)
            v = qMax(v, num(w, "wideMin", 355));
        return roundEven(v);
    }
    if (model == QLatin1String("inkMac")) {
        double v = req(w, "min");
        if (!key.secondary.isEmpty()) {
            const KeycapInk i = inkOf(str(w, "dualFont"), req(w, "dualSize"), key.secondary);
            v = qMax(v, double(i.r - i.l) + req(w, "dualPad"));
        }
        if (key.wide)
            v = qMax(v, req(w, "space"));
        // overflow guard, not in the proofs (it never fires on them): a legend must clear the plate
        const double px = key.glyph ? req(pm, "symbolSize") : req(pm, "size");
        const KeycapInk i = inkOf(str(pm, key.glyph ? "symbolFont" : "font"), px, key.primary);
        v = qMax(v, double(i.r - i.l) + 44);
        return roundEven(v);
    }
    if (model == QLatin1String("semantic")) {
        const Dictionary &d = dict();
        double base = 0.0;
        if (d.keys.contains(key.ident))
            base = req(d.keys.value(key.ident).toObject(), "w");
        else
            base = req(w, "default");
        const double pt = pyLen(key.primary) < num(pm, "longFrom", 99) ? req(pm, "size") : num(pm, "longSize", req(pm, "size"));
        const KeycapInk i = inkOf(str(pm, key.glyph ? "symbolFont" : "font"), pt, key.primary);
        const double textw = i.r - i.l;
        if (key.primary == QLatin1String("Command") || key.primary == QLatin1String("Option"))
            base = qMax(base, num(w, "plainCommandMin", 218));
        double sec = 0.0;
        if (!key.secondary.isEmpty()) {
            const KeycapInk si = inkOf(str(w, "dualFont"), req(w, "dualSize"), key.secondary);
            sec = si.r - si.l;
        }
        base = qMax(base, qMax(textw + req(w, "textPad"), sec + req(w, "dualPad")));
        return roundEven(base);
    }
    kfail(QStringLiteral("unknown width model %1").arg(pyRepr(model)));
}

} // namespace

namespace keycaps {

const QJsonObject &dictionary(QString *error)
{
    static const QJsonObject empty;
    try {
        return dict().root;
    } catch (const KFail &f) {
        if (error)
            *error = f.message;
        return empty;
    }
}

std::optional<QList<QList<QStringList>>> parseShortcut(const QString &text, QString *error)
{
    try {
        return parseOrFail(text);
    } catch (const KFail &f) {
        if (error)
            *error = f.message;
        return std::nullopt;
    }
}

std::optional<Key> resolveKey(const QString &token, const QString &platform, const QString &policy,
                              const QJsonObject &legends, QString *error)
{
    try {
        return resolveOrFail(token, platform, policy, legends);
    } catch (const KFail &f) {
        if (error)
            *error = f.message;
        return std::nullopt;
    }
}

std::optional<int> keyWidth(const Key &key, const QJsonObject &skin, const KeycapMeasure &measure, QString *error)
{
    try {
        return keyWidthOrFail(key, skin, measure);
    } catch (const KFail &f) {
        if (error)
            *error = f.message;
        return std::nullopt;
    }
}

} // namespace keycaps

bool expandKeycaps(const QJsonObject &a, QList<QJsonObject> &out, KeycapsPlan &plan, QString *error)
{
    try {
        QList<QJsonObject> made;
        expandOrFail(a, made, plan);
        out << made;
        return true;
    } catch (const KFail &f) {
        if (error)
            *error = f.message;
        return false;
    }
}

bool layoutKeycaps(Scene &scene, const KeycapMeasure &measure, QString *error)
{
    if (!scene.keycaps)
        return true;
    Layout lay{scene, measure, {}};
    for (int i = 0; i < scene.actors.size(); ++i)
        lay.index.insert(scene.actors.at(i).id, i);
    try {
        for (const KeycapsPlan::Block &blk : scene.keycaps->blocks)
            lay.block(blk);
    } catch (const KFail &f) {
        if (error)
            *error = f.message;
        return false;
    }
    return true;
}

} // namespace drift::scene
