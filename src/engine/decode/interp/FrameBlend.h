#pragma once

#include <QImage>

namespace drift::interp {

// Mix two frames at alpha: 0 gives the first, 1 the second.
//
// Done in linear light and premultiplied by alpha, which matters in two visible ways. Mixing
// transfer-encoded values directly darkens every midtone the mix touches, so a blended frame
// does not sit between its neighbours in brightness and a slowed shot pulses. And a fully
// transparent pixel carries whatever RGB happened to be in the buffer, so mixing it straight
// drags that colour into the edge of anything with an alpha channel — ProRes 4444 titles being
// the obvious case here. Premultiplying makes a transparent pixel contribute nothing, which is
// the only thing it should contribute.
//
// The transfer function assumed is sRGB, close enough to BT.709 for the 8-bit SDR frames the
// compositor works in. PQ and HLG are *not* handled and must not be claimed: Dafin's colour and
// HDR precision work is unvalidated, and this function is not where that gets decided. It is
// exact at alpha 0 and 1 — it returns the frame itself, not a round-trip of it — so turning
// blending on cannot change a frame that was not going to be mixed anyway.
//
// Returns a null image when the two frames cannot be mixed (either is null, or they differ in
// size), so the caller repeats the first frame instead of showing something half-built. A size
// change mid-clip is not supposed to happen, since the decode size is held stable precisely so
// the frame cache survives, but it is not worth a crash to assume so.
QImage blendFrames(const QImage &a, const QImage &b, double alpha);

} // namespace drift::interp
