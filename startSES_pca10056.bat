call setupEnv.bat

start "" /B "C:\Program Files\SEGGER\SEGGER Embedded Studio for ARM 3.40\bin\emStudio.exe" -D NRF_SDK=%NRF_SDK% pca10056\s140\ses\usbd_ble_uart_pca10056_s140.emProject
