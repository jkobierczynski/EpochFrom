#pragma once

#include "PlateSolver.h"

#include <QMetaType>
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
        : QObject(parent), request_(std::move(request))
    {
        // Registered here (constructor still runs on the GUI thread, before
        // moveToThread()/start()) so PlateSolveResult is a known QMetaType
        // before run() ever emits singleSolveReady() across the thread
        // boundary.
        qRegisterMetaType<epochfrom::PlateSolveResult>("epochfrom::PlateSolveResult");
    }

public slots:
    // Entry point once this object has been moved to its worker thread.
    void run();

signals:
    // One or more lines of human-readable progress/report text -- appended
    // verbatim to the GUI's log pane.
    void logLine(const QString &text);
    // Single-image mode only, emitted just before finished(true): the full
    // solve result, so the tab can prefill a pointing hint + pixel-scale
    // bounds for the directory batch that (usually) follows -- see
    // SolveTab::onSingleSolveReady(). Emitted for both a fresh solve-field
    // run and a --wcs-only read, since either way it's a real answer for
    // "where is this session pointed."
    void singleSolveReady(epochfrom::PlateSolveResult result);
    // Emitted once, when the job is done (successfully or not).
    void finished(bool ok);

private:
    Request request_;
};

} // namespace epochfrom::gui
