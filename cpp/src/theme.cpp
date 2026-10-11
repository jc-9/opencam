#include "theme.h"

#include <QDir>
#include <QFile>
#include <QMap>
#include <QProcess>
#include <QTextStream>

namespace {

QString tomlGet(const QMap<QString, QString>& kv, const QString& key, const QString& fb) {
    const QString v = kv.value(key).trimmed();
    return v.isEmpty() ? fb : v;
}

}  // namespace

Palette loadOmarchyPalette() {
    Palette pal;
    QString slug;
    QProcess proc;
    proc.start(QStringLiteral("omarchy"), {QStringLiteral("theme"), QStringLiteral("current")});
    if (proc.waitForFinished(2000) && proc.exitCode() == 0)
        slug = QString::fromUtf8(proc.readAllStandardOutput()).trimmed().toLower().replace(' ', '-');
    if (slug.isEmpty()) return pal;

    QString path;
    for (const QString& base :
         {QDir::homePath() + QStringLiteral("/.config/omarchy/themes"),
          QStringLiteral("/usr/share/omarchy/themes")}) {
        const QString cand = base + '/' + slug + QStringLiteral("/colors.toml");
        if (QFile::exists(cand)) { path = cand; break; }
    }
    if (path.isEmpty()) return pal;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return pal;
    QMap<QString, QString> kv;
    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        const int eq = line.indexOf('=');
        if (eq < 0) continue;
        QString key = line.left(eq).trimmed();
        QString val = line.mid(eq + 1).trimmed();
        if ((val.startsWith('"') && val.endsWith('"')) || (val.startsWith('\'') && val.endsWith('\'')))
            val = val.mid(1, val.size() - 2);
        kv[key] = val.trimmed();
    }

    pal.bg = tomlGet(kv, QStringLiteral("background"), pal.bg);
    pal.darkBg = tomlGet(kv, QStringLiteral("dark_background"), pal.darkBg);
    pal.lighterBg = tomlGet(kv, QStringLiteral("lighter_background"), pal.lighterBg);
    pal.fg = tomlGet(kv, QStringLiteral("foreground"), pal.fg);
    pal.muted = tomlGet(kv, QStringLiteral("muted"), pal.muted);
    pal.accent = tomlGet(kv, QStringLiteral("accent"), pal.accent);
    pal.border = tomlGet(kv, QStringLiteral("selection"), pal.border);
    pal.histBar = tomlGet(kv, QStringLiteral("cyan"), pal.histBar);
    return pal;
}
