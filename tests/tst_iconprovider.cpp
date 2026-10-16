#include "ui/IconProvider.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QImageReader>
#include <QRegularExpression>
#include <QTest>

#include <cstdlib>

using namespace atk::ui;

namespace {

/// The average colour of the visible (non-transparent) pixels.
QColor inkColor(const QImage& source)
{
    const QImage image = source.convertToFormat(QImage::Format_ARGB32);
    qint64 r = 0, g = 0, b = 0, n = 0;
    for (int y = 0; y < image.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x]) < 250) continue; // solid stroke pixels only, not antialiased edges
            r += qRed(line[x]);
            g += qGreen(line[x]);
            b += qBlue(line[x]);
            ++n;
        }
    }
    if (n == 0) return {};
    return QColor(int(r / n), int(g / n), int(b / n));
}

bool near(const QColor& a, const QColor& b)
{
    return a.isValid() && std::abs(a.red() - b.red()) <= 3 && std::abs(a.green() - b.green()) <= 3
        && std::abs(a.blue() - b.blue()) <= 3;
}

} // namespace

class TestIconProvider : public QObject {
    Q_OBJECT
private slots:
    void iconsDrawWithoutTheSvgImagePlugin();
    void everyBundledIconRenders();
    void iconsUsedByTheInterfaceExist();
    void unknownNameIsNullNotBlank();
    void tintFollowsModeAndState();
    void highDpiRendersAtDeviceSize();
};

// First, before anything loads an image plugin: an app bundle without Qt's
// "svg" image-format plugin (macdeployqt left it out on macOS) showed every
// button blank. Icons must not depend on it.
void TestIconProvider::iconsDrawWithoutTheSvgImagePlugin()
{
    QCoreApplication::setLibraryPaths({});
    QVERIFY2(!QImageReader::supportedImageFormats().contains("svg"),
             "an svg image plugin was still found; this test proves nothing");
    const QPixmap pm = icons::pixmap(QStringLiteral("play"), QSize(24, 24), Qt::white);
    QVERIFY(!pm.isNull());
    QVERIFY2(inkColor(pm.toImage()).isValid(), "the play icon drew nothing");
}

void TestIconProvider::everyBundledIconRenders()
{
    QVERIFY(icons::exists(QStringLiteral("pencil"))); // also registers the resources
    const QStringList files = QDir(QStringLiteral(":/icons/lucide")).entryList({QStringLiteral("*.svg")});
    QVERIFY2(files.size() >= 60, qPrintable(QString::number(files.size())));
    for (const QString& file : files) {
        const QString name = file.chopped(4);
        const QPixmap pm = icons::pixmap(name, QSize(24, 24), Qt::white);
        QVERIFY2(!pm.isNull(), qPrintable(name));
        QVERIFY2(inkColor(pm.toImage()).isValid(), qPrintable(name + QStringLiteral(" drew nothing")));
    }
}

void TestIconProvider::iconsUsedByTheInterfaceExist()
{
    // Names referenced from the widgets: a typo would otherwise only show up
    // as a blank button.
    const QStringList used{QStringLiteral("play"),         QStringLiteral("pause"),
                           QStringLiteral("pencil"),       QStringLiteral("skip-back"),
                           QStringLiteral("step-back"),    QStringLiteral("rewind"),
                           QStringLiteral("step-forward"), QStringLiteral("fast-forward"),
                           QStringLiteral("skip-forward"), QStringLiteral("repeat"),
                           QStringLiteral("volume-2"),     QStringLiteral("volume-x"),
                           QStringLiteral("folder-open"),  QStringLiteral("settings"),
                           QStringLiteral("bookmark-plus"), QStringLiteral("move-horizontal"),
                           QStringLiteral("plus"),         QStringLiteral("minus"),
                           QStringLiteral("arrow-up"),     QStringLiteral("arrow-down"),
                           QStringLiteral("scan"),         QStringLiteral("zoom-in"),
                           QStringLiteral("zoom-out"),     QStringLiteral("gauge"),
                           QStringLiteral("pin"),          QStringLiteral("unfold-horizontal"),
                           QStringLiteral("flip-horizontal-2")};
    for (const QString& name : used) QVERIFY2(icons::exists(name), qPrintable(name));
}

void TestIconProvider::unknownNameIsNullNotBlank()
{
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Unknown icon.*no-such-icon")));
    QVERIFY(icons::icon(QStringLiteral("no-such-icon")).isNull());
    QVERIFY(!icons::exists(QStringLiteral("no-such-icon")));
    QVERIFY(icons::pixmap(QStringLiteral("no-such-icon"), QSize(16, 16), Qt::white).isNull());
}

void TestIconProvider::tintFollowsModeAndState()
{
    const QIcon icon = icons::icon(QStringLiteral("square"));
    QVERIFY(!icon.isNull());
    const QSize size(32, 32);
    const auto colorOf = [&](QIcon::Mode mode, QIcon::State state) {
        return inkColor(icon.pixmap(size, mode, state).toImage());
    };
    QVERIFY(near(colorOf(QIcon::Normal, QIcon::Off), theme::iconNormal()));
    QVERIFY(near(colorOf(QIcon::Active, QIcon::Off), theme::iconHover()));
    QVERIFY(near(colorOf(QIcon::Disabled, QIcon::Off), theme::iconDisabled()));
    QVERIFY(near(colorOf(QIcon::Normal, QIcon::On), theme::textOnAccent()));

    const QIcon fixed = icons::icon(QStringLiteral("square"), QColor(0xFF, 0x40, 0x20));
    QVERIFY(near(inkColor(fixed.pixmap(size, QIcon::Disabled).toImage()), QColor(0xFF, 0x40, 0x20)));
}

void TestIconProvider::highDpiRendersAtDeviceSize()
{
    const QPixmap pm = icons::pixmap(QStringLiteral("pencil"), QSize(18, 18), Qt::white, 2.0);
    QCOMPARE(pm.size(), QSize(36, 36));
    QCOMPARE(pm.devicePixelRatio(), 2.0);
    QCOMPARE(pm.deviceIndependentSize().toSize(), QSize(18, 18));
}

QTEST_MAIN(TestIconProvider)
#include "tst_iconprovider.moc"
