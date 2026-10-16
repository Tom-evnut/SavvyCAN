#include "ldfparser.h"

#include <stdexcept>

namespace {

struct Token
{
    enum Type { Ident, Number, String, Punct, End };
    Type type = End;
    QString text;      //identifier, string contents, punctuation character, or the number as written
    double number = 0;
    bool isInteger = false;
    int line = 0;
};

struct ParseError : std::runtime_error
{
    ParseError(const QString &msg) : std::runtime_error(msg.toStdString()) {}
};

QList<Token> tokenize(const QString &text)
{
    QList<Token> tokens;
    int line = 1;
    int i = 0;
    const int n = text.size();
    while (i < n)
    {
        const QChar c = text.at(i);
        if (c == '\n') { line++; i++; continue; }
        if (c.isSpace()) { i++; continue; }
        if (c == '/' && i + 1 < n && text.at(i + 1) == '/')
        {
            while (i < n && text.at(i) != '\n') i++;
            continue;
        }
        if (c == '/' && i + 1 < n && text.at(i + 1) == '*')
        {
            const int end = text.indexOf("*/", i + 2);
            if (end < 0) throw ParseError(QString("line %1: comment never closed").arg(line));
            line += text.mid(i, end - i).count('\n');
            i = end + 2;
            continue;
        }

        Token t;
        t.line = line;
        if (c == '"')
        {
            const int end = text.indexOf('"', i + 1);
            if (end < 0) throw ParseError(QString("line %1: string never closed").arg(line));
            t.type = Token::String;
            t.text = text.mid(i + 1, end - i - 1);
            line += t.text.count('\n');
            i = end + 1;
        }
        else if (c.isLetter() || c == '_')
        {
            int j = i;
            while (j < n && (text.at(j).isLetterOrNumber() || text.at(j) == '_')) j++;
            t.type = Token::Ident;
            t.text = text.mid(i, j - i);
            i = j;
        }
        else if (c.isDigit() || ((c == '-' || c == '+' || c == '.') && i + 1 < n && (text.at(i + 1).isDigit() || text.at(i + 1) == '.')))
        {
            int j = i;
            bool ok = false;
            if (c == '0' && i + 1 < n && (text.at(i + 1) == 'x' || text.at(i + 1) == 'X'))
            {
                j = i + 2;
                while (j < n && (text.at(j).isDigit() || QString("abcdefABCDEF").contains(text.at(j)))) j++;
                t.number = double(text.mid(i + 2, j - i - 2).toLongLong(&ok, 16));
                t.isInteger = true;
            }
            else
            {
                if (c == '-' || c == '+') j++;
                while (j < n && (text.at(j).isDigit() || text.at(j) == '.')) j++;
                if (j < n && (text.at(j) == 'e' || text.at(j) == 'E') && j + 1 < n &&
                    (text.at(j + 1).isDigit() || text.at(j + 1) == '-' || text.at(j + 1) == '+'))
                {
                    j += 2;
                    while (j < n && text.at(j).isDigit()) j++;
                }
                const QString num = text.mid(i, j - i);
                t.number = num.toDouble(&ok);
                t.isInteger = !num.contains('.') && !num.contains('e') && !num.contains('E');
            }
            if (!ok) throw ParseError(QString("line %1: bad number \"%2\"").arg(line).arg(text.mid(i, j - i)));
            t.type = Token::Number;
            t.text = text.mid(i, j - i);
            i = j;
        }
        else if (QString("{};:,=").contains(c))
        {
            t.type = Token::Punct;
            t.text = c;
            i++;
        }
        else throw ParseError(QString("line %1: unexpected character '%2'").arg(line).arg(c));
        tokens.append(t);
    }
    Token end;
    end.line = line;
    tokens.append(end);
    return tokens;
}

class Parser
{
public:
    Parser(const QList<Token> &tokens, LdfDocument &doc) : t(tokens), d(doc) {}

    void parseFile()
    {
        while (peek().type != Token::End)
        {
            const Token name = expectIdent();
            if (name.text == "LIN_description_file") { expectPunct(";"); continue; }
            if (isPunct(peek(), "="))
            {
                next();
                header(name);
                continue;
            }
            if (isPunct(peek(), "{"))
            {
                next();
                section(name);
                continue;
            }
            throw error(peek(), QString("expected = or { after \"%1\"").arg(name.text));
        }
    }

private:
    typedef QList<Token> Item; //tokens between commas

    const QList<Token> &t;
    LdfDocument &d;
    int pos = 0;

    const Token &peek(int ahead = 0) const { return t.at(qMin(pos + ahead, int(t.size()) - 1)); }
    const Token &next() { const Token &tok = peek(); if (pos < t.size() - 1) pos++; return tok; }
    static bool isPunct(const Token &tok, const char *p) { return tok.type == Token::Punct && tok.text == p; }
    ParseError error(const Token &at, const QString &msg) const
    {
        return ParseError(QString("line %1: %2").arg(at.line).arg(msg));
    }
    void expectPunct(const char *p)
    {
        if (!isPunct(peek(), p))
            throw error(peek(), QString("expected '%1', found \"%2\"").arg(p).arg(peek().type == Token::End ? "end of file" : peek().text));
        next();
    }
    Token expectIdent()
    {
        if (peek().type == Token::End) throw error(peek(), "unexpected end of the file: a '{' never closed?");
        if (peek().type != Token::Ident) throw error(peek(), QString("expected a name, found \"%1\"").arg(peek().text));
        return next();
    }
    qint64 expectInt()
    {
        if (peek().type != Token::Number) throw error(peek(), QString("expected a number, found \"%1\"").arg(peek().text));
        return qint64(next().number);
    }

    //comma-separated items up to (and consuming) the terminator; commas inside { } don't split
    QList<Item> items(const char *terminator)
    {
        QList<Item> result;
        Item current;
        int depth = 0;
        while (true)
        {
            const Token &tok = peek();
            if (tok.type == Token::End) throw error(tok, QString("expected '%1' before the end of the file").arg(terminator));
            if (depth == 0 && isPunct(tok, terminator)) { next(); break; }
            if (depth == 0 && isPunct(tok, "}")) throw error(tok, QString("expected '%1' before '}'").arg(terminator));
            if (isPunct(tok, "{")) depth++;
            if (isPunct(tok, "}")) depth--;
            if (depth == 0 && isPunct(tok, ","))
            {
                result.append(current);
                current.clear();
                next();
                continue;
            }
            current.append(next());
        }
        if (!current.isEmpty() || !result.isEmpty()) result.append(current);
        return result;
    }

    static QString itemText(const Item &item)
    {
        QStringList parts;
        for (const Token &tok : item) parts << tok.text;
        return parts.join(' ');
    }
    qint64 itemInt(const Item &item, const char *what) const
    {
        if (item.isEmpty() || item.first().type != Token::Number)
            throw error(item.isEmpty() ? peek() : item.first(), QString("expected a number for %1").arg(what));
        return qint64(item.first().number);
    }
    double itemNumber(const Item &item, const char *what) const
    {
        if (item.isEmpty() || item.first().type != Token::Number)
            throw error(item.isEmpty() ? peek() : item.first(), QString("expected a number for %1").arg(what));
        return item.first().number;
    }
    QString itemName(const Item &item, const char *what) const
    {
        if (item.isEmpty() || item.first().type != Token::Ident)
            throw error(item.isEmpty() ? peek() : item.first(), QString("expected a name for %1").arg(what));
        return item.first().text;
    }

    void skipBlock() //after '{', up to and including the matching '}'
    {
        int depth = 1;
        while (depth > 0)
        {
            const Token &tok = next();
            if (tok.type == Token::End) throw error(tok, "'{' never closed");
            if (isPunct(tok, "{")) depth++;
            if (isPunct(tok, "}")) depth--;
        }
    }

    void header(const Token &name)
    {
        const QList<Item> values = items(";");
        const Item value = values.value(0);
        if (name.text == "LIN_protocol_version") d.protocolVersion = itemText(value);
        else if (name.text == "LIN_language_version") d.languageVersion = itemText(value);
        else if (name.text == "Channel_name") d.channelName = itemText(value);
        else if (name.text == "LIN_speed")
        {
            d.speedBps = itemNumber(value, "LIN_speed");
            const QString unit = value.size() > 1 ? value.at(1).text.toLower() : QString("kbps");
            if (unit != "bps")
            {
                if (unit != "kbps") d.warnings << QString("LIN_speed: unknown unit \"%1\", read as kbps").arg(unit);
                d.speedBps *= 1000;
            }
        }
        else d.warnings << QString("line %1: \"%2\" not used").arg(name.line).arg(name.text);
    }

    void section(const Token &name)
    {
        const QString s = name.text;
        if (s == "Nodes") nodes();
        else if (s == "Signals") signalList(false);
        else if (s == "Diagnostic_signals") signalList(true);
        else if (s == "Frames") frameList(false);
        else if (s == "Diagnostic_frames") frameList(true);
        else if (s == "Sporadic_frames") sporadicFrames();
        else if (s == "Event_triggered_frames") eventTriggeredFrames();
        else if (s == "Node_attributes") nodeAttributes();
        else if (s == "Schedule_tables") schedules();
        else if (s == "Signal_encoding_types") encodings();
        else if (s == "Signal_representation") representation();
        else
        {
            if (s != "Node_composition" && s != "Signal_groups" && s != "Dynamic_frames")
                d.warnings << QString("line %1: section \"%2\" skipped").arg(name.line).arg(s);
            skipBlock();
        }
    }

    void nodes()
    {
        while (!isPunct(peek(), "}"))
        {
            const Token key = expectIdent();
            expectPunct(":");
            const QList<Item> values = items(";");
            if (key.text == "Master")
            {
                d.master = itemName(values.value(0), "the master");
                if (values.size() > 1) d.timeBaseMs = itemNumber(values.at(1), "the time base");
                if (values.size() > 2) d.jitterMs = itemNumber(values.at(2), "the jitter");
            }
            else if (key.text == "Slaves")
            {
                for (const Item &v : values) d.slaves << itemName(v, "a slave");
            }
            else d.warnings << QString("line %1: Nodes entry \"%2\" not used").arg(key.line).arg(key.text);
        }
        next();
    }

    void signalList(bool diagnostic)
    {
        while (!isPunct(peek(), "}"))
        {
            LdfSignal sig;
            sig.diagnostic = diagnostic;
            sig.name = expectIdent().text;
            expectPunct(":");
            const QList<Item> values = items(";");
            sig.size = int(itemInt(values.value(0), "the signal size"));
            if (values.size() > 1)
            {
                const Item &init = values.at(1);
                if (!init.isEmpty() && isPunct(init.first(), "{"))
                {
                    sig.byteArray = true;
                    for (const Token &tok : init)
                        if (tok.type == Token::Number) sig.initValues << qint64(tok.number);
                }
                else sig.initValues << itemInt(init, "the initial value");
            }
            if (!diagnostic)
            {
                if (values.size() > 2) sig.publisher = itemName(values.at(2), "the publisher");
                for (int i = 3; i < values.size(); i++) sig.subscribers << itemName(values.at(i), "a subscriber");
            }
            if (sig.size < 1 || sig.size > 64)
                throw error(peek(), QString("signal %1: size %2 isn't 1 to 64 bits").arg(sig.name).arg(sig.size));
            d.allSignals.append(sig);
        }
        next();
    }

    void frameList(bool diagnostic)
    {
        while (!isPunct(peek(), "}"))
        {
            LdfFrame frame;
            frame.diagnostic = diagnostic;
            frame.name = expectIdent().text;
            expectPunct(":");
            const QList<Item> header = items("{");
            frame.id = int(itemInt(header.value(0), "the frame ID"));
            if (!diagnostic && header.size() > 1) frame.publisher = itemName(header.at(1), "the publisher");
            const int lengthItem = diagnostic ? 1 : 2;
            if (header.size() > lengthItem)
            {
                frame.length = int(itemInt(header.at(lengthItem), "the frame length"));
                frame.lengthGiven = true;
            }
            else frame.length = diagnostic ? 8 : LdfFrame::defaultLength(frame.id);
            if (frame.id < 0 || frame.id > 63)
                throw error(peek(), QString("frame %1: ID %2 isn't 0 to 63").arg(frame.name).arg(frame.id));
            if (frame.length < 1 || frame.length > 8)
                throw error(peek(), QString("frame %1: length %2 isn't 1 to 8").arg(frame.name).arg(frame.length));
            while (!isPunct(peek(), "}"))
            {
                LdfFrameSignal fs;
                const QList<Item> entry = items(";");
                fs.name = itemName(entry.value(0), "a signal");
                fs.offset = int(itemInt(entry.value(1), "the signal offset"));
                frame.frameSignals.append(fs);
            }
            next();
            d.frames.append(frame);
        }
        next();
    }

    void sporadicFrames()
    {
        while (!isPunct(peek(), "}"))
        {
            LdfSporadicFrame sf;
            sf.name = expectIdent().text;
            expectPunct(":");
            for (const Item &v : items(";")) sf.frames << itemName(v, "a frame");
            d.sporadicFrames.append(sf);
        }
        next();
    }

    void eventTriggeredFrames()
    {
        while (!isPunct(peek(), "}"))
        {
            LdfEventTriggeredFrame ef;
            ef.name = expectIdent().text;
            expectPunct(":");
            const QList<Item> values = items(";");
            int i = 0;
            if (!values.value(0).isEmpty() && values.at(0).first().type == Token::Ident)
                ef.collisionTable = values.at(i++).first().text; //LIN 2.1: collision resolving schedule first
            ef.id = int(itemInt(values.value(i++), "the event-triggered frame ID"));
            for (; i < values.size(); i++) ef.frames << itemName(values.at(i), "a frame");
            d.eventTriggeredFrames.append(ef);
        }
        next();
    }

    void nodeAttributes()
    {
        while (!isPunct(peek(), "}"))
        {
            LdfNodeAttributes na;
            na.node = expectIdent().text;
            expectPunct("{");
            while (!isPunct(peek(), "}"))
            {
                const Token key = expectIdent();
                if (isPunct(peek(), "{")) //configurable_frames { name [= id] ; ... }
                {
                    next();
                    while (!isPunct(peek(), "}"))
                    {
                        const QList<Item> entry = items(";");
                        const QString frameName = itemName(entry.value(0), "a configurable frame");
                        if (key.text == "configurable_frames") na.configurableFrames << frameName;
                    }
                    next();
                    continue;
                }
                expectPunct("=");
                QStringList parts;
                for (const Item &v : items(";")) parts << itemText(v);
                na.values.insert(key.text, parts.join(", "));
            }
            next();
            d.nodeAttributes.append(na);
        }
        next();
    }

    void schedules()
    {
        while (!isPunct(peek(), "}"))
        {
            LdfSchedule table;
            table.name = expectIdent().text;
            expectPunct("{");
            while (!isPunct(peek(), "}"))
            {
                LdfScheduleEntry entry;
                entry.command = expectIdent().text;
                if (isPunct(peek(), "{"))
                {
                    next();
                    for (const Item &v : items("}")) entry.arguments << itemText(v);
                }
                const Token delay = expectIdent();
                if (delay.text != "delay") throw error(delay, QString("expected \"delay\", found \"%1\"").arg(delay.text));
                if (peek().type != Token::Number) throw error(peek(), "expected the delay");
                entry.delayMs = next().number;
                if (peek().type == Token::Ident && peek().text == "ms") next();
                expectPunct(";");
                table.entries.append(entry);
            }
            next();
            d.schedules.append(table);
        }
        next();
    }

    void encodings()
    {
        while (!isPunct(peek(), "}"))
        {
            LdfEncoding enc;
            enc.name = expectIdent().text;
            expectPunct("{");
            while (!isPunct(peek(), "}"))
            {
                const QList<Item> values = items(";");
                const QString kind = itemName(values.value(0), "the encoding kind");
                LdfEncodingValue v;
                if (kind == "logical_value")
                {
                    v.kind = LdfEncodingValue::Logical;
                    v.value = itemInt(values.value(1), "the logical value");
                    if (values.size() > 2) v.text = itemText(values.at(2));
                }
                else if (kind == "physical_value")
                {
                    v.kind = LdfEncodingValue::Physical;
                    v.min = itemNumber(values.value(1), "the minimum");
                    v.max = itemNumber(values.value(2), "the maximum");
                    v.scale = itemNumber(values.value(3), "the scale");
                    v.offset = itemNumber(values.value(4), "the offset");
                    if (values.size() > 5) v.unit = itemText(values.at(5));
                }
                else if (kind == "bcd_value") v.kind = LdfEncodingValue::Bcd;
                else if (kind == "ascii_value") v.kind = LdfEncodingValue::Ascii;
                else
                {
                    d.warnings << QString("encoding %1: \"%2\" not known, skipped").arg(enc.name).arg(kind);
                    continue;
                }
                enc.values.append(v);
            }
            next();
            d.encodings.append(enc);
        }
        next();
    }

    void representation()
    {
        while (!isPunct(peek(), "}"))
        {
            const QString enc = expectIdent().text;
            expectPunct(":");
            for (const Item &v : items(";")) d.representation.insert(itemName(v, "a signal"), enc);
        }
        next();
    }
};

}

bool LdfDocument::parse(const QString &text, QString *error)
{
    *this = LdfDocument();
    try
    {
        const QList<Token> tokens = tokenize(text);
        Parser parser(tokens, *this);
        parser.parseFile();
    }
    catch (const ParseError &e)
    {
        if (error) *error = QString::fromStdString(e.what());
        return false;
    }
    if (frames.isEmpty() && allSignals.isEmpty())
    {
        if (error) *error = "no signals or frames: not a LIN description file?";
        return false;
    }
    return true;
}

const LdfSignal *LdfDocument::findSignal(const QString &name) const
{
    for (const LdfSignal &s : allSignals) if (s.name == name) return &s;
    return nullptr;
}

const LdfFrame *LdfDocument::findFrame(const QString &name) const
{
    for (const LdfFrame &f : frames) if (f.name == name) return &f;
    return nullptr;
}

const LdfEncoding *LdfDocument::findEncoding(const QString &name) const
{
    for (const LdfEncoding &e : encodings) if (e.name == name) return &e;
    return nullptr;
}
