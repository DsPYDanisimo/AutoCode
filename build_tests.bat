@echo off
setlocal

set BUILD_DIR=build_tests

echo.
echo === ECU Tests: configure ===
if not exist %BUILD_DIR% mkdir %BUILD_DIR%
cd %BUILD_DIR%

cmake .. -DBUILD_GTEST=ON -DBUILD_TEST=OFF -DCMAKE_BUILD_TYPE=Debug
if errorlevel 1 (
    echo.
    echo FAILED: cmake configure
    cd ..
    pause
    exit /b 1
)

echo.
echo === ECU Tests: build ===
cmake --build . --config Debug
if errorlevel 1 (
    echo.
    echo FAILED: build
    cd ..
    pause
    exit /b 1
)

echo.
echo === ECU Tests: run ===
ctest -C Debug --output-on-failure

cd ..
echo.
echo Done. To run again: cd %BUILD_DIR% ^&^& ctest -C Debug --output-on-failure
pause
