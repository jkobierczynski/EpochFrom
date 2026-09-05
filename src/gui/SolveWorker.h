#pragma once

#include "PlateSolver.h"

#include <QObject>
#include <QString>

namespace epochfrom::gui {

// Runs one `solve` job (single image, or a whole --dir batch) on a worker
// thread so the GUI's event loop -- and its Cancel button -- stay responsive
// while solve-field is blocking. Mirrors runSolveOne/runSolveDir in
// src/cli/main.cpp closely enough that the two shouldn't drift apart in
// behavior, just in how progress is delivered (signals instead of a
// QTextStream written straight to stdout).
class SolveWorker : public QObject {
    Q_OBJECT
public:
    struct Request {
        bool isDir = false;
        QString path; // single image (or .wcs with wcsOnly), or a directory
        bool wcsOnly = false; // single-image mode only
        bool force = false;   // dir mode only: re-solve files with an existing .wcs
        epochfrom::PlateSolveOptions options;
    };

    explicit SolveWorker(Request request, QObject *parent = nullptr)
        : QObject(parent), request_(std::move(request)) {}

public slots:
    // Entry point once this object has been moved to its worker thread.
    void run();

signals:
    // One or more lines of human-readable progress/report text -- appended
    // verbatim to the GUI's log pane.
    void logLine(const QString &text);
    // Emitted once, when the job is done (successfully or not).
    void finished(bool ok);

private:
    Request request_;
};

} // namespace epochfrom::gui
