@echo off
echo ============================================
echo  Launching Ghidra with GameHackRE project
echo ============================================
echo.
echo  Project: C:\Users\JJ\Documents\hacking\GameHackRE\GameHackRE
echo  Target:  Injector.dll (already imported + analyzed)
echo.
echo  QUICK START:
echo    1. Double-click "Injector.dll" in the project window
echo    2. CodeBrowser opens with full analysis ready
echo    3. See RE_WALKTHROUGH.md for the guided tour
echo.

set JAVA_HOME=C:\Program Files\Eclipse Adoptium\jdk-21.0.10.7-hotspot
C:\ghidra\ghidraRun.bat
