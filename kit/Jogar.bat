@echo off
chcp 65001 >nul
cd /d "%~dp0"
title Final Bout recomp
"ferramentas\python\python.exe" jogar.py %*
if errorlevel 1 (
  echo.
  echo O programa terminou com erro. Veja a mensagem acima.
  pause
)
