# Example blynk app. for ESP32-LyraT V4.3 Evb.
Based on [e-asphyx/esp-blynk-app](https://github.com/e-asphyx/esp-idf-blynk), reconfigure ESP32-LyraT Evb.


## Configuration
- ESP32-LyraT: 
  - Used Rec button: Toggle Green LED ON/OFF.
  - Green LED: Indicate ON/OFF.

  ![image](https://user-images.githubusercontent.com/26864945/73918834-bba46a00-4905-11ea-85f9-435448bf5c97.png)
  
- Blynk App. Settings:
  - 1 Button: Toggle Green LED ON/OFF.
  - 1 Notifications: Notify Wifi disconnection.
  - 1 Labeled Value: Get status Green LED (1 sec interval). To check for changes caused by ESP32's Rec button actions.

  ![image](https://user-images.githubusercontent.com/26864945/73923157-bc40fe80-490d-11ea-8d08-a2217d259776.png)
  
- Project
  ```c
  # make -j8 menuconfig // configure WiFi SSID, PW, Blynk token, server
  ```

## Run Blynk App.
- Control Green LED using App.'s button.
- Control Green LED using Evb's button.
- Share Green LED status.
