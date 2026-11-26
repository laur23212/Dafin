#include "core/scene/SceneTitle.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

#include <cfenv>
#include <cmath>

namespace drift::scene {

namespace {

struct Fail
{
    QString message;
};

[[noreturn]] void fail(const QString &message)
{
    throw Fail{message};
}

double clamp01(double u)
{
    return u < 0.0 ? 0.0 : (u > 1.0 ? 1.0 : u);
}

double cubicBezier(double a, double b, double c, double d, double t)
{
    const double mt = 1.0 - t;
    return mt * mt * mt * a + 3.0 * mt * mt * t * b + 3.0 * mt * t * t * c + t * t * t * d;
}

// Same bisection as core/base/Bezier.h and scene_core.py (24 halvings).
double bezierParameterForX(double x1, double x2, double x)
{
    double lo = 0.0, hi = 1.0;
    for (int i = 0; i < 24; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (cubicBezier(0.0, x1, x2, 1.0, mid) < x)
            lo = mid;
        else
            hi = mid;
    }
    return 0.5 * (lo + hi);
}

// ---- values ----------------------------------------------------------------------------------

enum class VK { None, Int, Float, Bool, Str, List };

VK kindOf(const QVariant &v)
{
    switch (v.metaType().id()) {
    case QMetaType::Bool:
        return VK::Bool;
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::UInt:
    case QMetaType::ULongLong:
        return VK::Int;
    case QMetaType::Double:
    case QMetaType::Float:
        return VK::Float;
    case QMetaType::QString:
        return VK::Str;
    case QMetaType::QVariantList:
        return VK::List;
    default:
        return VK::None;
    }
}

bool isNumber(const QVariant &v)
{
    const VK k = kindOf(v);
    return k == VK::Int || k == VK::Float || k == VK::Bool;
}

bool isIntLike(const QVariant &v)
{
    const VK k = kindOf(v);
    return k == VK::Int || k == VK::Bool;
}

double num(const QVariant &v)
{
    if (!isNumber(v))
        fail(QStringLiteral("not a number: %1").arg(v.toString()));
    return v.toDouble();
}

qlonglong inum(const QVariant &v)
{
    return v.toLongLong();
}

bool truthy(const QVariant &v)
{
    switch (kindOf(v)) {
    case VK::Bool:
        return v.toBool();
    case VK::Int:
        return v.toLongLong() != 0;
    case VK::Float:
        return v.toDouble() != 0.0;
    case VK::Str:
        return !v.toString().isEmpty();
    case VK::List:
        return !v.toList().isEmpty();
    default:
        return false;
    }
}

bool valuesEqual(const QVariant &a, const QVariant &b)
{
    if (isNumber(a) && isNumber(b))
        return num(a) == num(b);
    if (kindOf(a) == VK::Str && kindOf(b) == VK::Str)
        return a.toString() == b.toString();
    if (kindOf(a) == VK::List && kindOf(b) == VK::List) {
        const QVariantList x = a.toList(), y = b.toList();
        if (x.size() != y.size())
            return false;
        for (int i = 0; i < x.size(); ++i)
            if (!valuesEqual(x.at(i), y.at(i)))
                return false;
        return true;
    }
    return false;
}

QJsonValue valueToJson(const QVariant &v)
{
    switch (kindOf(v)) {
    case VK::Bool:
        return v.toBool();
    case VK::Int:
        return QJsonValue(qint64(v.toLongLong()));
    case VK::Float:
        return v.toDouble();
    case VK::Str:
        return v.toString();
    case VK::List: {
        QJsonArray a;
        for (const QVariant &x : v.toList())
            a.append(valueToJson(x));
        return a;
    }
    default:
        return QJsonValue();
    }
}

// Python's str() of a float: shortest round trip, always with a decimal point or exponent.
QString pyFloatStr(double d)
{
    if (std::isinf(d))
        return d > 0 ? QStringLiteral("inf") : QStringLiteral("-inf");
    if (std::isnan(d))
        return QStringLiteral("nan");
    QString s = QString::number(d, 'g', QLocale::FloatingPointShortest);
    if (!s.contains(QLatin1Char('.')) && !s.contains(QLatin1Char('e')) && !s.contains(QLatin1Char('n')))
        s += QStringLiteral(".0");
    return s;
}

QString pyStr(const QVariant &v)
{
    switch (kindOf(v)) {
    case VK::Bool:
        return v.toBool() ? QStringLiteral("True") : QStringLiteral("False");
    case VK::Int:
        return QString::number(v.toLongLong());
    case VK::Float:
        return pyFloatStr(v.toDouble());
    case VK::Str:
        return v.toString();
    case VK::List: {
        QStringList parts;
        for (const QVariant &x : v.toList())
            parts << pyStr(x);
        return QLatin1Char('[') + parts.join(QStringLiteral(", ")) + QLatin1Char(']');
    }
    default:
        return QStringLiteral("None");
    }
}

// ---- expressions -----------------------------------------------------------------------------

struct Token
{
    enum Kind { End, Number, String, Name, Op } kind = End;
    QString text;
    QVariant value;
};

class Parser
{
public:
    Parser(const QString &src, const QVariantMap &env) : m_src(src), m_env(env) { tokenize(); }

    QVariant run()
    {
        const QVariant v = ternary();
        if (peek().kind != Token::End)
            bad();
        return v;
    }

private:
    [[noreturn]] void bad() { fail(QStringLiteral("unsupported expression '%1'").arg(m_src)); }

    void tokenize()
    {
        int i = 0;
        const int n = m_src.size();
        while (i < n) {
            const QChar c = m_src.at(i);
            if (c.isSpace()) {
                ++i;
                continue;
            }
            Token t;
            if (c.isDigit() || (c == QLatin1Char('.') && i + 1 < n && m_src.at(i + 1).isDigit())) {
                int j = i;
                bool isFloat = false;
                while (j < n && (m_src.at(j).isDigit() || m_src.at(j) == QLatin1Char('_')))
                    ++j;
                if (j < n && m_src.at(j) == QLatin1Char('.')) {
                    isFloat = true;
                    ++j;
                    while (j < n && m_src.at(j).isDigit())
                        ++j;
                }
                if (j < n && (m_src.at(j) == QLatin1Char('e') || m_src.at(j) == QLatin1Char('E'))) {
                    int k = j + 1;
                    if (k < n && (m_src.at(k) == QLatin1Char('+') || m_src.at(k) == QLatin1Char('-')))
                        ++k;
                    if (k < n && m_src.at(k).isDigit()) {
                        isFloat = true;
                        j = k;
                        while (j < n && m_src.at(j).isDigit())
                            ++j;
                    }
                }
                QString lit = m_src.mid(i, j - i);
                lit.remove(QLatin1Char('_'));
                t.kind = Token::Number;
                t.value = isFloat ? QVariant(lit.toDouble()) : QVariant(lit.toLongLong());
                i = j;
            } else if (c.isLetter() || c == QLatin1Char('_')) {
                int j = i;
                while (j < n && (m_src.at(j).isLetterOrNumber() || m_src.at(j) == QLatin1Char('_')))
                    ++j;
                t.kind = Token::Name;
                t.text = m_src.mid(i, j - i);
                i = j;
            } else if (c == QLatin1Char('\'') || c == QLatin1Char('"')) {
                int j = i + 1;
                QString s;
                while (j < n && m_src.at(j) != c) {
                    if (m_src.at(j) == QLatin1Char('\\') && j + 1 < n)
                        ++j;
                    s += m_src.at(j++);
                }
                if (j >= n)
                    fail(QStringLiteral("bad expression '%1': unterminated string").arg(m_src));
                t.kind = Token::String;
                t.value = s;
                i = j + 1;
            } else {
                static const char *ops[] = {"**", "//", "<=", ">=", "==", "!=", "+", "-", "*", "/",
                                            "%",  "<",  ">",  "(",  ")",  ","};
                bool found = false;
                for (const char *op : ops) {
                    const QLatin1String o(op);
                    if (QStringView(m_src).mid(i).startsWith(o)) {
                        t.kind = Token::Op;
                        t.text = o;
                        i += int(o.size());
                        found = true;
                        break;
                    }
                }
                if (!found)
                    fail(QStringLiteral("bad expression '%1'").arg(m_src));
            }
            m_tokens << t;
        }
        m_tokens << Token{};
    }

    const Token &peek() const { return m_tokens.at(m_pos); }
    bool isOp(const char *op) const { return peek().kind == Token::Op && peek().text == QLatin1String(op); }
    bool isWord(const char *w) const { return peek().kind == Token::Name && peek().text == QLatin1String(w); }
    void expectOp(const char *op)
    {
        if (!isOp(op))
            bad();
        ++m_pos;
    }

    QVariant ternary()
    {
        const QVariant body = orExpr();
        if (isWord("if")) {
            ++m_pos;
            const QVariant test = orExpr();
            if (!isWord("else"))
                bad();
            ++m_pos;
            const QVariant other = ternary();
            return truthy(test) ? body : other;
        }
        return body;
    }

    QVariant orExpr()
    {
        QVariant v = andExpr();
        if (!isWord("or"))
            return v;
        bool any = truthy(v);
        while (isWord("or")) {
            ++m_pos;
            any = truthy(andExpr()) || any;
        }
        return any;
    }

    QVariant andExpr()
    {
        QVariant v = notExpr();
        if (!isWord("and"))
            return v;
        bool all = truthy(v);
        while (isWord("and")) {
            ++m_pos;
            all = truthy(notExpr()) && all;
        }
        return all;
    }

    QVariant notExpr()
    {
        if (isWord("not")) {
            ++m_pos;
            return !truthy(notExpr());
        }
        return comparison();
    }

    QVariant comparison()
    {
        QVariant left = arith();
        bool result = true, chained = false;
        while (peek().kind == Token::Op
               && (isOp("<") || isOp("<=") || isOp(">") || isOp(">=") || isOp("==") || isOp("!="))) {
            const QString op = peek().text;
            ++m_pos;
            const QVariant right = arith();
            bool ok;
            if (op == QLatin1String("==") || op == QLatin1String("!=")) {
                ok = valuesEqual(left, right) == (op == QLatin1String("=="));
            } else if (kindOf(left) == VK::Str && kindOf(right) == VK::Str) {
                const int c = QString::compare(left.toString(), right.toString());
                ok = op == QLatin1String("<") ? c < 0 : op == QLatin1String("<=") ? c <= 0
                   : op == QLatin1String(">") ? c > 0 : c >= 0;
            } else {
                const double a = num(left), b = num(right);
                ok = op == QLatin1String("<") ? a < b : op == QLatin1String("<=") ? a <= b
                   : op == QLatin1String(">") ? a > b : a >= b;
            }
            result = result && ok;
            chained = true;
            left = right;
        }
        return chained ? QVariant(result) : left;
    }

    QVariant arith()
    {
        QVariant v = term();
        while (isOp("+") || isOp("-")) {
            const bool plus = isOp("+");
            ++m_pos;
            const QVariant r = term();
            if (plus && kindOf(v) == VK::Str && kindOf(r) == VK::Str)
                v = v.toString() + r.toString();
            else if (isIntLike(v) && isIntLike(r))
                v = plus ? inum(v) + inum(r) : inum(v) - inum(r);
            else
                v = plus ? num(v) + num(r) : num(v) - num(r);
        }
        return v;
    }

    QVariant term()
    {
        QVariant v = factor();
        while (isOp("*") || isOp("/") || isOp("//") || isOp("%")) {
            const QString op = peek().text;
            ++m_pos;
            const QVariant r = factor();
            const bool ints = isIntLike(v) && isIntLike(r);
            if (op == QLatin1String("*")) {
                v = ints ? QVariant(inum(v) * inum(r)) : QVariant(num(v) * num(r));
                continue;
            }
            if (num(r) == 0.0)
                fail(QStringLiteral("division by zero in '%1'").arg(m_src));
            if (op == QLatin1String("/")) {
                v = num(v) / num(r);
            } else if (op == QLatin1String("//")) {
                if (ints) {
                    const qlonglong a = inum(v), b = inum(r);
                    qlonglong q = a / b;
                    if ((a % b != 0) && ((a < 0) != (b < 0)))
                        --q;
                    v = q;
                } else {
                    v = std::floor(num(v) / num(r));
                }
            } else {
                if (ints) {
                    const qlonglong a = inum(v), b = inum(r);
                    qlonglong m = a % b;
                    if (m != 0 && ((m < 0) != (b < 0)))
                        m += b;
                    v = m;
                } else {
                    const double a = num(v), b = num(r);
                    double m = std::fmod(a, b);
                    if (m != 0.0 && ((m < 0) != (b < 0)))
                        m += b;
                    v = m;
                }
            }
        }
        return v;
    }

    QVariant factor()
    {
        if (isOp("-") || isOp("+")) {
            const bool minus = isOp("-");
            ++m_pos;
            const QVariant v = factor();
            if (!minus) {
                num(v);
                return v;
            }
            return isIntLike(v) ? QVariant(-inum(v)) : QVariant(-num(v));
        }
        return power();
    }

    QVariant power()
    {
        const QVariant base = atom();
        if (isOp("**")) {
            ++m_pos;
            const QVariant ex = factor();
            if (isIntLike(base) && isIntLike(ex) && inum(ex) >= 0) {
                qlonglong r = 1, b = inum(base);
                for (qlonglong e = inum(ex); e > 0; --e)
                    r *= b;
                return r;
            }
            return std::pow(num(base), num(ex));
        }
        return base;
    }

    QVariant call(const QString &name, const QVariantList &args)
    {
        const auto need = [&](int n) {
            if (args.size() != n)
                bad();
        };
        if (name == QLatin1String("min") || name == QLatin1String("max")) {
            if (args.isEmpty())
                bad();
            QVariant best = args.first();
            for (const QVariant &a : args) {
                const bool better = name == QLatin1String("min") ? num(a) < num(best) : num(a) > num(best);
                if (better)
                    best = a;
            }
            return best;
        }
        if (name == QLatin1String("abs")) {
            need(1);
            return isIntLike(args[0]) ? QVariant(qAbs(inum(args[0]))) : QVariant(std::fabs(num(args[0])));
        }
        if (name == QLatin1String("round")) {
            if (args.size() == 1) {
                if (isIntLike(args[0]))
                    return inum(args[0]);
                std::fesetround(FE_TONEAREST);
                return qlonglong(std::nearbyint(num(args[0])));
            }
            need(2);
            const double p = std::pow(10.0, double(inum(args[1])));
            std::fesetround(FE_TONEAREST);
            return std::nearbyint(num(args[0]) * p) / p;
        }
        if (name == QLatin1String("int")) {
            need(1);
            if (kindOf(args[0]) == VK::Str)
                return args[0].toString().trimmed().toLongLong();
            return qlonglong(std::trunc(num(args[0])));
        }
        if (name == QLatin1String("float")) {
            need(1);
            if (kindOf(args[0]) == VK::Str)
                return args[0].toString().trimmed().toDouble();
            return num(args[0]);
        }
        if (name == QLatin1String("floor") || name == QLatin1String("ceil")) {
            need(1);
            const double x = num(args[0]);
            return qlonglong(name == QLatin1String("floor") ? std::floor(x) : std::ceil(x));
        }
        bad();
    }

    QVariant atom()
    {
        const Token t = peek();
        if (t.kind == Token::Number || t.kind == Token::String) {
            ++m_pos;
            return t.value;
        }
        if (t.kind == Token::Name) {
            ++m_pos;
            static const QSet<QString> funcs{QStringLiteral("min"), QStringLiteral("max"), QStringLiteral("abs"),
                                             QStringLiteral("round"), QStringLiteral("int"), QStringLiteral("float"),
                                             QStringLiteral("floor"), QStringLiteral("ceil")};
            if (isOp("(")) {
                if (!funcs.contains(t.text))
                    bad();
                ++m_pos;
                QVariantList args;
                if (!isOp(")")) {
                    args << ternary();
                    while (isOp(",")) {
                        ++m_pos;
                        args << ternary();
                    }
                }
                expectOp(")");
                return call(t.text, args);
            }
            if (t.text == QLatin1String("True"))
                return true;
            if (t.text == QLatin1String("False"))
                return false;
            const auto it = m_env.constFind(t.text);
            if (it == m_env.constEnd())
                fail(QStringLiteral("unknown name '%1' in expression '%2'").arg(t.text, m_src));
            return it.value();
        }
        if (isOp("(")) {
            ++m_pos;
            const QVariant v = ternary();
            expectOp(")");
            return v;
        }
        bad();
    }

    QString m_src;
    const QVariantMap &m_env;
    QList<Token> m_tokens;
    int m_pos = 0;
};

QVariant evalOrFail(const QString &src, const QVariantMap &env)
{
    return Parser(src, env).run();
}

// Python's format(value, spec) for the mini-language subset titles use:
// [[fill]align][sign][0][width][,][.precision][type], types d f e g s %.
QString formatOrFail(const QVariant &value, const QString &spec)
{
    static const QRegularExpression re(QStringLiteral(
        R"(^(?:(.)?([<>=^]))?([+\- ])?(#)?(0)?(\d+)?([,_])?(?:\.(\d+))?([bcdeEfFgGnosxX%])?$)"));
    const QRegularExpressionMatch m = re.match(spec);
    if (!m.hasMatch())
        fail(QStringLiteral("bad format spec '%1'").arg(spec));
    QChar fill = m.captured(1).isEmpty() ? QLatin1Char(' ') : m.captured(1).at(0);
    QString align = m.captured(2);
    const QString sign = m.captured(3);
    const bool zero = !m.captured(5).isEmpty();
    const int width = m.captured(6).isEmpty() ? 0 : m.captured(6).toInt();
    const QString group = m.captured(7);
    const int precision = m.captured(8).isEmpty() ? -1 : m.captured(8).toInt();
    const QString type = m.captured(9);

    QString body;
    bool numeric = false, negative = false;
    const VK k = kindOf(value);
    if (k == VK::Str || k == VK::List || k == VK::None) {
        if (!type.isEmpty() && type != QLatin1String("s"))
            fail(QStringLiteral("format '%1' needs a number").arg(spec));
        body = pyStr(value);
        if (precision >= 0)
            body.truncate(precision);
    } else {
        numeric = true;
        const bool intVal = isIntLike(value) && k != VK::Bool;
        if (k == VK::Bool && type.isEmpty() && precision < 0) {
            body = pyStr(value);
            numeric = false;
        } else if (intVal && (type.isEmpty() || type == QLatin1String("d") || type == QLatin1String("n"))) {
            const qlonglong v = inum(value);
            negative = v < 0;
            body = QString::number(qAbs(v));
        } else if (type == QLatin1String("x") || type == QLatin1String("X") || type == QLatin1String("o")
                   || type == QLatin1String("b")) {
            const qlonglong v = inum(value);
            negative = v < 0;
            body = QString::number(qAbs(v), type == QLatin1String("o") ? 8 : type == QLatin1String("b") ? 2 : 16);
            if (type == QLatin1String("X"))
                body = body.toUpper();
        } else {
            double v = num(value);
            negative = std::signbit(v);
            v = std::fabs(v);
            if (type == QLatin1String("%")) {
                body = QString::number(v * 100.0, 'f', precision < 0 ? 6 : precision) + QLatin1Char('%');
            } else if (type == QLatin1String("f") || type == QLatin1String("F")) {
                body = QString::number(v, 'f', precision < 0 ? 6 : precision);
            } else if (type == QLatin1String("e") || type == QLatin1String("E")) {
                body = QString::number(v, 'e', precision < 0 ? 6 : precision);
                if (type == QLatin1String("E"))
                    body = body.toUpper();
            } else if (type == QLatin1String("g") || type == QLatin1String("G")) {
                body = QString::number(v, 'g', precision < 0 ? 6 : qMax(1, precision));
            } else if (type.isEmpty()) {
                body = precision < 0 ? (intVal ? QString::number(qlonglong(v)) : pyFloatStr(v))
                                     : QString::number(v, 'g', qMax(1, precision));
            } else {
                fail(QStringLiteral("format '%1' is not supported").arg(spec));
            }
        }
        if (numeric && !group.isEmpty()) {
            const int dot = body.indexOf(QLatin1Char('.'));
            int end = dot < 0 ? int(body.size()) : dot;
            for (int i = end - 3; i > 0; i -= 3)
                body.insert(i, group);
        }
    }
    QString signText;
    if (numeric) {
        if (negative)
            signText = QStringLiteral("-");
        else if (sign == QLatin1String("+"))
            signText = QStringLiteral("+");
        else if (sign == QLatin1String(" "))
            signText = QStringLiteral(" ");
    }
    if (zero && align.isEmpty() && numeric) {
        fill = QLatin1Char('0');
        align = QStringLiteral("=");
    }
    if (align.isEmpty())
        align = numeric ? QStringLiteral(">") : QStringLiteral("<");
    const int pad = qMax(0, width - int(signText.size() + body.size()));
    const QString padding(pad, fill);
    if (align == QLatin1String("="))
        return signText + padding + body;
    const QString all = signText + body;
    if (align == QLatin1String("<"))
        return all + padding;
    if (align == QLatin1String("^"))
        return QString(pad / 2, fill) + all + QString(pad - pad / 2, fill);
    return padding + all;
}

QString formatTextOrFail(const QString &tmpl, const QVariantMap &env)
{
    static const QRegularExpression slot(QStringLiteral(R"(\{([A-Za-z_][A-Za-z0-9_]*)(?::([^{}]*))?\})"));
    QString out;
    int i = 0;
    while (i < tmpl.size()) {
        if (QStringView(tmpl).mid(i).startsWith(QLatin1String("{{"))) {
            out += QLatin1Char('{');
            i += 2;
            continue;
        }
        if (QStringView(tmpl).mid(i).startsWith(QLatin1String("}}"))) {
            out += QLatin1Char('}');
            i += 2;
            continue;
        }
        const QRegularExpressionMatch m = slot.match(tmpl, i, QRegularExpression::NormalMatch,
                                                     QRegularExpression::AnchorAtOffsetMatchOption);
        if (m.hasMatch()) {
            const QString name = m.captured(1);
            const auto it = env.constFind(name);
            if (it == env.constEnd())
                fail(QStringLiteral("unknown name '%1' in text '%2'").arg(name, tmpl));
            out += formatOrFail(it.value(), m.captured(2));
            i = int(m.capturedEnd());
            continue;
        }
        out += tmpl.at(i++);
    }
    return out;
}

// ---- loading ---------------------------------------------------------------------------------

QJsonValue subst(const QJsonValue &node, const QVariantMap &env)
{
    if (node.isObject()) {
        const QJsonObject o = node.toObject();
        if (o.size() == 1 && o.contains(QStringLiteral("expr")))
            return valueToJson(evalOrFail(o.value(QStringLiteral("expr")).toString(), env));
        QJsonObject r;
        for (auto it = o.begin(); it != o.end(); ++it) {
            if (it.key() == QLatin1String("actor") || it.key() == QLatin1String("children"))
                r.insert(it.key(), it.value());
            else
                r.insert(it.key(), subst(it.value(), env));
        }
        return r;
    }
    if (node.isArray()) {
        QJsonArray r;
        for (const QJsonValue &v : node.toArray())
            r.append(subst(v, env));
        return r;
    }
    return node;
}

double numberOf(const QJsonValue &v, const QString &what)
{
    if (!v.isDouble())
        fail(QStringLiteral("%1 must be a number").arg(what));
    return v.toDouble();
}

QPointF pointOf(const QJsonValue &v, const QString &what)
{
    const QJsonArray a = v.toArray();
    if (a.size() != 2)
        fail(QStringLiteral("%1 must be [x, y]").arg(what));
    return {numberOf(a.at(0), what), numberOf(a.at(1), what)};
}

Rgba colorOf(const QJsonValue &v, const QVariantMap &palette, const QString &what)
{
    const std::optional<Rgba> c = parseColor(jsonToValue(v), palette);
    if (!c)
        fail(QStringLiteral("%1: bad colour").arg(what));
    return *c;
}

Ease easeOf(const QJsonValue &v, const QString &what)
{
    Ease e;
    if (v.isUndefined() || v.isNull())
        return e;
    const QString s = v.toString();
    if (s == QLatin1String("linear"))
        e.kind = Ease::Linear;
    else if (s == QLatin1String("smoothstep"))
        e.kind = Ease::Smoothstep;
    else if (s == QLatin1String("easeOutCubic"))
        e.kind = Ease::OutCubic;
    else if (s == QLatin1String("easeOutQuart"))
        e.kind = Ease::OutQuart;
    else if (s == QLatin1String("smootherstep"))
        e.kind = Ease::Smootherstep;
    else {
        static const QRegularExpression re(QStringLiteral(R"(^cubic-bezier\(([^)]*)\)$)"));
        const QRegularExpressionMatch m = re.match(QString(s).remove(QLatin1Char(' ')));
        const QStringList parts = m.hasMatch() ? m.captured(1).split(QLatin1Char(',')) : QStringList();
        if (parts.size() != 4)
            fail(QStringLiteral("%1: unknown ease '%2'").arg(what, s));
        e.kind = Ease::Bezier;
        e.x1 = parts[0].toDouble();
        e.y1 = parts[1].toDouble();
        e.x2 = parts[2].toDouble();
        e.y2 = parts[3].toDouble();
    }
    return e;
}

const QSet<QString> &channelNames()
{
    static const QSet<QString> names{QStringLiteral("opacity"), QStringLiteral("wipe"), QStringLiteral("progress"),
                                     QStringLiteral("scale"), QStringLiteral("dx"), QStringLiteral("dy")};
    return names;
}

TimedBlock blockOf(const QJsonValue &v, bool isIn, const QString &actorId)
{
    TimedBlock b;
    if (!v.isObject())
        return b;
    const QJsonObject o = v.toObject();
    b.present = true;
    b.start = numberOf(o.value(QStringLiteral("start")), actorId + QStringLiteral(": start"));
    b.dur = o.contains(QStringLiteral("dur")) ? numberOf(o.value(QStringLiteral("dur")), actorId + QStringLiteral(": dur")) : 0.0;
    b.ease = easeOf(o.value(QStringLiteral("ease")), actorId);
    const QString key = isIn ? QStringLiteral("from") : QStringLiteral("to");
    const QJsonObject vals = o.contains(key) ? o.value(key).toObject()
                                             : QJsonObject{{QStringLiteral("opacity"), 0}};
    for (auto it = vals.begin(); it != vals.end(); ++it) {
        if (!channelNames().contains(it.key()))
            fail(QStringLiteral("%1: unknown channel '%2'").arg(actorId, it.key()));
        b.values.append({it.key(), numberOf(it.value(), actorId + QLatin1Char(' ') + it.key())});
    }
    return b;
}

void checkParam(const ParamSpec &p, const QVariant &v)
{
    const VK k = kindOf(v);
    if (p.type == QLatin1String("int")) {
        if (k != VK::Int)
            fail(QStringLiteral("param %1 must be an integer").arg(p.id));
    } else if (p.type == QLatin1String("number")) {
        if (k != VK::Int && k != VK::Float)
            fail(QStringLiteral("param %1 must be a number").arg(p.id));
    } else if (p.type == QLatin1String("bool")) {
        if (k != VK::Bool)
            fail(QStringLiteral("param %1 must be true/false").arg(p.id));
    } else if (p.type == QLatin1String("text") || p.type == QLatin1String("color") || p.type == QLatin1String("enum")) {
        if (k != VK::Str && k != VK::List)
            fail(QStringLiteral("param %1 has a bad value").arg(p.id));
        if (p.type == QLatin1String("enum") && !p.options.contains(v.toString()))
            fail(QStringLiteral("param %1 must be one of %2").arg(p.id, p.options.join(QStringLiteral(", "))));
    } else {
        fail(QStringLiteral("param %1: unknown type %2").arg(p.id, p.type));
    }
}

struct Loader
{
    Scene &sc;
    QVariantMap palette;

    QVariantMap baseEnv() const
    {
        QVariantMap env = palette;
        for (auto it = sc.values.begin(); it != sc.values.end(); ++it)
            env.insert(it.key(), it.value());
        return env;
    }

    void parseActor(const QJsonObject &r, const QString &parent, const QVariantMap &env)
    {
        Actor a;
        a.id = r.value(QStringLiteral("id")).toString();
        a.type = r.value(QStringLiteral("type")).toString();
        a.role = r.value(QStringLiteral("role")).toString();
        a.parent = parent;
        const QString snapMode = r.value(QStringLiteral("offsetSnap")).toString();
        if (snapMode == QLatin1String("trunc"))
            a.truncOffsets = true;
        else if (!snapMode.isEmpty() && snapMode != QLatin1String("round"))
            fail(QStringLiteral("%1: offsetSnap must be \"round\" or \"trunc\"").arg(a.id));
        a.in = blockOf(r.value(QStringLiteral("in")), true, a.id);
        a.out = blockOf(r.value(QStringLiteral("out")), false, a.id);
        for (const QJsonValue &d : r.value(QStringLiteral("dependsOn")).toArray())
            a.dependsOn << d.toString();
        const QString &t = a.type;
        const QJsonObject eff = r.value(QStringLiteral("effect")).toObject();
        const QString effType = eff.value(QStringLiteral("type")).toString();
        if (effType == QLatin1String("sdfMorph") || effType == QLatin1String("maskedLight")
            || effType == QLatin1String("focusMix"))
            fail(QStringLiteral("%1: effect %2 is reserved for a later version (docs/scene-titles.md, Stage10); "
                                "ask for the extension")
                     .arg(a.id, effType));

        if (t == QLatin1String("text")) {
            a.text = formatTextOrFail(r.value(QStringLiteral("text")).toString(), env);
            a.font = r.value(QStringLiteral("font")).toString();
            if (!sc.fonts.contains(a.font))
                fail(QStringLiteral("%1: unknown font key '%2'").arg(a.id, a.font));
            a.size = numberOf(r.value(QStringLiteral("size")), a.id + QStringLiteral(": size"));
            a.color = colorOf(r.value(QStringLiteral("color")), palette, a.id);
            a.letterSpacing = r.value(QStringLiteral("letterSpacing")).toDouble();
            const QJsonObject at = r.value(QStringLiteral("at")).toObject();
            if (at.contains(QStringLiteral("baseline"))) {
                a.baseline = pointOf(at.value(QStringLiteral("baseline")), a.id + QStringLiteral(": at"));
            } else if (at.contains(QStringLiteral("sprite")) || at.contains(QStringLiteral("lt"))) {
                a.anchorKind = at.contains(QStringLiteral("sprite")) ? QStringLiteral("sprite") : QStringLiteral("lt");
                a.anchorPoint = pointOf(at.value(a.anchorKind), a.id + QStringLiteral(": at"));
            } else if (parent.isEmpty() || !r.value(QStringLiteral("align")).toObject().contains(QStringLiteral("h"))) {
                fail(QStringLiteral("%1: text needs at.baseline").arg(a.id));
            }
            const QJsonObject clip = r.value(QStringLiteral("clipTo")).toObject();
            if (!clip.isEmpty()) {
                a.clipActor = clip.value(QStringLiteral("actor")).toString();
                const QJsonArray inset = clip.value(QStringLiteral("inset")).toArray();
                for (int i = 0; i < 4 && i < inset.size(); ++i)
                    a.clipInset[i] = inset.at(i).toDouble();
            }
            const QJsonObject cr = r.value(QStringLiteral("clipRect")).toObject();
            if (!cr.isEmpty()) {
                const QJsonArray box = cr.value(QStringLiteral("box")).toArray();
                if (box.size() != 4)
                    fail(QStringLiteral("%1: clipRect.box needs 4 numbers").arg(a.id));
                a.hasClipRect = true;
                for (int i = 0; i < 4; ++i)
                    a.clipRect[i] = box.at(i).toDouble();
                a.clipRise = cr.value(QStringLiteral("rise")).toDouble();
                const QString snap = cr.value(QStringLiteral("riseSnap")).toString();
                if (snap == QLatin1String("round"))
                    a.clipRiseRound = true;
                else if (!snap.isEmpty() && snap != QLatin1String("trunc"))
                    fail(QStringLiteral("%1: clipRect.riseSnap must be \"trunc\" or \"round\"").arg(a.id));
            }
            const QJsonObject al = r.value(QStringLiteral("align")).toObject();
            if (al.value(QStringLiteral("h")).toString() == QLatin1String("center")
                && al.value(QStringLiteral("v")).toString() == QLatin1String("center")) {
                a.centerInGroup = true;
                a.centerDy = al.value(QStringLiteral("dy")).toDouble();
            } else if (al.value(QStringLiteral("h")).toString() == QLatin1String("center")
                       && al.value(QStringLiteral("v")).toString() == QLatin1String("top")) {
                a.centerInGroup = true;
                a.alignTop = true;
                a.centerTop = al.value(QStringLiteral("top")).toDouble();
            }
        } else if (t == QLatin1String("rect") || t == QLatin1String("roundRect") || t == QLatin1String("ellipse")
                   || t == QLatin1String("ring")) {
            const QJsonArray box = r.value(QStringLiteral("box")).toArray();
            if (box.size() != 4)
                fail(QStringLiteral("%1: box must be [x0, y0, x1, y1]").arg(a.id));
            a.hasBox = true;
            for (int i = 0; i < 4; ++i)
                a.box[i] = numberOf(box.at(i), a.id + QStringLiteral(": box"));
            a.radius = r.value(QStringLiteral("radius")).toDouble();
            if (t == QLatin1String("rect") || t == QLatin1String("ring")) {
                a.hasFill = t == QLatin1String("rect");
                a.color = colorOf(r.value(QStringLiteral("color")), palette, a.id);
                a.fill = a.color;
                if (t == QLatin1String("ring")) {
                    a.hasStroke = true;
                    a.strokeColor = a.color;
                    a.stroke = r.contains(QStringLiteral("stroke")) ? r.value(QStringLiteral("stroke")).toDouble() : 2.0;
                }
            } else {
                if (r.contains(QStringLiteral("fill")) && !r.value(QStringLiteral("fill")).isNull()) {
                    a.hasFill = true;
                    a.fill = colorOf(r.value(QStringLiteral("fill")), palette, a.id);
                }
                if (r.contains(QStringLiteral("strokeColor")) && !r.value(QStringLiteral("strokeColor")).isNull()) {
                    a.hasStroke = true;
                    a.strokeColor = colorOf(r.value(QStringLiteral("strokeColor")), palette, a.id);
                    a.stroke = r.value(QStringLiteral("stroke")).toDouble();
                }
            }
            const QJsonObject fit = r.value(QStringLiteral("fitTo")).toObject();
            if (!fit.isEmpty()) {
                a.fitActor = fit.value(QStringLiteral("actor")).toString();
                a.fitPad = fit.value(QStringLiteral("pad")).toDouble();
                a.fitMin = fit.value(QStringLiteral("min")).toDouble();
                if (fit.contains(QStringLiteral("max")))
                    a.fitMax = fit.value(QStringLiteral("max")).toDouble();
            }
            const QString wipeDir = r.value(QStringLiteral("wipeDir")).toString();
            if (wipeDir == QLatin1String("down"))
                a.wipeDown = true;
            else if (!wipeDir.isEmpty() && wipeDir != QLatin1String("right"))
                fail(QStringLiteral("%1: wipeDir must be \"right\" or \"down\"").arg(a.id));
            for (const QJsonValue &w : r.value(QStringLiteral("widthOf")).toArray())
                a.widthOf << w.toString();
        } else if (t == QLatin1String("line")) {
            a.from = pointOf(r.value(QStringLiteral("from")), a.id + QStringLiteral(": from"));
            a.to = pointOf(r.value(QStringLiteral("to")), a.id + QStringLiteral(": to"));
            a.width = r.contains(QStringLiteral("width")) ? r.value(QStringLiteral("width")).toDouble() : 1.0;
            a.color = colorOf(r.value(QStringLiteral("color")), palette, a.id);
            a.mixWith = r.value(QStringLiteral("opacityMode")).toString() == QLatin1String("mixWith");
        } else if (t == QLatin1String("route")) {
            for (const QJsonValue &p : r.value(QStringLiteral("points")).toArray())
                a.points << pointOf(p, a.id + QStringLiteral(": points"));
            if (a.points.size() < 2)
                fail(QStringLiteral("%1: a route needs at least two points").arg(a.id));
            a.width = r.contains(QStringLiteral("width")) ? r.value(QStringLiteral("width")).toDouble() : 1.0;
            a.color = colorOf(r.value(QStringLiteral("color")), palette, a.id);
        } else if (t == QLatin1String("group")) {
            const QJsonArray at = r.value(QStringLiteral("at")).toArray();
            if (at.size() == 2)
                a.groupAt = {at.at(0).toDouble(), at.at(1).toDouble()};
            const QJsonArray size = r.value(QStringLiteral("size")).toArray();
            if (size.size() == 2) {
                a.groupW = size.at(0).toDouble();
                a.groupH = size.at(1).toDouble();
            }
            a.flatten = r.value(QStringLiteral("flatten")).toBool();
            if (a.flatten && (a.groupW <= 0 || a.groupH <= 0))
                fail(QStringLiteral("%1: a flattened group needs size [w, h]").arg(a.id));
        }
        // Static alignment is resolved after sync, on the parsed actors.
        const QJsonObject align = r.value(QStringLiteral("align")).toObject();
        if (align.contains(QStringLiteral("x")) || align.contains(QStringLiteral("y")))
            m_align.insert(a.id, align);
        if (r.contains(QStringLiteral("sync")))
            m_sync.insert(a.id, r.value(QStringLiteral("sync")).toString());
        sc.actors.append(a);
    }

    void expand(const QJsonArray &actors, const QVariantMap &local, const QString &parent)
    {
        QVariantMap env = baseEnv();
        for (auto it = local.begin(); it != local.end(); ++it)
            env.insert(it.key(), it.value());
        static const QSet<QString> types{QStringLiteral("text"),    QStringLiteral("rect"),  QStringLiteral("roundRect"),
                                         QStringLiteral("line"),    QStringLiteral("route"), QStringLiteral("ellipse"),
                                         QStringLiteral("ring"),    QStringLiteral("group"), QStringLiteral("repeat")};
        for (const QJsonValue &value : actors) {
            const QJsonObject a = value.toObject();
            const QString type = a.value(QStringLiteral("type")).toString();
            if (!types.contains(type))
                fail(QStringLiteral("actor '%1': unknown type '%2'").arg(a.value(QStringLiteral("id")).toString(), type));
            if (!a.contains(QStringLiteral("id")))
                fail(QStringLiteral("every actor needs an id"));
            const QString id = a.value(QStringLiteral("id")).toString();
            const QJsonValue en = a.value(QStringLiteral("enabled"));
            bool enabled = true;
            if (en.isBool())
                enabled = en.toBool();
            else if (en.isString())
                enabled = truthy(evalOrFail(en.toString(), env));
            else if (en.isObject())
                enabled = truthy(evalOrFail(en.toObject().value(QStringLiteral("expr")).toString(), env));
            if (!enabled) {
                if (!a.value(QStringLiteral("optional")).toBool())
                    fail(QStringLiteral("actor %1 is disabled but not optional").arg(id));
                continue;
            }
            if (type == QLatin1String("repeat")) {
                const QJsonValue c = a.value(QStringLiteral("count"));
                const QVariant n = c.isObject() ? evalOrFail(c.toObject().value(QStringLiteral("expr")).toString(), env)
                                 : c.isString() ? evalOrFail(c.toString(), env)
                                                : jsonToValue(c);
                const QString var = a.value(QStringLiteral("as")).toString(QStringLiteral("i"));
                const qlonglong count = qlonglong(num(n));
                for (qlonglong i = 0; i < count; ++i) {
                    QJsonObject sub = a.value(QStringLiteral("actor")).toObject();
                    QVariantMap inner = local;
                    inner.insert(var, i);
                    QVariantMap localEnv = env;
                    localEnv.insert(var, i);
                    sub.insert(QStringLiteral("id"), formatTextOrFail(sub.value(QStringLiteral("id")).toString(), localEnv));
                    expand(QJsonArray{sub}, inner, parent);
                }
                continue;
            }
            const QJsonObject r = subst(a, env).toObject();
            parseActor(r, parent, env);
            if (type == QLatin1String("group"))
                expand(a.value(QStringLiteral("children")).toArray(), local, id);
        }
    }

    // scene_core.resolve_align, on parsed actors.
    std::optional<double> edge(const Actor &a, const QString &e) const
    {
        if (a.type == QLatin1String("text")) {
            if (e == QLatin1String("start") || e == QLatin1String("left"))
                return a.baseline.x();
            if (e == QLatin1String("baseline") || e == QLatin1String("top") || e == QLatin1String("bottom"))
                return a.baseline.y();
            return std::nullopt;
        }
        if (a.hasBox) {
            if (e == QLatin1String("left") || e == QLatin1String("start"))
                return a.box[0];
            if (e == QLatin1String("right"))
                return a.box[2];
            if (e == QLatin1String("top"))
                return a.box[1];
            if (e == QLatin1String("baseline") || e == QLatin1String("bottom"))
                return a.box[3];
            return std::nullopt;
        }
        if (a.type == QLatin1String("line")) {
            if (e == QLatin1String("left"))
                return qMin(a.from.x(), a.to.x());
            if (e == QLatin1String("start"))
                return a.from.x();
            if (e == QLatin1String("right"))
                return qMax(a.from.x(), a.to.x());
            if (e == QLatin1String("top"))
                return qMin(a.from.y(), a.to.y());
            if (e == QLatin1String("baseline") || e == QLatin1String("bottom"))
                return qMax(a.from.y(), a.to.y());
        }
        return std::nullopt;
    }

    void move(Actor &a, double dx, double dy)
    {
        if (a.type == QLatin1String("text")) {
            a.baseline += QPointF(dx, dy);
        } else if (a.hasBox) {
            a.box[0] += dx;
            a.box[2] += dx;
            a.box[1] += dy;
            a.box[3] += dy;
        } else if (a.type == QLatin1String("line")) {
            a.from += QPointF(dx, dy);
            a.to += QPointF(dx, dy);
        } else {
            fail(QStringLiteral("%1: align is not supported for %2").arg(a.id, a.type));
        }
    }

    void visitAlign(int index, QSet<QString> &done, QSet<QString> &busy)
    {
        Actor &a = sc.actors[index];
        if (done.contains(a.id) || !m_align.contains(a.id)) {
            done.insert(a.id);
            return;
        }
        if (busy.contains(a.id))
            fail(QStringLiteral("%1: align cycle").arg(a.id));
        busy.insert(a.id);
        const QJsonObject rules = m_align.value(a.id);
        for (const QString axis : {QStringLiteral("x"), QStringLiteral("y")}) {
            const QJsonObject rule = rules.value(axis).toObject();
            if (rule.isEmpty())
                continue;
            const QString to = rule.value(QStringLiteral("to")).toString();
            const int ti = sc.indexOf(to);
            if (ti < 0)
                fail(QStringLiteral("%1: align to unknown actor '%2'").arg(a.id, to));
            const QString e = rule.value(QStringLiteral("edge")).toString();
            static const QSet<QString> xEdges{QStringLiteral("start"), QStringLiteral("left"), QStringLiteral("right")};
            static const QSet<QString> yEdges{QStringLiteral("baseline"), QStringLiteral("top"), QStringLiteral("bottom")};
            if (!(axis == QLatin1String("x") ? xEdges : yEdges).contains(e))
                fail(QStringLiteral("%1: '%2' is not an %3 edge").arg(a.id, e, axis));
            visitAlign(ti, done, busy);
            Actor &self = sc.actors[index];
            if (self.type == QLatin1String("text") && self.anchorKind != QLatin1String("baseline"))
                fail(QStringLiteral("%1: align needs at.baseline").arg(self.id));
            const std::optional<double> want = edge(sc.actors.at(ti), e);
            const QString own = rule.value(QStringLiteral("ownEdge")).toString(e);
            const std::optional<double> have = edge(self, own);
            if (!want || !have)
                fail(QStringLiteral("%1: cannot align '%2' to '%3'").arg(self.id, own, e));
            const double d = *want + rule.value(QStringLiteral("offset")).toDouble() - *have;
            move(self, axis == QLatin1String("x") ? d : 0.0, axis == QLatin1String("y") ? d : 0.0);
        }
        busy.remove(a.id);
        done.insert(a.id);
    }

    QHash<QString, QJsonObject> m_align;
    QHash<QString, QString> m_sync;
};

double phase(double t, const TimedBlock &b)
{
    if (b.dur <= 0.0)
        return t >= b.start ? 1.0 : 0.0;
    return b.ease((t - b.start) / b.dur);
}

} // namespace

double Ease::operator()(double u) const
{
    u = clamp01(u);
    switch (kind) {
    case Linear:
        return u;
    case Smoothstep:
        return u * u * (3.0 - 2.0 * u);
    case OutCubic: {
        const double v = 1.0 - u;
        return 1.0 - v * v * v;
    }
    case OutQuart: {
        const double v = 1.0 - u;
        return 1.0 - v * v * v * v;
    }
    case Smootherstep: // same operation order as the Stage03b C revision's ease5
        return u * u * u * (u * (6.0 * u - 15.0) + 10.0);
    case Bezier:
        if (u == 0.0 || u == 1.0)
            return u;
        return cubicBezier(0.0, y1, y2, 1.0, bezierParameterForX(x1, x2, u));
    }
    return u;
}

const Actor *Scene::actor(const QString &id) const
{
    const int i = indexOf(id);
    return i < 0 ? nullptr : &actors.at(i);
}

int Scene::indexOf(const QString &id) const
{
    for (int i = 0; i < actors.size(); ++i)
        if (actors.at(i).id == id)
            return i;
    return -1;
}

QVariant jsonToValue(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::Bool:
        return v.toBool();
    case QJsonValue::Double: {
        const QVariant x = v.toVariant();
        if (x.metaType().id() == QMetaType::LongLong || x.metaType().id() == QMetaType::Int)
            return x.toLongLong();
        return v.toDouble();
    }
    case QJsonValue::String:
        return v.toString();
    case QJsonValue::Array: {
        QVariantList l;
        for (const QJsonValue &x : v.toArray())
            l << jsonToValue(x);
        return l;
    }
    default:
        return {};
    }
}

std::optional<Rgba> parseColor(const QVariant &value, const QVariantMap &palette)
{
    if (kindOf(value) == VK::Str) {
        const QString s = value.toString();
        const auto it = palette.constFind(s);
        if (it != palette.constEnd())
            return parseColor(it.value(), palette);
        QString hex = s;
        while (hex.startsWith(QLatin1Char('#')))
            hex.remove(0, 1);
        static const QRegularExpression re(QStringLiteral("^[0-9a-fA-F]+$"));
        if ((hex.size() == 6 || hex.size() == 8) && re.match(hex).hasMatch()) {
            Rgba c;
            c.r = hex.mid(0, 2).toInt(nullptr, 16);
            c.g = hex.mid(2, 2).toInt(nullptr, 16);
            c.b = hex.mid(4, 2).toInt(nullptr, 16);
            c.a = hex.size() == 8 ? hex.mid(6, 2).toInt(nullptr, 16) : 255;
            return c;
        }
        return std::nullopt;
    }
    if (kindOf(value) == VK::List) {
        const QVariantList l = value.toList();
        if (l.size() != 3 && l.size() != 4)
            return std::nullopt;
        Rgba c;
        c.r = int(l.at(0).toDouble());
        c.g = int(l.at(1).toDouble());
        c.b = int(l.at(2).toDouble());
        c.a = l.size() == 4 ? int(l.at(3).toDouble()) : 255;
        return c;
    }
    return std::nullopt;
}

QVariant evalExpression(const QString &src, const QVariantMap &env, QString *error)
{
    try {
        return evalOrFail(src, env);
    } catch (const Fail &f) {
        if (error)
            *error = f.message;
        return {};
    }
}

QString formatText(const QString &tmpl, const QVariantMap &env, QString *error)
{
    try {
        return formatTextOrFail(tmpl, env);
    } catch (const Fail &f) {
        if (error)
            *error = f.message;
        return {};
    }
}

QString formatValue(const QVariant &value, const QString &spec, QString *error)
{
    try {
        return formatOrFail(value, spec);
    } catch (const Fail &f) {
        if (error)
            *error = f.message;
        return {};
    }
}

bool isSceneDocument(const QJsonObject &doc)
{
    return doc.value(QStringLiteral("format")).toString() == QLatin1String(kFormat);
}

std::optional<Scene> loadScene(const QJsonObject &doc, const QJsonObject &overrides, QString *error)
{
    Scene sc;
    try {
        if (!isSceneDocument(doc))
            fail(QStringLiteral("not a scene title (format must be '%1')").arg(QLatin1String(kFormat)));
        const QVariant version = jsonToValue(doc.value(QStringLiteral("version")));
        if (kindOf(version) != VK::Int || version.toLongLong() != kVersion)
            fail(QStringLiteral("unsupported version %1").arg(pyStr(version)));
        for (const char *key : {"id", "canvas", "timing", "actors"})
            if (!doc.contains(QLatin1String(key)))
                fail(QStringLiteral("missing '%1'").arg(QLatin1String(key)));
        sc.raw = doc;
        sc.id = doc.value(QStringLiteral("id")).toString();
        sc.label = doc.value(QStringLiteral("label")).toString(sc.id);
        sc.category = doc.value(QStringLiteral("category")).toString();
        const QJsonObject canvas = doc.value(QStringLiteral("canvas")).toObject();
        sc.width = canvas.value(QStringLiteral("width")).toInt(1920);
        sc.height = canvas.value(QStringLiteral("height")).toInt(1080);
        if (sc.width <= 0 || sc.height <= 0)
            fail(QStringLiteral("canvas must have a positive size"));
        const QJsonObject timing = doc.value(QStringLiteral("timing")).toObject();
        sc.duration = timing.value(QStringLiteral("duration")).toDouble(6.0);
        sc.fps = timing.value(QStringLiteral("fps")).toDouble(25.0);
        if (sc.duration <= 0.0 || sc.fps <= 0.0)
            fail(QStringLiteral("timing.duration and timing.fps must be positive"));
        const QJsonValue cadence = timing.value(QStringLiteral("cadence"));
        if (cadence.isDouble()) {
            sc.cadence = cadence.toDouble();
            if (sc.cadence != 25.0 && sc.cadence != 30.0 && sc.cadence != 60.0)
                fail(QStringLiteral("timing.cadence must be \"match\", 25, 30 or 60"));
        } else if (!cadence.isUndefined() && cadence.toString() != QLatin1String("match")) {
            fail(QStringLiteral("timing.cadence must be \"match\", 25, 30 or 60"));
        }
        const QJsonObject render = doc.value(QStringLiteral("render")).toObject();
        const QString profile = render.value(QStringLiteral("profile")).toString(QStringLiteral("sprite2x"));
        if (profile == QLatin1String("sprite2x"))
            sc.profile = Profile::Sprite2x;
        else if (profile == QLatin1String("mask1x"))
            sc.profile = Profile::Mask1x;
        else
            fail(QStringLiteral("unknown render profile '%1'").arg(profile));

        const QJsonObject fonts = doc.value(QStringLiteral("fonts")).toObject();
        for (auto it = fonts.begin(); it != fonts.end(); ++it) {
            const QJsonObject f = it.value().toObject();
            FontSpec spec;
            spec.key = it.key();
            spec.family = f.value(QStringLiteral("family")).toString();
            spec.file = f.value(QStringLiteral("file")).toString();
            spec.package = f.value(QStringLiteral("package")).toString();
            spec.sha256 = f.value(QStringLiteral("sha256")).toString().toLower();
            spec.weight = f.value(QStringLiteral("weight")).toInt(400);
            if (spec.file.isEmpty())
                fail(QStringLiteral("font '%1' names no file").arg(spec.key));
            sc.fonts.insert(spec.key, spec);
        }

        // Params: declared order, overrides checked, then bounds (which may name any param).
        QSet<QString> known;
        for (const QJsonValue &pv : doc.value(QStringLiteral("params")).toArray()) {
            const QJsonObject p = pv.toObject();
            ParamSpec spec;
            spec.id = p.value(QStringLiteral("id")).toString();
            spec.type = p.value(QStringLiteral("type")).toString(QStringLiteral("text"));
            spec.label = p.value(QStringLiteral("label")).toString(spec.id);
            spec.defaultValue = jsonToValue(p.value(QStringLiteral("default")));
            if (p.contains(QStringLiteral("min")))
                spec.min = jsonToValue(p.value(QStringLiteral("min")));
            if (p.contains(QStringLiteral("max")))
                spec.max = jsonToValue(p.value(QStringLiteral("max")));
            spec.clamp = p.value(QStringLiteral("clamp")).toBool();
            for (const QJsonValue &o : p.value(QStringLiteral("options")).toArray())
                spec.options << o.toString();
            if (spec.id.isEmpty())
                fail(QStringLiteral("every param needs an id"));
            known.insert(spec.id);
            sc.params << spec;
        }
        for (auto it = overrides.begin(); it != overrides.end(); ++it)
            if (!known.contains(it.key()))
                fail(QStringLiteral("unknown param '%1'").arg(it.key()));
        for (const ParamSpec &p : sc.params) {
            const QVariant v = overrides.contains(p.id) ? jsonToValue(overrides.value(p.id)) : p.defaultValue;
            checkParam(p, v);
            sc.values.insert(p.id, v);
        }
        for (const ParamSpec &p : sc.params) {
            for (int which = 0; which < 2; ++which) {
                const QVariant bound = which == 0 ? p.min : p.max;
                if (!bound.isValid())
                    continue;
                const QVariant b = kindOf(bound) == VK::Str ? evalOrFail(bound.toString(), sc.values) : bound;
                const QVariant v = sc.values.value(p.id);
                const bool out = which == 0 ? num(v) < num(b) : num(v) > num(b);
                if (!out)
                    continue;
                if (!p.clamp)
                    fail(QStringLiteral("param %1=%2 is outside %3 %4")
                             .arg(p.id, pyStr(v), which == 0 ? QStringLiteral("min") : QStringLiteral("max"), pyStr(b)));
                sc.values.insert(p.id, b);
            }
        }

        Loader loader{sc, {}};
        const QJsonObject palette = doc.value(QStringLiteral("palette")).toObject();
        for (auto it = palette.begin(); it != palette.end(); ++it)
            loader.palette.insert(it.key(), jsonToValue(it.value()));
        const QVariant mix = render.contains(QStringLiteral("mixColor"))
                                 ? jsonToValue(render.value(QStringLiteral("mixColor")))
                                 : QVariant(QVariantList{9, 12, 17});
        const std::optional<Rgba> mixColor = parseColor(mix, loader.palette);
        if (!mixColor)
            fail(QStringLiteral("render.mixColor: bad colour"));
        sc.mixColor = *mixColor;

        loader.expand(doc.value(QStringLiteral("actors")).toArray(), {}, {});

        // sync: take in/out from another actor (resolved actors, as the reference does).
        for (int self = 0; self < sc.actors.size(); ++self) {
            const auto it = loader.m_sync.constFind(sc.actors.at(self).id);
            if (it == loader.m_sync.constEnd())
                continue;
            const int other = sc.indexOf(it.value());
            if (other < 0)
                fail(QStringLiteral("%1: sync with unknown actor '%2'").arg(it.key(), it.value()));
            if (sc.actors.at(other).in.present)
                sc.actors[self].in = sc.actors.at(other).in;
            if (sc.actors.at(other).out.present)
                sc.actors[self].out = sc.actors.at(other).out;
        }
        QSet<QString> done, busy;
        for (int i = 0; i < sc.actors.size(); ++i)
            loader.visitAlign(i, done, busy);

        QSet<QString> ids, dup;
        for (const Actor &a : std::as_const(sc.actors)) {
            if (ids.contains(a.id))
                dup.insert(a.id);
            ids.insert(a.id);
        }
        if (!dup.isEmpty()) {
            QStringList d = dup.values();
            d.sort();
            fail(QStringLiteral("duplicate actor ids %1").arg(d.join(QStringLiteral(", "))));
        }
        for (const Actor &a : std::as_const(sc.actors)) {
            for (const QString &d : a.dependsOn)
                if (!ids.contains(d))
                    fail(QStringLiteral("%1: dependsOn unknown actor '%2'").arg(a.id, d));
            const QStringList targets = QStringList{a.clipActor, a.fitActor} + a.widthOf;
            for (const QString &t : targets)
                if (!t.isEmpty() && !ids.contains(t))
                    fail(QStringLiteral("%1: refers to unknown actor '%2'").arg(a.id, t));
            if (!a.fitActor.isEmpty() && sc.actor(a.fitActor)->type != QLatin1String("text"))
                fail(QStringLiteral("%1: fitTo must name a text actor").arg(a.id));
            if (!a.clipActor.isEmpty() && !sc.actor(a.clipActor)->hasBox)
                fail(QStringLiteral("%1: clipTo must name a box actor").arg(a.id));
        }

        // The hold: latest entrance end to earliest exit start.
        double hs = 0.0, he = sc.duration;
        bool anyIn = false, anyOut = false;
        for (const Actor &a : std::as_const(sc.actors)) {
            if (a.in.present) {
                hs = anyIn ? qMax(hs, a.in.start + a.in.dur) : a.in.start + a.in.dur;
                anyIn = true;
            }
            if (a.out.present) {
                he = anyOut ? qMin(he, a.out.start) : a.out.start;
                anyOut = true;
            }
        }
        if (he < hs)
            fail(QStringLiteral("exits start (%1s) before the last entrance ends (%2s)").arg(pyFloatStr(he), pyFloatStr(hs)));
        sc.holdStart = hs;
        sc.holdEnd = he;
    } catch (const Fail &f) {
        if (error)
            *error = f.message;
        return std::nullopt;
    }
    return sc;
}

Channels channels(const Actor &actor, double t)
{
    Channels ch;
    const auto apply = [&](const TimedBlock &b, bool isIn) {
        if (!b.present)
            return;
        const double p = phase(t, b);
        for (const auto &[k, v] : b.values) {
            double *reveal = k == QLatin1String("opacity") ? &ch.opacity
                           : k == QLatin1String("wipe")    ? &ch.wipe
                           : k == QLatin1String("progress") ? &ch.progress
                           : k == QLatin1String("scale")   ? &ch.scale
                                                            : nullptr;
            if (reveal) {
                *reveal *= isIn ? v + (1.0 - v) * p : 1.0 - (1.0 - v) * p;
            } else {
                double *off = k == QLatin1String("dx") ? &ch.dx : &ch.dy;
                *off += isIn ? v * (1.0 - p) : v * p;
            }
        }
    };
    apply(actor.in, true);
    apply(actor.out, false);
    return ch;
}

Channels drawChannels(const Scene &scene, const Actor &actor, double t, bool *skip)
{
    Channels ch = channels(actor, t);
    if (skip)
        *skip = false;
    QString p = actor.parent;
    while (!p.isEmpty()) {
        const Actor *g = scene.actor(p);
        if (!g)
            break;
        if (g->flatten) {
            if (skip)
                *skip = true;
            return ch;
        }
        const Channels gc = channels(*g, t);
        ch.opacity *= gc.opacity;
        ch.wipe *= gc.wipe;
        ch.progress *= gc.progress;
        ch.scale *= gc.scale;
        ch.dx += gc.dx + g->groupAt.x();
        ch.dy += gc.dy + g->groupAt.y();
        p = g->parent;
    }
    return ch;
}

double timeMap(const Scene &scene, double clipDuration, double t)
{
    const double d0 = scene.duration;
    if (clipDuration <= 0.0 || std::fabs(clipDuration - d0) < 1e-9)
        return t;
    const double hs = scene.holdStart, he = scene.holdEnd;
    const double d = clipDuration;
    const double tail = d0 - he;
    const double dmin = hs + tail;
    if (d >= dmin) {
        if (t <= hs)
            return t;
        if (t >= d - tail)
            return he + (t - (d - tail));
        return d - tail > hs ? hs + (t - hs) * ((he - hs) / qMax(1e-9, d - tail - hs)) : hs;
    }
    const double k = d > 0.0 ? dmin / d : 1.0;
    const double u = t * k;
    return u <= hs ? u : he + (u - hs);
}

double cadenceTime(const Scene &scene, double t, double projectFps)
{
    if (scene.cadence <= 0.0 || scene.cadence >= projectFps)
        return t;
    return std::floor(t * scene.cadence + 1e-9) / scene.cadence;
}

bool resolveConstraints(Scene &scene, const InkFn &ink, QString *error)
{
    QHash<QString, double> widths;
    QSet<QString> busy;
    std::function<double(const Actor &)> rest = [&](const Actor &a) -> double {
        if (const auto it = widths.constFind(a.id); it != widths.constEnd())
            return it.value();
        if (busy.contains(a.id))
            fail(QStringLiteral("%1: widthOf cycle").arg(a.id));
        busy.insert(a.id);
        double w = a.box[2] - a.box[0];
        if (!a.fitActor.isEmpty()) {
            const Actor *target = scene.actor(a.fitActor);
            const std::optional<QRectF> box = target && ink ? ink(*target) : std::nullopt;
            if (box)
                w = qMax(a.fitMin, box->right() - a.box[0] + a.fitPad);
            else
                w = qMax(a.fitMin, w);
        }
        if (!a.widthOf.isEmpty()) {
            double m = 0.0;
            bool first = true;
            for (const QString &id : a.widthOf) {
                const Actor *o = scene.actor(id);
                const double ow = o ? rest(*o) : w;
                m = first ? ow : qMin(m, ow);
                first = false;
            }
            w = m;
        }
        if (a.fitMax >= 0.0 && !a.fitActor.isEmpty())
            w = qMin(a.fitMax, w);
        busy.remove(a.id);
        widths.insert(a.id, w);
        return w;
    };
    try {
        for (Actor &a : scene.actors) {
            if (a.hasBox && (!a.fitActor.isEmpty() || !a.widthOf.isEmpty()))
                a.restWidth = rest(a);
            else
                a.restWidth = -1.0;
        }
    } catch (const Fail &f) {
        if (error)
            *error = f.message;
        return false;
    }
    return true;
}

QRectF currentBox(const Actor &actor, const Channels &ch)
{
    if (actor.wipeDown)
        return QRectF(QPointF(actor.box[0], actor.box[1]),
                      QPointF(actor.box[0] + actor.boxWidth(), actor.box[1] + (actor.box[3] - actor.box[1]) * ch.wipe));
    return QRectF(QPointF(actor.box[0], actor.box[1]), QPointF(actor.box[0] + actor.boxWidth() * ch.wipe, actor.box[3]));
}

quint64 sceneKey(const QJsonObject &doc, const QJsonObject &overrides)
{
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(QJsonDocument(doc).toJson(QJsonDocument::Compact));
    h.addData(QByteArrayView("\0", 1));
    h.addData(QJsonDocument(overrides).toJson(QJsonDocument::Compact));
    const QByteArray r = h.result();
    quint64 k = 0;
    for (int i = 0; i < 8; ++i)
        k = (k << 8) | quint8(r.at(i));
    return k ? k : 1;
}

QVariantList paramRows(const Scene &scene)
{
    QVariantList rows;
    for (const ParamSpec &p : scene.params) {
        QVariantMap row{{QStringLiteral("id"), p.id},
                        {QStringLiteral("type"), p.type},
                        {QStringLiteral("label"), p.label},
                        {QStringLiteral("value"), scene.values.value(p.id)},
                        {QStringLiteral("default"), p.defaultValue}};
        for (int which = 0; which < 2; ++which) {
            const QVariant bound = which == 0 ? p.min : p.max;
            if (!bound.isValid())
                continue;
            QString err;
            const QVariant b = kindOf(bound) == VK::Str ? evalExpression(bound.toString(), scene.values, &err) : bound;
            if (b.isValid())
                row.insert(which == 0 ? QStringLiteral("min") : QStringLiteral("max"), b);
        }
        if (!p.options.isEmpty())
            row.insert(QStringLiteral("options"), p.options);
        rows << row;
    }
    return rows;
}

} // namespace drift::scene
