@echo off
setlocal
REM ============================================================
REM  بناء CorelNestingEngine.dll (64-بت) بدون Visual Studio
REM  باستخدام MinGW-w64 المحمول (w64devkit).
REM
REM  الخطوات:
REM   1) نزّل w64devkit (ملف zip واحد) من صفحة إصداراته على GitHub: skeeto/w64devkit
REM   2) فك الضغط (لا يحتاج صلاحيات مدير)، وانسخ ملفات هذا المجلد إليه.
REM   3) شغّل w64devkit.exe لفتح نافذة الأوامر، ثم انتقل لمجلد الملفات وشغّل:
REM         build_windows.bat
REM ------------------------------------------------------------
REM  Build CorelNestingEngine.dll (x64) with portable MinGW-w64
REM  (w64devkit) -- no Visual Studio needed.
REM ============================================================

set "CXX=g++"
where %CXX% >nul 2>&1 || set "CXX=x86_64-w64-mingw32-g++"
where %CXX% >nul 2>&1 || (
  echo [خطأ] لم اجد g++ على المسار. افتح w64devkit.exe اولا ثم شغل هذا الملف من داخله.
  echo [ERROR] No g++ on PATH. Open w64devkit.exe first, then run this script.
  exit /b 1
)

echo Using compiler: %CXX%
%CXX% -shared -O2 -std=c++17 ^
  -static -static-libgcc -static-libstdc++ ^
  CorelNestingEngine.cpp CorelNestingEngine.def ^
  -o CorelNestingEngine.dll ^
  -Wl,--out-implib,CorelNestingEngine.lib

if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)
echo.
echo ================================================
echo  تم بنجاح: CorelNestingEngine.dll  ^(64-bit^)
echo  BUILD OK: CorelNestingEngine.dll
echo ================================================
endlocal
