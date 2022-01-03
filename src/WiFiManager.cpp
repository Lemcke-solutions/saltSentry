/**************************************************************
   WiFiManager is a library for the ESP8266/Arduino platform
   (https://github.com/esp8266/Arduino) to enable easy
   configuration and reconfiguration of WiFi credentials using a Captive Portal
   inspired by:
   http://www.esp8266.com/viewtopic.php?f=29&t=2520
   https://github.com/chriscook8/esp-arduino-apboot
   https://github.com/esp8266/Arduino/tree/esp8266/hardware/esp8266com/esp8266/libraries/DNSServer/examples/CaptivePortalAdvanced
   Built by AlexT https://github.com/tzapu
   Licensed under MIT license
 **************************************************************/

#include "WiFiManager.h"

WiFiManagerParameter::WiFiManagerParameter(const char *custom) {
  _id = NULL;
  _placeholder = NULL;
  _length = 0;
  _value = NULL;

  _customHTML = custom;
}

WiFiManagerParameter::WiFiManagerParameter(const char *id, const char *placeholder, const char *defaultValue, int length) {
  init(id, placeholder, defaultValue, length, "");
}

WiFiManagerParameter::WiFiManagerParameter(const char *id, const char *placeholder, const char *defaultValue, int length, const char *custom) {
  init(id, placeholder, defaultValue, length, custom);
}

void WiFiManagerParameter::init(const char *id, const char *placeholder, const char *defaultValue, int length, const char *custom) {
  _id = id;
  _placeholder = placeholder;
  _length = length;
  _value = new char[length + 1];
  for (int i = 0; i < length; i++) {
    _value[i] = 0;
  }
  if (defaultValue != NULL) {
    strncpy(_value, defaultValue, length);
  }

  _customHTML = custom;
}

const char* WiFiManagerParameter::getValue() {
  return _value;
}
const char* WiFiManagerParameter::getID() {
  return _id;
}
const char* WiFiManagerParameter::getPlaceholder() {
  return _placeholder;
}
int WiFiManagerParameter::getValueLength() {
  return _length;
}
const char* WiFiManagerParameter::getCustomHTML() {
  return _customHTML;
}

WiFiManager::WiFiManager() {
}

void WiFiManager::addParameter(WiFiManagerParameter *p) {
  _params[_paramsCount] = p;
  _paramsCount++;
  DEBUG_WM("Adding parameter");
  DEBUG_WM(p->getID());
}

void WiFiManager::setupConfigPortal() {
  dnsServer.reset(new DNSServer());
  server.reset(new ESP8266WebServer(80));

  DEBUG_WM(F(""));
  _configPortalStart = millis();

  DEBUG_WM(F("Configuring access point... "));
  DEBUG_WM(_apName);
  if (_apPassword != NULL) {
    if (strlen(_apPassword) < 8 || strlen(_apPassword) > 63) {
      // fail passphrase to short or long!
      DEBUG_WM(F("Invalid AccessPoint password. Ignoring"));
      _apPassword = NULL;
    }
    DEBUG_WM(_apPassword);
  }

  //optional soft ip config
  if (_ap_static_ip) {
    DEBUG_WM(F("Custom AP IP/GW/Subnet"));
    WiFi.softAPConfig(_ap_static_ip, _ap_static_gw, _ap_static_sn);
  }

  if (_apPassword != NULL) {
    WiFi.softAP(_apName, _apPassword);//password option
  } else {
    WiFi.softAP(_apName);
  }

  delay(500); // Without delay I've seen the IP address blank
  DEBUG_WM(F("AP IP address: "));
  DEBUG_WM(WiFi.softAPIP());

  /* Setup the DNS server redirecting all the domains to the apIP */
  dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer->start(DNS_PORT, "*", WiFi.softAPIP());

  /* Setup web pages: root, wifi config pages, SO captive portal detectors and not found. */
  server->on("/", std::bind(&WiFiManager::handleRoot, this));
  server->on("/wifi", std::bind(&WiFiManager::handleWifi, this, true));
  server->on("/0wifi", std::bind(&WiFiManager::handleWifi, this, false));
  server->on("/wifisave", std::bind(&WiFiManager::handleWifiSave, this));
  server->on("/i", std::bind(&WiFiManager::handleInfo, this));
  server->on("/r", std::bind(&WiFiManager::handleReset, this));
  //server->on("/generate_204", std::bind(&WiFiManager::handle204, this));  //Android/Chrome OS captive portal check.
  server->on("/fwlink", std::bind(&WiFiManager::handleRoot, this));  //Microsoft captive portal. Maybe not needed. Might be handled by notFound handler.
  server->onNotFound (std::bind(&WiFiManager::handleNotFound, this));
  server->begin(); // Web server start
  DEBUG_WM(F("HTTP server started"));

}

boolean WiFiManager::autoConnect() {
  String ssid = "ESP" + String(ESP.getChipId());
  return autoConnect(ssid.c_str(), NULL);
}

boolean WiFiManager::autoConnect(char const *apName, char const *apPassword) {
  DEBUG_WM(F(""));
  DEBUG_WM(F("AutoConnect"));

  // read eeprom for ssid and pass
  //String ssid = getSSID();
  //String pass = getPassword();

  // attempt to connect; should it fail, fall back to AP
  WiFi.mode(WIFI_STA);

  if (connectWifi("", "") == WL_CONNECTED)   {
    DEBUG_WM(F("IP Address:"));
    DEBUG_WM(WiFi.localIP());
    //connected
    return true;
  }

  return startConfigPortal(apName, apPassword);
}

boolean  WiFiManager::startConfigPortal(char const *apName, char const *apPassword) {
  //setup AP
  WiFi.mode(WIFI_AP_STA);
  DEBUG_WM("SET AP STA");

  _apName = apName;
  _apPassword = apPassword;

  //notify we entered AP mode
  if ( _apcallback != NULL) {
    _apcallback(this);
  }

  connect = false;
  setupConfigPortal();

  while (_configPortalTimeout == 0 || millis() < _configPortalStart + _configPortalTimeout) {
    //DNS
    dnsServer->processNextRequest();
    //HTTP
    server->handleClient();


    if (connect) {
      connect = false;
      delay(2000);
      DEBUG_WM(F("Connecting to new AP"));

      // using user-provided  _ssid, _pass in place of system-stored ssid and pass
      if (connectWifi(_ssid, _pass) != WL_CONNECTED) {
        DEBUG_WM(F("Failed to connect."));
      } else {
        //connected
        WiFi.mode(WIFI_STA);
        //notify that configuration has changed and any optional parameters should be saved
        if ( _savecallback != NULL) {
          //todo: check if any custom parameters actually exist, and check if they really changed maybe
          _savecallback();
        }
        break;
      }

      if (_shouldBreakAfterConfig) {
        //flag set to exit after config after trying to connect
        //notify that configuration has changed and any optional parameters should be saved
        if ( _savecallback != NULL) {
          //todo: check if any custom parameters actually exist, and check if they really changed maybe
          _savecallback();
        }
        break;
      }
    }
    yield();
  }

  server.reset();
  dnsServer.reset();

  return  WiFi.status() == WL_CONNECTED;
}


int WiFiManager::connectWifi(String ssid, String pass) {
  DEBUG_WM(F("Connecting as wifi client..."));

  // check if we've got static_ip settings, if we do, use those.
  if (_sta_static_ip) {
    DEBUG_WM(F("Custom STA IP/GW/Subnet"));
    WiFi.config(_sta_static_ip, _sta_static_gw, _sta_static_sn);
    DEBUG_WM(WiFi.localIP());
  }
  //fix for auto connect racing issue
  if (WiFi.status() == WL_CONNECTED) {
    DEBUG_WM("Already connected. Bailing out.");
    return WL_CONNECTED;
  }
  //check if we have ssid and pass and force those, if not, try with last saved values
  if (ssid != "") {
    WiFi.begin(ssid.c_str(), pass.c_str());
  } else {
    if (WiFi.SSID()) {
      DEBUG_WM("Using last saved values, should be faster");
      //trying to fix connection in progress hanging
      ETS_UART_INTR_DISABLE();
      wifi_station_disconnect();
      ETS_UART_INTR_ENABLE();

      WiFi.begin();
    } else {
      DEBUG_WM("No saved credentials");
    }
  }

  int connRes = waitForConnectResult();
  DEBUG_WM ("Connection result: ");
  DEBUG_WM ( connRes );
  //not connected, WPS enabled, no pass - first attempt
  if (_tryWPS && connRes != WL_CONNECTED && pass == "") {
    startWPS();
    //should be connected at the end of WPS
    connRes = waitForConnectResult();
  }
  return connRes;
}

uint8_t WiFiManager::waitForConnectResult() {
  if (_connectTimeout == 0) {
    return WiFi.waitForConnectResult();
  } else {
    DEBUG_WM (F("Waiting for connection result with time out"));
    unsigned long start = millis();
    boolean keepConnecting = true;
    uint8_t status;
    while (keepConnecting) {
      status = WiFi.status();
      if (millis() > start + _connectTimeout) {
        keepConnecting = false;
        DEBUG_WM (F("Connection timed out"));
      }
      if (status == WL_CONNECTED || status == WL_CONNECT_FAILED) {
        keepConnecting = false;
      }
      delay(100);
    }
    return status;
  }
}

void WiFiManager::startWPS() {
  DEBUG_WM("START WPS");
  WiFi.beginWPSConfig();
  DEBUG_WM("END WPS");
}
/*
  String WiFiManager::getSSID() {
  if (_ssid == "") {
    DEBUG_WM(F("Reading SSID"));
    _ssid = WiFi.SSID();
    DEBUG_WM(F("SSID: "));
    DEBUG_WM(_ssid);
  }
  return _ssid;
  }

  String WiFiManager::getPassword() {
  if (_pass == "") {
    DEBUG_WM(F("Reading Password"));
    _pass = WiFi.psk();
    DEBUG_WM("Password: " + _pass);
    //DEBUG_WM(_pass);
  }
  return _pass;
  }
*/
String WiFiManager::getConfigPortalSSID() {
  return _apName;
}

void WiFiManager::resetSettings() {
  DEBUG_WM(F("settings invalidated"));
  DEBUG_WM(F("THIS MAY CAUSE AP NOT TO START UP PROPERLY. YOU NEED TO COMMENT IT OUT AFTER ERASING THE DATA."));
  WiFi.disconnect(true);
  //delay(200);
}
void WiFiManager::setTimeout(unsigned long seconds) {
  setConfigPortalTimeout(seconds);
}

void WiFiManager::setConfigPortalTimeout(unsigned long seconds) {
  _configPortalTimeout = seconds * 1000;
}

void WiFiManager::setConnectTimeout(unsigned long seconds) {
  _connectTimeout = seconds * 1000;
}

void WiFiManager::setDebugOutput(boolean debug) {
  _debug = debug;
}

void WiFiManager::setAPStaticIPConfig(IPAddress ip, IPAddress gw, IPAddress sn) {
  _ap_static_ip = ip;
  _ap_static_gw = gw;
  _ap_static_sn = sn;
}

void WiFiManager::setSTAStaticIPConfig(IPAddress ip, IPAddress gw, IPAddress sn) {
  _sta_static_ip = ip;
  _sta_static_gw = gw;
  _sta_static_sn = sn;
}

void WiFiManager::setMinimumSignalQuality(int quality) {
  _minimumQuality = quality;
}

void WiFiManager::setBreakAfterConfig(boolean shouldBreak) {
  _shouldBreakAfterConfig = shouldBreak;
}

/** Handle root or redirect to captive portal */
void WiFiManager::handleRoot() {
  DEBUG_WM(F("Handle root"));
  if (captivePortal()) { // If caprive portal redirect instead of displaying the page.
    return;
  }

  String page = FPSTR(HTTP_HEADER);
  page.replace("{v}", "Options");
  page += FPSTR(HTTP_SCRIPT);
  page += FPSTR(HTTP_STYLE);
  page += _customHeadElement;
  page += FPSTR(HTTP_HEAD_END);
  page += "<img style=\"display: block;  margin-left: auto; margin-right: auto; margin-botom: 5px;  width: 70%;\" src=\"data:image/jpeg;base64,/9j/4AAQSkZJRgABAQEASABIAAD/4QBmRXhpZgAATU0AKgAAAAgABAEaAAUAAAABAAAAPgEbAAUAAAABAAAARgEoAAMAAAABAAIAAAExAAIAAAAQAAAATgAAAAAAAABIAAAAAQAAAEgAAAABcGFpbnQubmV0IDQuMy4xAP/hHU1odHRwOi8vbnMuYWRvYmUuY29tL3hhcC8xLjAvADw/eHBhY2tldCBiZWdpbj0i77u/IiBpZD0iVzVNME1wQ2VoaUh6cmVTek5UY3prYzlkIj8+DQo8eDp4bXBtZXRhIHhtbG5zOng9ImFkb2JlOm5zOm1ldGEvIiB4OnhtcHRrPSJBZG9iZSBYTVAgQ29yZSA1LjYtYzAxMSA3OS4xNTYyODksIDIwMTQvMDMvMzEtMjM6Mzk6MTIgICAgICAgICI+DQogIDxyZGY6UkRGIHhtbG5zOnJkZj0iaHR0cDovL3d3dy53My5vcmcvMTk5OS8wMi8yMi1yZGYtc3ludGF4LW5zIyI+DQogICAgPHJkZjpEZXNjcmlwdGlvbiByZGY6YWJvdXQ9IiIgeG1sbnM6ZGM9Imh0dHA6Ly9wdXJsLm9yZy9kYy9lbGVtZW50cy8xLjEvIiB4bWxuczp4bXA9Imh0dHA6Ly9ucy5hZG9iZS5jb20veGFwLzEuMC8iIHhtbG5zOnhtcEdJbWc9Imh0dHA6Ly9ucy5hZG9iZS5jb20veGFwLzEuMC9nL2ltZy8iIHhtbG5zOnhtcE1NPSJodHRwOi8vbnMuYWRvYmUuY29tL3hhcC8xLjAvbW0vIiB4bWxuczpzdFJlZj0iaHR0cDovL25zLmFkb2JlLmNvbS94YXAvMS4wL3NUeXBlL1Jlc291cmNlUmVmIyIgeG1sbnM6c3RFdnQ9Imh0dHA6Ly9ucy5hZG9iZS5jb20veGFwLzEuMC9zVHlwZS9SZXNvdXJjZUV2ZW50IyIgeG1sbnM6aWxsdXN0cmF0b3I9Imh0dHA6Ly9ucy5hZG9iZS5jb20vaWxsdXN0cmF0b3IvMS4wLyIgeG1sbnM6cGRmPSJodHRwOi8vbnMuYWRvYmUuY29tL3BkZi8xLjMvIj4NCiAgICAgIDxkYzpmb3JtYXQ+aW1hZ2UvanBlZzwvZGM6Zm9ybWF0Pg0KICAgICAgPGRjOnRpdGxlPg0KICAgICAgICA8cmRmOkFsdD4NCiAgICAgICAgICA8cmRmOmxpIHhtbDpsYW5nPSJ4LWRlZmF1bHQiPlByaW50PC9yZGY6bGk+DQogICAgICAgIDwvcmRmOkFsdD4NCiAgICAgIDwvZGM6dGl0bGU+DQogICAgICA8eG1wOk1ldGFkYXRhRGF0ZT4yMDIxLTA3LTA4VDEzOjE0OjQ5KzA1OjMwPC94bXA6TWV0YWRhdGFEYXRlPg0KICAgICAgPHhtcDpNb2RpZnlEYXRlPjIwMjEtMDctMDhUMDc6NDQ6NTJaPC94bXA6TW9kaWZ5RGF0ZT4NCiAgICAgIDx4bXA6Q3JlYXRlRGF0ZT4yMDIxLTA3LTA4VDEzOjE0OjQ5KzA1OjMwPC94bXA6Q3JlYXRlRGF0ZT4NCiAgICAgIDx4bXA6Q3JlYXRvclRvb2w+QWRvYmUgSWxsdXN0cmF0b3IgQ0MgMjAxNCAoV2luZG93cyk8L3htcDpDcmVhdG9yVG9vbD4NCiAgICAgIDx4bXA6VGh1bWJuYWlscz4NCiAgICAgICAgPHJkZjpBbHQ+DQogICAgICAgICAgPHJkZjpsaSByZGY6cGFyc2VUeXBlPSJSZXNvdXJjZSI+DQogICAgICAgICAgICA8eG1wR0ltZzp3aWR0aD4yNTY8L3htcEdJbWc6d2lkdGg+DQogICAgICAgICAgICA8eG1wR0ltZzpoZWlnaHQ+Mjg8L3htcEdJbWc6aGVpZ2h0Pg0KICAgICAgICAgICAgPHhtcEdJbWc6Zm9ybWF0PkpQRUc8L3htcEdJbWc6Zm9ybWF0Pg0KICAgICAgICAgICAgPHhtcEdJbWc6aW1hZ2U+LzlqLzRBQVFTa1pKUmdBQkFnRUFTQUJJQUFELzdRQXNVR2h2ZEc5emFHOXdJRE11TUFBNFFrbE5BKzBBQUFBQUFCQUFTQUFBQUFFQQ0KQVFCSUFBQUFBUUFCLys0QURrRmtiMkpsQUdUQUFBQUFBZi9iQUlRQUJnUUVCQVVFQmdVRkJna0dCUVlKQ3dnR0JnZ0xEQW9LQ3dvSw0KREJBTURBd01EQXdRREE0UEVBOE9EQk1URkJRVEV4d2JHeHNjSHg4Zkh4OGZIeDhmSHdFSEJ3Y05EQTBZRUJBWUdoVVJGUm9mSHg4Zg0KSHg4Zkh4OGZIeDhmSHg4Zkh4OGZIeDhmSHg4Zkh4OGZIeDhmSHg4Zkh4OGZIeDhmSHg4Zkh4OGZIeDhmLzhBQUVRZ0FIQUVBQXdFUg0KQUFJUkFRTVJBZi9FQWFJQUFBQUhBUUVCQVFFQUFBQUFBQUFBQUFRRkF3SUdBUUFIQ0FrS0N3RUFBZ0lEQVFFQkFRRUFBQUFBQUFBQQ0KQVFBQ0F3UUZCZ2NJQ1FvTEVBQUNBUU1EQWdRQ0JnY0RCQUlHQW5NQkFnTVJCQUFGSVJJeFFWRUdFMkVpY1lFVU1wR2hCeFd4UWlQQg0KVXRIaE14Wmk4Q1J5Z3ZFbFF6UlRrcUt5WTNQQ05VUW5rNk96TmhkVVpIVEQwdUlJSm9NSkNoZ1poSlJGUnFTMFZ0TlZLQnJ5NC9QRQ0KMU9UMFpYV0ZsYVcxeGRYbDlXWjJocGFtdHNiVzV2WTNSMWRuZDRlWHA3ZkgxK2YzT0VoWWFIaUltS2k0eU5qbytDazVTVmxwZVltWg0KcWJuSjJlbjVLanBLV21wNmlwcXF1c3JhNnZvUkFBSUNBUUlEQlFVRUJRWUVDQU1EYlFFQUFoRURCQ0VTTVVFRlVSTmhJZ1p4Z1pFeQ0Kb2JId0ZNSFI0U05DRlZKaWN2RXpKRFJEZ2hhU1V5V2lZN0xDQjNQU05lSkVneGRVa3dnSkNoZ1pKalpGR2lka2RGVTM4cU96d3lncA0KMCtQemhKU2t0TVRVNVBSbGRZV1ZwYlhGMWVYMVJsWm1kb2FXcHJiRzF1YjJSMWRuZDRlWHA3ZkgxK2YzT0VoWWFIaUltS2k0eU5qbw0KK0RsSldXbDVpWm1wdWNuWjZma3FPa3BhYW5xS21xcTZ5dHJxK3YvYUFBd0RBUUFDRVFNUkFEOEE5UlgxN2JXRmxjWDEyNGl0YldKNQ0KNTVEMFdPTlN6TjlBR0t2UHZ5cy9NTzk4dzZscTFocWhDM0Jta3ViQ09uRXh3Z3FzbG8yd3JKYmM0K2YrdjdZcGVrWW9kaXJzVmRpcg0Kc1ZkaXJzVmRpcnNWZGlyc1ZkaXFHMVNXU0xUTHVXTnVNa2NNakl3N0ZVSkJ4VjRMcDNuaWZWTkJndnZNRitzU3l1c2JOTnpsaGFRVw0KeVRnbEQ5WTRsaTdLdkdBNzdEZk50ZzFNWXc0aUszcmI4RC9kT2kxT2luTElZQ1hGWXVwYi9mWS8ySHhUSDh2UFBsOXFYbkhTOU90cA0KSklMQjV5c3NEU0ZoSWpXbDY5R2pabkVaU1MxVS9EeFBZZ1ppNnJVUm55SHgydjhBSHpjL1I2V1dQZVIrRzlmYitnRDNQY3N3M09kaQ0KcnNWZGlyc1ZkaXEweVJoMWpMQU93SlZDUlVoYVZJSHRVWXE1NUk0MTVTTUVXb0hKaUFLazBBMzhTY1ZYWXE3RlhZcTdGWFlxN0ZYWQ0KcTg4L00zWElydlNMelRZYmlPQzJoS3lYVnpNcGVKbWdsUnpFd1hjeDhnRWw0N3NXNEtDeEl6TEdtckdaUzI3dng1OVBtNFIxZDVSQw0KSXZ2L0FFL0xyOHVmTG1Ya2k1ZTI4eVJUcklMZTV0NTVybjk2T2N4bFZKRmxna0txb0o0M0JhZCtyb3FPaWppd1dyQmpFNVVXL1U1RA0KQ0JJSDQvSDYzdDJwZWVkSDAzeXRjK1lydFpQcTlsd1c2dDQrRFNwSTdyR0YrSmtUZHBCUml3WGo4VmFZTXVJd05GY0dZWkkyUGo1Zg0KajdlYURnL05EeTBkT1cvdkk3eXdoYVV3QXpXMGtrZklTQ0pUOVl0aFBiY0pIUEZIOVhpeDZIWTVVM0tmbDc4MWZMZXU2VnErcDJzTg0KNUZiYUxiSmVYbnJSS0NZNUxjM0FFZkIzVm1DRGNWNisyK0txZW4vbTc1VHVkT2JVTGxicXd0K2FKQ1pJZnJCbTV3bTRKaU5pMTJyOA0KSWxKa29hcCszVEZXOVQvT0x5Rlkya3R5dDdKZUNOUXlwYlF5TjZ0ZlNMTEZJNFNGM1JiaEdaQS9JQTlNVlJjMzVvK1JvZVJtMUZvaw0KVTNJOVI3ZTVXTm1zMDUzQ281aUN1MFk2cXBKcnQxMnhWazl2UEhjUVJ6eEVtS1ZWZENRVkpWaFVWVmdDTnV4eFZmaXJzVmRpcnNWZA0KaXFsZFc2M05yTmJzU0ZtUm8ySTZnTUNOdnZ4Vjh0K2JQSy9tZnlyWS9vblZiV21ubzRObmZJeEZ0SklJUmJodlVxRkhPTkRXS1JrUA0KTCtkZmd5ZkdlSGg2WGJEd3h4Y1hXcVpqK1Rma1B6RSt2MlBtTzhoYTAweXlWbXRtbVVvOHdhS2VLTllZaUVJaUMzVE9YWkVCK0VJZw0KRlRrR3g3eGloMkt1eFYyS3V4VjJLdlAvQU16L0FDZ3ZtYlVOTHRQcjBsaFMwMUJacFVaRlZvSlBxL3FxL05KTmp4RzRwbVRoeHhNVA0KS1Y3RURienY5VGlaODA0empHTmJpUjM4cS9Xd3kxL0wrS1ptMWNlWTdyVUk0N3lCYnlGbWpWbjllL2p1S2tHQlR3ZWJqS0NPdmJNaQ0KV2xnRHdrU0JvbnAwRnVMSFhUTWVJR01nSkFkZXBBL1RZZW1MNXhhUFV0UWl1MWdqdExTT2FTSXE3TkxJSVNBU0tLWTk2N3J5cUQyeQ0KSDVPNHhJdXpYMi9hMmZucW5JU29SaUQxM05mWjl0aFQwL3pzYnV5MCtZcEVzdHhlRzB1eHlQR01lbThpc0NmRUtPdnZrc21pNFpTRw0KKzBiSDNNY1hhSEZHSjJzeTRUNWJFb2pTZGYxaVpkVm12WTdRUWFielV0Rkl5aDNTTlphODVLSXFjVyswY3ExR0tFQU9HN08rN2RwYw0KK1RKS1FrSTFFMXRmY0QrbDVkUCtmWG1wTk9oZE5QMDk5UWx1QkcxcW4xcVF4eHRFMGk4Z0ZYa1R3MmVObVVqd3pFYzZremcvT3ZYVw0KMVMyMGVYVGJkTDY4dTlOdGJlYjk2SUQ5Y2dqa25WcW5tam8weWxhamRhOXhpcXpVdnpYL0FEQTB6VHJ1OXZiVFNGUzAxZzZHN1IvWA0KSEFtU015dkp4VU01ajRnVW9PWHRpcWFhWCtaZm5TL2wwSzIvUWx2RE41bWlTYlRKaTcrbkdrTEg2NDh5c1VaaDZRV1dJTFRad0RVMQ0KeFZEL0FKZ2ZsOTVrdXRSUWFKRTh1bUdMbGJyQ1lBOXRjK294TGNaNUlBd0N5dDZSNUV4a3NSdndLM1pjOHAxYlJoMDhNWkpIWDhWNw0KcnY1cERjK1FQUFU4dG5KK2gzaFd3aFdHQ08wRmxhbWtKTFFONmkzc2hWNFpEeVNUaVNLbXZJTXltbHZlbGFmNUhaL0tMYVJxZDNKRg0KcUY1QkRIcUY3cDdlaVEwUUE0UTFVajB4UWppeTBZRTFHNXk3TG5sT3I2T1BoMDhjZDhQWDlISWU0SkYveW9QeWEwNG5rdWIxM014bg0KbUgraW9zanRJa3BvaVc2TEZ2RW8vY2hLalk1UzNwMTVYL0xIUnZMVmxxVm5wMTlmK2xxY01VRXJOTEdza2Zvdytnc2tNa1VjVG8vSA0KdlhydUtZcWswMzVDK1RwNFpWbnVMdVc0bGVObXVtRm56cEZDOEFYMHhiQ0ExV1FrdTBaZmxSdVZSaXE2ZjhpZkowdW14YWN0eGZRMg0KMEU4MXhDSTVJcXEwOGNNUlVGb20rRlZ0azQxMzYxSnhXMnZNUDVRMnQ1b09qZVg3Ti9Wc0xMVkRxRjVkWGNsSi9TbGtkNTQ0eEZEdw0KYjFQVkswUEVEYnJpcjBmRlhZcTdGWFlxN0ZYai93Q1kvbWE2MEg4d2JTN2ZWSnJtMEVWdXFhRmFYTThFMFQ4cEhNNzJxRDA3dUVoUA0KM2dMQmh0dUIxVXNMMXI4My9ObXU2UGJYbHRkL1VMMjJPb3F5NmV6UkxJaTJjTTBUeVJDVzQzVW1TbFhQUTlDRGlyTnZ6WDErYURTLw0KTDJwcHIwVnhhbU9lU2V6MDY2bTA4NmlmM2FxOXBKRWJnRXhzM0pZM2NnKytLRXJuL09yenkyc1htbld0bHAwSWgxRVdFU3pwSkpMRw0KdnF5eGoxVVM1RHMxSTFQSXFuZWdOUWNVMHQwUDgxUE45N3FwYUc3c29adFl1OUdoaDAyNmlsbjlCTDJ5VXpUd0t0eEEzcENZZE45eg0KOXF1S3M2L0xiemw1bzh4M2Vzd2F6WlFXZzBhVVdNclFySVBVdTBaL1dLbDNmNEFucGtEcnYxeFF6ckZYWXE3RlhZcXgzem41Y20xZQ0KeVY3WnY5SmdTUkJBVHhXV09RcVhUbCt5MVkxS25wWFlpaHpNMGVvR09XL0kvWitMZGYyaHBEbGpZNWk5dThHckgyRDlPeVJhUm8ydQ0KNnpxaVhPcVJHMWd0NHJSSkg0aU5wREdzYy9wS2dBMkUyN01mRGl0Tjh5OHViSGpoVURaSmwra1g4djFsd2NHRExtbnhaQndnQ1B4cQ0KcFYvcHVaK0E2c3piUmRJYWFXWTJVQm1tSU0wbnBweWNxd1ljalRmNGhYTlo0MDZxelFkeCtYeDJUd2l6NU91OUYwaThMbTdzb1p6SQ0KeXM1a2pWcXNnS3FUVWRRQ1FNTU0wNDhpUXVUVDQ1L1ZFSDNoVXQ5TjArMmdlQzN0b29vSk5wSWtSVlZoeENiZ0NoK0VBWkdjNVM1bQ0KMmVQRkdBcUlBOXlUeWZsOTVGa3RqYk41ZjA4MjVrTXhpK3JSQmZVSXB5b0Y2MHlETkdXM2xUeXpiSXNkdnBWcEVpU1F6b0ZoakhHVw0KMlVKRElOdG1qVlFGUGJGVlUrWDlETlNkUHR5VGRmWHorNlQvQUhycHg5ZnAvZVUyNWRjVmRiYUJvbHI5VCtyV0Z2RCtqeEl0aHdqVg0KZlFFMzk0SXFENE9mN1ZPdUtvL0ZYWXE3RlhZcTdGWFlxN0ZYWXE3RlhZcTdGWFlxN0ZYWXE3RlhZcTdGWFlxeC93QWtmNFUvUk0zKw0KR2VYMVQ2MVA5YjlYNng2LzF2bCsrOWY2MSsvOVN2WG52aXJJTVZkaXJzVmRpcnNWZGlyc1ZkaXJzVmRpcnNWZGlyc1ZmLy9aPC94bXBHSW1nOmltYWdlPg0KICAgICAgICAgIDwvcmRmOmxpPg0KICAgICAgICA8L3JkZjpBbHQ+DQogICAgICA8L3htcDpUaHVtYm5haWxzPg0KICAgICAgPHhtcE1NOkluc3RhbmNlSUQ+eG1wLmlpZDpjY2IyM2U4ZS01NTYyLTFiNDktODY5Zi05MjA5OTM3Yzg1OTU8L3htcE1NOkluc3RhbmNlSUQ+DQogICAgICA8eG1wTU06RG9jdW1lbnRJRD54bXAuZGlkOmNjYjIzZThlLTU1NjItMWI0OS04NjlmLTkyMDk5MzdjODU5NTwveG1wTU06RG9jdW1lbnRJRD4NCiAgICAgIDx4bXBNTTpPcmlnaW5hbERvY3VtZW50SUQ+dXVpZDo1RDIwODkyNDkzQkZEQjExOTE0QTg1OTBEMzE1MDhDODwveG1wTU06T3JpZ2luYWxEb2N1bWVudElEPg0KICAgICAgPHhtcE1NOlJlbmRpdGlvbkNsYXNzPnByb29mOnBkZjwveG1wTU06UmVuZGl0aW9uQ2xhc3M+DQogICAgICA8eG1wTU06RGVyaXZlZEZyb20gcmRmOnBhcnNlVHlwZT0iUmVzb3VyY2UiPg0KICAgICAgICA8c3RSZWY6aW5zdGFuY2VJRD54bXAuaWlkOmUxMTk2ZGQ0LTc2OGEtNzg0NC05YTg5LTA1NzYwYWQ1NzZjOTwvc3RSZWY6aW5zdGFuY2VJRD4NCiAgICAgICAgPHN0UmVmOmRvY3VtZW50SUQ+eG1wLmRpZDplMTE5NmRkNC03NjhhLTc4NDQtOWE4OS0wNTc2MGFkNTc2Yzk8L3N0UmVmOmRvY3VtZW50SUQ+DQogICAgICAgIDxzdFJlZjpvcmlnaW5hbERvY3VtZW50SUQ+dXVpZDo1RDIwODkyNDkzQkZEQjExOTE0QTg1OTBEMzE1MDhDODwvc3RSZWY6b3JpZ2luYWxEb2N1bWVudElEPg0KICAgICAgICA8c3RSZWY6cmVuZGl0aW9uQ2xhc3M+cHJvb2Y6cGRmPC9zdFJlZjpyZW5kaXRpb25DbGFzcz4NCiAgICAgIDwveG1wTU06RGVyaXZlZEZyb20+DQogICAgICA8eG1wTU06SGlzdG9yeT4NCiAgICAgICAgPHJkZjpTZXE+DQogICAgICAgICAgPHJkZjpsaSByZGY6cGFyc2VUeXBlPSJSZXNvdXJjZSI+DQogICAgICAgICAgICA8c3RFdnQ6YWN0aW9uPnNhdmVkPC9zdEV2dDphY3Rpb24+DQogICAgICAgICAgICA8c3RFdnQ6aW5zdGFuY2VJRD54bXAuaWlkOmUxMTk2ZGQ0LTc2OGEtNzg0NC05YTg5LTA1NzYwYWQ1NzZjOTwvc3RFdnQ6aW5zdGFuY2VJRD4NCiAgICAgICAgICAgIDxzdEV2dDp3aGVuPjIwMjEtMDctMDhUMTA6NDA6MjArMDU6MzA8L3N0RXZ0OndoZW4+DQogICAgICAgICAgICA8c3RFdnQ6c29mdHdhcmVBZ2VudD5BZG9iZSBJbGx1c3RyYXRvciBDQyAyMDE0IChXaW5kb3dzKTwvc3RFdnQ6c29mdHdhcmVBZ2VudD4NCiAgICAgICAgICAgIDxzdEV2dDpjaGFuZ2VkPi88L3N0RXZ0OmNoYW5nZWQ+DQogICAgICAgICAgPC9yZGY6bGk+DQogICAgICAgICAgPHJkZjpsaSByZGY6cGFyc2VUeXBlPSJSZXNvdXJjZSI+DQogICAgICAgICAgICA8c3RFdnQ6YWN0aW9uPnNhdmVkPC9zdEV2dDphY3Rpb24+DQogICAgICAgICAgICA8c3RFdnQ6aW5zdGFuY2VJRD54bXAuaWlkOmNjYjIzZThlLTU1NjItMWI0OS04NjlmLTkyMDk5MzdjODU5NTwvc3RFdnQ6aW5zdGFuY2VJRD4NCiAgICAgICAgICAgIDxzdEV2dDp3aGVuPjIwMjEtMDctMDhUMTM6MTQ6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\"></img>";
  page += FPSTR(HTTP_PORTAL_OPTIONS);
  page += FPSTR(HTTP_END);

  server->send(200, "text/html", page);



}

/** Wifi config page handler */
void WiFiManager::handleWifi(boolean scan) {

  String page = FPSTR(HTTP_HEADER);
  page.replace("{v}", "Configure Salt sentry");
  page += FPSTR(HTTP_SCRIPT);
  page += FPSTR(HTTP_STYLE);
  page += _customHeadElement;
  page += FPSTR(HTTP_HEAD_END);

  if (scan) {
    int n = WiFi.scanNetworks();
    DEBUG_WM(F("Scan done"));
    if (n == 0) {
      DEBUG_WM(F("No networks found"));
      page += F("No networks found. Refresh to scan again.");
    } else {

      //sort networks
      int indices[n];
      for (int i = 0; i < n; i++) {
        indices[i] = i;
      }

      // RSSI SORT

      // old sort
      for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
          if (WiFi.RSSI(indices[j]) > WiFi.RSSI(indices[i])) {
            std::swap(indices[i], indices[j]);
          }
        }
      }

      /*std::sort(indices, indices + n, [](const int & a, const int & b) -> bool
        {
        return WiFi.RSSI(a) > WiFi.RSSI(b);
        });*/

      // remove duplicates ( must be RSSI sorted )
      if (_removeDuplicateAPs) {
        String cssid;
        for (int i = 0; i < n; i++) {
          if (indices[i] == -1) continue;
          cssid = WiFi.SSID(indices[i]);
          for (int j = i + 1; j < n; j++) {
            if (cssid == WiFi.SSID(indices[j])) {
              DEBUG_WM("DUP AP: " + WiFi.SSID(indices[j]));
              indices[j] = -1; // set dup aps to index -1
            }
          }
        }
      }

      //display networks in page
      for (int i = 0; i < n; i++) {
        if (indices[i] == -1) continue; // skip dups
        DEBUG_WM(WiFi.SSID(indices[i]));
        DEBUG_WM(WiFi.RSSI(indices[i]));
        int quality = getRSSIasQuality(WiFi.RSSI(indices[i]));

        if (_minimumQuality == -1 || _minimumQuality < quality) {
          String item = FPSTR(HTTP_ITEM);
          String rssiQ;
          rssiQ += quality;
          item.replace("{v}", WiFi.SSID(indices[i]));
          item.replace("{r}", rssiQ);
          if (WiFi.encryptionType(indices[i]) != ENC_TYPE_NONE) {
            item.replace("{i}", "l");
          } else {
            item.replace("{i}", "");
          }
          //DEBUG_WM(item);
          page += item;
          delay(0);
        } else {
          DEBUG_WM(F("Skipping due to quality"));
        }

      }
      page += "<br/>";
    }
  }

  page += FPSTR(HTTP_FORM_START);
  char parLength[2];
  // add the extra parameters to the form
  for (int i = 0; i < _paramsCount; i++) {
    if (_params[i] == NULL) {
      break;
    }

    String pitem = FPSTR(HTTP_FORM_PARAM);
    if (_params[i]->getID() != NULL) {
      pitem.replace("{i}", _params[i]->getID());
      pitem.replace("{n}", _params[i]->getID());
      pitem.replace("{p}", _params[i]->getPlaceholder());
      snprintf(parLength, 2, "%d", _params[i]->getValueLength());
      pitem.replace("{l}", parLength);
      pitem.replace("{v}", _params[i]->getValue());
      pitem.replace("{c}", _params[i]->getCustomHTML());
    } else {
      pitem = _params[i]->getCustomHTML();
    }

    page += pitem;
  }
  if (_params[0] != NULL) {
    page += "<br/>";
  }

  if (_sta_static_ip) {

    String item = FPSTR(HTTP_FORM_PARAM);
    item.replace("{i}", "ip");
    item.replace("{n}", "ip");
    item.replace("{p}", "Static IP");
    item.replace("{l}", "15");
    item.replace("{v}", _sta_static_ip.toString());

    page += item;

    item = FPSTR(HTTP_FORM_PARAM);
    item.replace("{i}", "gw");
    item.replace("{n}", "gw");
    item.replace("{p}", "Static Gateway");
    item.replace("{l}", "15");
    item.replace("{v}", _sta_static_gw.toString());

    page += item;

    item = FPSTR(HTTP_FORM_PARAM);
    item.replace("{i}", "sn");
    item.replace("{n}", "sn");
    item.replace("{p}", "Subnet");
    item.replace("{l}", "15");
    item.replace("{v}", _sta_static_sn.toString());

    page += item;

    page += "<br/>";
  }

  page += FPSTR(HTTP_FORM_END);
  page += FPSTR(HTTP_SCAN_LINK);

  page += FPSTR(HTTP_END);

  server->send(200, "text/html", page);


  DEBUG_WM(F("Sent config page"));
}

/** Handle the WLAN save form and redirect to WLAN config page again */
void WiFiManager::handleWifiSave() {
  DEBUG_WM(F("WiFi save"));

  //SAVE/connect here
  _ssid = server->arg("s").c_str();
  _pass = server->arg("p").c_str();

  //parameters
  for (int i = 0; i < _paramsCount; i++) {
    if (_params[i] == NULL) {
      break;
    }
    //read parameter
    String value = server->arg(_params[i]->getID()).c_str();
    //store it in array
    value.toCharArray(_params[i]->_value, _params[i]->_length);
    DEBUG_WM(F("Parameter"));
    DEBUG_WM(_params[i]->getID());
    DEBUG_WM(value);
  }

  if (server->arg("ip") != "") {
    DEBUG_WM(F("static ip"));
    DEBUG_WM(server->arg("ip"));
    //_sta_static_ip.fromString(server->arg("ip"));
    String ip = server->arg("ip");
    optionalIPFromString(&_sta_static_ip, ip.c_str());
  }
  if (server->arg("gw") != "") {
    DEBUG_WM(F("static gateway"));
    DEBUG_WM(server->arg("gw"));
    String gw = server->arg("gw");
    optionalIPFromString(&_sta_static_gw, gw.c_str());
  }
  if (server->arg("sn") != "") {
    DEBUG_WM(F("static netmask"));
    DEBUG_WM(server->arg("sn"));
    String sn = server->arg("sn");
    optionalIPFromString(&_sta_static_sn, sn.c_str());
  }

  String page = FPSTR(HTTP_HEADER);
  page.replace("{v}", "Credentials Saved");
  page += FPSTR(HTTP_SCRIPT);
  page += FPSTR(HTTP_STYLE);
  page += _customHeadElement;
  page += FPSTR(HTTP_HEAD_END);
  page += FPSTR(HTTP_SAVED);
  page += FPSTR(HTTP_END);

  server->send(200, "text/html", page);

  DEBUG_WM(F("Sent wifi save page"));

  connect = true; //signal ready to connect/reset
}

/** Handle the info page */
void WiFiManager::handleInfo() {
  DEBUG_WM(F("Info"));

  String page = FPSTR(HTTP_HEADER);
  page.replace("{v}", "Info");
  page += FPSTR(HTTP_SCRIPT);
  page += FPSTR(HTTP_STYLE);
  page += _customHeadElement;
  page += FPSTR(HTTP_HEAD_END);
  page += F("<dl>");
  page += F("<dt>Chip ID</dt><dd>");
  page += ESP.getChipId();
  page += F("</dd>");
  page += F("<dt>Flash Chip ID</dt><dd>");
  page += ESP.getFlashChipId();
  page += F("</dd>");
  page += F("<dt>IDE Flash Size</dt><dd>");
  page += ESP.getFlashChipSize();
  page += F(" bytes</dd>");
  page += F("<dt>Real Flash Size</dt><dd>");
  page += ESP.getFlashChipRealSize();
  page += F(" bytes</dd>");
  page += F("<dt>Soft AP IP</dt><dd>");
  page += WiFi.softAPIP().toString();
  page += F("</dd>");
  page += F("<dt>Soft AP MAC</dt><dd>");
  page += WiFi.softAPmacAddress();
  page += F("</dd>");
  page += F("<dt>Station MAC</dt><dd>");
  page += WiFi.macAddress();
  page += F("</dd>");
  page += F("</dl>");
  page += FPSTR(HTTP_END);

  server->send(200, "text/html", page);

  DEBUG_WM(F("Sent info page"));
}

/** Handle the reset page */
void WiFiManager::handleReset() {
  DEBUG_WM(F("Reset"));

  String page = FPSTR(HTTP_HEADER);
  page.replace("{v}", "Info");
  page += FPSTR(HTTP_SCRIPT);
  page += FPSTR(HTTP_STYLE);
  page += _customHeadElement;
  page += FPSTR(HTTP_HEAD_END);
  page += F("Module will reset in a few seconds.");
  page += FPSTR(HTTP_END);
  server->send(200, "text/html", page);

  DEBUG_WM(F("Sent reset page"));
  delay(5000);
  ESP.reset();
  delay(2000);
}



//removed as mentioned here https://github.com/tzapu/WiFiManager/issues/114
/*void WiFiManager::handle204() {
  DEBUG_WM(F("204 No Response"));
  server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server->sendHeader("Pragma", "no-cache");
  server->sendHeader("Expires", "-1");
  server->send ( 204, "text/plain", "");
}*/

void WiFiManager::handleNotFound() {
  if (captivePortal()) { // If captive portal redirect instead of displaying the error page.
    return;
  }
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server->uri();
  message += "\nMethod: ";
  message += ( server->method() == HTTP_GET ) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server->args();
  message += "\n";

  for ( uint8_t i = 0; i < server->args(); i++ ) {
    message += " " + server->argName ( i ) + ": " + server->arg ( i ) + "\n";
  }
  server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server->sendHeader("Pragma", "no-cache");
  server->sendHeader("Expires", "-1");
  server->send ( 404, "text/plain", message );
}


/** Redirect to captive portal if we got a request for another domain. Return true in that case so the page handler do not try to handle the request again. */
boolean WiFiManager::captivePortal() {
  if (!isIp(server->hostHeader()) ) {
    DEBUG_WM(F("Request redirected to captive portal"));
    server->sendHeader("Location", String("http://") + toStringIp(server->client().localIP()), true);
    server->send ( 302, "text/plain", ""); // Empty content inhibits Content-length header so we have to close the socket ourselves.
    server->client().stop(); // Stop is needed because we sent no content length
    return true;
  }
  return false;
}

//start up config portal callback
void WiFiManager::setAPCallback( void (*func)(WiFiManager* myWiFiManager) ) {
  _apcallback = func;
}

//start up save config callback
void WiFiManager::setSaveConfigCallback( void (*func)(void) ) {
  _savecallback = func;
}

//sets a custom element to add to head, like a new style tag
void WiFiManager::setCustomHeadElement(const char* element) {
  _customHeadElement = element;
}

//if this is true, remove duplicated Access Points - defaut true
void WiFiManager::setRemoveDuplicateAPs(boolean removeDuplicates) {
  _removeDuplicateAPs = removeDuplicates;
}



template <typename Generic>
void WiFiManager::DEBUG_WM(Generic text) {
  if (_debug) {
    Serial.print("*WM: ");
    Serial.println(text);
  }
}

int WiFiManager::getRSSIasQuality(int RSSI) {
  int quality = 0;

  if (RSSI <= -100) {
    quality = 0;
  } else if (RSSI >= -50) {
    quality = 100;
  } else {
    quality = 2 * (RSSI + 100);
  }
  return quality;
}

/** Is this an IP? */
boolean WiFiManager::isIp(String str) {
  for (int i = 0; i < str.length(); i++) {
    int c = str.charAt(i);
    if (c != '.' && (c < '0' || c > '9')) {
      return false;
    }
  }
  return true;
}

/** IP to String? */
String WiFiManager::toStringIp(IPAddress ip) {
  String res = "";
  for (int i = 0; i < 3; i++) {
    res += String((ip >> (8 * i)) & 0xFF) + ".";
  }
  res += String(((ip >> 8 * 3)) & 0xFF);
  return res;
}
