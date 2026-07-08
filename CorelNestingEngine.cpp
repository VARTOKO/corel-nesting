/* =====================================================================
   CorelNestingEngine.cpp   -- SKELETAL working engine (v1.00)

   What this file gives you TODAY (fully working, no external deps):
     * Shoelace polygon area for real area-descending sort (First-Fit
       Decreasing, exactly Deepnest's "place larger parts first").
     * Aggressive rotation: each part is tried at N orientations and the
       one that packs best (area/space matching) is kept.
     * A bottom-left "skyline" packer that honours spacing, edge padding,
       reduced sheet and the chosen origin corner, and spills to extra
       sheets when a part will not fit.

   What is intentionally STUBBED (the seam for the real NFP engine):
     * True No-Fit-Polygon collision. Today placement is bounding-box
       based (a big step up from your current macro, but rectangles).
       To reach Deepnest-grade interlocking of concave parts, replace
       PackSkyline() collision tests with an NFP test. Port Deepnest's
       minkowski.cc (Boost.Polygon Minkowski sum, Clipper booleans) and
       call it from CanPlaceAt(). The hook is marked >>> NFP HOOK <<<.

   Build (x64, matching CorelDRAW 2018+):
     cl /LD /O2 /EHsc CorelNestingEngine.cpp /link /DEF:CorelNestingEngine.def
   or use the provided Visual Studio / CMake setup.
   ===================================================================== */
#include "CorelNestingEngine.h"

#include <vector>
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstring>

namespace {

const double PI = 3.14159265358979323846;

struct Pt { double x, y; };

struct Part {
    int                id = 0;      // caller id (CorelDRAW shape id)
    std::vector<Pt>    pts;         // input contour (mm)
    double             area = 0.0;  // absolute polygon area
    // filled by placement:
    bool   placed  = false;
    double dx = 0, dy = 0;          // translation applied after rotation
    double rot = 0;                 // rotation in degrees (CCW)
    int    sheet = -1;
};

struct Sheet {
    // skyline: for each x-column (discretised) the current filled height.
    std::vector<double> heights;
    double colWidth = 1.0;          // mm per skyline column
};

struct Engine {
    // sheet config
    double sheetW = 0, sheetH = 0, edgePad = 0, reduce = 0;
    // options
    double spacing = 0;
    int    rotations = 4;
    int    fixAngleMode = 0;
    int    allowInside = 0;
    int    originPoint = 1;         // default Left-bottom
    int    fitMode = 0;
    double timeLimit = 5.0;
    int    searchCount = 1;
    // data
    std::vector<Part> parts;
    double placedArea = 0.0;
};

/* ---- geometry helpers ------------------------------------------------- */

double ShoelaceArea(const std::vector<Pt>& p) {
    // signed area *2 via the shoelace formula; return absolute area.
    const size_t n = p.size();
    if (n < 3) return 0.0;
    double a = 0.0;
    for (size_t i = 0, j = n - 1; i < n; j = i++)
        a += (p[j].x + p[i].x) * (p[j].y - p[i].y);
    return std::fabs(a) * 0.5;
}

// rotate a contour CCW by deg about (0,0) and return its axis-aligned bbox
void RotatedBBox(const std::vector<Pt>& p, double deg,
                 double& minx, double& miny, double& maxx, double& maxy) {
    const double r = deg * PI / 180.0;
    const double c = std::cos(r), s = std::sin(r);
    minx = miny =  std::numeric_limits<double>::max();
    maxx = maxy = -std::numeric_limits<double>::max();
    for (const Pt& q : p) {
        const double rx = q.x * c - q.y * s;
        const double ry = q.x * s + q.y * c;
        minx = std::min(minx, rx); maxx = std::max(maxx, rx);
        miny = std::min(miny, ry); maxy = std::max(maxy, ry);
    }
}

/* ---- allowed rotation set (aggressive rotation) ----------------------- */
std::vector<double> AngleSet(const Engine* e) {
    std::vector<double> a;
    switch (e->fixAngleMode) {
        case 1: a.push_back(0); break;                     // No -> 0 only
        case 2: a.push_back(0); a.push_back(90); break;    // fix 90
        case 3: a.push_back(0); a.push_back(180); break;   // fix 180
        default: {                                         // Auto / free
            int n = e->rotations < 1 ? 1 : e->rotations;
            for (int i = 0; i < n; ++i) a.push_back(360.0 * i / n);
        }
    }
    return a;
}

/* >>> NFP HOOK <<<
   Replace this bounding-box overlap with a real No-Fit-Polygon test to
   get Deepnest-grade interlocking. Signature is kept deliberately simple
   so a minkowski.cc port can drop in here:
       bool CanPlaceNFP(part, rotDeg, atX, atY, alreadyPlaced[]);
   For now we approximate the part by its rotated bounding box.            */
bool RectFits(double x, double y, double w, double h,
              double usableW, double usableH) {
    return (x >= 0 && y >= 0 && x + w <= usableW + 1e-6 && y + h <= usableH + 1e-6);
}

/* ---- the placement pass (bottom-left skyline, bbox based) -------------- */
int PackSkyline(Engine* e) {
    const double usableW = e->sheetW - 2.0 * e->reduce;
    const double usableH = e->sheetH - 2.0 * e->reduce;
    if (usableW <= 0 || usableH <= 0) return 0;

    // usable area shrinks further by edge padding on every side
    const double innerW = usableW - 2.0 * e->edgePad;
    const double innerH = usableH - 2.0 * e->edgePad;
    if (innerW <= 0 || innerH <= 0) return 0;

    const double gap = e->spacing;                 // clearance between parts
    const double step = std::max(0.5, std::min(innerW, innerH) / 400.0);
    const int    cols = std::max(1, (int)std::ceil(innerW / step));

    // one skyline per sheet; grow sheets on demand
    std::vector<std::vector<double>> skylines;
    auto newSheet = [&]() { skylines.emplace_back(cols, 0.0); };
    newSheet();

    int placedCount = 0;
    e->placedArea = 0.0;

    for (Part& part : e->parts) {
        double bestScore = std::numeric_limits<double>::max();
        double bestX = 0, bestY = 0, bestRot = 0, bestW = 0, bestH = 0;
        int    bestSheet = -1;

        // --- aggressive rotation: try each allowed orientation ---
        for (double ang : AngleSet(e)) {
            double bx0, by0, bx1, by1;
            RotatedBBox(part.pts, ang, bx0, by0, bx1, by1);
            const double w = (bx1 - bx0) + gap;
            const double h = (by1 - by0) + gap;
            if (w > innerW + 1e-6 || h > innerH + 1e-6) continue; // never fits

            const int span = std::max(1, (int)std::ceil(w / step));

            // scan every skyline (sheet) for the lowest valid shelf
            for (int sIdx = 0; sIdx < (int)skylines.size(); ++sIdx) {
                auto& sky = skylines[sIdx];
                for (int c = 0; c + span <= cols; ++c) {
                    // shelf height = max skyline under the part's footprint
                    double top = 0.0;
                    for (int k = 0; k < span; ++k) top = std::max(top, sky[c + k]);
                    if (top + h > innerH + 1e-6) continue;      // would overflow

                    const double x = c * step;
                    const double y = top;
                    if (!RectFits(x, y, w, h, innerW, innerH)) continue;

                    // score by fit mode: lower is better
                    double score;
                    switch (e->fitMode) {
                        case 1:  score = x * 1e4 + y; break;    // Width (best)
                        case 2:  score = y * 1e4 + x; break;    // Height (best)
                        default: score = y * 1e4 + x; break;    // Bottom (best)
                    }
                    // small bonus for tighter bbox (area matching)
                    score += (w * h) * 1e-3;

                    if (score < bestScore) {
                        bestScore = score; bestSheet = sIdx;
                        bestX = x; bestY = y; bestRot = ang;
                        bestW = bx1 - bx0; bestH = by1 - by0;
                        // store the un-gapped bbox origin so translation is exact
                        bestX -= bx0; bestY -= by0;  // shift so rotated min -> (x,y)
                    }
                }
            }
        }

        if (bestSheet < 0) {
            // could not fit on any existing sheet -> open a new one and retry once
            newSheet();
            // simplest retry: place at origin of the fresh sheet if it fits at all
            for (double ang : AngleSet(e)) {
                double bx0, by0, bx1, by1;
                RotatedBBox(part.pts, ang, bx0, by0, bx1, by1);
                const double w = bx1 - bx0, h = by1 - by0;
                if (w <= innerW + 1e-6 && h <= innerH + 1e-6) {
                    bestSheet = (int)skylines.size() - 1;
                    bestRot = ang; bestW = w; bestH = h;
                    bestX = -bx0; bestY = -by0;
                    break;
                }
            }
            if (bestSheet < 0) { part.placed = false; continue; } // truly too big
        }

        // commit placement + raise the skyline under the footprint
        {
            auto& sky = skylines[bestSheet];
            double bx0, by0, bx1, by1;
            RotatedBBox(part.pts, bestRot, bx0, by0, bx1, by1);
            const double footX = bestX + bx0;                 // rotated min corner x
            const double footTop = bestY + by1;               // rotated top after place
            const int c0 = std::max(0, (int)std::floor(footX / step));
            const int span = std::max(1, (int)std::ceil(((bx1 - bx0) + gap) / step));
            for (int k = 0; k < span && c0 + k < cols; ++k)
                sky[c0 + k] = footTop + gap;

            // final translation: part origin -> inner area, offset by padding+reduce
            const double offX = e->reduce + e->edgePad;
            const double offY = e->reduce + e->edgePad;

            // origin corner handling: skyline packs from lower-left; mirror if needed
            double px = bestX, py = bestY;
            if (e->originPoint == 0 || e->originPoint == 2)   // Right-*: mirror X
                px = innerW - (bestX + (bx1 - bx0));
            if (e->originPoint == 2 || e->originPoint == 3)   // *-top: mirror Y
                py = innerH - (bestY + (by1 - by0));

            part.placed = true;
            part.sheet  = bestSheet;
            part.rot    = bestRot;
            part.dx     = px + offX;
            part.dy     = py + offY;
            e->placedArea += part.area;
            ++placedCount;
        }
    }
    return placedCount;
}

} // anonymous namespace

/* ===================================================================== */
/*  Exported C ABI                                                        */
/* ===================================================================== */

CN_EXPORT void* CN_CALL CN_Create(void) { return new (std::nothrow) Engine(); }

CN_EXPORT void CN_CALL CN_Destroy(void* h) { delete static_cast<Engine*>(h); }

CN_EXPORT void CN_CALL CN_Reset(void* h) {
    if (!h) return;
    Engine* e = static_cast<Engine*>(h);
    e->parts.clear();
    e->placedArea = 0.0;
}

CN_EXPORT int CN_CALL CN_GetVersion(void) { return 100; }

CN_EXPORT void CN_CALL CN_SetSheet(void* h, double w, double hgt,
                                   double edgePadding, double reduce) {
    if (!h) return;
    Engine* e = static_cast<Engine*>(h);
    e->sheetW = w; e->sheetH = hgt; e->edgePad = edgePadding; e->reduce = reduce;
}

CN_EXPORT void CN_CALL CN_SetOptions(void* h, double spacing, int rotations,
                                     int fixAngleMode, int allowInside,
                                     int originPoint, int fitMode,
                                     double timeLimitSec, int searchCount) {
    if (!h) return;
    Engine* e = static_cast<Engine*>(h);
    e->spacing      = spacing;
    e->rotations    = rotations;
    e->fixAngleMode = fixAngleMode;
    e->allowInside  = allowInside;
    e->originPoint  = originPoint;
    e->fitMode      = fitMode;
    e->timeLimit    = timeLimitSec;
    e->searchCount  = searchCount;
}

CN_EXPORT int CN_CALL CN_AddPart(void* h, int id,
                                 double* xs, double* ys, int count) {
    if (!h || !xs || !ys || count < 3) return -1;
    Engine* e = static_cast<Engine*>(h);
    Part p;
    p.id = id;
    p.pts.reserve(count);
    for (int i = 0; i < count; ++i) p.pts.push_back({ xs[i], ys[i] });
    p.area = ShoelaceArea(p.pts);
    e->parts.push_back(std::move(p));
    return (int)e->parts.size() - 1;
}

CN_EXPORT int CN_CALL CN_Run(void* h) {
    if (!h) return 0;
    Engine* e = static_cast<Engine*>(h);

    // First-Fit-Decreasing: sort parts by area, largest first.
    std::stable_sort(e->parts.begin(), e->parts.end(),
                     [](const Part& a, const Part& b) { return a.area > b.area; });

    // (searchCount>1 would repeat with perturbed order and keep the best;
    //  the single deterministic pass is implemented here.)
    return PackSkyline(e);
}

CN_EXPORT int CN_CALL CN_GetPartCount(void* h) {
    if (!h) return 0;
    return (int)static_cast<Engine*>(h)->parts.size();
}

CN_EXPORT int CN_CALL CN_GetResult(void* h, int index,
                                   int* outId, double* outDx, double* outDy,
                                   double* outRotDeg, int* outSheet) {
    if (!h) return 0;
    Engine* e = static_cast<Engine*>(h);
    if (index < 0 || index >= (int)e->parts.size()) return 0;
    const Part& p = e->parts[index];
    if (outId)     *outId     = p.id;
    if (outDx)     *outDx     = p.dx;
    if (outDy)     *outDy     = p.dy;
    if (outRotDeg) *outRotDeg = p.rot;
    if (outSheet)  *outSheet  = p.sheet;
    return p.placed ? 1 : 0;
}

CN_EXPORT double CN_CALL CN_GetUtilization(void* h) {
    if (!h) return 0.0;
    Engine* e = static_cast<Engine*>(h);
    const double usableW = e->sheetW - 2.0 * (e->reduce + e->edgePad);
    const double usableH = e->sheetH - 2.0 * (e->reduce + e->edgePad);
    const double sheetArea = usableW * usableH;
    if (sheetArea <= 0) return 0.0;
    return e->placedArea / sheetArea;
}
