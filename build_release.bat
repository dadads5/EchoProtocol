@echo off
call "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat"
cd /D D:/daimajihe/cpp-demo/EchoProtocol
cmake -S . -B out/build/release -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE=D:/vcp/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows
if errorlevel 1 exit /b 1
ninja -C out/build/release EchoProtocol EchoProtocolTests
if errorlevel 1 exit /b 1
echo BUILD_DONE
