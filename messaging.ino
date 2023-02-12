void sendOpenHabMessage(float percentage, float distanceCm){
  Serial.println(percentage);
  Serial.println(distanceCm);
  
  char result[8];
  Serial.print("sending ");
  Serial.print(percentage);
  Serial.print(" as percentage to openHAB on url: ");
  Serial.println("http://" + String(mqtt_server) + ":" + String(mqtt_port) +"/rest/items/" + String(oh_itemid)); 
  http.begin("http://" + String(mqtt_server) + ":" + String(mqtt_port) +"/rest/items/" + String(oh_itemid)); 
  http.addHeader("Content-Type", "text/plain");
  http.POST(String(percentage));
  http.end();
  
//  dtostrf(distanceCm, 3, 1, result); 
  Serial.print("sending ");
  Serial.print(distanceCm);
  Serial.print(" as distance to openHAB on url: ");
  Serial.println("http://" + String(mqtt_server) + ":" + String(mqtt_port) +"/rest/items/" + String(oh_itemid) + "_cm"); 
  http.begin("http://" + String(mqtt_server) + ":" + String(mqtt_port) +"/rest/items/" + String(oh_itemid) + "_cm");
  http.addHeader("Content-Type", "text/plain"); 
  http.POST(String(distanceCm));
  http.end();
}

void sendDomoticzMessage(float percentage, float distanceCm){
  char result[8];
  if (espClient.connect(mqtt_server,atoi(mqtt_port))){
      Serial.print("sending ");
      Serial.print(percentage);
      Serial.print(" as percentage to domotics on IDX ");
      Serial.println(dz_idx);
      espClient.print("GET /json.htm?type=command&param=udevice&idx=");
      espClient.print(String(dz_idx));
      espClient.print("&nvalue=0");
      espClient.print("&svalue=");
      espClient.print(percentage);
      
      if (strlen(mqtt_username) != 0){
        espClient.print("&username=");
        espClient.print(base64::encode(mqtt_username));
        espClient.print("&password=");
        espClient.print(base64::encode(mqtt_password));
      }
      
      espClient.println(" HTTP/1.1");
      espClient.print("Host: ");
      espClient.print(String(mqtt_server));
      espClient.print(":");
      espClient.println(String(mqtt_port));
      espClient.println("User-Agent: Salt Sentry");
      espClient.println("Connection: close");
      espClient.println();
      espClient.stop();
  } 
  //reconnect required
  if (espClient.connect(mqtt_server,atoi(mqtt_port))){

//      dtostrf(distanceCm, 3, 0, result);   
      Serial.print("sending ");
      Serial.print(distanceCm);
      Serial.print(" as distance to domotics on IDX ");
      Serial.println(atoi(dz_idx) + 1);
      espClient.print("GET /json.htm?type=command&param=udevice&idx=");
      espClient.print(String(atoi(dz_idx) + 1));
      espClient.print("&nvalue=0");
      espClient.print("&svalue=");
      
      espClient.print(String(distanceCm));
      
      if (strlen(mqtt_username) != 0){
        espClient.print("&username=");
        espClient.print(base64::encode(mqtt_username));
        espClient.print("&password=");
        espClient.print(base64::encode(mqtt_password));
      }
      
      espClient.println(" HTTP/1.1");
      espClient.print("Host: ");
      espClient.print(String(mqtt_server));
      espClient.print(":");
      espClient.println(String(mqtt_port));
      espClient.println("User-Agent: Salt Sentry");
      espClient.println("Connection: close");
      espClient.println();
      espClient.stop();
      
      
   } else {
     Serial.println("connect failed");
   }
}

void sendMqttMessage(float percentage, float distanceCm){
  char tempString[8];
  dtostrf(percentage, 4, 1, tempString);
  client.publish(mqtt_topic, tempString , true);
  strcpy(mqtt_distance_topic, mqtt_topic);
  strcat(mqtt_distance_topic, "_distance");
  dtostrf(distanceCm, 4, 1, tempString);    
  client.publish(mqtt_distance_topic, tempString , true);
  
  Serial.print("sending ");
  Serial.print(percentage);
  Serial.print("  to ");
  Serial.print(mqtt_server);
  Serial.print(" on port ");
  Serial.print(mqtt_port);
  Serial.print(" with topic ");
  Serial.println(mqtt_topic);

  Serial.print("sending ");
  Serial.print(distanceCm);
  Serial.print("  to ");
  Serial.print(mqtt_server);
  Serial.print(" on port ");
  Serial.print(mqtt_port);
  Serial.print(" with topic ");
  Serial.println(mqtt_distance_topic);
}
