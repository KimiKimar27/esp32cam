/*
brownout
xclk
ap (const ip)
img quality (delay after 1st)
deep sleep
*/

#include "WiFi.h"
#include "SPIFFS.h"
#include "FS.h"
#include "esp_camera.h"
#include "ESPmDNS.h"
#include "ESP32WebServer.h"

#include "camera_pins.h"
#include "utils.h"

#define uS_TO_MIN_FACTOR 60000000  // Conversion factor for micro seconds to minutes
#define TIME_TO_SLEEP 10           // Duration ESP32 will go to sleep (in minutes)

#define FORMAT_SPIFFS false

const char* apSSID = "ESP32CAM";
const char* apPassword = "1234567890";
ESP32WebServer server(80);
IPAddress apIP(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

void setup() {
  Serial.begin(115200);
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_MIN_FACTOR);

  //
  // WIFI SETUP
  //
  // Activate AP
  WiFi.mode(WIFI_AP);
  delay(250);
  WiFi.softAP(apSSID, apPassword);
  delay(250);
  WiFi.softAPConfig(apIP, gateway, subnet);
  // Activate DNS (connect to esp32cam.local instead of IP address)
  if (!MDNS.begin("esp32cam")) {
    Serial.println("Error setting up MDNS responder!");
    ESP.restart();
  }
  // Create routes
  server.on("/download", download);
  // Begin server
  server.begin();
  Serial.println("HTTP server started on esp32cam.local");

  //
  // SPIFFS SETUP
  //
  if (!SPIFFS.begin(FORMAT_SPIFFS)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }

  //
  // CAMERA SETUP
  //
  // Camera init
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = 5;
  config.pin_d1 = 18;
  config.pin_d2 = 19;
  config.pin_d3 = 21;
  config.pin_d4 = 36;
  config.pin_d5 = 39;
  config.pin_d6 = 34;
  config.pin_d7 = 35;
  config.pin_xclk = 0;
  config.pin_pclk = 22;
  config.pin_vsync = 25;
  config.pin_href = 23;
  config.pin_sscb_sda = 26;
  config.pin_sscb_scl = 27;
  config.pin_pwdn = 32;
  config.pin_reset = -1;
  config.xclk_freq_hz = 20000000;
  //config.xclk_freq_hz = 10000000;
  config.pixel_format = PIXFORMAT_JPEG;
  //config.frame_size = FRAMESIZE_UXGA;
  config.frame_size = FRAMESIZE_VGA;
  config.jpeg_quality = 10;
  config.fb_count = 2;
  // Start the camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  } else Serial.println("Camera initialized");
  // Image settings
  sensor_t* s = esp_camera_sensor_get();
  s->set_brightness(s, 0);                  // -2 to 2
  s->set_contrast(s, 0);                    // -2 to 2
  s->set_saturation(s, 0);                  // -2 to 2
  s->set_special_effect(s, 0);              // 0 to 6 (0 - No Effect, 1 - Negative, 2 - Grayscale, 3 - Red Tint, 4 - Green Tint, 5 - Blue Tint, 6 - Sepia)
  s->set_whitebal(s, 1);                    // 0 = disable , 1 = enable
  s->set_awb_gain(s, 1);                    // 0 = disable , 1 = enable
  s->set_wb_mode(s, 0);                     // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
  s->set_exposure_ctrl(s, 1);               // 0 = disable , 1 = enable
  s->set_aec2(s, 0);                        // 0 = disable , 1 = enable
  s->set_ae_level(s, 0);                    // -2 to 2
  s->set_aec_value(s, 300);                 // 0 to 1200
  s->set_gain_ctrl(s, 1);                   // 0 = disable , 1 = enable
  s->set_agc_gain(s, 0);                    // 0 to 30
  s->set_gainceiling(s, (gainceiling_t)0);  // 0 to 6
  s->set_bpc(s, 0);                         // 0 = disable , 1 = enable
  s->set_wpc(s, 1);                         // 0 = disable , 1 = enable
  s->set_raw_gma(s, 1);                     // 0 = disable , 1 = enable
  s->set_lenc(s, 1);                        // 0 = disable , 1 = enable
  s->set_hmirror(s, 0);                     // 0 = disable , 1 = enable
  s->set_vflip(s, 0);                       // 0 = disable , 1 = enable
  s->set_dcw(s, 1);                         // 0 = disable , 1 = enable
  s->set_colorbar(s, 0);                    // 0 = disable , 1 = enable

  //
  // EXECUTE FUNCTIONS
  //
  capturePhoto();          // Save photo to SPIFFS
  esp_camera_deinit();     // Deinitialize camera
  delay(250);
}

void capturePhoto() {
  // Store image in buffer
  Serial.println("Capturing image...");
  camera_fb_t* fb = esp_camera_fb_get();
  delay(1000);
  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }

  // Save the captured image to SPIFFS
  File file = SPIFFS.open("/image.jpg", FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
  } else {
    file.write(fb->buf, fb->len);
    Serial.println("Image saved to SPIFFS");
  }
  file.close();

  // Return the frame buffer to the camera
  esp_camera_fb_return(fb);
}

void download() {
  File image = SPIFFS.open("/image.jpg");
  if (image) 
  {
    server.sendHeader("Content-Type", "text/text");
    server.sendHeader("Content-Disposition", "attachment; filename=image.jpg");
    server.sendHeader("Connection", "close");
    server.streamFile(image, "application/octet-stream");
    image.close();
  } else Serial.println("Image not found"); 
}

void loop() {
  server.handleClient();
  if (false) {
    // go to deep sleep after like 20 seconds, to allow the raspberry pi to connect and download
    Serial.println("This should never be called");
    Serial.flush();          // Waits for the transmission of outgoing serial data to complete
    esp_deep_sleep_start();  // Go to deep sleep
  }
}
