#pragma once

// Small layout helpers shared by every tab (SolveTab/CalibrateTab/DateTab/
// GaiaTab/StarfieldTab), so all five get the same fix for two related
// complaints: (1) each tab used to stack its options and its command-output
// log in one flat column, so growing the log required scrolling past a wall
// of options first, with no way to give the log more room without also
// fighting the wheel-event bug in NoWheelWidgets.h; and (2) the primary
// action button lived inside that same scrollable/splittable column, so it
// could scroll out of view.
//
// The fix: each tab now puts its options (in a QScrollArea) and its output
// pane into a QSplitter, and keeps the primary action button in a small
// bar below the splitter -- always visible -- alongside three one-click
// presets for the divider, since dragging a thin splitter handle precisely
// is fiddly. All five tabs also start out on the same even, "Balanced"
// split -- see applyInitialSplitterRatio() -- so the layout looks and feels
// consistent switching between tabs; the presets are there for whoever
// wants more room for one side or the other.

#include <QHBoxLayout>
#include <QPushButton>
#include <QSplitter>
#include <QTimer>
#include <QWidget>

namespace epochfrom::gui {

// QSplitter::setSizes() does NOT scale a small ratio list (e.g. {1, 5}) up
// to the splitter's actual size -- confirmed empirically: passing tiny
// values whose sum is far below the splitter's real extent snaps the
// stretch-0 widget down near its minimum regardless of which side of the
// ratio was bigger, so {1,5} and {5,1} rendered identically. Sizes that
// actually sum close to the splitter's current extent are honored
// correctly, so this computes the split from the splitter's real size
// (height for the Qt::Vertical splitters every tab uses, width otherwise)
// instead of handing it bare ratios.
inline QList<int> splitterRatioSizes(QSplitter *splitter, int optionsParts, int outputParts)
{
    const int total =
        splitter->orientation() == Qt::Vertical ? splitter->height() : splitter->width();
    const int totalParts = optionsParts + outputParts;
    const int optionsSize = total * optionsParts / totalParts;
    return {optionsSize, total - optionsSize};
}

// Applies an options:output ratio once this splitter has a real, on-screen
// size to divide. Called right after a tab builds its splitter, this can't
// just call splitter->setSizes() immediately -- at that point, before the
// top-level window has ever been shown, the splitter's height is still 0,
// so (per the note on splitterRatioSizes() above) the ratio wouldn't be
// honored and every tab would fall back to whatever Qt's default split is
// for an unshown splitter, which is what previously made some tabs' initial
// layout look different from others despite asking for the same ratio.
// Deferring through a zero-delay singleShot runs this once the event loop
// starts (i.e. after MainWindow::show()/the standalone app's window.show()),
// when the splitter's real size is known.
inline void applyInitialSplitterRatio(QSplitter *splitter, int optionsParts, int outputParts)
{
    QTimer::singleShot(0, splitter, [splitter, optionsParts, outputParts]() {
        if (splitter->orientation() == Qt::Vertical ? splitter->height() <= 0
                                                      : splitter->width() <= 0)
            return;
        splitter->setSizes(splitterRatioSizes(splitter, optionsParts, outputParts));
    });
}

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

    // See splitterRatioSizes() above for why this goes through the
    // splitter's real current size rather than a bare ratio list.
    QObject::connect(balancedButton, &QPushButton::clicked, splitter, [splitter]() {
        splitter->setSizes(splitterRatioSizes(splitter, 1, 1));
    });
    QObject::connect(moreOutputButton, &QPushButton::clicked, splitter, [splitter]() {
        splitter->setSizes(splitterRatioSizes(splitter, 1, 5));
    });
    QObject::connect(moreOptionsButton, &QPushButton::clicked, splitter, [splitter]() {
        splitter->setSizes(splitterRatioSizes(splitter, 5, 1));
    });
    return bar;
}

} // namespace epochfrom::gui
