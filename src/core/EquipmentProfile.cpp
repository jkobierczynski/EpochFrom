#include "EquipmentProfile.h"
#include "SipPolynomial.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVector>

namespace epochfrom {

PolyCorrectionMas EquipmentProfile::evaluateCorrectionMas(double uPx, double vPx) const
{
    PolyCorrectionMas out;
    if (pixelScaleNorm == 0.0)
        return out;
    const double un = uPx / pixelScaleNorm;
    const double vn = vPx / pixelScaleNorm;

    QVector<double> terms;
    terms.reserve(sipPolyTermCount(polyOrderChosen));
    sipPolyTermsInto(un, vn, polyOrderChosen, terms);

    if (terms.size() != polyCoeffsXi.size() || terms.size() != polyCoeffsEta.size())
        return out; // malformed/mismatched profile -- fail closed (no correction) rather than crash

    double dxi = 0.0, deta = 0.0;
    for (int i = 0; i < terms.size(); ++i) {
        dxi += terms[i] * polyCoeffsXi[i];
        deta += terms[i] * polyCoeffsEta[i];
    }
    out.dxiMas = dxi;
    out.detaMas = deta;
    return out;
}

namespace {

QJsonArray toJsonArray(const QVector<double> &v)
{
    QJsonArray arr;
    for (double x : v)
        arr.append(x);
    return arr;
}

QVector<double> fromJsonArray(const QJsonArray &arr)
{
    QVector<double> v;
    v.reserve(arr.size());
    for (const QJsonValue &val : arr)
        v.push_back(val.toDouble());
    return v;
}

} // namespace

bool EquipmentProfile::saveToFile(const EquipmentProfile &p, const QString &path,
                                   QString *errorMessage)
{
    QJsonObject o;
    o["label"] = p.label;
    o["telescopeApertureMm"] = p.telescopeApertureMm;
    o["focalLengthMm"] = p.focalLengthMm;
    o["correctorType"] = p.correctorType;
    o["cameraModel"] = p.cameraModel;
    o["pixelSizeUm"] = p.pixelSizeUm;
    o["calibrationFilter"] = p.calibrationFilter;
    o["validFrom"] = p.validFrom;
    o["validTo"] = p.validTo;

    o["nSubsUsed"] = p.nSubsUsed;
    o["referenceCatalogDescription"] = p.referenceCatalogDescription;
    o["nStarsMatched"] = p.nStarsMatched;
    o["detectionThresholdSigma"] = p.detectionThresholdSigma;

    o["crpix1"] = p.crpix1;
    o["crpix2"] = p.crpix2;
    o["pixelScaleNorm"] = p.pixelScaleNorm;
    o["polyOrderChosen"] = p.polyOrderChosen;
    o["polyCoeffsXi"] = toJsonArray(p.polyCoeffsXi);
    o["polyCoeffsEta"] = toJsonArray(p.polyCoeffsEta);

    o["rmsBeforeMas"] = p.rmsBeforeMas;
    o["rmsAfterInSampleMas"] = p.rmsAfterInSampleMas;
    o["rmsAfterHeldoutMas"] = p.rmsAfterHeldoutMas;
    o["rmsAfterHeldoutStdMas"] = p.rmsAfterHeldoutStdMas;
    o["internalRepeatabilityMedianMas"] = p.internalRepeatabilityMedianMas;
    o["internalRepeatabilityRmsMas"] = p.internalRepeatabilityRmsMas;

    o["chromaticCorrectorWarningShown"] = p.chromaticCorrectorWarningShown;
    o["limitingFactor"] = p.limitingFactor;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("could not open %1 for writing: %2")
                                 .arg(path, file.errorString());
        return false;
    }
    file.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return true;
}

bool EquipmentProfile::loadFromFile(const QString &path, EquipmentProfile *out,
                                     QString *errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage)
            *errorMessage =
                QStringLiteral("could not open %1 for reading: %2").arg(path, file.errorString());
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("invalid profile JSON: %1").arg(parseError.errorString());
        return false;
    }
    const QJsonObject o = doc.object();

    EquipmentProfile p;
    p.label = o["label"].toString();
    p.telescopeApertureMm = o["telescopeApertureMm"].toDouble();
    p.focalLengthMm = o["focalLengthMm"].toDouble();
    p.correctorType = o["correctorType"].toString();
    p.cameraModel = o["cameraModel"].toString();
    p.pixelSizeUm = o["pixelSizeUm"].toDouble();
    p.calibrationFilter = o["calibrationFilter"].toString();
    p.validFrom = o["validFrom"].toString();
    p.validTo = o["validTo"].toString();

    p.nSubsUsed = o["nSubsUsed"].toInt();
    p.referenceCatalogDescription = o["referenceCatalogDescription"].toString();
    p.nStarsMatched = o["nStarsMatched"].toInt();
    p.detectionThresholdSigma = o["detectionThresholdSigma"].toDouble();

    p.crpix1 = o["crpix1"].toDouble();
    p.crpix2 = o["crpix2"].toDouble();
    p.pixelScaleNorm = o["pixelScaleNorm"].toDouble();
    p.polyOrderChosen = o["polyOrderChosen"].toInt();
    p.polyCoeffsXi = fromJsonArray(o["polyCoeffsXi"].toArray());
    p.polyCoeffsEta = fromJsonArray(o["polyCoeffsEta"].toArray());

    p.rmsBeforeMas = o["rmsBeforeMas"].toDouble();
    p.rmsAfterInSampleMas = o["rmsAfterInSampleMas"].toDouble();
    p.rmsAfterHeldoutMas = o["rmsAfterHeldoutMas"].toDouble();
    p.rmsAfterHeldoutStdMas = o["rmsAfterHeldoutStdMas"].toDouble();
    p.internalRepeatabilityMedianMas = o["internalRepeatabilityMedianMas"].toDouble(
        std::numeric_limits<double>::quiet_NaN());
    p.internalRepeatabilityRmsMas =
        o["internalRepeatabilityRmsMas"].toDouble(std::numeric_limits<double>::quiet_NaN());

    p.chromaticCorrectorWarningShown = o["chromaticCorrectorWarningShown"].toBool();
    p.limitingFactor = o["limitingFactor"].toString();

    *out = p;
    return true;
}

} // namespace epochfrom
