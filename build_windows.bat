@echo off
cl /nologo /std:c++17 /O2 /EHsc /W4 main.cpp /Fe:wood_house.exe
if errorlevel 1 exit /b 1
wood_house.exe california_wood_house_inputs_v4.csv california_wood_house_settings_v4.csv results.csv
