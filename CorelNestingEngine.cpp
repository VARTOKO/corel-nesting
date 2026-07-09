/* =====================================================================
   CorelNestingEngine.cpp   (v2)  -- correct bounding-box nester.

   Guarantees (unit-tested in the test harness):
     * No two placed parts overlap (explicit rect overlap tests).
     * Every placed part lies fully inside the padded/reduced sheet.
     * `spacing` (minimum distance) is kept between parts.
     * A part larger than the usable sheet in every allowed rotation is
       reported NOT placed -> VBA leaves it exactly where it is.
     * Divide-by-color (mode 1) never mixes groups on one sheet.

   Placement = First-Fit-Decreasing + Bottom-Left drop. Parts are sorted
   largest-area first (Deepnest's heuristic). For each part we try each
   allowed rotation, generate candidate X positions from the left/right
   edges of already-placed parts, drop each candidate straight down to
   the lowest non-overlapping Y, and keep the best by the fit rule.

   This is bounding-box based (rectangles). The seam for true polygon
   NFP is still CanPlaceAt() -> replace the rect overlap test with an
   NFP/Minkowski test (port of Deepnest's minkowski.cc) in Stage 2.
   ===================================================================== */
#include "CorelNestingEngine.h"

#include <vector>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

const double PI  = 3.14159265358979323846;
const double EPS = 1e-6;

struct Pt   { double x, y; };
struct Rect { double x, y, w, h; };   // lower-left + size (sheet-local mm)

struct Part {
    int    id = 0, group = 0;
    std::vector<Pt> pts;
    double area = 0.0;
    // result
    bool   placed = false;
    double dx = 0, dy = 0, rot = 0;
    int    sheet = -1;
};

struct Sheet {
    int  group = -1;             // -1 = accepts any (mode 0)
    std::vector<Rect> rects;     // occupied footprints (already inflated by spacing)
};

struct Engine {
    double sheetW = 0, sheetH = 0, edgePad = 0, reduce = 0;
    double spacing = 0;
    int    rotations = 4, fixAngleMode = 0, allowInside = 0;
    int    originPoint = 1, fitMode = 0, divideMode = 0;
    double timeLimit = 5.0;
    int    searchCount = 1;
    std::vector<Part>  parts;
    std::vector<Sheet> sheets;
};

double ShoelaceArea(const std::vector<Pt>& p) {
    const size_t n = p.size();
    if (n < 3) return 0.0;
    double a = 0.0;
    for (size_t i = 0, j = n - 1; i < n; j = i++)
        a += (p[j].x + p[i].x) * (p[j].y - p[i].y);
    return std::fabs(a) * 0.5;
}

void RotatedBBox(const std::vector<Pt>& p, double deg,
                 double& minx, double& miny, double& maxx, double& maxy) {
    const double r = deg * PI / 180.0, c = std::cos(r), s = std::sin(r);
    minx = miny =  std::numeric_limits<double>::max();
    maxx = maxy = -std::numeric_limits<double>::max();
    for (const Pt& q : p) {
        const double rx = q.x * c - q.y * s;
        const double ry = q.x * s + q.y * c;
        minx = std::min(minx, rx); maxx = std::max(maxx, rx);
        miny = std::min(miny, ry); maxy = std::max(maxy, ry);
    }
}

std::vector<double> AngleSet(const Engine* e) {
    std::vector<double> a;
    switch (e->fixAngleMode) {
        case 1: a.push_back(0); break;                    // No -> 0 only
        case 2: a.push_back(0); a.push_back(90); break;   // fix 90
        case 3: a.push_back(0); a.push_back(180); break;  // fix 180
        default: {                                        // Auto / free
            int n = e->rotations < 1 ? 1 : e->rotations;
            for (int i = 0; i < n; ++i) a.push_back(360.0 * i / n);
        }
    }
    return a;
}

inline bool Overlap(const Rect& a, const Rect& b) {
    return a.x < b.x + b.w - EPS && b.x < a.x + a.w - EPS &&
           a.y < b.y + b.h - EPS && b.y < a.y + a.h - EPS;
}

// Bottom-left drop: lowest y>=0 where [x,x+w]x[y,y+h] hits nothing and
// stays within usableH. Returns false if it cannot fit at this x.
bool DropY(const std::vector<Rect>& placed, double x, double w, double h,
           double usableW, double usableH, double& outY) {
    if (x < -EPS || x + w > usableW + EPS) return false;
    double y = 0.0;
    bool moved = true;
    int guard = 0;
    while (moved && guard++ < 100000) {
        moved = false;
        if (y + h > usableH + EPS) return false;
        Rect cand{ x, y, w, h };
        for (const Rect& r : placed) {
            if (Overlap(cand, r)) { y = r.y + r.h; moved = true; break; }
        }
    }
    if (y + h > usableH + EPS) return false;
    outY = y;
    return true;
}

// Try to place footprint (w,h) on one sheet. Returns best (x,y) by fit rule.
bool PlaceOnSheet(const Engine* e, const std::vector<Rect>& placed,
                  double w, double h, double usableW, double usableH,
                  double& bx, double& by) {
    // candidate X = 0 plus left & right edges of every placed rect
    std::vector<double> xs;
    xs.push_back(0.0);
    for (const Rect& r : placed) { xs.push_back(r.x); xs.push_back(r.x + r.w); }

    bool found = false;
    double best = std::numeric_limits<double>::max();
    for (double x : xs) {
        if (x < -EPS || x + w > usableW + EPS) continue;
        double y;
        if (!DropY(placed, x, w, h, usableW, usableH, y)) continue;
        double score;
        switch (e->fitMode) {
            case 1:  score = x * 1e6 + y; break;   // Width (best): fill across
            default: score = y * 1e6 + x; break;   // Bottom/Height: fill up
        }
        if (score < best) { best = score; bx = x; by = y; found = true; }
    }
    return found;
}

} // namespace

/* ===================================================================== */
/*  Core run                                                              */
/* ===================================================================== */
static int RunImpl(Engine* e) {
    e->sheets.clear();
    for (Part& p : e->parts) { p.placed = false; p.sheet = -1; }

    const double inset   = e->reduce + e->edgePad;      // hard inner margin
    const double usableW = e->sheetW - 2.0 * inset;
    const double usableH = e->sheetH - 2.0 * inset;
    if (usableW <= 0 || usableH <= 0) return 0;

    const double gap = std::max(0.0, e->spacing);
    // modes that force each group onto its own sheet(s):
    //   1 = On different sheet (by color) , 4 = Divide by layer
    const bool   splitGroups = (e->divideMode == 1 || e->divideMode == 4);

    // order: (group if splitting) then area descending (FFD)
    std::vector<int> order(e->parts.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        if (e->divideMode != 0 && e->parts[a].group != e->parts[b].group)
            return e->parts[a].group < e->parts[b].group;
        return e->parts[a].area > e->parts[b].area;
    });

    int placedCount = 0;
    for (int idx : order) {
        Part& part = e->parts[idx];

        double bestScore = std::numeric_limits<double>::max();
        double bX = 0, bY = 0, bRot = 0, bMinx = 0, bMiny = 0, bW = 0, bH = 0;
        int    bSheet = -1;

        // try each allowed rotation
        for (double ang : AngleSet(e)) {
            double x0, y0, x1, y1;
            RotatedBBox(part.pts, ang, x0, y0, x1, y1);
            const double w = (x1 - x0) + gap;    // footprint incl. spacing
            const double h = (y1 - y0) + gap;
            if (w > usableW + EPS || h > usableH + EPS) continue;  // too big

            // scan existing compatible sheets (first-fit), then a new one
            for (int si = 0; si <= (int)e->sheets.size(); ++si) {
                bool isNew = (si == (int)e->sheets.size());
                std::vector<Rect> empty;
                const std::vector<Rect>* placed;
                if (isNew) {
                    placed = &empty;
                } else {
                    if (splitGroups && e->sheets[si].group != part.group) continue;
                    placed = &e->sheets[si].rects;
                }
                double px, py;
                if (!PlaceOnSheet(e, *placed, w, h, usableW, usableH, px, py))
                    continue;
                // prefer earliest sheet, then fit score
                double score = si * 1e12 +
                               ((e->fitMode == 1) ? (px * 1e6 + py)
                                                  : (py * 1e6 + px));
                if (score < bestScore) {
                    bestScore = score; bSheet = si;
                    bX = px; bY = py; bRot = ang;
                    bMinx = x0; bMiny = y0; bW = (x1 - x0); bH = (y1 - y0);
                }
                // if it fit on an existing sheet at a good spot, no need to
                // keep opening new sheets for this rotation
                if (!isNew) break;
            }
        }

        if (bSheet < 0) { part.placed = false; continue; }   // leave in place

        if (bSheet == (int)e->sheets.size()) {
            Sheet s; s.group = part.group; e->sheets.push_back(s);
        }
        // commit occupied footprint (inflated by spacing)
        e->sheets[bSheet].rects.push_back(Rect{ bX, bY, bW + gap, bH + gap });

        // dx/dy = full sheet-local position of the rotated min corner
        part.placed = true;
        part.sheet  = bSheet;
        part.rot    = bRot;
        part.dx     = inset + bX;      // + inset keeps the padding visible
        part.dy     = inset + bY;
        ++placedCount;
    }
    return placedCount;
}

/* ===================================================================== */
/*  Exported C ABI                                                        */
/* ===================================================================== */
CN_EXPORT void* CN_CALL CN_Create(void) { return new (std::nothrow) Engine(); }
CN_EXPORT void  CN_CALL CN_Destroy(void* h) { delete static_cast<Engine*>(h); }
CN_EXPORT void  CN_CALL CN_Reset(void* h) {
    if (!h) return;
    Engine* e = static_cast<Engine*>(h);
    e->parts.clear(); e->sheets.clear();
}
CN_EXPORT int CN_CALL CN_GetVersion(void) { return 200; }

CN_EXPORT void CN_CALL CN_SetSheet(void* h, double w, double hgt,
                                   double edgePadding, double reduce) {
    if (!h) return;
    Engine* e = static_cast<Engine*>(h);
    e->sheetW = w; e->sheetH = hgt; e->edgePad = edgePadding; e->reduce = reduce;
}

CN_EXPORT void CN_CALL CN_SetOptions(void* h, double spacing, int rotations,
                                     int fixAngleMode, int allowInside,
                                     int originPoint, int fitMode,
                                     int divideMode,
                                     double timeLimitSec, int searchCount) {
    if (!h) return;
    Engine* e = static_cast<Engine*>(h);
    e->spacing = spacing; e->rotations = rotations;
    e->fixAngleMode = fixAngleMode; e->allowInside = allowInside;
    e->originPoint = originPoint; e->fitMode = fitMode;
    e->divideMode = divideMode;
    e->timeLimit = timeLimitSec; e->searchCount = searchCount;
}

CN_EXPORT int CN_CALL CN_AddPart(void* h, int id, int group,
                                 double* xs, double* ys, int count) {
    if (!h || !xs || !ys || count < 3) return -1;
    Engine* e = static_cast<Engine*>(h);
    Part p; p.id = id; p.group = group;
    p.pts.reserve(count);
    for (int i = 0; i < count; ++i) p.pts.push_back({ xs[i], ys[i] });
    p.area = ShoelaceArea(p.pts);
    e->parts.push_back(std::move(p));
    return (int)e->parts.size() - 1;
}

CN_EXPORT int    CN_CALL CN_Run(void* h) { return h ? RunImpl(static_cast<Engine*>(h)) : 0; }
CN_EXPORT int    CN_CALL CN_GetPartCount(void* h) { return h ? (int)static_cast<Engine*>(h)->parts.size() : 0; }
CN_EXPORT int    CN_CALL CN_GetSheetCount(void* h) { return h ? (int)static_cast<Engine*>(h)->sheets.size() : 0; }

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
    const int sheets = (int)e->sheets.size();
    if (sheets <= 0) return 0.0;
    const double inset = e->reduce + e->edgePad;
    const double usable = (e->sheetW - 2 * inset) * (e->sheetH - 2 * inset);
    if (usable <= 0) return 0.0;
    double used = 0.0;
    for (const Part& p : e->parts) if (p.placed) used += p.area;
    return used / (usable * sheets);
}
