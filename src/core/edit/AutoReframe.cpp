#include "core/edit/AutoReframe.h"

#include <QtGlobal>

#include <algorithm>

namespace drift::reframe {

QList<FaceSample> smoothSamples(const QList<FaceSample> &samples, int radius)
{
    QList<FaceSample> smoothed = samples;
    for (int i = 0; i < samples.size(); ++i) {
        double cx = 0, cy = 0, n = 0;
        for (int j = qMax(0, i - radius); j <= qMin(int(samples.size()) - 1, i + radius); ++j) {
            cx += samples.at(j).cx;
            cy += samples.at(j).cy;
            n += 1;
        }
        smoothed[i].cx = cx / n;
        smoothed[i].cy = cy / n;
    }
    return smoothed;
}

Result reframeKeys(const QList<FaceSample> &samples, double canvasW, double canvasH, double sourceAspect,
                   double targetAspect, int sourcePixelWidth)
{
    Result out;
    if (canvasW <= 0 || canvasH <= 0 || sourceAspect <= 0 || targetAspect <= 0)
        return out;

    // Where the target-aspect window lands on the canvas. When the canvas already is that aspect
    // this is the whole canvas and the crop fills it. When it is not — a 9:16 crop asked for on a
    // 16:9 timeline — the window is fitted inside instead. Filling the canvas in that case would
    // mean stretching the picture, and a reframe that distorts the subject has not reframed anything.
    const double frameH = qMin(canvasH, canvasW / targetAspect);
    const double frameW = frameH * targetAspect;
    const double frameX = (canvasW - frameW) * 0.5;
    const double frameY = (canvasH - frameH) * 0.5;

    for (const FaceSample &s : samples) {
        // Crop window of targetAspect centred on the face, in normalised source coords. The two
        // axes have different lengths in pixels, so the source's own aspect is divided out to make
        // the window the shape asked for.
        double cropH = qMin(1.0, qMax(s.ry * 2.4, 0.35));
        double cropW = cropH * targetAspect / sourceAspect;
        if (cropW > 1.0) {
            cropW = 1.0;
            cropH = qMin(1.0, cropW * sourceAspect / targetAspect);
        }
        const double cropX = qBound(0.0, s.cx - cropW * 0.5, 1.0 - cropW);
        const double cropY = qBound(0.0, s.cy - cropH * 0.5, 1.0 - cropH);
        // The whole source, scaled so that its crop window covers exactly the framed area. w/h
        // comes out at the source's own aspect, so nothing is stretched.
        Key key;
        key.t = s.t;
        key.width = frameW / cropW;
        key.height = frameH / cropH;
        key.x = frameX - cropX * key.width;
        key.y = frameY - cropY * key.height;
        if (sourcePixelWidth > 0)
            out.tightestScale = qMax(out.tightestScale, frameW / (cropW * sourcePixelWidth));
        out.keys.append(key);
    }
    return out;
}

QList<FaceSample> fillFaceGaps(const QList<FaceSample> &found, double step)
{
    QList<FaceSample> out;
    out.reserve(found.size());
    int firstSeen = -1;
    for (int i = 0; i < found.size(); ++i) {
        if (found.at(i).t >= 0) {
            firstSeen = i;
            break;
        }
    }
    FaceSample last;
    bool haveLast = false;
    for (int i = 0; i < found.size(); ++i) {
        const double t = i * step;
        FaceSample s;
        if (found.at(i).t >= 0) {
            s = found.at(i);
            last = s;
            haveLast = true;
        } else if (haveLast) {
            s = last;
        } else if (firstSeen >= 0) {
            s = found.at(firstSeen);
        } else {
            s = centreSample(t);
        }
        s.t = t;
        out.append(s);
    }
    return out;
}

} // namespace drift::reframe
