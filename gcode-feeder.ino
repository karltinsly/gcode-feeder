/*
   This is my arduino sketch to run a CNC device
   in demo mode. It reads files off an SD card and
   streams them to the device. Note that the files must be named
   1.g, 2.g, etc. There can be one other file in the directory - it's the
   file for homing the machine, home.g.

   This is the v3 version. It checks for low on pin 2.
   If it's low, it prints the next pattern. You can hook
   a toggle switch and a momentary switch to the same pin (2)
   and be able to print continuously or just the next pattern and stop.

   This code is based on ArdSketch by tchepperz on github - thanks, Pierre!

   I have used this successfully with both a mega and a pro micro - both have
   additional hardware serial ports. This could probably be made to work
   with software serial, but I haven't tried it. You'll need the arduino and
   an SPI SD card reader. Hook them up like so:
   SD      ProMicro        Mega
   MOSI      16             51
   MISO      14             50
   SCLK      15             52
   CS        10             53

   Don't forget 5v power and ground.

   You will also need to connect GND, RX>TX, and TX>RX between the arduino and
   your controller. I've only done this with GRBL so far, but it should work with
   Marlin as well.

   If you try this and it homes but doesn't do anything else, make sure you're
   pulling pin 2 low. You can just connect pin 2 to ground if you just want it
   to print continuously.

   - Karl Tinsley

 */

#include <SD.h>

File currentFile;

// Storage for file information
const int MAX_FILES = 10;
int patternOrder[MAX_FILES];
int currentPattern = 0;
int fileCount = 0;
String filenames[MAX_FILES];
String homeFilename = "";

//settings for continuous pattern switches
const int CONTINUE_PIN = 2;     // pin for continous switch
boolean abortPattern = false;   // Used to abort an in progress pattern
const unsigned long DEBOUNCE_TIME_MS = 250;

// Aliases for serial ports
#define DebugSerial Serial
#define GrblSerial  Serial1

void setup() {
    // initialize pin for next pattern selector - notice the pullup
    pinMode(CONTINUE_PIN, INPUT_PULLUP);

    DebugSerial.begin(115200);  // regular serial monitor for troubleshooting
    GrblSerial.begin(115200);   // streams gcode to the controller

    currentPattern = 0;         // initialize the pattern array index - later incremented in openFileSD()

    checkSD();                  // make sure SD is working
    delay(2000);                // needed this or would sometimes miss the homing cycle

    countFiles(SD.open("/"),0); // counting files and saving as fileCount

    randomizePatterns();        // fills the patternOrder[] array and scrambles the index

    homeTable();                // opens home.gc file, and sends it.
}

void loop() {

    if(LOW == digitalRead(CONTINUE_PIN) || abortPattern) {
        // clear the abort pattern bit.
        abortPattern = false;

        openFileSD(); // Opens the currentPattern
        sendGcode();  // sends the file to the controller
        nextPattern();
    }

}

void nextPattern() {
    currentPattern++;
    if (currentPattern >= fileCount) {
        // start over when we reach the end
        currentPattern = 0;
    }
}

void checkSD() {

    // Check if SD card is OK

    // On the Ethernet Shield, CS is pin 4. It's set as an output by default.
    // Note that even if it's not used as the CS pin, the hardware SS pin
    // (10 on most Arduino boards, 53 on the Mega) must be left as an output
    // or the SD library functions will not work.
    // If possible, it's easiest to just use the SS pin for CS.
    // pinMode(10,OUTPUT); //if you use a different pin for CS, you might need these two lines
    // digitalWrite(10,HIGH); //change the pin number to 53 for a mega

    while(!SD.begin(10)) {
        DebugSerial.println("Please insert SD card...\n");
        delay(1000);
    }
    DebugSerial.println("SD card OK...\n");
    delay(1000);
}

// Counts files to be used in selecting from the array
void countFiles(File dir, int numTabs) {
    fileCount=0;
    while (true) {
        File entry =  dir.openNextFile();
        if (! entry) {
            // no more files
            break;
        }

        String filename(entry.name());
        if (entry.isDirectory()) {
            // Do nothing with directories
        } else if (filename.startsWith("home.")) {
            // Store this name, and move on.
            homeFilename = entry.name();
        } else {
            // it's a file, add to the file count
            filenames[fileCount] = entry.name();
            DebugSerial.println("Found pattern " + filename + "\tsz: " + String(entry.size()/1024) + "kB");
            fileCount++;
            if (fileCount >= MAX_FILES) {
                DebugSerial.println("ERROR: Too many patterns! Overwriting the last one.");
                // Go back one index. This will end in problems, but it will be more stable.
                fileCount--;
            }
        }
        entry.close();
    }
    DebugSerial.println("File count is " + String(fileCount)); //this shows up in the serial monitor if hooked up to a computer
}


void homeTable() {
    if (homeFilename.length() == 0) {
        // There's no home file on the disk, don't do anything
        DebugSerial.println("No home.gc file found.");
        return;
    }

    currentFile = SD.open(homeFilename, FILE_READ);
    DebugSerial.print("-- ");
    DebugSerial.print("File : ");
    DebugSerial.print(homeFilename);
    DebugSerial.print(" opened!");
    DebugSerial.println(" --\n");
    sendGcode();
}

// Randomize the indices. This will leave exactly one instance of each number in the array, and it
// will leave them in some random order.
void randomizePatterns() {

    //first fill array with sequential numbers
    for (int i=0; i < MAX_FILES; ++i) {
        patternOrder[i]=i;
    }

    randomSeed(analogRead(0)); // this seeds the random number generator

    // randomize the array. swap each item once with a new location.
    for (int j=0; j< fileCount; j++) {
        int pos = random(fileCount);

        // swap pos and j
        int t = patternOrder[j];
        patternOrder[j] = patternOrder[pos];
        patternOrder[pos] = t;
    }
}


void openFileSD() {

    // Fetch the next pattern id
    int nextPatternNumber = patternOrder[currentPattern];
    String fileName = filenames[nextPatternNumber];

    // open the file for printing
    currentFile = SD.open(fileName, FILE_READ);
    DebugSerial.print("-- ");
    DebugSerial.print("File : ");
    DebugSerial.print(fileName);
    DebugSerial.print(" opened!");
    DebugSerial.println(" --\n");
    delay(1000);
}

void emptySerialBuf() {
    while(GrblSerial.available()) {
        GrblSerial.read();
    }
}

// Wait for data on Serial
// Argument serialNum for Serial number
void waitSerial() {
    while(!GrblSerial.available()){
        checkButton(); // Check the button while we are waiting
        delay(1);
    }
}

// Return String  from serial line reading
String getSerial() {

    waitSerial();

    String inLine = "";
    while(GrblSerial.available()) {
        inLine += (char)GrblSerial.read();
        checkButton(); // Check the button while we are waiting
        delay(2);
    }
    return inLine;
}

// Checks the button, and determines if there was a button press.
//
// OK, this might be a little confusing, so here's how it (is supposed to) work.
//
// This function needs to be checked frequently, as often as possible, almost.
//
// This function will read the state of the button. If it doesn't match our current state, then it will
// add the time since the last check to the counter. If the counter goes over DEBOUNCE_TIME_MS, then
// The state change happens.
//
// If the state change goes from false to true, then we just pressed the button.
//
// This debounce is two way, so that if we have the switch in place, and it registers a false
// momentarily, it won't also trigger an abort.
//
void checkButton() {
    // Static variables (local, but persistent).
    static int debounceCounter = 0;
    static unsigned long prevTime = millis();

    // Since we are triggering on a false to true, set this to true to avoid the first trigger.
    static boolean buttonState = true;

    // Save the dt right now.
    unsigned long deltaTime_ms = millis() - prevTime;
    // Set up for the next dt.
    prevTime += deltaTime_ms;

    // Read the button
    boolean buttonPressed = (LOW == digitalRead(CONTINUE_PIN));

    if ((buttonPressed != buttonState) && debounceCounter < DEBOUNCE_TIME_MS) {
        // The button is changing, but we haven't triggered the change state yet.
        debounceCounter += deltaTime_ms;

        if (debounceCounter >= DEBOUNCE_TIME_MS) {
            // The state of the button has changed.

            if (buttonPressed) {
                // We just pressed the button.
                abortPattern = true;
            }

            buttonState = buttonPressed;
        }
    }

    if (buttonPressed == buttonState) {
        // We stopped pushing the button
        debounceCounter = 0;
    }
}

void sendGcode() {
    // READING GCODE FILE AND SEND ON SERIAL PORT TO GRBL
    // START GCODE SENDING PROTOCOL ON SERIAL 1

    String line = "";

    GrblSerial.print("\r\n\r\n");          //Wake up grbl
    delay(2);
    emptySerialBuf();

    if(currentFile) {
        // until the file's end
        while(currentFile.available()) {

            checkButton();
            if (abortPattern) {
                // The user pressed the button!
                DebugSerial.println("Pattern Aborted");
                break;
            }

            line = readLine(currentFile);        // read line in gcode file
            DebugSerial.print(line);        // send to serials
            GrblSerial.print(line);
            DebugSerial.print(getSerial()); // print grbl return on serial
        }
    }
    else {
        fileError();
    }

    currentFile.close();
    DebugSerial.println("Finish!!\n");
    delay(2000);
}

/*
void sendGcode() {
    // Testing Code - comment out the sendGcode stuff above and uncomment this
    // to test without having to stream the whole gcode file

    DebugSerial.println("Printing the file.");
    currentFile.close();
    DebugSerial.println("Finish!\n");
    delay(2000);
    // End Testing code
}
*/

// For file open or read error
void fileError() {

    DebugSerial.println("\n");
    DebugSerial.println("File Error !");
}

// return line from file reading
String readLine(File f) {

    char inChar = 0;
    String line = "";

    do {
        inChar =(char)f.read();
        line += inChar;
    } while(inChar != '\n');

    return line;
}

