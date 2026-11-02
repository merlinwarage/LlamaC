@echo off

rem Run from "x64 Native Tools Command Prompt for VS 2022"
icacls "." /setintegritylevel "(OI)(CI)M" /T /C
cl /O2 /W3 /utf-8 llamac.c
