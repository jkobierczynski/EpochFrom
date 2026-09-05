#pragma once

// Small layout helpers shared by SolveTab/CalibrateTab/DateTab/GaiaTab, so
// all four tabs get the same fix for two related complaints: (1) each tab
// used to stack its options and its command-output log in one flat column,
// so growing the log required scrolling past a wall of options first, with
// no way to give the log more room without also fighting the wheel-event
// bug in NoWheelWidgets.h; and (2) the primary action button lived inside
// that same scrollable/splittable column, so it could scroll out of view.
//
// The fix: each tab now puts its options (in a QScrollArea) and its output
// pane into a QSplitter, and keeps the primary action button in a small
// bar below the splitter -- always visible -- alongside three one-click
// presets for the divider, since dragging a thin splitter handle precisely
// is fiddly.

#include <QHBoxLayout>
#include <QPushButton>
#include <QSplitter>
#include <QWidget>

namespace epochfrom::gui {

// Builds the persistent action bar: `primaryButton` on the left (always
// visible, regardless of where the splitter divider sits), then a stretch,
// then three quick presets that snap `splitter`'s divider. `extraButtons`
// (e.g. a "Fill from Project" or "Cancel" button) are placed right after
// `primaryButton`, still inside the always-visible bar.
inline QWidget *makeActionBar(QSplitter *splitter, QPushButton *primaryButton,
                               std::initializer_list<QWidget *> extraButtons = {})
{
    auto *bar = new QWidget;
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(primaryButton);
    for (QWidget *extra : extraButtons)
        layout->addWidget(extra);
    layout->addStretch(1);

    auto *balancedButton = new QPushButton(QObject::tr("Balanced"));
    auto *moreOutputButton = new QPushButton(QObject::tr("More Output"));
    auto *moreOptionsButton = new QPushButton(QObject::tr("More Options"));
    balancedButton->setToolTip(QObject::tr("Split the options and the output evenly"));
    moreOutputButton->setToolTip(QObject::tr("Give the command output most of the space"));
    moreOptionsButton->setToolTip(QObject::tr("Give the options most of the space"));
    layout->addWidget(balancedButton);
    layout->addWidget(moreOutputButton);
    layout->addWidget(moreOptionsButton);

    QObject::connect(balancedButton, &QPushButton::clicked, splitter,
                      [splitter]() { splitter->setSizes({1, 1}); });
    QObject::connect(moreOutputButton, &QPushButton::clicked, splitter,
                      [splitter]() { splitter->setSizes({1, 5}); });
    QObject::connect(moreOptionsButton, &QPushButton::clicked, splitter,
                      [splitter]() { splitter->setSizes({5, 1}); });
    return bar;
}

} // namespace epochfrom::gui
