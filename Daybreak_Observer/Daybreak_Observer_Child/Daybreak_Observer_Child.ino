#include<WiFi.h>
#include<WebServer.h>
#include<HTTPClient.h>
#include<SPI.h>
#include<MFRC522.h>

const char *ssid = ENV_SSID;
const char *pass = ENV_PASS;
IPAddress ip(ENV_IP_CHILD);
IPAddress gateway(ENV_GW);
const char* stop_url = ENV_STOP_URL;

#define SWITCH 4
#define LED 13
#define BUZZER 14
#define SS_PIN 5
#define RST_PIN 22
MFRC522 mfrc522(SS_PIN, RST_PIN);

bool is_alarm=false;

WebServer server(80);

//親機のアラームがONになったときの処理
void handleAlarm()
{
  is_alarm=true;
  digitalWrite(LED,HIGH);
  Serial.println("Alarm started");
  server.send(200,"text/plain","Alarm started");
}

//無効なURLが指定された場合
void handleNotFound()
{
  String messege="Not Found:";
  messege+=server.uri();
  server.send(404,"text/plain","messege");
}

void setup() {
  Serial.begin(115200);
  
  SPI.begin();
  delay(100);
  mfrc522.PCD_Init();
  delay(100);
  mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max);

  //GPIO設定
  pinMode(SWITCH,INPUT_PULLDOWN);
  pinMode(LED,OUTPUT);
  pinMode(BUZZER,OUTPUT);
  digitalWrite(LED,LOW);
  digitalWrite(BUZZER,LOW);

  //WiFi接続
  WiFi.begin(ssid,pass);
  while(WiFi.status()!=WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  WiFi.config(ip,gateway,WiFi.subnetMask(),IPAddress(8,8,8,8),IPAddress(8,8,4,4));
  Serial.println("");
  Serial.println("WiFi Connected.");

  //WebServerの初期化
  server.on("/alarm",handleAlarm);
  server.onNotFound(handleNotFound);
  server.begin();
}

void loop()
{
  //Webサーバを動作させる
  server.handleClient();

  if(is_alarm)
  {
     if(mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial())
     {
      Serial.println("RFID detected.");
      digitalWrite(BUZZER,HIGH);
      delay(100);
      digitalWrite(BUZZER,LOW);
      digitalWrite(LED,LOW);

      HTTPClient http;
      http.begin(stop_url);
      int httpCode=http.GET();
      http.end();

      is_alarm = false;

      mfrc522.PICC_HaltA();       // カードとの通信を終了
      mfrc522.PCD_StopCrypto1();

      delay(500);
     }
  }
}
