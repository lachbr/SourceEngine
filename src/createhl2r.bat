@echo off
devtools\bin\vpc.exe /hl2r +gamedlls +shaders_all /define:VS2019 /mksln game_hl2r.sln
devtools\bin\vpc.exe /hl2r +gamedlls +shaders_all /define:VS2019 /mksln game_hl2r_x64.sln /win64
pause