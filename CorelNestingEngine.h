/* =====================================================================
   CorelNestingEngine.h
   High-performance nesting engine for CorelDRAW (called from VBA).

   ABI notes (READ THIS):
   - Exports are plain C (extern "C") so VBA `Declare` can bind by name.
   - Build TARGET MUST MATCH CorelDRAW's bitness. CorelDRAW 2018+ is
     x64-only, so build the DLL as x64. For x86 CorelDRAW (X6/X7/X8 32-bit)
     build Win32 and keep the __stdcall names clean via the .def file.
   - On x64 there is a single calling convention, so __stdcall == __cdecl.
     We still tag CN_CALL so the x86 build is unambiguous.

   The engine is stateful and referenced through an opaque handle
   (void*), which VBA holds as LongPtr. This keeps the FFI surface tiny
   and avoids marshalling large structs across the boundary.
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
CN_EXPORT void   CN_CALL CN_Reset(void* h);      /* clears parts + results  */
CN_EXPORT int    CN_CALL CN_GetVersion(void);    /* returns e.g. 100 = 1.00 */

/* ---- configuration ---------------------------------------------------- */
/* sheet geometry, all in millimetres.
   edgePadding = safe gap between parts and the live sheet edge.
   reduce      = "Reduce sheet size": shrink the usable sheet on every side. */
CN_EXPORT void   CN_CALL CN_SetSheet(void* h,
                                     double width, double height,
                                     double edgePadding, double reduce);

/* nesting options.
   spacing      = "Minimum distance": clearance between parts (mm).
   rotations    = number of discrete orientations tried per part
                  (aggressive rotation). e.g. 4 => 0/90/180/270.
   fixAngleMode = 0 Auto | 1 No(0 only) | 2 fix 90 | 3 fix 180 | 4 free.
   allowInside  = 1 to permit nesting small parts inside concavities.
   originPoint  = packing corner: 0 R-bottom |1 L-bottom |2 R-top |3 L-top.
   fitMode      = 0 Bottom(best) | 1 Width(best) | 2 Height(best).
   timeLimitSec = wall-clock budget for the iterative search.
   searchCount  = number of independent attempts (best kept). */
CN_EXPORT void   CN_CALL CN_SetOptions(void* h,
                                       double spacing, int rotations,
                                       int fixAngleMode, int allowInside,
                                       int originPoint, int fitMode,
                                       double timeLimitSec, int searchCount);

/* ---- part input ------------------------------------------------------- */
/* Add one closed polygon. xs/ys are parallel arrays of length `count`
   (outer contour, mm, any winding). `id` is the caller's shape id so the
   VBA side can map results back to CorelDRAW shapes.
   Returns the engine-side part index, or -1 on error. */
CN_EXPORT int    CN_CALL CN_AddPart(void* h, int id,
                                    double* xs, double* ys, int count);

/* ---- run + results ---------------------------------------------------- */
/* Runs the placement pipeline. Returns number of parts successfully placed. */
CN_EXPORT int    CN_CALL CN_Run(void* h);

CN_EXPORT int    CN_CALL CN_GetPartCount(void* h);

/* Read the transform for one result row (index 0..PartCount-1).
   The transform maps the part FROM its input coordinates TO its nested
   position: rotate by outRotDeg (CCW, about the part's own origin/min
   corner) then translate by (outDx,outDy). outSheet is the 0-based sheet
   the part landed on (multi-sheet layouts). outId echoes the caller id.
   Returns 1 if placed, 0 if it could not be placed (off-layout). */
CN_EXPORT int    CN_CALL CN_GetResult(void* h, int index,
                                      int* outId, double* outDx, double* outDy,
                                      double* outRotDeg, int* outSheet);

/* Material utilisation of sheet 0 as a fraction 0..1 (parts area / sheet). */
CN_EXPORT double CN_CALL CN_GetUtilization(void* h);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* COREL_NESTING_ENGINE_H */
