// Import required libraries


#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESP8266mDNS.h>  
#include <Ticker.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>
#include <algorithm> 
#include <EEPROM.h>            // read and write from flash memory
#include <Esp.h>

/*
enum rst_reason {
REANSON_DEFAULT_RST   = 0, // normal startup by power on
REANSON_WDT_RST = 1, // hardware watch dog reset
// exception reset, GPIO status won’t change
REANSON_EXCEPTION_RST = 2,   

// software watch dog reset, GPIO status won’t change
REANSON_SOFT_WDT_RST = 3,  

// software restart ,system_restart , GPIO status won’t change
REANSON_SOFT_RESTART = 4,
REANSON_DEEP_SLEEP_AWAKE = 5, // wake up from deep-sleep
REANSON_EXT_SYS_RST= 6, // external system reset
};
*/

boolean network_needs_config=false;

#define MAX_SSID_LENGTH 32
#define MAX_SSID_PASSWORD 63
#define NUM_RESETS_FOR_REINIT 5

struct NetworkObject {
  char ssid[MAX_SSID_LENGTH];
  char ssid_password[MAX_SSID_PASSWORD];
};


#define EEPROM_SIZE 512 // FOR esp8266

const int  eeprom_obj_addr =  (EEPROM_SIZE - sizeof(NetworkObject));
//const int  config_network_addr = eeprom_obj_addr - sizeof(boolean);
const int  last_reset_reason_addr = eeprom_obj_addr - sizeof(int);  
const int  last_reset_was_system_reset_addr = last_reset_reason_addr - sizeof(boolean);
const int  num_seq_reset_addr = last_reset_was_system_reset_addr - sizeof(int);

const char* ssid_prefix="ESP_";

String network_list;
String ssid_ap;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    html {
     font-family: Arial;
     display: inline-block;
     margin: 0px auto;
     text-align: center;
    }
    h2 { font-size: 1.5 rem; }
    p { font-size: 1.5 rem; margin: 2px}
    .units { font-size: 1.2rem; }
    .network-labels{
      font-size: 1.2em;
      vertical-align:middle;
      margin: 1px;
    }
    #select-network {
      margin:10px;
    }
  </style>
</head>
<body>
  <h2>Configure Device %DEVICENAME% WiFi Network Configuration</h2>
  <p>
    <span class="network-labels">Available Networks</span> 
  </p>
  <form id="network-form">
    <p>
      <select id="select-network">
        <option value="" disabled selected style="display:none;">Select Network</option>
        <span id="networks">%NETWORKS%</span>
      </select>
      <br>
      Password: <input id=password type="text" name="password">
      <input type="submit" value="Submit">
    </p>
  </form>
</body>
<script>

   var xhttp = new XMLHttpRequest();
   xhttp.onreadystatechange = function() {
      if(xhttp.readyState == 4 && xhttp.status == 200) {
          console.log(xhttp.responseText);
      }   

    };
   xhttp.open("GET", "/networks", true);
   xhttp.send();

   xhttp.onreadystatechange = function() {
      if(xhttp.readyState == 4 && xhttp.status == 200) {
          console.log(xhttp.responseText);
      }   

    };
   xhttp.open("GET", "/devicename", true);
   xhttp.send();

  var storeNetworkInfo = function() {
    var xhttp = new XMLHttpRequest();
    var net = document.getElementById("select-network").value;
    var password = document.getElementById("password").value;
    var params="ssid=" + net + "&password=" + password;
    
    xhttp.open("POST", "/networks/set", true);
    //Send the proper header information along with the request
    xhttp.setRequestHeader('Content-type', 'application/x-www-form-urlencoded');

    xhttp.onreadystatechange = function() {
      if(xhttp.readyState == 4 && xhttp.status == 200) {
          console.log(xhttp.responseText);
      }   
    };

    xhttp.send(params);
  };

  const network_selected = document.querySelector("#select-network");
  network_selected.addEventListener('change', function() {
    var myselect = document.getElementById("select-network");
    //alert(myselect.options[myselect.selectedIndex].value);
  });

  const network_form = document.querySelector("#network-form");
  network_form.addEventListener('submit', function() {
    var password = document.getElementById("password").value;
    storeNetworkInfo()
  });

</script>
</html>)rawliteral";



// Replaces placeholder with  values
String processor(const String& var){
  //Serial.println(var);
  if(var == "NETWORKS"){
    return network_list;
  }

  if(var == "DEVICENAME"){
    return ssid_ap;
  }

  return String("none");
}
void storeNetworkInfo(String _ssid,String _password) {

  struct NetworkObject  networkObj;
  
  strcpy(networkObj.ssid,_ssid.c_str());
  strcpy(networkObj.ssid_password,_password.c_str());

  EEPROM.put(eeprom_obj_addr,networkObj);
  EEPROM.commit();
 
  Serial.printf("In storeNeworkInfo ssid %s, password is %s\n", _ssid.c_str(),_password.c_str());

}

void stop_WiFi_AP() {
  server.end();
  WiFi.disconnect();
  delay(100);
  WiFi.mode(WIFI_STA);
}

void web_configure_network() {

  // clear old networks
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  
  //find available _networks
   int n = WiFi.scanNetworks();
  Serial.printf("%d network(s) found\n",n);

  // Create the options for the select form.  This will update the html on the browswer to include a drop-down of available networks
  for (int i = 0; i < n; i++)
  {

    network_list =  network_list + "<option value=\"" + (WiFi.SSID(i)) + "\">" + (WiFi.SSID(i)) + " [ Ch:" +  WiFi.channel(i) + " (" + WiFi.RSSI(i) + "dbM) ] " + "</option>";
    Serial.printf("%d: %s, Ch:%d (%ddBm) %s\n", i+1, WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i), WiFi.encryptionType(i) == ENC_TYPE_NONE ? "open" : "");

  }
  Serial.printf("Network list is \n%s\n",network_list.c_str());
  //
  
  ssid_ap = ssid_prefix + WiFi.macAddress();
  ssid_ap.replace(":","");
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
 
  Serial.print("IP address:\t");
  Serial.println(WiFi.softAPIP());         // Send the IP address of the ESP8266 to the computer
  Serial.println(WiFi.macAddress());


  Serial.println();

  Serial.print("Setting soft-AP ... ");

  
  Serial.println(WiFi.softAP(ssid_ap.c_str(), NULL) ? "Ready" : "Failed!");

   if (!MDNS.begin(ssid_ap)) {             
    Serial.println("Error setting up MDNS responder!");
  }
  Serial.print("mDNS responder started registered ");
  Serial.println(String(ssid_ap+".local"));

  Serial.print("Access Point \"");
  Serial.print(ssid_ap);
  Serial.println("\" started");

  
  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
  });
  
  server.on("/networks", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", network_list.c_str());
  });
  
  server.on("/devicename", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", ssid_ap.c_str());
  });
  
  server.on("/networks/set", HTTP_POST, [](AsyncWebServerRequest *request){

    if(request->hasParam("ssid",true)  && request->hasParam("password",true)) {
        AsyncWebParameter* ssid = request->getParam("ssid",true);
        AsyncWebParameter* password = request->getParam("password",true);
        storeNetworkInfo(ssid->value(),password->value());
    }

    request->send_P(200, "text/plain", "Done");
    stop_WiFi_AP();
  });

  MDNS.addService("http", "tcp", 80);
  // Start server
  server.begin();
  
}
void setup() {

  int last_reset_was_system_reset;
  int last_stored_seq_number;
  // put your setup code here, to run once:
  Serial.begin(115200);

  EEPROM.begin(EEPROM_SIZE);
  
  // This is a trick to handle case of unitialized eeprom
  // if the sequence isn't between 0 and NUM_RESETS_FOR_REINIT then just set it to 0.  Evenutally after a number of resets all will be good
  last_stored_seq_number = EEPROM.read(num_seq_reset_addr);
  if ((last_stored_seq_number < 0) || (last_stored_seq_number > NUM_RESETS_FOR_REINIT)) {
      EEPROM.write(num_seq_reset_addr,0);     
      EEPROM.commit();
  }
  
  rst_info* rinfo = ESP.getResetInfoPtr();

  Serial.printf("\nReset reason %d\n",rinfo->reason);

  if (rinfo->reason != REASON_EXT_SYS_RST) {     
    EEPROM.write(num_seq_reset_addr,0);  
    //EEPROM.write(config_network_addr,0);
    EEPROM.write(last_reset_was_system_reset_addr,0);
    EEPROM.commit();
    network_needs_config = false;
  }
  else {
    int num_reset_sequence = EEPROM.read(num_seq_reset_addr) + 1;
    Serial.printf("\nResets in a row = %d\n",num_reset_sequence);
    EEPROM.write(num_seq_reset_addr,num_reset_sequence); 
    if (num_reset_sequence == NUM_RESETS_FOR_REINIT) {      
      EEPROM.write(num_seq_reset_addr,0);  
      network_needs_config = true;
    }
    EEPROM.commit();
  }
 
  if (network_needs_config) {
    Serial.println("Network settings need configuration");
    web_configure_network();    
    //EEPROM.write(config_network_addr,0);  
    EEPROM.commit();
  }
  else {
    Serial.println("Network settings are configured");
  }


}

void loop() {

  MDNS.update();
 
}
