@echo off
setlocal enabledelayedexpansion

rem Get current date and time for filename
for /f "tokens=2 delims==" %%a in ('wmic OS Get localdatetime /value') do set "dt=%%a"
set "YY=%dt:~2,2%" & set "YYYY=%dt:~0,4%" & set "MM=%dt:~4,2%" & set "DD=%dt:~6,2%"
set "HH=%dt:~8,2%" & set "Min=%dt:~10,2%" & set "Sec=%dt:~12,2%"
set "datestamp=%YYYY%%MM%%DD%_%HH%%Min%%Sec%"

rem Create logs directory if it doesn't exist
if not exist "logs" mkdir logs

rem Set log filename
set "logfile=logs\monitor_%datestamp%.txt"

echo Starting ESP32 Monitor with logging...
echo Log file: %logfile%
echo Press Ctrl+C to stop monitoring
echo =====================================

rem Start monitor with logging (requires ESP-IDF environment)
idf.py monitor --print_filter "*" --log-file "%logfile%"

echo.
echo Monitor session ended. Log saved to: %logfile%
pause