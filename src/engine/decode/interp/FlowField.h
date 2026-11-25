#pragma once

#include <QImage>

#include <cstdint>
#include <vector>

namespace drift::interp {

// A dense displacement field, in pixels of the grid it was computed on and oriented the same way
// as the frames it came from.
//
// Pixels of the analysis grid, not normalized UV. The unit has to be stated somewhere or it gets
// guessed, and it gets guessed differently by whoever scales the grid: a field computed at half
// resolution and read as UV is wrong by a factor of two in a way that looks almost right, which
// is the worst kind of wrong to debug.
struct FlowField
{
    int width = 0;
    int height = 0;
    std::vector<float> dx;
    std::vector<float> dy;

    bool isValid() const
    {
        return width > 0 && height > 0 && dx.size() == static_cast<size_t>(width) * height
            && dy.size() == dx.size();
    }

    void resize(int w, int h)
    {
        width = w;
        height = h;
        dx.assign(static_cast<size_t>(w) * h, 0.0f);
        dy.assign(static_cast<size_t>(w) * h, 0.0f);
    }
};

// A scalar grid in 0..255, for the two things that must not be confused with each other.
struct ScalarMap
{
    int width = 0;
    int height = 0;
    std::vector<quint8> value;

    bool isValid() const
    {
        return width > 0 && height > 0
            && value.size() == static_cast<size_t>(width) * height;
    }

    void resize(int w, int h, quint8 fill)
    {
        width = w;
        height = h;
        value.assign(static_cast<size_t>(w) * h, fill);
    }

    quint8 at(int x, int y) const { return value[static_cast<size_t>(y) * width + x]; }
};

// Everything a pair of frames yields, before anything is drawn.
//
// `confidence` and `visibility` are deliberately separate maps and separate ideas, and nothing
// is allowed to collapse one into the other:
//
//   - Confidence is how much the solver believes its own vector. It is low on a blank wall, in
//     a repeating texture, and wherever forward and backward disagree. It *weights a sample*.
//   - Visibility is whether the thing being drawn can be seen in that endpoint at all. A patch
//     of background about to be uncovered by a moving arm is simply not in the earlier frame,
//     however certain the solver is; a blank wall is perfectly visible, however little the
//     solver trusts its vector there.
//
// So low confidence does not mean invisible, and invisible does not mean low confidence. And
// neither of them is opacity: an untrustworthy vector must never turn into a transparent or
// black output pixel. Something was photographed there, and the job is to show the best
// available guess at it and say that it was a guess — not to punch a hole and call it honest.
struct FlowPairAnalysis
{
    FlowField forward;        // A -> B
    FlowField backward;       // B -> A
    ScalarMap confidenceA;
    ScalarMap confidenceB;
    // Visibility, each defined on the grid of the frame it describes -- which is the opposite
    // frame from the one it is a statement about, and worth spelling out because getting it the
    // wrong way round produces a result that looks nearly right:
    //
    //   visibleFromA lives on B's grid: 255 where that part of *B* can also be seen in A, 0
    //   where B is showing something newly uncovered that A never had.
    //   visibleFromB lives on A's grid: 255 where that part of *A* can also be seen in B, 0
    //   where A is showing something that B has covered up.
    //
    // So for a frame being built between them, "can I take this pixel from A" is answered by
    // visibleFromA at the pixel's position in B.
    ScalarMap visibleFromA;
    ScalarMap visibleFromB;
    int analysisWidth = 0;
    int analysisHeight = 0;
    // The frame size the fields were scaled down from, so a caller can convert units without
    // having to remember what it passed in.
    int sourceWidth = 0;
    int sourceHeight = 0;

    bool isValid() const
    {
        return forward.isValid() && backward.isValid() && confidenceA.isValid()
            && confidenceB.isValid() && visibleFromA.isValid() && visibleFromB.isValid();
    }
};

} // namespace drift::interp
