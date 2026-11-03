@echo off
rem Build llamac - run from the "x64 Native Tools Command Prompt for VS 2022" 
cl /nologo /O2 /GL /Gy /GF /GS /W3 /utf-8 llamac.c /link /LTCG /OPT:REF /OPT:ICF /out:llamac.exe
if errorlevel 1 exit /b 1
echo Built llamac.exe
