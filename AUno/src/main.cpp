#include <Arduino.h>
#include <SoftwareSerial.h>
#include <String.h>
#include <lowpower.h>

#define RX 8                      // RX-PIN der SoftwareSerial
#define TX 9                      // TX-PIN der SoftwareSerial

#define TRANSISTORPIN 5           // Pin an dem der transistor für die Stromversorgung 

#define METHODE 1                 // 0 -> NB-IoT | 1 -> GSM | 2 -> GNSS
#define SEND_MIN 1                // nach wie vielen Minuten Daten gesendet werden sollen
#define TIMEOUT 10                // wie lange auf eine erwartete Antwort des shields gewartet werden soll (Sekunden)

#define PORT 5006                 // Server Port
String IP = "123.123.456.456";    // Server Adresse
String SERVER_TYPE = "UDP";       // UDP | TCP

unsigned long time = 0;
bool contextActive = false;
bool gnssActive = false;

bool bSendData = true;           //FOR TESTING ONLY  | ob commands-funktionen in loop ausgeführt werden sollen 

SoftwareSerial softSerial(RX,TX); //       Arduino | 8 - RX | 9 TX |            Shield | 10 - TX | 11 RX |      
void setup() {
  Serial.begin(9600);
  while(!Serial){
  }
  softSerial.begin(9600);
  Serial.println("Wellcom to Draguino QG96");
}

//überprüfen ob das shield wie erwartet antwortet
//in dem z.B. auf das OK(waitForString) vom shield gewartet wird
//ansonsten timeout
String waitFor(String waitForString){
  String reciveData = "";
  int16_t i = 0;

  while(1){
    //lesen was das shield zurück gibt und in recieveData schreiben   
    while(softSerial.available()) {
      reciveData = reciveData + softSerial.readString();
    }
    
    //in recieveData nach dem gesuchten String suchen
    if(reciveData.indexOf(waitForString) > 0){
      Serial.println("COMMAND SUCCESS");
      return reciveData;
    }
    //spezial Fall für GNSS(wenn auf first fix gewartet wird)
    if(METHODE == 2){
      if(reciveData.indexOf("ERROR: 516") > 0){
        Serial.println("COMMAND SUCCESS AND WAIT FOR FIRST FIX");
        Serial.println(reciveData);
        return reciveData;
      }
    }
    
    //nach 1 Sekunde * TIMEOUT die Funktion abbrechen
    if(i>=1000*TIMEOUT){
      Serial.println(reciveData + "TIMEOUT");
      return reciveData + "TIMEOUT";
    }
    i++;
    delay(1);
  }
}

//sonder Fall für die Informationen von Neighbourcell da hier regelmäßig der buffer überläuft
//gibt die relevanten Informationen von Neighbourcell zurück
String waitForNeighbour(){
  String reciveData;
  char inArray[256];
  char inChar;
  int8_t k = 0;
  int16_t j = 0;
  int16_t i = 0;
  bool nextK = false;
  bool gotOK = false;

  while(1){
    //Inhalt des softwareSerial buffers entgegen nehmen und verarbeiten
    while(softSerial.available()) {
      inChar = softSerial.read();
      //relevante Informationen (nach ',') in array schreiben
      if(inChar == ',') k++;
      if((METHODE == 0 && (k%12 == 3 || k%12 == 6)) || (METHODE == 1 && (k%12 == 4 || k%12 == 5 || k%12 == 8))){
        inArray[j] = inChar;
        j++;
      } 
      //auf OK prüfen
      if(inChar == 'O') nextK = true;
      if(nextK == true && inChar == 'K') gotOK = true;
      
      //sicherstellen dass nichts überläuft
      if(j > 255){
        for(int8_t l = 0; l <=j; l++) reciveData = reciveData + inArray[l];
        j=0;
      }
    }
     
    //wenn OK erhalten wurde char array in String schreiben und zurück geben
    if(gotOK == true){
      for(int8_t l = 0; l <j; l++) reciveData = reciveData + inArray[l];
      Serial.println("COMMAND SUCCESS");
      Serial.println(reciveData);
      if(softSerial.available()) softSerial.read();
      return reciveData;
    }
      
    //nach 1 Sekunde * TIMEOUT die Funktion abbrechen
    if(i>=1000*TIMEOUT){
      for(int8_t l = 0; l <j; l++) reciveData = reciveData + inArray[l];
      Serial.println(reciveData);
      Serial.println("Neigh TIMEOUT");
      return reciveData + "NEIGHBOUR TIMEOUT";
    }
    i++;
    delay(1);
  }
}

//sammeln aller wichtigen Informationen für die Positionierung
//gibt String mit "METHOD,para1,para2,para3,..." zurück
String collectData(){
  String reciveAnswer = "";
  String positionData = "";
  int8_t i = 0;

  //sammeln der Informationen über servingcell in reciveData &
  //warten auf OK von shield / ansonsten timeout
  softSerial.println("AT+QENG=\"servingcell\"");
  reciveAnswer = waitFor("OK");
  if(reciveAnswer.indexOf("TIMEOUT") > 0){
    Serial.println("OK timeout servingcell");
    return "OK TIMEOUT servingcell";
  }
  
  //so lange über reciveAnswer iterieren bis keine "," mehr vorhanden sind
  while(reciveAnswer.indexOf(",") > 0){
    reciveAnswer = reciveAnswer.substring(reciveAnswer.indexOf(",")+1, reciveAnswer.length());
    //mcc(3),mnc(4),cellid(5) und tac(11) in positionData schreiben für NB-IoT
    if(((i>=3 && i<=5) || i == 11) && METHODE == 0)
      positionData = positionData + "," + reciveAnswer.substring(0, reciveAnswer.indexOf(","));
    //mcc(2),mnc(3),lac(4) und cellid(5) in positionData schreiben für GSM
    if((i>=2 && i<=5) && METHODE == 1)
      positionData = positionData + "," + reciveAnswer.substring(0, reciveAnswer.indexOf(","));
    i++;
  }

  //sammeln der Informationen über neighbourcell in reciveData &
  //warten auf OK von shield / ansonsten timeout
  softSerial.println("AT+QENG=\"neighbourcell\"");
  reciveAnswer = waitForNeighbour();
  if(reciveAnswer.indexOf("TIMEOUT") > 0){
    Serial.println("OK timeout neighbourcell");
    return "OK TIMEOUT neighbourcell";
  }
  positionData = positionData + reciveAnswer;
 
  //METHODE vor positionData schreiben
  if(METHODE == 0) positionData = "NB" + positionData;
  if(METHODE == 1) positionData = "GSM" + positionData;
  Serial.println("DATA: " + positionData);
  
  return positionData;
}

//senden aller Informationen an eine bestehende Verbindung
//Funktion bricht ab wenn das shield einen command nicht richtig verarbeitet
void sendData(String data){

  //Befehl zum senden der Informationen
  //bei bestehender Verbindung zum Server antwortet das shield mit einem ">"
  //warten auf ">" von shield / ansonsten timeout
  softSerial.println("AT+QISEND=2");
  if(waitFor(">").indexOf("TIMEOUT") > 0){
    Serial.println("> timeout");
    return;
  }
  
  //Informationen an das shield senden und mit \x1a (ctrl z) die Eingabe bestätigen
  softSerial.println(data + "\x1a");
  
  //warten auf OK von shield (Daten erfolgreich gesendet) / ansonsten timeout
  if(waitFor("OK").indexOf("TIMEOUT") > 0){
    Serial.println("OK timeout send data");
    return;
  }
  Serial.println("DATA SEND: " + data);
  return;
}

//aktivieren(activateContext=true) oder deaktivieren(activateContext=false) des PDP-Context
//gibt true zurück wenn context aktiviert ist ansonsten false
bool shieldContext(bool activateContext){
  String reciveAnswer = "";
  
  //Status vom context abfragen. Bei timeout return false
  softSerial.println("AT+QIACT?");
  reciveAnswer = waitFor("OK");
  if(reciveAnswer.indexOf("TIMEOUT") > 0){
    Serial.println("OK timeout context");
    return false;
  }

  //wenn der context bereits aktiviert ist return true bei activateContext=true
  //bei activateContext=false context deaktiviern und return false
  if(reciveAnswer.indexOf("+QIACT: 1,1") > 0){
    
    if(activateContext == true) return true;
    
    softSerial.println("AT+QIDEACT=1");
    if(waitFor("OK").indexOf("TIMEOUT") > 0)
      Serial.println("OK timeout context deact");
    Serial.println("CONTEXT DEACT");
    return false;
  }

  //wenn context nicht aktivier ist und activateContext=false return false
  //bei activateContext=true context aktivieren und return true
  if(activateContext == false) return false;

  softSerial.println("AT+QIACT=1");
  if(waitFor("OK").indexOf("TIMEOUT") > 0){
    Serial.println("OK timeout context act");
    return false;
  }
  Serial.println("CONTEXT ACT");
  return true;
}

//aktivieren(activateConnection=true) oder deaktivieren(activateConnection=false) der Serververbindung
//gibt true zurück wenn Serververbindung aktiviert ist ansonsten false
bool shieldConnection(bool activateConnection){
  String reciveAnswer = "";
  
  //Status vom Serververbindung abfragen. Bei timeout return false
  softSerial.println("AT+QISTATE=0,1");
  reciveAnswer = waitFor("OK");
  if(reciveAnswer.indexOf("TIMEOUT") > 0){
    Serial.println("OK timeout connection");
    return false;
  }

  //wenn die Serververbindung bereits aktiviert ist return true bei activateConnection=true
  //bei activateConnectiont=false Serververbindung deaktiviern und return false
  if(reciveAnswer.indexOf("+QISTATE: 2") > 0){
    
    if(activateConnection == true) return true;
    
    softSerial.println("AT+QICLOSE=2");
    if(waitFor("OK").indexOf("TIMEOUT") > 0)
      Serial.println("OK timeout connection deact");
    Serial.println("CONNECTION DEACT");
    return false;
  }

  //wenn Serververbindung nicht aktivier ist und activateConnection=false return false
  //bei activateConnection=true Serververbindung aktivieren und return true
  if(activateConnection == false) return false;

  softSerial.println("AT+QIOPEN=1,2,\"" + SERVER_TYPE + "\",\"" + IP + "\"," + PORT + ",0,0");
  if(waitFor("+QIOPEN: 2,0").indexOf("TIMEOUT") > 0){
    Serial.println("OK timeout connection act");
    return false;
  }
  Serial.println("CONNECTION ACT");
  return true;
}

//aktivieren(activateGNSS=true) oder deaktivieren(activateGNSS=false) der GNSS-Positionierung
//gibt true zurück wenn GNSS-Positionierung aktiviert ist ansonsten false
bool shieldGNSS(bool activateGNSS){
  String reciveAnswer = "";
  
  //Status vom GNSS abfragen. Bei timeout return false
  softSerial.println("AT+QGPS?");
  reciveAnswer = waitFor("OK");
  if(reciveAnswer.indexOf("TIMEOUT") > 0){
    Serial.println("OK timeout GNSS");
    return false;
  }

  //wenn GNSS bereits aktiviert ist return true bei activateGNSS=true
  //bei activateGNSS=false GNSS deaktiviern und return false
  if(reciveAnswer.indexOf("+QGPS: 1") > 0){
    if(activateGNSS == true) return true;

    softSerial.println("AT+QGPSEND");
    if(waitFor("OK").indexOf("TIMEOUT") > 0)
      Serial.println("OK timeout GNSS deact");
    Serial.println("GNSS DEACT");
    return false;
  }

  //wenn GNSS nicht aktivier ist und activateGNSS=false return false
  //bei activateGNSS=true GNSS aktivieren und return true
  if(activateGNSS == false) return false;

  softSerial.println("AT+QGPS=1");
  if(waitFor("OK").indexOf("TIMEOUT") > 0){
    Serial.println("OK timeout GNSS act");
    return false;
  }
  Serial.println("GNSS ACT");
  return true;
}

//Sammeln der Lat,Long von GNSS
//gibt "GNSS,lat,long" zurück wenn first fix vorhanden ist
String getGNSSPos(){
  String reciveAnswer = "";
  String positionData = "";
  int8_t i = 0;

  //sammeln der Informationen über GNSS in reciveData &
  //warten auf OK von shield / ansonsten timeout
  softSerial.println("AT+QGPSLOC=2");
  reciveAnswer = waitFor("OK");
  if(reciveAnswer.indexOf("TIMEOUT") > 0){
    Serial.println("OK timeout GNSS Position");
    return "OK TIMEOUT GNSS Position";
  }

  //wenn noch kein first fix vorhanden ist, Funktion beenden mit "wait for FIRST FIX"
  if(reciveAnswer.indexOf("ERROR: 516") > 0) return "wait for FIRST FIX";

  //so lange über reciveAnswer iterieren bis keine "," mehr vorhanden sind
  while(reciveAnswer.indexOf(",") > 0){
    reciveAnswer = reciveAnswer.substring(reciveAnswer.indexOf(",")+1, reciveAnswer.length());
    //lat(0),long(1) in positionData schreiben
    if(i==0 || i==1)
      positionData = positionData + "," + reciveAnswer.substring(0, reciveAnswer.indexOf(","));
    i++;
  }

  positionData = "GNSS" + positionData;
  Serial.println("DATA: " + positionData);
  return positionData;
}

//Ausschalten des Shields und übergehen in SLEEP-Modus
void sleepminutes(int minutes){
  int steps = (minutes*60)/8;         //(min zu sek) / 8sek
 
  String reciveAnswer;
  
  softSerial.println("AT+QPOWD");                                       //Shield "Normal power down"
  reciveAnswer = waitFor("OK");     
  if(reciveAnswer.indexOf("OK")) reciveAnswer= waitFor("POWERED DOWN");
  if(reciveAnswer.indexOf("POWERED DOWN")) digitalWrite(TRANSISTORPIN, LOW);   //transistor "sperre 
  
  for(int count = 0; count < steps; count++)
  {
    LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);  
    //the time (8s) is limited by what the watchdog timer can provide which is 8s maximum
  }

  digitalWrite(TRANSISTORPIN, HIGH); // Transistor öffnen 
  waitFor("APP RDY");
  
}

void loop() {

  //Jede Minute * SEND_MIN wird die Zeit ausgegeben(temp) 
  //und die Informationen der neigbour-cells an einen Server gesendet
  Serial.print("ArduinoTime:");
  time = millis();
  Serial.println(time);
  String data;

  if(METHODE==2){ //GNSS
    if(contextActive)contextActive=shieldContext(false);
    if(!gnssActive)gnssActive=shieldGNSS(true);
    data = getGNSSPos();
    Serial.println(data);

  } else {        // GSM & NB-IoT
    if(gnssActive)gnssActive=shieldGNSS(false);
    //Sammeln der zur Positionsbestimmung relevanten Daten 
    data = collectData();
    //APN-Kontext aktivieren
    if(!contextActive)contextActive=shieldContext(true);
    //Verbindung zum Server aufbauen
    if(contextActive && shieldConnection(true)){
      //Daten senden
      sendData(data);
      //Verbindung und APN-Kontext schließen
      shieldConnection(false);
      contextActive=shieldContext(false);
    }
    Serial.println(data);
  }

  Serial.println("###########################################################################");

  
  while (softSerial.available()) {
    Serial.write(softSerial.read());
  }
  while (Serial.available()) {
    softSerial.write(Serial.read());
  }
  
  if(SEND_MIN>=1)sleepminutes(SEND_MIN);
}