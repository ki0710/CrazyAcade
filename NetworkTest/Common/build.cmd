@echo off
setlocal
pushd "%~dp0"
if not exist build mkdir build
cl /nologo /EHsc /W4 /std:c++14 ..\Server\server.cpp Packet.cpp SocketUtils.cpp MapItemSync.cpp ..\..\CrazyArcade\Board.cpp ..\..\CrazyArcade\IceState.cpp /Fe:build\Server.exe /Fo:build\ /link Ws2_32.lib
if errorlevel 1 goto failed
cl /nologo /EHsc /W4 /std:c++14 ..\Client\client.cpp Packet.cpp SocketUtils.cpp MapItemSync.cpp /Fe:build\Client.exe /Fo:build\ /link Ws2_32.lib
if errorlevel 1 goto failed
cl /nologo /EHsc /W4 /std:c++14 Packet.cpp PacketTests.cpp /Fe:build\PacketTests.exe /Fo:build\ /link Ws2_32.lib
if errorlevel 1 goto failed
build\PacketTests.exe
if errorlevel 1 goto failed
popd
exit /b 0
:failed
popd
exit /b 1
