#include <Arduino.h>

#include <WiFi.h>
#include <Wire.h>
#include <Stepper.h>
#include <TFT_eSPI.h>
#include <AsyncUDP.h>
#include <IRremote.hpp>
#include <ArduinoJson.h>
#include <ArduinoHttpClient.h>
#include <Adafruit_MCP23X17.h>
#include <ESP32Servo.h>

//In /c/Users/<user>/.platformio/packages/framework-arduinoespressif\variants\ttgo-t1\pins_arduino.h:24
//Changed SCL pin from 23 to 22
TFT_eSPI tft = TFT_eSPI();

Adafruit_MCP23X17 mcp = Adafruit_MCP23X17();

AsyncUDP UDP;
char server[] = "hauntedhallow.xyz";
WiFiClient client;
HttpClient httpClient = HttpClient(client, server, 80);

// change this to the number of steps on your motor
#define STEPS 100

int runtime = 0;
int lastDisplayUpdateTime;

const int Button1 = 35;

struct PrintBed{
    int size = 260;
    int width = size;
    int depth = size;
    int height = size;
};
PrintBed bed;

//Motor stuff
struct Motor{
    int max = 0; //This is how many steps our stepper can take
    bool ready = false;
    bool useMcp = false; //Use our external mcp pins?
    bool reverseDirection = false;
    float position;
    float lastPosition;
    int pins[4] = {0};
    int switches[2] = {-1, -1};
    Stepper stepper = Stepper(STEPS, 0, 0, 0, 0, useMcp, mcp);
    void init(int steps = STEPS, int speed = 30) {

        if (switches[0] > -1) {
            Serial.println("Initialized switches on pins " + String(switches[0]) + " & " + String(switches[1]));
            mcp.pinMode(switches[0], INPUT_PULLUP);
            mcp.pinMode(switches[1], INPUT_PULLUP);
        }

        Serial.println("Initializing motor on pins " + String(pins[0]) + ", " +  String(pins[1]) + ", " + String(pins[2]) + " & " + String(pins[3]) + " | Uses MCP: " + String(useMcp));

        if (reverseDirection)
            stepper = Stepper(steps, pins[2], pins[3], pins[0], pins[1], useMcp, mcp);
        else
            stepper = Stepper(steps, pins[0], pins[1], pins[2], pins[3], useMcp, mcp);

        stepper.setSpeed(speed);
    }
    bool scrolling = false;
    float source;
    float destination;
    void scrollTo(float  coord) {
        if (coord == position)
            return;

        Serial.println("Scrolling to " + String(coord));

        source = position;
        destination = coord;
        scrolling = true;
    }
    //This must be <= 1
    void changePosition(float amount) {
        Serial.println("Changing by: " + String(amount));

        //The stepper only takes ints so accomodate for that
        if ((int)lastPosition != (int)position)
            stepper.step(amount < 0 ? 1 : -1);

        lastPosition = position;
        position += amount;
    }
    void endScroll() {
        Serial.println("Ending scroll at " + String(position));

        scrolling = false;
        destination = position;
        lastPosition = position;
    }
};
Motor motors[3];

bool isInitialized() {
    bool initialized = true;
    for (auto & motor : motors) {
        if (!motor.ready) {
            initialized = false;
            break;
        }
    }

    return initialized;
}

//Pen vars
#define PEN_FWD 12
#define PEN_BCK 13
struct PenObj {
    Servo speed;
    bool Hot = false;
    bool Retracting = false;
    bool Extruding = false;
    void init(){
        Serial.println("Attaching servo pin");
        speed.attach(2);
        speed.write(90); //halfway between 0 and 180
    };
    void heat() {
        Serial.println("Heating pen...");
        digitalWrite(PEN_FWD, HIGH);
        delay(500);
        digitalWrite(PEN_FWD, LOW);
        Hot = true;
    }
    void toggleRetract() {
        if (Extruding) {
            Serial.println("Could not retract while extruding!");
            return;
        }

        //Handle pen functions
        if (!Retracting) {
            Serial.println("Began retracting");
            Retracting = true;
            digitalWrite(PEN_BCK, HIGH);
        } else {
            Serial.println("Began retracting");
            Retracting = false;
            digitalWrite(PEN_BCK, LOW);
        }
    }
    void toggleExtrude() {
        if (Retracting) {
            Serial.println("Could not extrude while retracting!");
            return;
        }

        //Handle pen functions
        if (!Extruding) {
            Serial.println("Began extruding");
            Extruding = true;
            digitalWrite(PEN_FWD, HIGH);
        } else {
            Serial.println("Stopped extruding");
            Extruding = false;
            digitalWrite(PEN_FWD, LOW);
        }
    }
    void setSpeed(int newSpeed) {
        Serial.println("Setting speed to " + String(newSpeed));
        speed.write(newSpeed);
    }
};
PenObj Pen;

String version = "3.1";
IPAddress LocalIP(192, 168, 1, 222);

void sendPost(String uri, String data) {
    httpClient.beginRequest();
    int responseCode = httpClient.post(uri);
    Serial.println("Sending " + uri + "@" + String(responseCode) + " | " + data);
    httpClient.sendHeader("Content-Type", "application/json");
    httpClient.sendHeader("Content-Length", data.length());
    httpClient.beginBody();
    httpClient.print(data);
    httpClient.endRequest();
}
void sendStatus() {
    sendPost("/api/printer/status", "{\"status\":" + String(isInitialized()) + "}");
}

void drawScreen(String message = "", bool updateStatus = true);
void drawScreen(String message, bool updateStatus) {
    if (runtime - lastDisplayUpdateTime < 30000)
        return;

    Serial.println("Drawing screen...");

    //Draw our background
    tft.fillScreen(TFT_BLACK);

    String ip = WiFi.localIP().toString();

    Serial.println("str ip: " + ip + " | " + WiFi.status());

    //Draw IP and version
    tft.setTextColor(TFT_WHITE);
    tft.drawString("IP: " + LocalIP.toString(), 3, 126, 1);
    tft.drawString("v" + version, 207, 126, 1);

    if (WiFi.status() != WL_CONNECTED && WiFi.status() != 255) {
        tft.setTextColor(TFT_RED);
        tft.drawString("DISCONNECTED!", 20, 58, 4);

        //Else display whatever message we were passed here
    } else {
        tft.setTextColor(TFT_BLUE);
        message = "Connected!";
        tft.drawString(message, 45
                       , 58, 4);
    }

    if (updateStatus)
        sendStatus();

    lastDisplayUpdateTime = runtime;
}
void alert(String message, int timer = 1000);
void alert(String message, int timer) {
    Serial.println("ALERT: " + message);
    drawScreen(message, false);

    delay(timer);

    drawScreen("", false);
}

//Use -1 to not move axis at all
void scrollToCoords(float x, float y, float z, bool useRelative = false);
void scrollToCoords(float x, float y, float z, bool useRelative) {
    Serial.println("Sending to coords: " + String(x) + " | " + String(y) + " | " + String(z));

    if (useRelative) {
        x = motors[0].position + x;
        y = motors[1].position + y;
        z = motors[2].position + z;
    }

    if (x > -1)
        motors[0].scrollTo(x);

    if (y > -1)
        motors[1].scrollTo(y);

    if (z > -1)
        motors[2].scrollTo(z);
}

void initialize() {
    alert("Initializing...");
    delay(500);

    Serial.println("Switch set 8:" + String(mcp.digitalRead(8)) + " | 9: " + String(mcp.digitalRead(9)));
    Serial.println("Switch set 10: " + String(mcp.digitalRead(10)) + " | 11: " + String(mcp.digitalRead(11)));
    Serial.println("Switch set 12: " + String(mcp.digitalRead(12)) + " | 13: " + String(mcp.digitalRead(13)));

    // Zero out all axis
    while (mcp.digitalRead(8) == HIGH || mcp.digitalRead(10) == HIGH || mcp.digitalRead(12) == HIGH) {
        if (mcp.digitalRead(8) == HIGH)
            motors[0].stepper.step(-1);

        if (mcp.digitalRead(10) == HIGH)
            motors[1].stepper.step(-1);

        if (mcp.digitalRead(12) == HIGH)
            motors[2].stepper.step(-1);
    }

    motors[0].position = 0;
    motors[1].position = 0;
    motors[2].position = 0;

    Serial.println("Zero'd out all axis'.");

    // Max out all axis
    while (mcp.digitalRead(9) == HIGH || mcp.digitalRead(11) == HIGH || mcp.digitalRead(13) == HIGH) {
        if (mcp.digitalRead(9) == HIGH) {
            motors[0].stepper.step(1);
            motors[0].position++;
        }

        if (mcp.digitalRead(11) == HIGH) {
            motors[1].stepper.step(1);
            motors[1].position++;
        }

        if (mcp.digitalRead(13) == HIGH) {
            motors[2].stepper.step(1);
            motors[2].position++;
        }
    }

    motors[0].max = motors[0].position;
    motors[1].max = motors[1].position;
    motors[2].max = motors[2].position;

//    while (motors[0].position > (motors[0].max / 2)) {
//        motors[0].stepper.step(1);
//    }

    motors[0].ready = true;
    motors[1].ready = true;
    motors[2].ready = true;

    Serial.println("Initialized axis with the following sizes;");
    Serial.println("X: " + String(motors[0].max) + " | Y: " + String(motors[1].max) + " | Z: " + String(motors[2].max));

    scrollToCoords(motors[0].max / 2, motors[1].max / 2, 0);

    Serial.println("Initialization complete!");

    //Draw the screen again after we re-init
    drawScreen();
}

struct IRButtons{
    char power[9] = "ba45ff00";
    char volUp[9] = "b946ff00";
    char func[9] = "b847ff00";
    char back[9] = "bb44ff00";
    char play[9] = "bf40ff00";
    char frwrd[9] = "bc43ff00";
    char down[9] = "f807ff00";
    char volDwn[9] = "ea15ff00";
    char up[9] = "f609ff00";
    char zero[9] = "e916ff00";
    char eq[9] = "e619ff00";
    char repeat[9] = "f20dff00";
    char one[9] = "f30cff00";
    char two[9] = "e718ff00";
    char three[9] = "a15eff00";
    char four[9] = "f708ff00";
    char five[9] = "e31cff00";
    char six[9] = "a55aff00";
    char seven[9] = "bd42ff00";
    char eight[9] = "ad52ff00";
    char nine[9] = "b54aff00";
};
IRButtons buttons;
void performIRFunction(char event[9]) {
    alert("IR: " + String(event));
    Serial.println("Caught IR function " + String(event));

    //Power Button = Full Initialize
    if (strcmp(event, buttons.power) == 0)
        initialize();
    else
        //Zero Button = Set initialized without moving motors
    if (strcmp(event, buttons.zero) == 0)
        initialize();
    else
        //Skip Back = Zero all motors
    if (strcmp(event, buttons.back) == 0)
        scrollToCoords(0, 0, -1);
    else
        //Skip Forward = Max all motors
    if (strcmp(event, buttons.frwrd) == 0)
        scrollToCoords(bed.size, bed.size, -1);
    else
        //Play Button = Center all motors
    if (strcmp(event, buttons.play) == 0) {
        float half = (float)bed.size / 2;
        scrollToCoords(half, half, -1);
    } else
        //Volume Down = toggle lower pen
    if (strcmp(event, buttons.volDwn) == 0) {
        Serial.println("Sending down!");
        scrollToCoords(-1, -1, -50, true);
    } else
        //Volume Up = toggle raise pen
    if (strcmp(event, buttons.volUp) == 0) {
        Serial.println("Sending up!");
        scrollToCoords(-1, -1, 50, true);
    } else
        //Func Button = Turn on pen
    if (strcmp(event, buttons.func) == 0) {
        if (!Pen.Hot)
            Pen.heat();
    } else

        //Up Button = Retract
    if (strcmp(event, buttons.up) == 0)
        Pen.toggleRetract();
    else

        //Down Button = Extrude
    if (strcmp(event, buttons.down) == 0)
        Pen.toggleExtrude();
    else

    if (strcmp(event, buttons.one) == 0)
        Pen.setSpeed(0);
    else

    if (strcmp(event, buttons.two) == 0)
        Pen.setSpeed(180);

    alert("Func Complete");
}

void processUdp(AsyncUDPPacket packet) {
    //Validate the incoming IP address
    String source = "UNKNOWN";
    if (packet.remoteIP().toString().indexOf("128.199.7.114") != 0) {
        alert("Received UDP request from unknown source");
        return;
    }

    char* tmpStr = (char*) malloc(packet.length() + 1);
    memcpy(tmpStr, packet.data(), packet.length());
    tmpStr[packet.length()] = '\0'; // ensure null termination
    String dataString = String(tmpStr);
    free(tmpStr);

    Serial.println("Received " + dataString + " from UDP@" + packet.remoteIP().toString());

    StaticJsonDocument<200> json;
    DeserializationError error = deserializeJson(json, dataString);
    if (error) {
        Serial.println("JSON ERROR!");
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        alert("DeserializeJson!");
        return;
    }

    if (json.containsKey("CMD")) {
        if (json["CMD"] == "PEN_ON") {
            if (!Pen.Hot)
                Pen.heat();
        } else if (json["CMD"] == "PEN_FWD") {
            Pen.toggleExtrude();
        } else if (json["CMD"] == "PEN_BCK") {
            Pen.toggleRetract();
        } else
            alert("Requested action not recognized.");
    } else
        alert("Spirit requested no action.");
}
void connectToWifi() {
    WiFi.mode(WIFI_STA);

    if (!WiFi.config(
            LocalIP,
            IPAddress(192, 168, 1, 1),
            IPAddress(255, 255, 0, 0),
            IPAddress(8, 8, 8, 8),
            IPAddress(8, 8, 4, 4)
    ))
        Serial.println("STA Failed to configure");

    const char* ssid       = "###";
    const char* password   = "###";
    Serial.println("Connecting to " + String(ssid));

    WiFi.begin(ssid, password);
    for (int x = 1; x < 100; x++) {
        delay(500);
        if (WiFi.status() == WL_CONNECTED) {
            x = 200;
            Serial.println(" CONNECTED");
        }
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.print("Connected, IP address: ");
        Serial.println(WiFi.localIP().toString());
        Serial.print("MAC Address: ");
        Serial.println(WiFi.macAddress());
        Serial.print("Gateway IP: ");
        Serial.println(WiFi.gatewayIP());
        Serial.print("DNS Server: ");
        Serial.println(WiFi.dnsIP());
    }
}

void setup() {
    //Init the display
    tft.init();
    tft.setRotation(3);
    tft.setSwapBytes(true);

    tft.fillScreen(TFT_BLACK);

    Serial.begin(115200);

    drawScreen("Booting Up...");

    if (!mcp.begin_I2C()) {
        Serial.println("Error Initializing MCP.");
    }

    pinMode(Button1, INPUT);

    //3D Pen
    pinMode(PEN_BCK, OUTPUT); //Bck
    pinMode(PEN_FWD, OUTPUT); //Fwd
    Pen.init();

    //connect to WiFi
    connectToWifi();

    if(UDP.listen(LocalIP, 4225)) {
        Serial.println("UDP listening locally on IP \"" + LocalIP.toString() + ":" + 4225 + "\"");
        UDP.onPacket([](AsyncUDPPacket packet) {
            Serial.println("Received UDP Packet. Processing...");
            processUdp(packet);
        });
    }

    IrReceiver.begin(36);

    /*
     * Initialize our axis'
     * Note that all directions are relative to the viewer when facing the printer.
     */

    //X Axis - Left/Right
    motors[0].useMcp = true;
    motors[0].switches[0] = 8;
    motors[0].switches[1] = 9;
    motors[0].pins[0] = 0;
    motors[0].pins[1] = 1;
    motors[0].pins[2] = 2;
    motors[0].pins[3] = 3;
    motors[0].reverseDirection = true;
    motors[0].init();

    //Y Axis - Backwards/Forwards
    motors[1].useMcp = true;
    motors[1].switches[0] = 10;
    motors[1].switches[1] = 11;
    motors[1].pins[0] = 4;
    motors[1].pins[1] = 5;
    motors[1].pins[2] = 6;
    motors[1].pins[3] = 7;
    motors[1].reverseDirection = true;
    motors[1].init();

    //Z Axis - Up/Down
    motors[2].switches[0] = 12;
    motors[2].switches[1] = 13;
    motors[2].pins[0] = 27;
    motors[2].pins[1] = 26;
    motors[2].pins[2] = 25;
    motors[2].pins[3] = 33;
    motors[2].reverseDirection = true;
    motors[2].init(32, 200);
}

void loop() {
    //Tne global increment value for our steppers
    int increment = 1;
    int motorIndex = 0;
    for (auto & motor : motors) {
        if (motor.scrolling) {

            //If the motor is moving forward and is at or past its destination,
            //Or if the motor is moving backwards and is at or in front of its destination,
            if (motor.lastPosition < motor.position && motor.position >= motor.destination ||
                motor.lastPosition > motor.position && motor.position <= motor.destination) {

                Serial.println("Last: " + String(motor.lastPosition) + " | Current: " + String(motor.position));

                motor.endScroll();
            } else {
                float xIncrement = 1;
                float yIncrement = 1;

                Serial.println("updating motor destination from " + String(motor.position) + " to " + String(motor.destination));

                //If this isn't our vertical axis then normalize our diagonal movement
                if (motorIndex != 2) {

                    //pos1 = 25 | dest1 = 32
                    //pos2 = 20 | dest2 = 35
                    //7 > 15
                    float xDiff = abs(motors[0].destination - motors[0].position); // 7
                    float yDiff = abs(motors[1].destination - motors[1].position); // 15
                    //x is bigger get y in pieces
                    if (xDiff > yDiff) {
                        yIncrement = yDiff / xDiff; //Only move  y by this amount
                    } else {
                        xIncrement = xDiff / yDiff;
                    }
                }

                if (motor.destination > motor.position) {
                    switch (motorIndex) {
                        case 0:
                            motor.changePosition(xIncrement);
                            break;
                        case 1:
                            motor.changePosition(yIncrement);
                            break;
                        case 2:
                        default:
                            motor.changePosition(increment);
                            break;
                    }

                } else {
                    switch (motorIndex) {
                        case 0:
                            motor.changePosition(-xIncrement);
                            break;
                        case 1:
                            motor.changePosition(-yIncrement);
                            break;
                        case 2:
                        default:
                            motor.changePosition(-increment);
                            break;
                    }
                }
            }
        }

        motorIndex++;
    }

//    handle ir commands
    if (IrReceiver.decode()) {
        if (IrReceiver.decodedIRData.decodedRawData > 0) {
            char code[9];
            itoa(IrReceiver.decodedIRData.decodedRawData, code, 16);
            performIRFunction(code);
        }

        IrReceiver.resume(); // Enable receiving of the next value
    }

    if (digitalRead(Button1) == LOW) {
        Serial.println("Redrawing screen...");

        Serial.println("Switch set 8:" + String(mcp.digitalRead(8)) + " | 9: " + String(mcp.digitalRead(9)));
        Serial.println("Switch set 10: " + String(mcp.digitalRead(10)) + " | 11: " + String(mcp.digitalRead(11)));
        Serial.println("Switch set 12: " + String(mcp.digitalRead(12)) + " | 13: " + String(mcp.digitalRead(13)));

        drawScreen("Message");

        delay(500);
    }

    runtime++;
}