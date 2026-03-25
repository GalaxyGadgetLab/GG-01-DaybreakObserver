#include<Arduino.h>
#include<SPI.h>
#include<Adafruit_GFX.h>
#include<Adafruit_ILI9341.h>
#include<Fonts/FreeSans12pt7b.h>
#include<Fonts/FreeSans18pt7b.h>
#include<Fonts/FreeSans40pt7b.h>
#include<Wire.h>
#include<RTClib.h>
#include<time.h>
#include<HardwareSerial.h>
#include<DFRobotDFPlayerMini.h>
#include<WiFi.h>
#include<WebServer.h>
#include<HTTPClient.h>
#include "env.h"


//初期設定
const char *ssid = ENV_SSID;
const char *pass= ENV_PASS;
IPAddress ip(ENV_IP_PARENT);
IPAddress gateway(ENV_GW);
const char *notify_url = ENV_NOTIFY_URL;
const char *adjust_time="04:00:00";
#define DF_VOLUME 18


//定数
#define ALARM_SIG 25  //アラーム枠点滅周期
#define TFT_DC 2  //液晶ディスプレイのDCピンをGPIO2に接続
#define TFT_CS 5  //液晶ディスプレイのCSピンをGPIO5に接続
#define TFT_RST 4  //液晶ディスプレイのRESETピンをGPIO4に接続
#define TFT_WIDTH 320  //液晶ディスプレイの幅320

Adafruit_ILI9341 tft=Adafruit_ILI9341(TFT_CS,TFT_DC,TFT_RST);
RTC_DS3231 rtc;
HardwareSerial hs(1); //DFPlayerMini用シリアルポート
DFRobotDFPlayerMini myDFPlayer;
WebServer server(80);   //WebServer型の変数server

char old_date[15];
char old_time[9];
char old_alarm[15];
char alarm_time[9];
char wdays[7][4]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
bool alarm_checked=false;
bool alarm_on=false;
bool ntp_adjusted=false;
int alarm_ctr;

//NTPで日時を設定
void setTimeByNTP(){
  struct tm tm;  //time.hで定義されているtm構造体
  
  configTime(9*3600L,0,"ntp.nict.jp","time.google.com","ntp.jst.mfeed.ad.jp"); 
  //UTCとJSTの時差9*3600s
  //9*3600=32400
  //16ビットのint型は32767だからギリギリ！一応Long型を指定
  //今回使用したESP32は32ビットマイコンだけど安全側の設計で！
  if(!getLocalTime(&tm)){
    Serial.println("getLocalTime Error");
    return;
  }
  rtc.adjust(DateTime(tm.tm_year+1900,tm.tm_mon+1,tm.tm_mday,tm.tm_hour,tm.tm_min,tm.tm_sec));
  //NTPから返ってくる年は1900年からの経過年, 月は0-11
}

//現在の日時を液晶ディスプレイに表示(差分更新)
void showMessage(char* s_new,char* s_old,int y0,int height){
  int16_t x1,y1;
  uint16_t w, w2,h;
  int x,y;
  if(strcmp(s_new,s_old)!=0){     //新しく時刻を取得したら以下の処理を実行
    tft.getTextBounds(s_old,0,0,&x1,&y1,&w,&h);   //テキストサイズ取得
    w2=w*11/10;
    tft.fillRect((TFT_WIDTH-w2)/2,y0-(height/2)+1,w2,height,ILI9341_BLACK);   //黒で塗りつぶす
    tft.getTextBounds(s_new,0,0,&x1,&y1,&w,&h);
    tft.setCursor((TFT_WIDTH-w)/2,y0+(h/2)-1);    //テキスト表示位置を設定
    tft.print(s_new);
    strcpy(s_old,s_new);
  }
}

//設定メインページ
void handleRoot() //"http://ESP32のIPアドレス/"にアクセス時に実行
{                 //入力フォーム作成
  int i;

  String html=
    "<!DOCTYPE html>\n"
    "<html>\n"
    "<head>\n"
    "<meta charset=\"utf-8\">\n"
    "<title>アラーム時計設定</title>\n"
    "</head>\n"
    "<body>\n"
    "<form method=\"get\" action=\"/set\">\n"
    "<p>\n"
    "<select name=\"hour\">\n";
  for(i=0;i<24;i++)
    {
    html+="<option value=\"";
    html+=String(i);
    html+="\">";
    html+=String(i);
    html+="</option>\n";
    }
  html+="</select>時\n";
  html+="<select name=\"min\">\n";
  for(i=0;i<60;i++)
    {
    html+="<option value=\"";
    html+=String(i);
    html+="\">";
    html+=String(i);
    html+="</optioon>\n";
    }
   html+="</select>分\n";
   html+="<input type=\"submit\" name=\"submit\" value=\"アラーム設定\" />\n";
   html+="</p>\n";
   html+="</form>\n";
   html+="<form method=\"get\" action=\"/set\">\n";
   html+="<input type=\"hidden\" name=\"off\" value=\"1\">\n";
   html+="</p><input type=\"submit\" name=\"submit\" value=\"アラームoff\" /></p>\n";
   html+="</form>";
   html+="</body>";
   html+="</html>";
   server.send(200,"text/html",html);
}

//アラームの設定
void handleSetAlarm()
{
  int i,hour,min,sec;
  bool is_off=false;
  String s_hour="",s_min="",s_sec="";

  //URLからoff/hour/min/secのパラメータを取得する
  for(i=0;i<server.args();i++)   //リクエストパラメータの数だけ繰り返す
  {
    if(server.argName(i).compareTo("off")==0)
    {
      is_off=true;
      break;
    }
    else if(server.argName(i).compareTo("hour")==0)
    {
      s_hour=server.arg(i);   //リクエストパラメータ名がhourのときのリクエストパラメータの値を代入
    }
    else if(server.argName(i).compareTo("min")==0)
    {
      s_min=server.arg(i);
    }
    else if(server.argName(i).compareTo("sec")==0)
    {
      s_sec=server.arg(i);
    }
  }

  //offのパラメータが設定されているとき,アラームをオフにする
  if(is_off)
  {
    strcpy(alarm_time,"Off");
    server.send(200,"text/plain, charset=utf-8","アラームをオフにしました.");
  }

  //アラームの時刻を設定する
  else if(s_hour.length()>0&&s_min.length()>0)
  {
    hour=s_hour.toInt();    //String型からint型へ変換
    min=s_min.toInt();
    if(s_sec.length()>0)
    {
      sec=s_sec.toInt();
    }
    else
    {
      sec=0;
    }

    if(hour>=0 && hour<=23 && min>=0 && min<=59 && sec>=0 && sec<=59)
    {
      sprintf(alarm_time,"%02d:%02d:%02d",hour,min,sec);
      String msg="アラームを ";
      msg.concat(alarm_time);
      msg.concat("に設定しました.");
      server.send(200,"text/plain; charset=utf-8",msg);   
                         // MIMEタイプ(ファイルの分類/ファイルの種類)
    }
    else
    {
      server.send(200,"text/plain; charset=utf-8","日時が正しくありません.");
    }
  }
  else
  {
     server.send(200,"text/plain; charset=utf-8","パラメータが正しくありません.");
  }  
}

//アラームを止める
void handleStopAlarm()
{
  myDFPlayer.stop();
  alarm_on=false;
  tft.drawRect(30,180,260,40,ILI9341_BLACK);    //短形を描画
  server.send(200,"text/plain","Alarm stop");
}

//無効なURLが選択された場合
void handleNotFound()
{
  String Message="Not Found : ";
  Message+=server.uri();
  server.send(404,"text/plain","errorMessage");
}

//セットアップ
void setup(){
  int16_t x1,y1;
  uint16_t w,h;

  
 
   Serial.begin(115200);    //115200bpsでポートを開く

   strcpy(old_date,"00000000000000");
   strcpy(old_time,"00000000");
   strcpy(old_alarm,"00000000000000");
   strcpy(alarm_time,"Off");

   //ディスプレイの初期化
   tft.begin();
   tft.setRotation(3);    //ディスプレイの文字を90deg回転
   tft.fillScreen(ILI9341_BLACK);
   tft.setTextColor(ILI9341_YELLOW);
   tft.setFont(&FreeSans12pt7b);
   String s="Initializing...";
   tft.getTextBounds(s,0,0,&x1,&y1,&w,&h);
   tft.setCursor(0,h);
   tft.println(s);

  //WiFi接続
  WiFi.begin(ssid,pass);
 while(WiFi.status()!=WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  WiFi.config(ip,gateway,WiFi.subnetMask(),IPAddress(8,8,8,8));
      //8.8.8.8や8.8.4.4はGoogle Public DNSのIPアドレス
  Serial.print("");
  Serial.println("WiFi Connectid.");
  tft.println("WiFi Connected.");

  //DFPlayer初期化
  hs.begin(9600,SERIAL_8N1,16,17);
  delay(2000);
  myDFPlayer.begin(hs,false,false);
  //非純正チップは返事くれないor遅いからisACKはfalse
  
  delay(500);
  Serial.println("DFPlayer Initialized.");
  tft.println("DFPlayer Initialized.");
  myDFPlayer.volume(DF_VOLUME);
  
  //RTC初期化
  if(!rtc.begin())
  {
    Serial.println("Could't find RTC");
    while(1);
  }

  Serial.println("RTC Initialized.");
  tft.println("RTC Initialized.");

  //NTPで現在時刻を取得してRTCに設定
  setTimeByNTP();

  //WebServer初期化
  server.on("/",handleRoot);
  server.on("/set",handleSetAlarm);
  server.on("/stop",handleStopAlarm);
  server.onNotFound(handleNotFound);
  server.begin();
  //それぞれ「/」,「/set」,「/stop」,それ以外にアクセスされたときに実行する関数を指定

  //ディスプレイを黒で塗りつぶす
  tft.fillScreen(ILI9341_BLACK);


}

void loop()
{
  char new_time[9],new_date[15],new_alarm[15];

  //Webサーバーを動作させる
  server.handleClient();

  //現在の時刻を液晶ディスプレイに表示 
  DateTime now=rtc.now();
  sprintf(new_date,"%04d/%02d/%02d ",now.year(),now.month(),now.day());
  strcat(new_date,wdays[now.dayOfTheWeek()]);
  sprintf(new_time,"%02d:%02d:%02d",now.hour(),now.minute(),now.second());
  strcpy(new_alarm,"Alarm ");
  strcat(new_alarm,alarm_time);
  tft.setFont(&FreeSans18pt7b);
  tft.setTextColor(ILI9341_GREEN);
  showMessage(new_date,old_date,40,28);
  showMessage(new_alarm,old_alarm,200,28);
  tft.setFont(&FreeSans18pt7b);
  showMessage(new_time,old_time,120,64);

  //現在の時刻がアラームの時刻になっているかどうかを判断
  if(strstr(new_time,alarm_time)!=NULL) //書式揺れに対応するため部分一致で判定
  {
    if(!alarm_checked)  //alarm_checkedで処理の重複を防ぐ
    {
      myDFPlayer.play(1);
      alarm_checked=true;
      alarm_on=true;
      alarm_ctr=0;
      HTTPClient http;
      http.begin(notify_url);
      int httpCode=http.GET();
    }
  }
  else
  {
    alarm_checked=false;
  }

  //アラームが鳴っている間は,液晶ディスプレイのアラーム時刻の周りに赤枠を点滅させる
  if(alarm_on)
  {
    if(alarm_ctr==0)
    {
      tft.drawRect(30,180,260,40,ILI9341_RED);
    }
    else if(alarm_ctr==ALARM_SIG)
    {
      tft.drawRect(30,180,260,40,ILI9341_BLACK);
    }
    alarm_ctr++;
    alarm_ctr%=ALARM_SIG*2;
    //alarm_ctrが1ずつ増加し,ALARM_SIGのとき黒,ALARM_SIG*2のとき余り0で赤
    //
  }

  //毎日一定の時刻にNTPで時刻合わせを行う.
  if(strstr(new_time,adjust_time)!=NULL)
  {
    if(!ntp_adjusted)  //ntp_adjustedで処理の重複を防ぐ
    {
      setTimeByNTP();
      ntp_adjusted=true;
    }
  }
  else
  {
  ntp_adjusted=false;
  }

  delay(20);
}