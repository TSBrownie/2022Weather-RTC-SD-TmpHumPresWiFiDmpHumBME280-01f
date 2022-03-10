//Arduino Wemos D1 mini w/ RTC, SD Shield (ESP8266 Boards 2.7.4 D1R2&mini)
//WiFi Viewing, RTC, SD File: Absolute Barometric Pressure (SL & Comp), Temp, Humidity
//Averages readings for trending pressure / temp
//Wemos SDA(D2)-->BME280 SDA pin; Wemos SCL(D1)-->BME280 SCL
//Wemos 3v3-->BME280 Vdd; Wemos GND-->BME280 GND
//UNUSED: Wemos Vdd/GND-->BME280=SDO(Addr control), Wemos Vdd/GND-->BME280=CSB(I2C select, GND=SPI)
//BME280 Data is LSBMSB ijklmnopabcdefgh order (not abcdefghijklmnop)
//WeMos Micro SD Shield (uses HSPI(12-15) not (5-8), 3V3, G)
//GPIO12(D6)=MISO (main in, secondary out); GPIO13(D7)=MOSI (main out, secondary in) 
//GPIO14(D5)=CLK (Clock); GPIO15(D8)=CS (Chip select)
//SD library-->8.3 filenames (ABCDEFGH.txt = abcdefgh.txt)
//RTC DS1307. I2C--> SCL(clk)=D1, SDA(data)=D2 (Shared with BME280)
//20220310 - TSBrownie.  Non-commercial use.
#include <SD.h>                      //SD card library
#include <SPI.h>                     //COM Serial Peripheral Interface bus for COMM, SD com
//WiFi Serving Def ===============================
#include <ESP8266WiFi.h>             //WiFi library
#include <WiFiClient.h>              //WiFi Client library
#include <ESP8266WebServer.h>        //Web Server library
#include <DNSServer.h>               //Domain Name Server
#include <ESP8266mDNS.h>             //ESP8266 mDNS specific
#define DBG_OUTPUT_PORT Serial       //Direct debug to Serial
//Set to desired softAP credentials. NOT configurable at runtime
#ifndef APSSID                       //Prevents incursive lib inclusions
#define APSSID "FreeLocalWeather"    //USER SUPPLIED: SSID
#define APPSK  ""                    //#define APPSK  "12345678"
#endif                               //

const char *softAP_ssid = APSSID;    //Soft access point ssid (def above)
const char *softAP_password = APPSK; //Soft access point password (def above)
const char *myHostname = "FreeInfo"; //Hostname for multicast DNS. Works on win. Try http://esp8266.local
char ssid[32] = "";                  //Don't set this credential. Configured at runtime & stored on EEPROM
char password[32] = "";              //Don't set this credential. Configured at runtime & stored on EEPROM
const byte DNS_PORT = 53;            //DNS server.  Port 53 for UDP activities & as server port for TCP
DNSServer dnsServer;                 //DNS server.  Port 53 for UDP activities & as server port for TCP
ESP8266WebServer server(80);         //Port 80, standard internet
IPAddress apIP(8, 8, 8, 8);          //Soft AP network parameters 8888 for auto show
IPAddress netMsk(255, 255, 255, 0);  //Soft AP network parameters
//End WiFi Serving Def ===========================
#include<Wire.h>                     //Wire library I2C for BMx
#define BME280Addr 0x76              //I2C addr BME280 I2C 0x76(108), or 0x77
#define DS1307 0x68                  //I2C addr RTC1307 (Default=0x68 Hex)
#define S_prt Serial.print           //Short name for Serial.print
#define S_pln Serial.println         //Short name for Serial.println
File dataTemp;                       //SD file4 temp txt weather data to display
File dataFile;                       //SD handle for SD files
char *FName2 = "Connect1.txt";       //USER: SD file keep connect info
char *FName3 = "20220122.txt";       //USER: SD file weather (ANSI encoding is best)
String FName4 = "DataTemp.txt";      //USER: SD weather WiFi display data (can not be char) 
String outBuff;                      //Output to COM and SD file
String timeString;                   //Build date time data 21 chars + null
byte second, minute, hour, DoW, Date, month, year;   //Btye variables for BCD time
const char *DoWList[]={"Null",",Sun,",",Mon,",",Tue,",",Wed,",",Thr,",",Fri,",",Sat,"}; //DOW from 1-7

unsigned long time_last = 0;         //Last time data was updated for millis()
unsigned int time2update = 10000;    //USER SUPPLIED: Milliseconds between readings
double calib = 0;                    //USER SUPPLIED: Clock calibration
double TempCal = -5;                 //USER SUPPLIED: Temp C calibration factor
double baroCal = 5;                  //USER SUPPLIED: Barometer calibration factor hPa
double Dh = 6.0;                     //USER SUPPLIED: Device height above sea level m
double P, p0;                        //P Uncomp Press at device alt, p0 Sea Level
double pressure;                     //Pressure variable
double pLast = 0;                    //Keep last pressure for rising, falling, steady
double cTemp = 0;                    //Temp in C
double cTempLast = 0;                //Keep last temp for rising, falling, steady
unsigned int b1[24];                 //16 bit array 0-24 (2 bytes). For BMx280 comp
unsigned int data[8];                //16 bit Data array 0-8. For BMx280 comp
//unsigned int dig_H1 = 0;
unsigned int con = 0;                //Number of user connects since startup 
const unsigned int avgPTsz = 10;     //USER SUPPLIED: # of past data to average,size of avgPT array
double avgPT[2][avgPTsz+1];          //Store past press & temp for averages
unsigned int avgPTindx = 0;          //Index to avgPT
bool avgFlag = false;                //Suppress avg press until enough samples

int Compare(float f1, float f2, int ex1){          //Compare(F1, F2, # decimal places)
  if(round(f1 * pow(10,ex1)) > (round(f2 * pow(10,ex1)))){      //Compare ex1 decimals           
     return(1);}
     else if(round(f1*pow(10,ex1)) < (round(f2*pow(10,ex1)))){  //Compare ex1 decimals
       return(-1);}
       else return(0);}

void avgPTCalc(){                    //Calc all avgs
  avgPT[0][0] = 0;                   //Init Press accumulator
  avgPT[1][0] = 0;                   //Init Temp accumulator
  for(int k=1; k<=avgPTsz; k++){     //Loop thru avg readings
    avgPT[0][0] += avgPT[0][k];      //Add to accumulator for P
    avgPT[1][0] += avgPT[1][k];      //Add to accumulator for T
  }
  avgPT[0][0] = avgPT[0][0]/avgPTsz; //Put average P at loc 0
  avgPT[1][0] = avgPT[1][0]/avgPTsz; //Put average T at loc 0
}

//RTC FUNCTIONS =====================================
byte BCD2DEC(byte val){              //Ex: 51 = 01010001 BCD. 01010001/16-->0101=5 then x10-->50  
  return(((val/16)*10)+(val%16));}   //         01010001%16-->0001. 50+0001 = 51 DEC

void GetRTCTime(){                               //Routine read real time clock, format data
  byte second;byte minute;byte hour;byte DoW;byte Date;byte month;byte year;
  Wire.beginTransmission(DS1307);                //Open I2C to RTC DS1307
  Wire.write(0x00);                              //Write reg pointer to 0x00 Hex
  Wire.endTransmission();                        //End xmit to I2C.  Send requested data.
  Wire.requestFrom(DS1307, 7);                   //Get 7 bytes from RTC buffer
  second = BCD2DEC(Wire.read() & 0x7f);          //Seconds.  Remove hi order bit
  minute = BCD2DEC(Wire.read());                 //Minutes
  hour = BCD2DEC(Wire.read() & 0x3f);            //Hour.  Remove 2 hi order bits
  DoW = BCD2DEC(Wire.read());                    //Day of week
  Date = BCD2DEC(Wire.read());                   //Date
  month = BCD2DEC(Wire.read());                  //Month
  year = BCD2DEC(Wire.read());                   //Year
  timeString = 2000+year;                        //Build Date-Time data to write to SD
  if (month<10){timeString = timeString + '0';}  //Pad leading 0 if needed
  timeString = timeString + month;               //Month (1-12)  
  if(Date<10){timeString = timeString + '0';}    //Pad leading 0 if needed
  timeString = timeString + Date;                //Date (1-30)
  timeString = timeString + DoWList[DoW];        //1Sun-7Sat (0=null)
  if (hour<10){timeString = timeString + '0';}   //Pad leading 0 if needed
  timeString = timeString + hour + ':';          //HH (0-24)
  if (minute<10){timeString = timeString + '0';} //Pad leading 0 if needed
  timeString = timeString + minute + ':';        //MM (0-60)
  if (second<10){timeString = timeString + '0';} //Pad leading 0 if needed
  timeString = timeString + second;              //SS (0-60)
}

//SD CARD FUNCTIONS =================================
void openSD() {                              //Routine to open SD card
  S_pln(); S_pln("Open SD card");            //User message
  if (!SD.begin(15)) {                       //If not open, print message.
    S_pln("Open SD card failed");
    return;}
  S_pln("SD Card open");
}

void openFile(char *FName, char RW) {        //Open 1 SD file at a time. Shared func.
  dataFile.close();                          //Ensure file status, before re-opening
  dataFile = SD.open(FName, RW);}            //Open Read at start.  Open at EOF for write/append

void print2File(char *FN, String tmp1){      //Print data to SD file. Shared func.
  openFile(FN, FILE_WRITE);                  //Open user SD file for write
  if (dataFile) {                            //If file there & opened --> write
    dataFile.println(tmp1);                  //Print string to file
    dataFile.close();                        //Close file, flush buffer (reliable but slower)
//    dataFile.flush();                        //Flush file buffer
  } else {S_pln("Error opening SD file");}   //File didn't open
}

void openFile4(String FN, byte RW) {     //Open SD WiFi data out file. char RW. Only 1 at a time.
  dataTemp.close();                      //Ensure file status, before re-opening
  dataTemp = SD.open(FN, RW);            //Open Read at end.  Open at EOF for write/append
}

void print4File(String B) {              //Print data to SD WiFi temp file
  openFile4(FName4,FILE_WRITE);          //Open user SD file for write
  if (dataTemp) {                        //If file there & opened --> write
    dataTemp.println(B);                 //Print string to file
    dataTemp.close();                    //Close file, flush buffer (reliable but slower)
  } else {S_pln("Error opening SD WiFi file for write");}   //File didn't open
}

void initTempFile(){                     //Initialize SD temp data file
  dataFile.close();                      //Ensure file is closed         
  SD.remove(FName4);                     //Delete connection info file
  dataFile.close();                      //Ensure file status, before re-opening
  dataFile = SD.open(FName4,FILE_WRITE); //Open Read at end.  Open at EOF for write/append
  dataFile.close();                      //Close SD connect file
//  server.send(200, "text/html", "Connect Data File Initialized");
}
//End SD Card FUNCTIONS ====================
//Web Routines =============================
void handleRoot(){ 
  openFile4(FName4,FILE_READ);                                   //Open SD file for read
  int SDfileSz = dataTemp.size();                                //Get file size
  S_prt("SDfileSz: "); S_pln(SDfileSz);                          //Data file size
  server.sendHeader("Content-Length", (String)(SDfileSz));       //Header info
  server.sendHeader("Cache-Control", "max-age=2628000, public"); //Cache 30 days
  String dataType = "text/plain";
  if(FName4.endsWith("/")) FName4 += "index.htm";
  if(FName4.endsWith(".src")) FName4 = FName4.substring(0, FName4.lastIndexOf("."));
  else if(FName4.endsWith(".html")) dataType = "text/html";
  else if(FName4.endsWith(".mhtml")) dataType = "text/mhtml";
  else if(FName4.endsWith(".css")) dataType = "text/css";
  else if(FName4.endsWith(".js")) dataType = "application/javascript";
  else if(FName4.endsWith(".png")) dataType = "image/png";
  else if(FName4.endsWith(".gif")) dataType = "image/gif";
  else if(FName4.endsWith(".jpg")) dataType = "image/jpeg";
//  else if(FName4.endsWith(".ico")) dataType = "image/x-icon";
  else if(FName4.endsWith(".xml")) dataType = "text/xml";
  else if(FName4.endsWith(".pdf")) dataType = "application/pdf";
//  else if(FName4.endsWith(".zip")) dataType = "application/zip";
  size_t fsizeSent = server.streamFile(dataTemp, dataType);   //Get data size
  S_prt("fsizeSent: "); S_pln(fsizeSent);                     //Size sent data

  GetRTCTime();                                  //Get time from RTC
  outBuff=String(timeString+','+con++ +" Client IP: "+server.client().remoteIP().toString());
  //NOTE: WiFi.macAddress() returns SERVER mac address. Not interesting.
  dataTemp.close();                              //Close SD file
  delay(100);                                    //File settling time
  S_pln(outBuff);                                //Print connect data to serial
  print2File(FName2, outBuff);                   //Write connect data to SD
  delay(100);                                    //File settling time
}

void handleNotFound() {                          //Server Errors
  String message = F("File Not Found\n\n");
  message += F("URI: ");
  message += server.uri();
  message += F("\nMethod: ");
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += F("\nArguments: ");
  message += server.args();
  message += F("\n");
  for (uint8_t i = 0; i < server.args(); i++) {
    message += String(F(" ")) + server.argName(i) + F(": ") + server.arg(i) + F("\n");}
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.send(404, "text/plain", message);
}

void getdata() {                         //Dedicated routine because of server.on call
  openFile(FName3, FILE_READ);           //Open file 3 (all weather data)
  int SDfileSz = dataFile.size();        //Get file size
  S_prt("SDfileSz: "); S_pln(SDfileSz);  //User info.
  server.sendHeader("Content-Length", (String)(SDfileSz));       //
  server.sendHeader("Cache-Control", "max-age=2628000, public"); //Cache 30 days
  size_t fsizeSent = server.streamFile(dataFile, "text/plain");  //
  S_prt("fsizeSent: "); S_pln(fsizeSent);                        //User info on data sent
  dataFile.close();                                              //Close SD file
  delay(100);
}

void getConData() {                      //Get connection data
  dataFile.close();                      //Ensure file status, before re-opening
  dataFile = SD.open(FName2, FILE_READ); //Open Read at end.  Open at EOF for write/append
  int SDfileSz = dataFile.size();        //Get file size
//  S_prt("SDfileSz: "); S_pln(SDfileSz);  //User info.
  server.sendHeader("Content-Length", (String)(SDfileSz));       //
  server.sendHeader("Cache-Control", "max-age=2628000, public"); //Cache 30 days
  size_t fsizeSent = server.streamFile(dataFile, "text/plain");  //
//  S_prt("fsizeSent: "); S_pln(fsizeSent);                        //Data size sent
  dataFile.close();                                              //Close SD file
  delay(100);
}
//End Web Routines =========================
//SETUP  ===================================
void setup(){                            //SETUP()
  Wire.begin();                          //Init I2C com
  Serial.begin(115200);                  //Init Serial com
  delay(1000);                           //Allow serial to come online
  S_pln("Connecting to network ");       //User msg
  WiFi.softAPConfig(apIP, apIP, netMsk); //Soft access point set IP, mask
  WiFi.softAP(softAP_ssid, softAP_password);  //Remove password parameter for open AP
  delay(500);                            //500 Delay to avoid blank IP address
  S_prt("AP IP address: ");
  S_pln(WiFi.softAPIP());
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);  //Setup DNS server redirecting all the domains to apIP
  dnsServer.start(DNS_PORT, "*", apIP);  //Setup DNS server redirecting all the domains to apIP
  server.on("/", handleRoot);            //First web input
//  server.on("/initTempfile", initTempFile);  //Delete file
  server.on("/getdata", getdata);        //Weather Data download
  server.on("/getConData", getConData);  //Connection Data download
  server.onNotFound(handleNotFound);     //Handle not found error
  server.begin();                        //Start server
  S_pln("HTTP Server Started");          //User info
  
  openSD();                              //Call open SD card routine
  GetRTCTime();                          //Get time from real time clock
  outBuff = String("RecType,DeviceAlt,DateTime,DevicehPa,SeaLevel-hPa,TempC,RelHum"); //File Header
  print2File(FName3, outBuff);           //Write string to SD file
  outBuff = "C,"+timeString+calib;       //Prepare calibration string
  print2File(FName3, outBuff);           //Write string to SD file
}

//LOOP ===============
void loop(){                             //LOOP()
  unsigned int dig_H1 = 0;
  dnsServer.processNextRequest();        //DNS NECESSARY
  server.handleClient();                 //Handle client calls
  for (int i = 0; i < 24; i++)  {        //Get data 1 reg (2 bytes, 16 bits) at a time
    Wire.beginTransmission(BME280Addr);  //Begin I2C to BMx280
    Wire.write((136 + i));               //Data register 136-159 (0x88-0x9F)
    Wire.endTransmission();              //End I2C Transmission
    Wire.requestFrom(BME280Addr, 1);     //Get BMx280 1 byte data in LSBMSB order
    if (Wire.available() == 1){          //If device available
      b1[i] = Wire.read();}              //Read reg, store in b1[0-23]
  }

  //Temperature coefficients (byte1: LSB<>MSB, bitwise & 11111111; byte 2 same+shift)
  unsigned int dig_T1=(b1[0] & 0xFF)+((b1[1] & 0xFF)*256); //0x88/0x89 
  int dig_T2 = b1[2] + (b1[3] * 256);    //0x8A/0x8B
  int dig_T3 = b1[4] + (b1[5] * 256);    //0x8C/0x8D

  //Pressure coefficients (byte1: LSB<>MSB, bitwise & 11111111; byte 2 same+shift)
  unsigned int dig_P1=(b1[6] & 0xFF)+((b1[7] & 0xFF)*256);//0x8E/0x8F
  int dig_P2 = b1[8] + (b1[9] * 256);    //0x90/0x91
  int dig_P3 = b1[10] + (b1[11] * 256);  //0x92/0x93
  int dig_P4 = b1[12] + (b1[13] * 256);  //0x94/0x95
  int dig_P5 = b1[14] + (b1[15] * 256);  //0x96/0x97
  int dig_P6 = b1[16] + (b1[17] * 256);  //0x98/0x99
  int dig_P7 = b1[18] + (b1[19] * 256);  //0x9A/0x9B
  int dig_P8 = b1[20] + (b1[21] * 256);  //0x9C/0x9D
  int dig_P9 = b1[22] + (b1[23] * 256);  //0x9E/0x9F

  Wire.beginTransmission(BME280Addr);    //Start I2C Transmission BMx280
  Wire.write(161);                       //Select data register
  Wire.endTransmission();                //End I2C Transmission
  Wire.requestFrom(BME280Addr, 1);       //Request 1 byte of data
  if (Wire.available() == 1){            //If data
    dig_H1 = Wire.read();                //Read & store 1 byte of data
  }
  for (int i = 0; i < 7; i++)  {
    Wire.beginTransmission(BME280Addr);  //Start I2C Transmission
    Wire.write((225 + i));               //Select data register
    Wire.endTransmission();              //End I2C Transmission
    Wire.requestFrom(BME280Addr, 1);     //Request 1 byte of data
    if (Wire.available() == 1){          //If wire
      b1[i] = Wire.read();               //Read 7 bytes data
    }
  }

  //Convert Humidity Coefficients
  int dig_H2 = b1[0] + (b1[1] * 256);
  unsigned int dig_H3 = b1[2] & 0xFF ;
  int dig_H4 = (b1[3] * 16) + (b1[4] & 0xF);
  int dig_H5 = (b1[4] / 16) + (b1[5] * 16);
  int dig_H6 = b1[6];
  Wire.beginTransmission(BME280Addr);    //Start I2C Transmission
  Wire.write(0xF2);                      //Select humidity register
  Wire.write(0x01);                      //Humidity over-sampling rate=1
  Wire.endTransmission();                //End I2C Transmission
  Wire.beginTransmission(BME280Addr);    //Start I2C Transmission
  Wire.write(0xF4);                      //Select control measurement register
  Wire.write(0x27);                      //Normal mode, temp & pressure over-sampling rate=1
  Wire.endTransmission();                //End I2C Transmission
  Wire.beginTransmission(BME280Addr);    //Start I2C Transmission
  Wire.write(0xF5);                      //Select config register
  Wire.write(0xA0);                      //Stand_by time = 1000ms
  Wire.endTransmission();                //End I2C Transmission
  for (int i = 0; i < 8; i++){
    Wire.beginTransmission(BME280Addr);  //Start I2C Transmission
    Wire.write((247 + i));               //Select data register
    Wire.endTransmission();              //End I2C Transmission
    Wire.requestFrom(BME280Addr, 1);     //Request 1 byte of data
    if (Wire.available() == 1){          //If data
      data[i] = Wire.read();}            //Read 8 bytes of data
  }

  //Convert pressure & temperature data to 19-bits
  long adc_p = (((long)(data[0] & 0xFF) * 65536) + ((long)(data[1] & 0xFF) * 256) + (long)(data[2] & 0xF0)) / 16;
  long adc_t = (((long)(data[3] & 0xFF) * 65536) + ((long)(data[4] & 0xFF) * 256) + (long)(data[5] & 0xF0)) / 16;

  //Convert humidity data
  long adc_h = ((long)(data[6] & 0xFF) * 256 + (long)(data[7] & 0xFF));

  //Temperature offset calculations (per Bosch)
  double var1 = (((double)adc_t) / 16384.0 - ((double)dig_T1) / 1024.0) * ((double)dig_T2);
  double var2 = ((((double)adc_t) / 131072.0 - ((double)dig_T1) / 8192.0) *
                 (((double)adc_t) / 131072.0 - ((double)dig_T1) / 8192.0)) * ((double)dig_T3);
  double t_fine = (long)(var1 + var2);              //
  cTemp = ((var1 + var2) / 5120.0) + TempCal;//Calc C, add calibration factor
  double fTemp = cTemp * 1.8 + 32;                  //Convert C to F

  //Pressure offset calculations (per Bosch)
  var1 = ((double)t_fine / 2.0) - 64000.0;          //
  var2 = var1 * var1 * ((double)dig_P6) / 32768.0;  //
  var2 = var2 + var1 * ((double)dig_P5) * 2.0;      //
  var2 = (var2 / 4.0) + (((double)dig_P4) * 65536.0); //
  var1 = (((double) dig_P3) * var1 * var1 / 524288.0 + ((double) dig_P2) * var1) / 524288.0;
  var1 = (1.0 + var1 / 32768.0) * ((double)dig_P1); //
  double p = 1048576.0 - (double)adc_p;
  p = (p - (var2 / 4096.0)) * 6250.0 / var1;        //
  var1 = ((double) dig_P9) * p * p / 2147483648.0;  //
  var2 = p * ((double) dig_P8) / 32768.0;           //
//  pressure = ((p + (var1 + var2 + ((double)dig_P7)) / 16.0) / 100) + baroCal; //hPa
  pressure=((p+(var1+var2+((double)dig_P7))/16.0)/100)+baroCal; //hPa

  //Humidity offset calculations
  double var_H = (((double)t_fine) - 76800.0);
  var_H = (adc_h - (dig_H4 * 64.0 + dig_H5 / 16384.0 * var_H)) * (dig_H2 / 65536.0 * (1.0 + dig_H6 / 67108864.0 * var_H * (1.0 + dig_H3 / 67108864.0 * var_H)));
  double humidity = var_H * (1.0 - dig_H1 * var_H / 524288.0);
  if (humidity > 100.0){humidity = 100.0;}
    else if (humidity < 0.0){humidity = 0.0;}

  //OUTPUT TO SERIAL MONITOR + WiFi CONNECTIONS
  if(millis() >= time_last + time2update){          //Delay data update per user
    time_last += time2update;                       //Keep last time of update
    initTempFile();                                 //Reset the temp file, fill with current data
    p0 = ((pressure/100) * pow(1 - (0.0065 * Dh / (cTemp + 0.0065 * Dh + 273.15)), -5.257));

    avgPTCalc();                                    //Calculate averages
    outBuff = "========= " + String(timeString); 
    S_pln(outBuff);print4File(outBuff);
    outBuff = "Device Altitude(abs): "+ String(Dh)+" meters AMSL";
    S_pln(outBuff);print4File(outBuff);
//    outBuff = "Temperature: "+String(cTemp)+("°C; ")+String(fTemp)+"°F  ";
    outBuff = "Temperature: "+String(cTemp)+("C; ")+String(fTemp)+"F  ";

    if (Compare(cTemp,avgPT[1][0],0) == 1){         //Compare(F1,F2,# decimal places compared)
        S_pln(outBuff+"Rising");print4File(outBuff+"Rising");} //If temp is rising from prior
      else if(Compare(cTemp,avgPT[1][0],0) == -1){      
        S_pln(outBuff+"Falling");print4File(outBuff+"Falling");}      //If temp is falling from prior 
        else {S_pln(outBuff+"Steady");print4File(outBuff+"Steady");}  //Else steady

//    S_prt("Relative Humidity: ");S_prt(humidity);S_pln("%RH");
    outBuff = "Relative Humidity: "+String(humidity)+"%RH";
    S_pln(outBuff);print4File(outBuff);
  
    outBuff = "Barometric Pressure(abs): ";
    if (Compare(pressure,avgPT[0][0],1) == 1){      //Compare(F1, F2, # decimal places compared)
      if(avgFlag){outBuff+="Rising-Fair Weather";}} //If press is rising from prior
      else if(Compare(pressure,avgPT[0][0],1) == -1){
        if(avgFlag){outBuff+="Falling-Stormy";}}    //If press is falling from prior
        else {if(avgFlag){outBuff+="Steady-No Change";}}   //Else steady
    S_pln(outBuff);print4File(outBuff);             //Output buffer string

    if(avgPTindx >= avgPTsz){                       //Circular index to averages
      avgPTindx = 1;                                //If too big-->reset to 1
      avgFlag = true;}                              //Display average press
      else {avgPTindx++;}                           //Increment index
    avgPT[0][avgPTindx] = pressure;                 //Save press to avg
    avgPT[1][avgPTindx] = cTemp;                    //Save temp to avg

    outBuff = "  "+String(pressure, 3)+" hPa(mbar); "+String(pressure * 100, 3)+" Pa"; //Print Pressure in Pa mbar
    S_pln(outBuff);print4File(outBuff);
    outBuff = "  "+String(avgPT[0][0], 3) +" hPa, Short Term Past Average "; //Avg Pressure
    if(avgFlag){S_pln(outBuff);print4File(outBuff);}                         //Avg Press if available
    outBuff = "  "+String(pressure * 0.750061683, 3)+" mmHg"; //Print Pressure in mmHg
    S_pln(outBuff);print4File(outBuff);
    outBuff = "  "+String(pressure * 0.000986923, 6)+" Atm "; //Print Pressure in atm
    S_pln(outBuff);print4File(outBuff);
    outBuff = "Barometric Pressure-Comp'ed To Sealevel";
    S_pln(outBuff);print4File(outBuff);
    outBuff = "  "+String(p0 * 100, 3)+" hPa(mbar); "+String(p0 * 10000, 3)+" Pa";//Comp'ed Pressure in Pa, mbar, hPa
    S_pln(outBuff);print4File(outBuff);
    outBuff = "  "+String(p0 * 075.0061683, 3)+" mmHg";       //Comp'ed Pressure in mmHg
    S_pln(outBuff);print4File(outBuff);
    outBuff = "  "+String(p0 * 000.0986923, 6)+" Atm ";       //Comp'ed Pressure in Atm
    S_pln(outBuff);print4File(outBuff);
   
    GetRTCTime();                                             //Get time from RTC
    outBuff = "D,"+String(Dh)+','+timeString+','+String(pressure)+','+
      String(p0*100)+','+String(cTemp)+','+String(humidity);  //Ongoing SD weather data
    print2File(FName3, outBuff);                              //Write weather data to SD
  }                                                           //End: if(millis() >= time_last + time2update)
}                                                             //End: Loop()
