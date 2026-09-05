#include "SolveWorker.h"
#include "FitsDirScan.h"
#include "ReportFormatting.h"

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QTextStream>

using namespace epochfrom;

namespace epochfrom::gui {

void SolveWorker::run()
{
    if (!request_.isDir) {
        const PlateSolveResult result = request_.wcsOnly
                                             ? PlateSolver::readWcsFile(request_.path)
                                             : PlateSolver::solve(request_.path, request_.options);
        QString text;
        QTextStream out(&text);
        if (!result.solved) {
            out << "error: " << result.errorMessage << "\n";
            emit logLine(text);
            emit finished(false);
            return;
        }
        printSolveResult(result, out);
        emit logLine(text);
        emit finished(true);
        return;
    }

    const QDir dir(request_.path);
    if (!dir.exists()) {
        emit logLine(QStringLiteral("error: directory %1 does not exist\n").arg(dir.path()));
        emit finished(false);
        return;
    }
    const QStringList fitsFiles = listFitsFiles(dir);
    if (fitsFiles.isEmpty()) {
        emit logLine(QStringLiteral("error: no .fits/.fit/.fts files found in %1\n").arg(dir.path()));
        emit finished(false);
        return;
    }

    int solved = 0, failed = 0, skipped = 0;
    for (const QString &fitsName : fitsFiles) {
        const QFileInfo fi(dir.filePath(fitsName));
        const QString wcsPath = wcsSidecarPath(fi);
        QString line;
        QTextStream out(&line);
        out << fitsName << ": ";
        if (!request_.force && QFileInfo::exists(wcsPath)) {
            out << "already solved, skipping (enable \"re-solve already-solved files\" to redo)\n";
            ++skipped;
            emit logLine(line);
            continue;
        }
        const PlateSolveResult result = PlateSolver::solve(fi.filePath(), request_.options);
        if (!result.solved) {
            out << "FAILED -- " << result.errorMessage.section('\n', 0, 0) << "\n";
            ++failed;
            emit logLine(line);
            continue;
        }
        out << QString("RA %1  Dec %2  field %3 x %4 arcmin  scale %5\"/px\n")
                   .arg(result.centerRaDeg, 0, 'f', 4)
                   .arg(result.centerDecDeg, 0, 'f', 4)
                   .arg(result.fieldWidthArcmin, 0, 'f', 1)
                   .arg(result.fieldHeightArcmin, 0, 'f', 1)
                   .arg(result.pixelScaleArcsecPerPix, 0, 'f', 3);
        ++solved;
        emit logLine(line);
    }

    emit logLine(QString("\n%1 solved, %2 already solved (skipped), %3 failed, out of %4 file(s)\n")
                     .arg(solved)
                     .arg(skipped)
                     .arg(failed)
                     .arg(fitsFiles.size()));
    emit finished((solved + skipped) > 0);
}

} // namespace epochfrom::gui
