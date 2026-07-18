<div dir="rtl" align="right">

# ps5sim

[🇺🇸 English](README.md) | [🇮🇷 فارسی](README-fa.md)


[![Platform](https://img.shields.io/badge/platform-Windows%20x64-0078D4.svg)](#system-requirements)
[![Status](https://img.shields.io/badge/status-early%20development-orange.svg)](#current-status)
[![License](https://img.shields.io/badge/license-GPL--2.0-blue.svg)](LICENSE)


پروژه ps5sim یک شبیه‌ساز رایگان و متن‌باز پلی‌استیشن ۵ است که با زبان ++C برای ویندوز نوشته شده است. این پروژه بر پایه نسخه به شدت تغییر یافته‌ای از [Kyty](https://github.com/InoriRus/Kyty) (از طریق [KytyPS5](https://github.com/KytyPS5/KytyPS5)) ساخته شده است. این پروژه در مراحل اولیه توسعه قرار دارد، بنابراین سازگاری آن محدود است و رفتار آن ممکن است در نسخه‌های (Build) مختلف به طور قابل توجهی تغییر کند.

> [!IMPORTANT]
> پروژه ps5sim هیچ‌گونه وابستگی به Sony Interactive Entertainment یا PlayStation ندارد. این پروژه هیچ‌گونه بازی یا نرم‌افزار سیستمیِ دارای حق کپی‌رایت را توزیع نمی‌کند. فقط از فایل بازی‌هایی استفاده کنید که به صورت قانونی تهیه کرده‌اید.

## وضعیت فعلی (Current Status)

پروژه ps5sim می‌تواند بازی‌های دو بعدی و منتخبی از بازی‌های سه بعدی را اجرا کند، از جمله عناوینی که با موتورهای Unreal Engine 4/5، Unity و موتورهای اختصاصی ساخته شده‌اند. در حال حاضر نیازی به ماژول‌های شبیه‌ساز سطح پایینِ خارجی نیست.

تمرکز توسعه بر روی سازگاری و پایداری در اجرای اولیه (Boot) بازی‌هاست.

پشتیبانی از لینوکس در برنامه‌های آینده قرار دارد، اما در حال حاضر ویندوز تنها پلتفرم پشتیبانی شده است.

## باگ‌ها و مشکلات

این پروژه در مراحل اولیه است، پس لطفاً هنگام ثبت مشکل (Issue) جدید به این موضوع توجه داشته باشید. انتظار کرش کردن، ایرادات گرافیکی، سازگاری پایین و عملکرد ضعیف را داشته باشید.

## اسکرین‌شات‌ها

<table align="center">
  <tr>
    <td align="center">
      <strong>Disgaea 6</strong><br>
      <img src="docs/screenshots/ps5-01.png" width="300" alt="اجرای بازی Disgaea 6 در ps5sim">
    </td>
    <td align="center">
      <strong>Dreaming Sarah</strong><br>
      <img src="docs/screenshots/ps5-03.png" width="300" alt="اجرای بازی Dreaming Sarah در ps5sim">
    </td>
  </tr>
  <tr>
    <td align="center">
      <strong>Minecraft Legends</strong><br>
      <img src="docs/screenshots/ps5-04.png" width="300" alt="اجرای بازی Minecraft Legends در ps5sim">
    </td>
    <td align="center">
      <strong>SILENT HILL: The Short Message</strong><br>
      <img src="docs/screenshots/ps5-05.png" width="300" alt="اجرای بازی SILENT HILL در ps5sim">
    </td>
  </tr>
</table>

## مشارکت در پروژه

تست کردن بازی‌ها و ارسال گزارش باگ‌های دقیق، راه‌های مفیدی برای مشارکت در پروژه هستند. ابتدا مشکلات (Issues) موجود را جستجو کنید، سپس از قالب **Game Emulation Bug Report** استفاده کرده و فایل لاگ کامل را ضمیمه کنید.

مشارکت در کدنویسی باید متمرکز باشد، با موفقیت روی ویندوز بیلد شود و در صورت امکان شامل تست‌های مرتبط باشد. از آنجا که ps5sim هنوز به سرعت در حال تکامل است، لطفاً قبل از شروع یک تغییر بزرگ، ثبت یک Issue را برای مشورت در نظر بگیرید.

## اطلاعات برای توسعه‌دهندگان

معماری گرافیک PS5 بر پایه AMD RDNA 2 است. هنگام کار بر روی رمزگشایی شیدرها و کامپایل مجدد، از راهنمای [RDNA 2 Instruction Set Architecture Reference Guide (document 70648)](https://docs.amd.com/v/u/en-US/rdna2-shader-instruction-set-architecture) شرکت AMD به عنوان مرجع اصلیِ انکدینگ دستورالعمل‌ها استفاده کنید.

بخش‌های مهم سورس‌کد:

- [`src/graphics/shader/recompiler`](src/graphics/shader/recompiler) — رمزگشایی دستورالعمل‌ها، نمایندگی میانی (IR)، کنترل جریان، ردیابی منابع و انتشار SPIR-V
- [`src/graphics/guest_gpu`](src/graphics/guest_gpu) — فرمت‌های پردازنده گرافیکی PS5 (Prospero) و پردازش دستورات
- [`src/graphics/host_gpu`](src/graphics/host_gpu) — بک‌اند Vulkan برای هاست و مدیریت منابع
- [`tests`](tests) — تست‌های رگرسیونِ متمرکزِ حافظه، شیدر و ردیابی منابع

رندرگیر (Renderer) موتور بر پایه Vulkan 1.3 هدف‌گذاری شده است. تغییرات شیدر را همسو با معناشناسی ISA RDNA 2 و قوانین اعتبارسنجی Vulkan/SPIR-V نگه دارید.

## بیلد کردن (Building)

### نیازمندی‌های سیستم
- ویندوز 10 نسخه 1803
- یک پردازنده 64 بیتی x86
- کارت گرافیک با پشتیبانی از Vulkan 1.3 و درایورهای به‌روز

### نیازمندی‌های بیلد
- Git
- CMake 3.12 یا جدیدتر
- Ninja
- ویژوال استودیو 2022 یا Build Tools 2022 به همراه ابزار **Desktop development with C++** و کامپوننت **C++ Clang tools for Windows**
- فریم‌ورک Qt 6 برای MSVC 2022 نسخه 64-bit، شامل ماژول‌های Concurrent، Network و Widgets
- کیت توسعه Vulkan SDK 1.3 یا جدیدتر

کامپایلر مایکروسافت `cl.exe` پشتیبانی نمی‌شود؛ حتماً از `clang-cl` استفاده کنید.

یک **x64 Native Tools Command Prompt for Visual Studio 2022** (یا معادل آن در PowerShell) باز کنید، به مسیر ریشه (Root) ریپازیتوری بروید و وابستگی‌ها را دریافت کنید:

```powershell
git submodule update --init --recursive
پروژه را پیکربندی کنید. مسیر Qt را با نسخه‌ای که روی سیستم شما نصب شده جایگزین کنید:

PowerShell
cmake -S src -B _Build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_PREFIX_PATH="C:/Qt/6.x.x/msvc2022_64"
لانچر را بیلد کرده و یک پوشه نصبِ قابل اجرا آماده کنید:

PowerShell
cmake --build _Build/windows --target launcher
cmake --install _Build/windows --prefix _Build/windows/install
برنامه نهایی و وابستگی‌های اجرای آن در پوشه _Build/windows/install قرار خواهند گرفت.

ویژوال استودیو کد (Visual Studio Code)
یک ستاپِ آماده برای ویژوال استودیو کد در پوشه .vscode قرار داده شده است. این ستاپ ابزار CMake را برای بیلد پروژه با Ninja و clang-cl پیکربندی می‌کند و پروفایل‌های اجرایی را برای هر دو برنامه launcher.exe و ps5sim_emulator.exe فراهم می‌کند.

قبل از استفاده:

افزونه‌های CMake Tools و C/C++ را در ویژوال استودیو کد نصب کنید.

متغیر CMAKE_PREFIX_PATH را در فایل .vscode/settings.json آپدیت کنید تا به محل نصب Qt 6 MSVC شما اشاره کند.

مسیر --game را در فایل .vscode/launch.json برای پروفایل Debug ps5sim_emulator آپدیت کنید.

ریپازیتوری را در یک محیط x64 Visual Studio باز کنید، پروژه CMake را پیکربندی کرده و یک پروفایل اجرایی از منوی Run and Debug انتخاب کنید.

اجرای شبیه‌ساز (Running)
قبل از گزارش مشکلات گرافیکی، درایور گرافیک خود را آپدیت کنید.

برای استفاده از لانچر گرافیکی:

PowerShell
.\_Build\windows\install\launcher.exe
در اولین اجرا، یک یا چند پوشه حاوی بازی را در تنظیمات عمومی اضافه کنید. لانچر این پوشه‌ها را به صورت بازگشتی جستجو کرده و بازی‌هایی که دارای فایل eboot.bin هستند را پیدا می‌کند. یک بازی شناخته‌شده را انتخاب کرده و از لیست بازی‌ها اجرا کنید.

شبیه‌ساز همچنین می‌تواند مستقیماً از طریق خط فرمان و دایرکتوری یک بازی قانونی اجرا شود:

PowerShell
.\_Build\windows\install\ps5sim_emulator.exe --game "D:\Games\ExampleGame"
با اجرای دستور ps5sim_emulator.exe --help می‌توانید تمام تنظیمات مربوط به گرافیک، لاگ‌گیری، اعتبارسنجی، پروفایلینگ و دیباگ را مشاهده کنید.

استفاده از هوش مصنوعی (AI Use)
ابزارهای هوش مصنوعی ممکن است برای تحقیق، مهندسی معکوس و کمک در توسعه استفاده شوند. مشارکت‌کنندگان باید تمام کدهایی که ارسال می‌کنند را به طور کامل درک، بررسی و تست کنند و مسئولیت صحت آن‌ها را بپذیرند. ارتباطات در ریپازیتوری (از جمله توضیحات Pull-requestها، کامنت‌های کد و توضیحات Issueها) باید توسط شخصِ انسانی نوشته شده باشد، نه یک عامل هوش مصنوعی خودکار.

درخواست‌های ادغامِ کدی (Pull requests) که شامل کدهای تولیدشده توسط هوش مصنوعی هستند، باید محدوده دخالت هوش مصنوعی را شفاف‌سازی کنند و توضیح دهند که چه بررسی و تست انسانی روی آن‌ها انجام شده است. تغییرات تولیدشده‌ای که تایید و تست نشده باشند، ممکن است بدون بررسی بسته شوند.

لایسنس (License)
پروژه ps5sim تحت لایسنس GNU General Public License version 2 (GPL-2.0-only) منتشر شده است.

این پروژه بر پایه نسخه اصلی شبیه‌ساز Kyty است که تحت لایسنس MIT منتشر شده بود. کپی‌رایت اصلی Kyty و اعلان لایسنس آن در فایل LICENSES/Kyty-MIT.txt محفوظ است. کامپوننت‌های شخص‌ثالث تابع لایسنس‌های خودشان هستند که به همراه آن‌ها ارائه شده است.

تشکر ویژه
InoriRus/Kyty — پروژه ps5sim بر پایه نسخه به شدت تغییر یافته‌ای (از طریق KytyPS5) از پروژه اصلی Kyty ساخته شده است.

shadps4-emu/shadPS4 — مرجعی عالی برای درک مدل حافظه و پیاده‌سازی AVPlayer.
