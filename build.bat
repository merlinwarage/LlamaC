@echo off
rem Build llamac - run from the "x64 Native Tools Command Prompt for VS 2022" 
rem /MANIFEST:EMBED puts the manifest (Common Controls v6 from the #pragma in win_base.h, asInvoker) inside the exe,
rem so tooltips, cue banners and themed controls work wherever the exe is copied - no llamac.exe.manifest needed.
rc /nologo /fo res\llamac.res res\llamac.rc
if errorlevel 1 exit /b 1
cl /nologo /O2 /GL /Gy /GF /GS /W3 /utf-8 llamac.c res\llamac.res /link /LTCG /OPT:REF /OPT:ICF /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'" /out:dist\llamac.exe
if errorlevel 1 exit /b 1
echo Built dist\llamac.exe
