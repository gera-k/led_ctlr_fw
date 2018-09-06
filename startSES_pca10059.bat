call setupEnv.bat

start "" /B "C:\Program Files\SEGGER\SEGGER Embedded Studio for ARM 3.50\bin\emStudio.exe" -D NRF_SDK=%NRF_SDK% pca10059\s140\ses\led_ctlr_pca10059_s140.emProject
