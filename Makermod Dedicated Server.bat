echo off
cls
TITLE Makermod Dedicated Server
:MAIN
cd ../
jampded.exe +set dedicated 2 +set fs_game makermod +set sv_pure 0 +exec makermod.cfg +set net_port 29070
cd makermod
start renameLog.bat
goto MAIN
