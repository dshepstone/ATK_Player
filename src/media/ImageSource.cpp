#include "media/ImageSource.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>

namespace atk::media {
namespace {

/// A file name split around its frame number: prefix, digits, suffix (the
/// suffix includes the extension).
struct NumberedName {
    QString prefix;
    QString digits;
    QString suffix;
};

/// Splits on the *last* run of digits before the extension, so
/// "shot010_v2.1001.exr" numbers frames by 1001, not by 010 or 2.
std::optional<NumberedName> splitNumberedName(const QString& fileName)
{
    const QFileInfo info(fileName);
    const QString extension = info.suffix();
    if (extension.isEmpty()) return std::nullopt;
    const QString stem = fileName.left(fileName.size() - extension.size() - 1);
    static const QRegularExpression lastDigits(QStringLiteral("^(.*?)(\\d+)(\\D*)$"));
    const QRegularExpressionMatch match = lastDigits.match(stem);
    if (!match.hasMatch()) return std::nullopt;
    return NumberedName{ match.captured(1), match.captured(2),
                         match.captured(3) + QLatin1Char('.') + extension };
}

/// The frame token in a pattern's file name: "%04d" -> padding 4, "%d" -> 0.
struct PatternParts {
    QString directory;
    QString prefix;
    int padding = 0;
    QString suffix;
};

std::optional<PatternParts> splitPattern(const QString& pattern)
{
    const QFileInfo info(pattern);
    static const QRegularExpression token(QStringLiteral("^(.*)%(?:0(\\d+))?d([^%]*)$"));
    const QRegularExpressionMatch match = token.match(info.fileName());
    if (!match.hasMatch()) return std::nullopt;
    PatternParts parts;
    parts.directory = info.path();
    parts.prefix = match.captured(1);
    parts.padding = match.captured(2).isEmpty() ? 0 : match.captured(2).toInt();
    parts.suffix = match.captured(3);
    return parts;
}

/// Numbers of the files in `parts.directory` that match the pattern exactly.
QList<int64_t> matchingFrames(const PatternParts& parts)
{
    QList<int64_t> frames;
    const QDir directory(parts.directory);
    const QStringList names = directory.entryList(
        { parts.prefix + QLatin1Char('*') + parts.suffix }, QDir::Files);
    for (const QString& name : names) {
        if (name.size() <= parts.prefix.size() + parts.suffix.size()) continue;
        if (!name.startsWith(parts.prefix, Qt::CaseInsensitive)
            || !name.endsWith(parts.suffix, Qt::CaseInsensitive))
            continue;
        const QString digits = name.mid(parts.prefix.size(),
                                        name.size() - parts.prefix.size() - parts.suffix.size());
        bool numeric = !digits.isEmpty();
        for (const QChar c : digits) numeric = numeric && c.isDigit();
        if (!numeric) continue;
        // "%04d" frames are exactly N digits ("10000" is also valid once a
        // padded counter overflows its width); "%d" frames carry no padding.
        if (parts.padding > 0) {
            if (digits.size() < parts.padding) continue;
            if (digits.size() > parts.padding && digits.startsWith(QLatin1Char('0'))) continue;
        } else if (digits.size() > 1 && digits.startsWith(QLatin1Char('0'))) {
            continue;
        }
        bool ok = false;
        const qlonglong number = digits.toLongLong(&ok);
        if (ok) frames.append(number);
    }
    std::sort(frames.begin(), frames.end());
    frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
    return frames;
}

} // namespace

ImageSourceOptions ImageSourceOptions::normalized() const
{
    ImageSourceOptions result = *this;
    result.holdFrames = std::clamp(holdFrames, kMinimumHoldFrames, kMaximumHoldFrames);
    if (!result.frameRate.isValid()) {
        result.frameRate = kDefaultFrameRate;
    }
    if ((sequenceFirst >= 0 || sequenceLast >= 0)
        && (!isSequence() || sequenceLength() > kMaximumSequenceFrames)) {
        result.sequenceFirst = -1;
        result.sequenceLast = -1;
    }
    return result;
}

const QList<FrameRate>& imageFrameRatePresets()
{
    static const QList<FrameRate> presets{
        { 24000, 1001 }, { 24, 1 }, { 25, 1 }, { 30000, 1001 }, { 30, 1 },
        { 48, 1 }, { 50, 1 }, { 60000, 1001 }, { 60, 1 },
    };
    return presets;
}

QString frameRateLabel(const FrameRate& rate)
{
    return QStringLiteral("%1 fps").arg(QString::number(rate.toDouble(), 'g', 5));
}

const QStringList& stillImageExtensions()
{
    static const QStringList extensions{
        QStringLiteral("png"),  QStringLiteral("jpg"),  QStringLiteral("jpeg"),
        QStringLiteral("tif"),  QStringLiteral("tiff"), QStringLiteral("bmp"),
        QStringLiteral("tga"),  QStringLiteral("webp"), QStringLiteral("exr"),
    };
    return extensions;
}

bool isStillImagePath(const QString& filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    return !suffix.isEmpty() && stillImageExtensions().contains(suffix);
}

std::optional<ImageSequence> detectImageSequence(const QString& filePath)
{
    if (!isStillImagePath(filePath)) return std::nullopt;
    const QFileInfo info(filePath);
    const auto numbered = splitNumberedName(info.fileName());
    if (!numbered) return std::nullopt;

    // Padded when the clicked number has a leading zero; otherwise accept the
    // same width only if every neighbour is that width (1001..1096), or any
    // unpadded width (1..120).
    const QString& digits = numbered->digits;
    PatternParts padded{ info.absolutePath(), numbered->prefix,
                         static_cast<int>(digits.size()), numbered->suffix };
    PatternParts unpadded = padded;
    unpadded.padding = 0;

    QList<int64_t> frames;
    PatternParts chosen = padded;
    if (digits.size() > 1 && digits.startsWith(QLatin1Char('0'))) {
        frames = matchingFrames(padded);
    } else {
        const QList<int64_t> plain = matchingFrames(unpadded);
        const QList<int64_t> fixed = matchingFrames(padded);
        if (plain.size() > fixed.size()) {
            frames = plain;
            chosen = unpadded;
        } else {
            frames = fixed;
        }
    }
    if (frames.size() < 2) return std::nullopt;

    // Built by concatenation: "%0" would itself be read as an arg() marker.
    const QString token = chosen.padding > 0
        ? QStringLiteral("%0") + QString::number(chosen.padding) + QLatin1Char('d')
        : QStringLiteral("%d");
    ImageSequence sequence;
    sequence.pattern = QDir(chosen.directory).filePath(chosen.prefix + token + chosen.suffix);
    sequence.first = frames.first();
    sequence.last = frames.last();
    sequence.presentCount = frames.size();
    if (sequence.length() > ImageSourceOptions::kMaximumSequenceFrames) return std::nullopt;
    return sequence;
}

QString sequenceFramePath(const QString& pattern, int64_t frameNumber)
{
    const auto parts = splitPattern(pattern);
    if (!parts || frameNumber < 0) return {};
    const QString number = QString::number(frameNumber).rightJustified(parts->padding, QLatin1Char('0'));
    return QDir(parts->directory).filePath(parts->prefix + number + parts->suffix);
}

QList<int64_t> presentSequenceFrames(const QString& pattern, int64_t first, int64_t last)
{
    QList<int64_t> present;
    const auto parts = splitPattern(pattern);
    if (!parts) return present;
    for (const int64_t frame : matchingFrames(*parts)) {
        if (frame >= first && frame <= last) present.append(frame);
    }
    return present;
}

QString sequenceDisplayName(const QString& pattern, int64_t first, int64_t last)
{
    const auto parts = splitPattern(pattern);
    if (!parts) return QFileInfo(pattern).fileName();
    const auto pad = [&](int64_t value) {
        return QString::number(value).rightJustified(parts->padding, QLatin1Char('0'));
    };
    return QStringLiteral("%1[%2-%3]%4").arg(parts->prefix, pad(first), pad(last), parts->suffix);
}

QString sequenceBaseName(const QString& pattern)
{
    const auto parts = splitPattern(pattern);
    if (!parts) return QFileInfo(pattern).completeBaseName();
    QString prefix = parts->prefix;
    while (!prefix.isEmpty()
           && (prefix.endsWith(QLatin1Char('.')) || prefix.endsWith(QLatin1Char('_'))
               || prefix.endsWith(QLatin1Char('-')) || prefix.endsWith(QLatin1Char(' '))))
        prefix.chop(1);
    return prefix.isEmpty() ? QStringLiteral("sequence") : prefix;
}

bool imageSourceExists(const QString& path, const ImageSourceOptions& options)
{
    if (!options.isSequence()) {
        const QFileInfo info(path);
        return info.exists() && info.isFile();
    }
    return !presentSequenceFrames(path, options.sequenceFirst, options.sequenceLast).isEmpty();
}

} // namespace atk::media
