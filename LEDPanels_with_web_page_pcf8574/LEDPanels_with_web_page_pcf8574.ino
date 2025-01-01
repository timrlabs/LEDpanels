/******************************************************************************
This is an Arduino sketch to drive 1 LED panels based on MBI5034 LED drivers.

Originally written by Oliver Dewdney and Jon Russell for the Arduino Micro.

Modified and web enabled by Tim Endean for the ESP8266.

#
Basic Operation:

The objective was to be able to drive a panels with a single esp8266. 
The panel has two data lines.

ESP8266 improved clock speed and built in Wifi makes it a good cheap 
upgrade to the Arduino.

The code has a frame buffer in RAM with 4 sets of 384 bits 
(1 bank = 64 LEDs x 2 Rows x 3 bits (RGB) = 384) for each data line. 

The UpdateFrame loop iterates over the line of 384 bits of serial data from the
frame buffer and clocks them out quickly. This is done on an Timer interrupt, 
the interval between FrameUpdates is 1ms, I have not measured the refresh rate 
but it is fast enough that there is no flicker.

The need for a contiguous port mentioned below is negated by the much higher 
clock speed.

Ideally, we needed a contiguous port on the Microcontroller to be attached to 
8 external data lines for the four panels. But most Arduinos don’t have this. 
On the Arduino Micro, PortB has the RX LED on bit 0 and PortD has the TX LED on
bit 5. So, I have connected 4 data lines to the high nibble on PortB and 4 data
lines to the low nibble on PortD.

For each half a panel (one data line) there are 8 rows of 64 LEDs, addresses in 
banks. Bank 0x00 is row 0&4,  Bank 0x01 is row 1&5, Bank 0x02 is row 2&6, Bank 
0x03 is row 3&7.

Each call updates one bank and leaves it lit until the next interrupt.

This method updates the entire frame (1024 RGB LEDs) about 400 times a second.

This sketch now inherits from the Adafruit GFX library classes. This means we
can use the graphics functions like drawCircle, drawRect, drawTriangle, plus
the various text functions and fonts.

Wifi has been added and access to the graphics functions added to a web page.
Credit to copilot for implementing the web page and handling routines.

The plan is to use the panel in vertical orientation, to change change the GFX 
width / height, and change the setpixel to swap co-ordinates.


******************************************************************************/
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Adafruit_GFX.h>
#include <gfxfont.h>
#include "Ticker.h"
#include <fonts/TomThumb.h>
#include <Adafruit_I2CDevice.h>
#include <Adafruit_PCF8574.h>

/* Example for 1 button that is connected from PCF GPIO #0 to ground, 
 * and one LED connected from power to PCF GPIO #7
 * We also have the IRQ output connected to an Interrupt input pin on the 
 * Arduino so we are not constantly polling from the PCF8574 expander
 */

Adafruit_PCF8574 pcf;
#define I2C_ADDRESS 0x20 // Replace with your device's I2C address

//Adafruit_I2CDevice i2c_dev(I2C_ADDRESS);

class LedPanel : public Adafruit_GFX
{
  public:
    LedPanel() : Adafruit_GFX(64,16) {}; // change here to go into landscape
    void drawPixel(int16_t x, int16_t y, uint16_t color);
    uint8_t getPixel(int16_t x, int16_t y);
    uint16_t newColor(uint8_t red, uint8_t green, uint8_t blue);
    uint16_t getColor() { return textcolor; }
    void drawBitmapMem(int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t color);
};

LedPanel panel;

#define PAN_PIN_D1    0   //PD0 - D1 on Panel 1  d3
#define PAN_PIN_D2    2 //PD1 - D2 on Panel 1  d4
#define PAN_PIN_CLK   14  //PF4 - CLK on all Panels D5

#define PAN_PIN_D3    13   //PD0 - D1 on Panel 2  D7
#define PAN_PIN_D4    4   //PD1 - D2 on Panel 2 D2


#define PAN_PIN_A0    0  //PF1 - A0 on all panels pcf P0
#define PAN_PIN_A1    1  //PF6 - A1 on all Panels pcf p1
#define PAN_PIN_LAT   2  //PF5 - LAT on all Panels pcf p2
#define PAN_PIN_OE    3  //PF0 - OE on all Panels pcf p3

#define LED_BLACK 0
#define LED_BLUE 1
#define LED_GREEN 2
#define LED_CYAN 3
#define LED_RED 4
#define LED_MAGENTA 5
#define LED_YELLOW 6
#define LED_WHITE 7

// Replace with your network credentials
const char* ssid = "";
const char* password = "";


String webpage =  "<!DOCTYPE html>" 
 "<html>"
 "<head>" 
 "  <title>Adafruit GFX Tester</title>"
 "<style>   .led { width: 5px; height: 5px; margin: 1px; }   .black { background-color: black; }   .blue { background-color: blue; }    .green { background-color: green; }   .cyan { background-color: cyan; }    .red { background-color: red; }"
  " .magenta { background-color: magenta; }    .yellow { background-color: yellow; }    .white { background-color: white; }  </style>" 
 "  <script>"
 " function sendRequest(url) {"
   "   var xhr = new XMLHttpRequest();"
   "   xhr.open(\"GET\", url, true);"
   "   xhr.onreadystatechange = function () {"
   "   if (xhr.readyState === 4 && xhr.status === 200) {"
   "       var response = xhr.responseText;"
  "       updateLEDColors(response);" 
   "     }"
   "   };"
   "   xhr.send();"
   " }"
    "    function getColorClass(colorChar) {" 
 "      switch (colorChar) {" 
 "        case '0': return 'black';" 
 "        case '1': return 'blue';" 
 "        case '2': return 'green';" 
 "        case '3': return 'cyan';" 
 "        case '4': return 'red';" 
 "        case '5': return 'magenta';" 
 "        case '6': return 'yellow';" 
 "        case '7': return 'white';" 
 "        default: return 'black';" 
 "      }" 
 "    }" 
 "    function updateLEDColors(response) {" 
 "      var ledPanel = document.getElementById('ledPanel');" 
 "      ledPanel.innerHTML = '';  " 
 "      var rows = response.split('E');  " 
 "      rows.pop();  "  
 "      rows.forEach(row => {" 
 "        var rowDiv = document.createElement('div');" 
 "        rowDiv.style.display = 'flex';  " 
 "        for (var i = 0; i < row.length; i++) {" 
 "          var colorClass = getColorClass(row.charAt(i));" 
 "          var ledDiv = document.createElement('div');" 
 "          ledDiv.className = 'led ' + colorClass;" 
 "          rowDiv.appendChild(ledDiv);" 
 "        }" 
 "        " 
 "        ledPanel.appendChild(rowDiv);" 
 "      });" 
 "    }" 
 "" 
 
 " function sendText() {" 
 " var text = document.getElementById(\"textInput\").value;" 
 " var color = document.getElementById(\"colorSelector\").value;" 
 " sendRequest(\"/drawText?text=\" + encodeURIComponent(text) + \"&color=\" + color);" 
 " }" 
 "" 
 " function drawRect() {" 
 " var x = document.getElementById(\"rectX\").value;" 
 " var y = document.getElementById(\"rectY\").value;" 
 " var width = document.getElementById(\"rectWidth\").value;" 
 " var height = document.getElementById(\"rectHeight\").value;" 
 " var color = document.getElementById(\"colorSelector\").value;" 
 " sendRequest(\"/drawRect?x=\" + x + \"&y=\" + y + \"&width=\" + width + \"&height=\" + height + \"&color=\" + color);" 
 " }" 
 "" 
 " function fillRect() {" 
 " var x = document.getElementById(\"fillRectX\").value;" 
 " var y = document.getElementById(\"fillRectY\").value;" 
 " var width = document.getElementById(\"fillRectWidth\").value;" 
 " var height = document.getElementById(\"fillRectHeight\").value;" 
 " var color = document.getElementById(\"colorSelector\").value;" 
 " sendRequest(\"/fillRect?x=\" + x + \"&y=\" + y + \"&width=\" + width + \"&height=\" + height + \"&color=\" + color);" 
 " }" 
 " function drawCircle() {" 
 " var x = document.getElementById(\"circleX\").value;" 
 " var y = document.getElementById(\"circleY\").value;" 
 " var radius = document.getElementById(\"circleRadius\").value;" 
 " var color = document.getElementById(\"colorSelector\").value;" 
 " sendRequest(\"/drawCircle?x=\" + x + \"&y=\" + y + \"&radius=\" + radius + \"&color=\" + color);" 
 " }" 
 " function fillCircle() {" 
 " var x = document.getElementById(\"fillCircleX\").value;" 
 " var y = document.getElementById(\"fillCircleY\").value;" 
 " var radius = document.getElementById(\"fillCircleRadius\").value;" 
 " var color = document.getElementById(\"colorSelector\").value;"
 " sendRequest(\"/fillCircle?x=\" + x + \"&y=\" + y + \"&radius=\" + radius + \"&color=\" + color);" 
 " }"
 " window.onload = function () {"
   "   sendRequest('/getLEDColors');"
   " };"
 "  </script>" 
 "</head>" 
 "<body>" 
 "<h1>Adafruit GFX API Test</h1>"
  "<h1>LED Panel</h1>"
  "<div id=\"ledPanel\"></div>"
  " <h2>Color</h2>" 
 " <select id=\"colorSelector\">" 
 " <option value=\"0\">Black</option>" 
 " <option value=\"1\">Blue</option>" 
 " <option value=\"2\">Green</option>" 
 " <option value=\"3\">Cyan</option>" 
 " <option value=\"4\">Red</option>" 
 " <option value=\"5\">Magenta</option>" 
 " <option value=\"6\">Yellow</option>" 
 " <option value=\"7\">White</option>" 
 " </select>"
 "  <h2>Text</h2>" 
 "  <input type=\"text\" id=\"textInput\" placeholder=\"Enter text\">" 
 "  <button onclick=\"sendText()\">Send</button>" 
 "" 
 "  <h2>Draw Rectangle</h2>" 
 "  <input type=\"number\" id=\"rectX\" placeholder=\"X\">" 
 "  <input type=\"number\" id=\"rectY\" placeholder=\"Y\">" 
 "  <input type=\"number\" id=\"rectWidth\" placeholder=\"Width\">" 
 "  <input type=\"number\" id=\"rectHeight\" placeholder=\"Height\">" 
 "  <button onclick=\"drawRect()\">Draw Rectangle</button>" 
 "" 
 "  <h2>Fill Rectangle</h2>" 
 "  <input type=\"number\" id=\"fillRectX\" placeholder=\"X\">" 
 "  <input type=\"number\" id=\"fillRectY\" placeholder=\"Y\">" 
 "  <input type=\"number\" id=\"fillRectWidth\" placeholder=\"Width\">" 
 "  <input type=\"number\" id=\"fillRectHeight\" placeholder=\"Height\">" 
 "  <button onclick=\"fillRect()\">Fill Rectangle</button>" 
 "" 
 "  <h2>Draw Circle</h2>" 
 "  <input type=\"number\" id=\"circleX\" placeholder=\"X\">" 
 "  <input type=\"number\" id=\"circleY\" placeholder=\"Y\">" 
 "  <input type=\"number\" id=\"circleRadius\" placeholder=\"Radius\">" 
 "  <button onclick=\"drawCircle()\">Draw Circle</button>" 
 "" 
 "  <h2>Fill Circle</h2>" 
 "  <input type=\"number\" id=\"fillCircleX\" placeholder=\"X\">" 
 "  <input type=\"number\" id=\"fillCircleY\" placeholder=\"Y\">" 
 "  <input type=\"number\" id=\"fillCircleRadius\" placeholder=\"Radius\">" 
 "  <button onclick=\"fillCircle()\">Fill Circle</button>" 
 "</body>" 
 "</html>" ;
ESP8266WebServer server(80);

 String getColor(int color) 
 { 
  switch (color) { 
 case LED_BLACK: return "0"; 
 case LED_BLUE: return "1"; 
 case LED_GREEN: return "2"; 
 case LED_CYAN: return "3"; 
 case LED_RED: return "4"; 
 case LED_MAGENTA: return "5"; 
 case LED_YELLOW: return "6"; 
 case LED_WHITE: return "7"; 
 default: return "0";   
 /*case LED_BLACK: return "black"; 
 case LED_BLUE: return "blue"; 
 case LED_GREEN: return "green"; 
 case LED_CYAN: return "cyan"; 
 case LED_RED: return "red"; 
 case LED_MAGENTA: return "magenta"; 
 case LED_YELLOW: return "yellow"; 
 case LED_WHITE: return "white"; 
 default: return "black";*/ 
 }
return "b";
 }
String generateHTML() { String html =  ""; 
 for (int y = 0; y < panel.height();  y++) 
 { 
  for (int x = 0; x < panel.width(); x++) 
 { int color = panel.getPixel(x, y); 
 html += getColor(color);
 //"<div class='led' style='background-color:" + getColor(color) + ";'></div>"; 
 } 
 //Serial.println(html);
 html += "E"; 
 } 
 return html; 
}

String buildwebpage()
{
  
  String bwebpage = webpage;
 
  return bwebpage;
}



void loop() {
  server.handleClient();
}

void handleRoot() {
  server.send(200,"text/html", buildwebpage());
}

void handleGetLeds()
{
  server.send(200, "text/plain", generateHTML());
}

void handleDrawText() {
  String text = server.arg("text");
uint8_t color = server.arg("color").toInt();
  panel.fillScreen(LED_BLACK);
  panel.setFont(&TomThumb);
  panel.setCursor(0, 8);
  panel.setTextColor(color);
  panel.println(text);
  
  server.send(200, "text/plain", generateHTML());
}


void handleDrawRect() {
  int x = server.arg("x").toInt();
  int y = server.arg("y").toInt();
  int width = server.arg("width").toInt();
  int height = server.arg("height").toInt();
  uint8_t color = server.arg("color").toInt();
  panel.drawRect(x, y, width, height, color);
  server.send(200, "text/plain", generateHTML());
}

void handleFillRect() {
  int x = server.arg("x").toInt();
  int y = server.arg("y").toInt();
  int width = server.arg("width").toInt();
  int height = server.arg("height").toInt();
  uint8_t color = server.arg("color").toInt();
  panel.fillRect(x, y, width, height, color);
  server.send(200, "text/plain", generateHTML());
}

void handleDrawCircle() {
  int x = server.arg("x").toInt();
  int y = server.arg("y").toInt();
  int radius = server.arg("radius").toInt();
  uint8_t color = server.arg("color").toInt();
  panel.drawCircle(x, y, radius, color);
  server.send(200, "text/plain", generateHTML());
}

void handleFillCircle() {
  int x = server.arg("x").toInt();
  int y = server.arg("y").toInt();
  int radius = server.arg("radius").toInt();
  uint8_t color = server.arg("color").toInt();
  panel.fillCircle(x, y, radius, color);
 
  server.send(200, "text/plain", generateHTML());
}


void digitalWritefast(uint16_t pin, uint8_t val)
{
  if (val) {
  GPOS = (1 << pin);
  }
  else{
    GPOC = (1 << pin);
  }
}
uint8_t p20;
uint8_t i2csendbuffer[1024];
const uint8_t * i2csendptr = (const uint8_t *) i2csendbuffer;
uint16_t bufferindex = 0;
void pcfdigitalWritefast(uint16_t pin, uint8_t val)
{
  if (val)
    {
      p20 |= 1<<pin;
    }
    else 
    {
      p20 &= ~(1<<pin);
    }
  pcf.digitalWrite(pin,  val);
}

void sendport()
{
  //pcf.digitalWriteByte(p20);
  i2csendbuffer[bufferindex++] = p20;
 // Serial.println(bufferindex);
  //if (bufferindex == i2c_dev.maxBufferSize() )
    {
      i2csendbuf();
    }
}

void i2csendbuf()
{
//Serial.println(bufferindex);
if (bufferindex>0){
 // i2c_dev.write(i2csendptr, bufferindex);
  bufferindex=0;
}
  //Serial.print(bufferindex);
}

 class bgrc {
      public: byte blue[2];
      public: byte green[2];
      public: byte red[2];
   };

union sixteenpixels{
  byte bgr[6];
  bgrc bgrcc ;
  };


union sendbuffer
{
  byte twoline[48];
  sixteenpixels buffer[8];
};

union framebuffer 
{
public: byte frame[4][2][48];
// frame buffer, 16 lines in 4 partitions, 2 data lines, 64 leds, 3 bits per pixel, 24 bytes per frame line.
sendbuffer sendbuffers[8];
} ;

class Thepanel {

  public: framebuffer frame; // to use multiple pannels you could have an array of panels here, adjust setpixel and the display ISR.
  uint8_t widthpixel = 64;
  uint8_t hightpixel = 16;



   public: uint8_t getpixel(uint8_t x, uint8_t y)
   {
    uint8_t tmp = x;
      x=y;
      y=tmp;
      uint8_t bank = (x & 0b1100) >> 2;
      uint8_t dataline = (x & 0b1000) >> 3;
      uint8_t bufferindex = (((x & 0b011)<<1) + ( dataline ) ) ;
      uint8_t highbyte = (x & 0b100) >> 2;
    uint8_t c = (frame.sendbuffers[bufferindex].buffer[y/8].bgrcc.blue[highbyte] & 1<<(y%8)) >> (y%8);
    c |= ((frame.sendbuffers[bufferindex].buffer[y/8].bgrcc.green[highbyte] & 1<<(y%8)) >> (y%8) ) << 1;
    c |= ((frame.sendbuffers[bufferindex].buffer[y/8].bgrcc.red[highbyte] & 1<<(y%8)) >> (y%8)  )<< 2;
    return c;
   }

  public: void setpixel(uint8_t x, uint8_t y, uint8_t colour)
    {
      // To go into landscape, swap X and y co-ordinates.]]
      uint8_t tmp = x;
      x=y;
      y=tmp;
    //  x=15-x;
      uint8_t b = colour & 0x01;
      uint8_t g = (colour & 0x02) >> 1;
      uint8_t r = (colour & 0x04) >> 2;
      uint8_t bank = (x & 0b1100) >> 2;
      uint8_t dataline = (x & 0b1000) >> 3;
      uint8_t bufferindex = (((x & 0b011)<<1) + ( dataline ) ) ;
      uint8_t highbyte = (x & 0b100) >> 2;
    
      if (b) {
      frame.sendbuffers[bufferindex].buffer[y/8].bgrcc.blue[highbyte] |= 1<<(y%8);
      } else {
        frame.sendbuffers[bufferindex].buffer[y/8].bgrcc.blue[highbyte] &= ~(1<<(y%8));
      }
     if (r) {
      frame.sendbuffers[bufferindex].buffer[y/8].bgrcc.red[highbyte] |= 1<<(y%8);
      } else {
        frame.sendbuffers[bufferindex].buffer[y/8].bgrcc.red[highbyte] &= ~(1<<(y%8));
      }
 if (g) {
      frame.sendbuffers[bufferindex].buffer[y/8].bgrcc.green[highbyte] |= 1<<(y%8);
      } else {
        frame.sendbuffers[bufferindex].buffer[y/8].bgrcc.green[highbyte] &= ~(1<<(y%8));
      }      
    }

     public: void FillBuffer( uint8_t colour)
     {
      for (uint8_t x = 0; x< widthpixel; x++)
        for (uint8_t y = 0; y < hightpixel; y++)
          {
            setpixel(x,y,colour);
          }
     }

      public: void FillBufferCol( )
     {
      for (uint8_t x = 0; x< widthpixel; x++)
        for (uint8_t y = 0; y < hightpixel; y++)
          {
            setpixel(x,y,y&0x7);
          }
     }
};

Thepanel pframeb;

void LedPanel::drawPixel(int16_t x, int16_t y, uint16_t color) {
  pframeb.setpixel(x,y,color);
}

uint8_t LedPanel::getPixel(int16_t x, int16_t y){
  return pframeb.getpixel(x,y);
}

uint16_t LedPanel::newColor(uint8_t red, uint8_t green, uint8_t blue) {
  return (blue>>7) | ((green&0x80)>>6) | ((red&0x80)>>5);
}

void LedPanel::drawBitmapMem(int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t color) {
  int16_t i, j, byteWidth = (w + 7) / 8;

  for(j=0; j<h; j++) {
    for(i=0; i<w; i++ ) {
      if(bitmap[j * byteWidth + i / 8] & (128 >> (i & 7))) {
        panel.drawPixel(x+i, y+j, color);
      }
    }
  }
}

Ticker timer1;

uint8_t bank;
void displaybanks()
{
  timer1.detach();
  //Serial.println("start display banks");
   pcfdigitalWritefast(PAN_PIN_OE,HIGH);     // disable output
   sendport();
  /// Serial.println("start display bankzcxzzxs");
    if (bank & 0x01) {
    pcfdigitalWritefast(PAN_PIN_A0, HIGH);
  } else {
    pcfdigitalWritefast(PAN_PIN_A0, LOW);
  }
  if (bank & 0x02) {
    pcfdigitalWritefast(PAN_PIN_A1, HIGH);
  } else {
    pcfdigitalWritefast(PAN_PIN_A1, LOW);
  }
     sendport();
//Serial.println("start display banks1");
  for (uint32_t n = 0; n<384; n++) {
   uint32_t masku =  (n % 8);
   uint32_t byte = (n /8) ;
//Serial.println(n);

    if (((pframeb.frame.frame[bank][0][byte]>>masku) & 0x01 ) == 1)
      {
    digitalWritefast(PAN_PIN_D1,HIGH );//(*f & 00x01));
    } else {
      digitalWritefast(PAN_PIN_D1,LOW );//(*f & 00x01));
    }
    if (((pframeb.frame.frame[bank][1][byte]>>masku) & 0x01 ) == 1)
      {
       digitalWritefast(PAN_PIN_D2,HIGH );//(*f & 00x01));
    } else {
       digitalWritefast(PAN_PIN_D2,LOW );//(*f & 00x01));
    }
    
    digitalWritefast(PAN_PIN_CLK, LOW);
//    
    digitalWritefast(PAN_PIN_CLK, HIGH);
  //  sendport();
    }

  pcfdigitalWritefast(PAN_PIN_LAT, HIGH);   // toggle latch
  sendport();
   delayMicroseconds(1);
  pcfdigitalWritefast(PAN_PIN_LAT, LOW);
  pcfdigitalWritefast(PAN_PIN_OE, LOW);     // enable output
  sendport();
 // i2csendbuf();
 delayMicroseconds(1); // brightness, at 10us per 1ms, the panel uses approx 0.3A, which is managable instead of 10A.
  pcfdigitalWritefast(PAN_PIN_OE, HIGH);
  sendport();
  i2csendbuf(); 
  if (++bank>3) bank=0;
 timer1.attach(0.001, displaybanks); 
}


void setup() {
  Serial.begin(74880);
  Serial.println("Begin...");
  

while (!
pcf.begin(0x20, &Wire)) {
    Serial.println("Couldn't find PCF8574");
    delay(1000);
  }
 // i2c_dev.begin();
 // Wire.setClock(100000);
  for (uint8_t p=0; p<8; p++) {
    pcf.pinMode(p, OUTPUT);
  }
  pinMode(PAN_PIN_D1, OUTPUT);
  pinMode(PAN_PIN_D2, OUTPUT);
  
 // pinMode(PAN_PIN_A0, OUTPUT);
 // pinMode(PAN_PIN_A1, OUTPUT);
  pinMode(PAN_PIN_CLK, OUTPUT);
 // pinMode(PAN_PIN_LAT, OUTPUT);
 // pinMode(PAN_PIN_OE, OUTPUT);

  digitalWritefast(PAN_PIN_D1, LOW);
  digitalWritefast(PAN_PIN_D2, LOW);
  

  pcfdigitalWritefast(PAN_PIN_A0, LOW);
  pcfdigitalWritefast(PAN_PIN_A1, LOW);

  pcfdigitalWritefast(PAN_PIN_OE, HIGH);
  pcfdigitalWritefast(PAN_PIN_LAT, LOW);
  digitalWritefast(PAN_PIN_CLK, LOW);

  sendport();
   
  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  // Output the IP address 
 Serial.print("IP Address: "); 
 Serial.println(WiFi.localIP());

 timer1.attach(1, timer1ISR); // Attach ISR to fire every 0.01 seconds (100Hz)
  // Define server routes
server.on("/", HTTP_GET, handleRoot); 
server.on("/drawText", HTTP_GET, handleDrawText); 
server.on("/drawRect", HTTP_GET, handleDrawRect);
 server.on("/fillRect", HTTP_GET, handleFillRect); 
 server.on("/drawCircle", HTTP_GET, handleDrawCircle); 
 server.on("/fillCircle", HTTP_GET, handleFillCircle);
 server.on("/getLEDColors",HTTP_GET,handleGetLeds);
  server.begin();
  Serial.println("HTTP server started");
  }

void timer1ISR() {     // timer compare interrupt service routine
  displaybanks();
}


void testFonts(){
  panel.fillScreen(LED_BLACK);
  panel.setFont(&TomThumb);
  panel.setCursor(2, 6);
  panel.setTextColor(LED_GREEN);  
  panel.setTextSize(1);
  panel.println("S:0");
  panel.setCursor(2, 12);
  panel.setTextColor(LED_BLUE);  
  panel.println("L:2");
  panel.setCursor(2, 18);
  panel.setTextColor(LED_RED);  
  panel.println("501");
  panel.setTextColor(LED_WHITE);  
  panel.setCursor(2, 48);
  panel.println("Free");
  delay(10000);
}









