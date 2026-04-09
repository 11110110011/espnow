@echo off
set MSYSTEM=
call F:\Espressif\frameworks\esp-idf-v5.5.3\export.bat
cd /d H:\esp_projects\espnow
idf.py build
