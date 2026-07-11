/* =====================================================================
   CorelNestingEngine.h   (v2)
   High-performance nesting engine for CorelDRAW (called from VBA).

   ABI: plain C exports (extern "C") + .def so VBA `Declare` binds by name.
   Build x64 to match CorelDRAW 2018+. VBA holds the engine as a LongPtr.

   v2 changes vs v1:
     * CN_AddPart takes a `group` id (used by Divide-by-color).
     * CN_SetOptions takes `divideMode`.
     * Placement is a provably non-overlapping bottom-left packer that
       strictly respects the padded/reduced sheet. Parts that cannot fit
       any sheet are reported NOT placed (VBA leaves them where they are).
     * dx/dy are returned in FULL sheet-local mm (0..sheetW, 0..sheetH),
       i.e. the inner padding offset is already applied.
     * CN_GetSheetCount reports how many sheets the layout used.

   v2.1 changes vs v2 (ABI UNCHANGED -- same signatures, just rebuild):
     * fitMode 2 (Height best) now fills the sheet height first (minimal
       used width); 0 (Bottom) / 1 (Width) fill bottom rows first.
     * spacing is enforced BETWEEN parts only, never against the sheet
       edge -- an exact-fit part is no longer rejected because of it.
       (Edge distance stays reduce + edgePadding.)
     * CN_GetVersion returns 210 so VBA can detect a stale DLL.
   ===================================================================== */
#ifndef COREL_NESTING_ENGINE_H
#define COREL_NESTING_ENGINE_H

#ifdef _WIN32
  #define CN_EXPORT __declspec(dllexport)
  #define CN_CALL   __stdcall
#else
  #define CN_EXPORT
  #define CN_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- lifecycle -------------------------------------------------------- */
CN_EXPORT void*  CN_CALL CN_Create(void);
CN_EXPORT void   CN_CALL CN_Destroy(void* h);
CN_EXPORT void   CN_CALL CN_Reset(void* h);
CN_EXPORT int    CN_CALL CN_GetVersion(void);      /* 210 = 2.10 */

/* ---- configuration ---------------------------------------------------- */
CN_EXPORT void   CN_CALL CN_SetSheet(void* h,
                                     double width, double height,
                                     double edgePadding, double reduce);

/* divideMode: 0 No | 1 On different sheet | 2 On same sheet |
               3 On same sheet without mix | 4 Divide by layer
   (1 keeps each group on its own sheet(s); 2/3/4 currently cluster by
    group on shared sheets — see .cpp). */
CN_EXPORT void   CN_CALL CN_SetOptions(void* h,
                                       double spacing, int rotations,
                                       int fixAngleMode, int allowInside,
                                       int originPoint, int fitMode,
                                       int divideMode,
                                       double timeLimitSec, int searchCount);

/* ---- part input ------------------------------------------------------- */
/* group = color/layer bucket for Divide-by-color (0 if unused). */
CN_EXPORT int    CN_CALL CN_AddPart(void* h, int id, int group,
                                    double* xs, double* ys, int count);

/* ---- run + results ---------------------------------------------------- */
CN_EXPORT int    CN_CALL CN_Run(void* h);          /* # parts placed */
CN_EXPORT int    CN_CALL CN_GetPartCount(void* h);
CN_EXPORT int    CN_CALL CN_GetSheetCount(void* h);/* # sheets used  */

/* index 0..PartCount-1. Transform: rotate CCW by outRotDeg about the
   part's own min corner, then place so its rotated min corner sits at
   (outDx,outDy) in FULL sheet-local mm on sheet outSheet.
   Returns 1 if placed, 0 if not placed (leave the shape untouched). */
CN_EXPORT int    CN_CALL CN_GetResult(void* h, int index,
                                      int* outId, double* outDx, double* outDy,
                                      double* outRotDeg, int* outSheet);

CN_EXPORT double CN_CALL CN_GetUtilization(void* h); /* over used sheets */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* COREL_NESTING_ENGINE_H */
