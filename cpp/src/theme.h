#pragma once

// Omarchy theme palette (colors.toml), with Tokyo-Night fallbacks.

#include <QString>

struct Palette {
    QString bg = QStringLiteral("#1a1b26");
    QString darkBg = QStringLiteral("#13141c");
    QString lighterBg = QStringLiteral("#24283b");
    QString fg = QStringLiteral("#a9b1d6");
    QString muted = QStringLiteral("#565f89");
    QString accent = QStringLiteral("#7aa2f7");
    QString accentHover = QStringLiteral("#9ece6a");
    QString border = QStringLiteral("#414868");
    QString redLine = QStringLiteral("#f7768e");
    QString histBar = QStringLiteral("#7aa2f7");
};

Palette loadOmarchyPalette();
