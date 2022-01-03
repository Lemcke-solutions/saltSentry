//Handle webserver root request
void handleRoot() {
  Serial.println("Config page is requested");
  String addy = server.client().remoteIP().toString();

  //determine if this user is connected to the AP or comming from the network the device is connected to
  //when connected to AP, configuration is not allowed
  if (addy == "192.168.4.2"){
      server.send(200, "text/html", "The Salt sentry can be configured on address http:// " + WiFi.localIP().toString() + " when connected to wifi network " + WiFi.SSID());
  } else {

      String configPage = FPSTR(config_page);
    configPage.replace("{1}", mqtt_server);
    configPage.replace("{2}", mqtt_port);
    configPage.replace("{3}", mqtt_username);
    configPage.replace("{4}", mqtt_password);
    configPage.replace("{5}", mqtt_topic);
    configPage.replace("{6}", mqtt_status);
    configPage.replace("{7}", dz_idx);
    configPage.replace("{8}", oh_itemid);
    configPage.replace("{9}", min_range);
    configPage.replace("{10}", max_range);
    configPage.replace("{11}", currentFirmwareVersion);
    
    server.send(200, "text/html", configPage);
  }
}
