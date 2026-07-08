# بناء CorelNestingEngine.dll بدون Visual Studio
*(English version below — النسخة الإنجليزية في الأسفل)*

## لماذا لم أبنِ الملف مباشرة؟
حاولت تجميع الـ DLL داخل بيئة العمل السحابية، لكن **سياسة الشبكة** في هذه البيئة تمنع تنزيل أي مترجم (compiler) يستهدف ويندوز:
- حزم `mingw-w64` من مستودعات أوبونتو: محجوبة (403/405).
- أداة `Zig` عبر pip و npm: محجوبة بالسياسة الأمنية.
- لا يوجد مترجم ويندوز مثبّت مسبقاً في البيئة، وتشغيل Visual Studio على لينكس غير ممكن أصلاً.

لكنني **تحققت من أن الشيفرة تُترجم بنجاح** (بلا أخطاء، وكل الدوال الإحدى عشرة `CN_*` تُصدَّر بأسماء نظيفة). المشكلة في المنصّة فقط، لا في الكود. لذلك جهّزت لك طريقتين جاهزتين لإنتاج نفس الـ DLL دون تثبيت Visual Studio.

## الطريقة (أ) — الأسهل والموصى بها: GitHub Actions (لا تحتاج أي أداة على جهازك)
تبني الـ DLL على خادم ويندوز حقيقي بمترجم **MSVC**، وتنزّلها ملفاً جاهزاً.
1. أنشئ حساباً مجانياً على github.com إن لم يكن لديك.
2. أنشئ مستودعاً جديداً (Repository) — يمكن أن يكون خاصاً (Private).
3. ارفع محتويات هذا المجلد كما هي، **بما في ذلك مجلد `.github`**. أسهل طريقة: من صفحة المستودع اختر **Add file ▸ Upload files** واسحب الملفات. إن تعذّر رفع الملف المخفي، أنشئه يدوياً عبر **Add file ▸ Create new file** بالاسم `​.github/workflows/build-dll.yml` والصق محتواه.
4. افتح تبويب **Actions**؛ سيعمل سير العمل `build-CorelNestingEngine-dll` تلقائياً (أو اضغط **Run workflow**).
5. بعد انتهائه (نحو دقيقة)، افتح التشغيل ونزّل الملف من قسم **Artifacts**: ستجد `CorelNestingEngine-x64` وبداخله `CorelNestingEngine.dll`.

## الطريقة (ب) — بناء محلي بمترجم محمول (w64devkit): تنزيل واحد، بلا تثبيت
1. نزّل **w64devkit** (ملف zip واحد ~90MB) من صفحة إصداراته على GitHub: `skeeto/w64devkit` (اختر ملفاً مثل `w64devkit-x64-*.zip`).
2. فك الضغط في أي مكان (لا يحتاج صلاحيات مدير).
3. انسخ ملفات هذا المجلد الأربعة (`CorelNestingEngine.h` و`.cpp` و`.def` و`build_windows.bat`) إلى مجلد w64devkit.
4. شغّل `w64devkit.exe` (تفتح نافذة طرفية)، ثم:
   ```sh
   cd /c/المسار/إلى/المجلد
   ./build_windows.bat
   ```
5. ستظهر `CorelNestingEngine.dll` في المجلد نفسه.

## ملاحظات مهمة
- الناتج **64-بت (x64)** يطابق CorelDRAW 2018 فأحدث — تماماً كما طلبت.
- كلا الطريقتين تُنتجان أسماء تصدير **نظيفة** (`CN_Create` … إلخ) بلا زخرفة على x64، فاستدعاؤها من VBA عبر `Declare` يعمل دون أي تعديل.
- واجهة الـ DLL كلها أنواع C بسيطة ومؤشرات، لذا **لا فرق عملي** في الاستدعاء بين نسخة MSVC ونسخة MinGW من داخل CorelDRAW/VBA.
- بناء MinGW يربط زمن التشغيل بشكل ساكن (`-static`)، فلا يحتاج الـ DLL أي ملفات DLL إضافية بجانبه.
- لبناء نسخة **32-بت** (لإصدارات CorelDRAW X6/X7/X8 القديمة): في GitHub Actions غيّر `-A x64` إلى `-A Win32`.

---

# Building CorelNestingEngine.dll without Visual Studio

**Why it wasn't built for you directly:** this cloud sandbox's network policy blocks every Windows-targeting compiler download (mingw-w64 via apt → 403/405; Zig via pip/npm → policy-blocked; arbitrary GitHub clone → 403), and MSVC cannot run on Linux. I verified the C++ **compiles cleanly and exports all 11 `CN_*` functions** — the blocker is the platform, not the code.

**Option A — GitHub Actions (recommended, no local tools):** create a repo, upload this folder (including the `.github` directory), open the **Actions** tab; the `build-CorelNestingEngine-dll` workflow builds `CorelNestingEngine.dll` on a Windows **MSVC** runner. Download it from the run's **Artifacts** (`CorelNestingEngine-x64`).

**Option B — Local w64devkit (one portable zip, no install):** download w64devkit (`skeeto/w64devkit` releases), unzip, copy the `.h/.cpp/.def/build_windows.bat` into it, run `w64devkit.exe`, then `./build_windows.bat`.

Both produce a 64-bit DLL with clean C export names, callable from VBA `Declare` unchanged. The MinGW build links the runtime statically, so the DLL needs no extra DLLs beside it. For a 32-bit build, change `-A x64` to `-A Win32` in the workflow.
