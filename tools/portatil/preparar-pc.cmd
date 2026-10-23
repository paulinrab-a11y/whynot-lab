@echo off
rem Abre o preparar-pc.ps1 sem esbarrar na politica de execucao de scripts do Windows.
rem Sem argumentos (dois cliques), instala o que faltar e deixa a janela aberta no fim.
if "%~1"=="" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0preparar-pc.ps1" -Instalar
    pause
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0preparar-pc.ps1" %*
)
