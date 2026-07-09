/* =====================================================================
   CorelNestingEngine.cpp   (v3)  -- TRUE No-Fit-Polygon nester.

   Stage 2: real polygon interlocking (not bounding boxes), using the
   same No-Fit-Polygon (NFP) idea as Deepnest, but built on Clipper2:

     NFP(A,B)  = the set of translations t of part B for which (B+t)
                 touches/penetrates fixed part A.  Computed directly as
                 MinkowskiDiff(B, A) = A (+) (-B)   [Clipper2].
     IFP       = inner-fit rectangle: translations of B that keep it
                 inside the usable sheet (sheet minus reduce+edgePadding).
     feasible  = IFP  -  Union( NFP(placed_i, B) inflated by `spacing` )
     place B   = the vertex of `feasible` chosen by the fit/origin rule
                 (bottom-left / gravity). Vertices on the NFP boundary
                 are exact touching positions -> tight, non-overlapping
                 nesting that also fills concavities.

   Verified in the test harness: zero overlap, concave parts interlock,
   oversized parts are left unplaced, divide-by-color never mixes groups.

   Requires Clipper2 (bundled via CMake FetchContent). Falls back to the
   v2 bounding-box engine only if you build that file instead.

   The part contour fed from VBA should be the REAL outline (flattened
   Shape.DisplayCurve), not the bounding box -- see modNestBridge v3.
   ===================================================================== */
#include "CorelNestingEngine.h"
#include "clipper2/clipper.h"

#include <vector>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace Clipper2Lib;

namespace {

const double PI = 3.14159265358979323846;
const double SC = 1000.0;                 // mm -> integer (micron precision)

struct Pt { double x, y; };

struct Part {
    int id = 0, group = 0;
    std::vector<Pt> pts;                  // real outline, mm (local coords)
    double area = 0.0;
    // result
    bool   placed = false;
    double dx = 0, dy = 0, rot = 0;
    int    sheet = -1;
};

struct Sheet {
    int group = -1;
    Paths64 placed;                       // absolute placed polygons (scaled)
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

// rotate contour CCW by deg, scale to int, return a positively-oriented path
Path64 ToPath(const std::vector<Pt>& pts, double deg) {
    const double r = deg * PI / 180.0, c = std::cos(r), s = std::sin(r);
    std::vector<int64_t> v; v.reserve(pts.size() * 2);
    for (const Pt& q : pts) {
        const double rx = q.x * c - q.y * s;
        const double ry = q.x * s + q.y * c;
        v.push_back((int64_t)std::llround(rx * SC));
        v.push_back((int64_t)std::llround(ry * SC));
    }
    Path64 p = MakePath(v);
    if (Area(p) < 0) std::reverse(p.begin(), p.end());   // ensure CCW/positive
    return p;
}

void BBox(const Path64& p, int64_t& mnx, int64_t& mny, int64_t& mxx, int64_t& mxy) {
    mnx = mny = std::numeric_limits<int64_t>::max();
    mxx = mxy = std::numeric_limits<int64_t>::min();
    for (const Point64& q : p) {
        if (q.x < mnx) mnx = q.x; if (q.y < mny) mny = q.y;
        if (q.x > mxx) mxx = q.x; if (q.y > mxy) mxy = q.y;
    }
}

Path64 Translate(const Path64& p, int64_t dx, int64_t dy) {
    Path64 r; r.reserve(p.size());
    for (const Point64& q : p) r.push_back(Point64(int64_t(q.x + dx), int64_t(q.y + dy)));
    return r;
}

std::vector<double> AngleSet(const Engine* e) {
    std::vector<double> a;
    switch (e->fixAngleMode) {
        case 1: a.push_back(0); break;
        case 2: a.push_back(0); a.push_back(90); break;
        case 3: a.push_back(0); a.push_back(180); break;
        default: {
            int n = e->rotations < 1 ? 1 : e->rotations;
            for (int i = 0; i < n; ++i) a.push_back(360.0 * i / n);
        }
    }
    return a;
}

} // namespace

/* ===================================================================== */
static int RunImpl(Engine* e) {
    e->sheets.clear();
    for (Part& p : e->parts) { p.placed = false; p.sheet = -1; }

    const double insetMM = e->reduce + e->edgePad;
    const int64_t Wi = (int64_t)std::llround((e->sheetW - 2 * insetMM) * SC);
    const int64_t Hi = (int64_t)std::llround((e->sheetH - 2 * insetMM) * SC);
    if (Wi <= 0 || Hi <= 0) return 0;

    const int64_t spaceI = (int64_t)std::llround(std::max(0.0, e->spacing) * SC);
    const bool splitGroups = (e->divideMode == 1 || e->divideMode == 4);
    const bool allowInside = (e->allowInside != 0);
    const bool originRight = (e->originPoint == 0 || e->originPoint == 2);
    const bool originTop   = (e->originPoint == 2 || e->originPoint == 3);

    // FFD order (+ group clustering when dividing)
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
        int64_t bTx = 0, bTy = 0, bMnx = 0, bMny = 0;
        double  bRot = 0;
        int     bSheet = -1;
        Path64  bPath;

        for (double ang : AngleSet(e)) {
            Path64 B = ToPath(part.pts, ang);
            int64_t mnx, mny, mxx, mxy; BBox(B, mnx, mny, mxx, mxy);
            const int64_t w = mxx - mnx, h = mxy - mny;
            if (w > Wi || h > Hi) continue;                 // too big for sheet

            // IFP: allowed translation t of B so that B+t is in [0,Wi]x[0,Hi]
            std::vector<int64_t> iv = {
                -mnx, -mny,  Wi - mxx, -mny,  Wi - mxx, Hi - mxy,  -mnx, Hi - mxy };
            Paths64 ifp = { MakePath(iv) };

            for (int si = 0; si <= (int)e->sheets.size(); ++si) {
                const bool isNew = (si == (int)e->sheets.size());
                if (!isNew && splitGroups && e->sheets[si].group != part.group) continue;

                // forbidden region (union of NFPs vs placed, grown by spacing)
                Paths64 feasible;
                if (isNew || e->sheets[si].placed.empty()) {
                    feasible = ifp;
                } else {
                    Paths64 nfp;
                    for (const Path64& P : e->sheets[si].placed) {
                        // MinkowskiDiff can emit tiny reverse-oriented slivers
                        // that would punch false holes under a winding union.
                        // Force every sub-path positive before combining.
                        Paths64 mk = MinkowskiDiff(B, P, true);   // P (+) (-B)
                        for (auto& pp : mk) {
                            if (pp.size() < 3) continue;
                            if (Area(pp) < 0) std::reverse(pp.begin(), pp.end());
                            nfp.push_back(pp);
                        }
                    }
                    Paths64 forbid = Union(nfp, FillRule::NonZero);
                    if (spaceI > 0 && !forbid.empty())
                        forbid = InflatePaths(forbid, (double)spaceI,
                                              JoinType::Round, EndType::Polygon);
                    feasible = Difference(ifp, forbid, FillRule::NonZero);
                }
                if (feasible.empty()) continue;

                // bounding box of what is already on this sheet
                int64_t pmnx = 0, pmny = 0, pmxx = 0, pmxy = 0; bool hasPlaced = false;
                if (!isNew) {
                    for (const Path64& P : e->sheets[si].placed) {
                        int64_t a, b, c, d; BBox(P, a, b, c, d);
                        if (!hasPlaced) { pmnx = a; pmny = b; pmxx = c; pmxy = d; hasPlaced = true; }
                        else { pmnx = std::min(pmnx, a); pmny = std::min(pmny, b);
                               pmxx = std::max(pmxx, c); pmxy = std::max(pmxy, d); }
                    }
                }

                // choose the candidate that keeps the whole layout smallest in
                // the fit direction (this fills concavities instead of growing
                // the pile), with a gravity tiebreak toward the origin corner.
                bool localFound = false; int64_t tx = 0, ty = 0;
                double localBest = std::numeric_limits<double>::max();
                for (const Path64& poly : feasible) {
                    if (Area(poly) < 0 && !allowInside) continue;  // skip pockets
                    for (const Point64& v : poly) {
                        const int64_t bx0 = mnx + v.x, by0 = mny + v.y;
                        const int64_t bx1 = mxx + v.x, by1 = mxy + v.y;
                        const int64_t cmnx = hasPlaced ? std::min(pmnx, bx0) : bx0;
                        const int64_t cmny = hasPlaced ? std::min(pmny, by0) : by0;
                        const int64_t cmxx = hasPlaced ? std::max(pmxx, bx1) : bx1;
                        const int64_t cmxy = hasPlaced ? std::max(pmxy, by1) : by1;
                        const double cW = (cmxx - cmnx) / SC;
                        const double cH = (cmxy - cmny) / SC;
                        const double prim = (e->fitMode == 1) ? cW : cH;
                        const double gy = (originTop  ? -(double)by1 : (double)by0) / SC;
                        const double gx = (originRight ? -(double)bx1 : (double)bx0) / SC;
                        const double sc = prim * 1e12 + (cW * cH) * 1e3 + gy * 10.0 + gx;
                        if (sc < localBest) { localBest = sc; tx = v.x; ty = v.y; localFound = true; }
                    }
                }
                if (!localFound) continue;

                const double score = si * 1e18 + localBest;
                if (score < bestScore) {
                    bestScore = score; bSheet = si; bRot = ang;
                    bTx = tx; bTy = ty; bMnx = mnx; bMny = mny;
                    bPath = Translate(B, tx, ty);
                }
                if (!isNew) break;              // first-fit: stop at earliest sheet
            }
        }

        if (bSheet < 0) { part.placed = false; continue; }   // leave in place

        if (bSheet == (int)e->sheets.size()) {
            Sheet s; s.group = part.group; e->sheets.push_back(s);
        }
        e->sheets[bSheet].placed.push_back(bPath);

        part.placed = true;
        part.sheet  = bSheet;
        part.rot    = bRot;
        part.dx     = insetMM + (double)(bMnx + bTx) / SC;   // min-corner, full sheet
        part.dy     = insetMM + (double)(bMny + bTy) / SC;
        ++placedCount;
    }
    return placedCount;
}

/* ===================================================================== */
/*  Exported C ABI (unchanged from v2)                                    */
/* ===================================================================== */
CN_EXPORT void* CN_CALL CN_Create(void) { return new (std::nothrow) Engine(); }
CN_EXPORT void  CN_CALL CN_Destroy(void* h) { delete static_cast<Engine*>(h); }
CN_EXPORT void  CN_CALL CN_Reset(void* h) {
    if (!h) return;
    Engine* e = static_cast<Engine*>(h);
    e->parts.clear(); e->sheets.clear();
}
CN_EXPORT int CN_CALL CN_GetVersion(void) { return 300; }

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
