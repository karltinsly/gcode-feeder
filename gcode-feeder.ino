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

File myFile;
File root;
boolean restart = true;

String fileExt=".g";

const int MAX_FILES = 100;
int patternOrder[MAX_FILES];
int currentPattern = 0;
int fileCount = 0;
String filenames[MAX_FILES];
String homeFilename = "";

//settings for continuous pattern switches
int contPin = 2; //pin for continous switch

boolean continuous = true;
boolean nextPat = true;
int contRead = HIGH;
int patRead = HIGH;
int prevCont; // the previous cont reading
int prevPat; //
long time = 0; //the last time the output pin was toggled;
long debounce = 200; // debounce time

// Aliases for serial ports
#define DebugSerial Serial
#define GrblSerial  Serial1

void setup() {

    pinMode(2, INPUT_PULLUP);   // initialize pin for next pattern selector - notice the pullup
    DebugSerial.begin(115200);  // regular serial monitor for troubleshooting
    GrblSerial.begin(115200);   // serial1 streams gcode to the controller

    nextPat = false;            // we won't print the next pattern until pin 2 goes low
    currentPattern = 0;         // initialize the pattern array index - later incremented in openFileSD()

    checkSD();                  // make sure SD is working
    delay(2000);                // needed this or would sometimes miss the homing cycle

    root = SD.open("/");        // opening to count files
    countFiles(root,0);         // counting files and saving as fileCount

    homeTable();                // opens the home.g file which homes and sets the zero points
    sendGcode();                // sends home.g
    randomizePatterns();        // fills the patternOrder[] array and scrambles the index

}

void loop() {

    contPat(); //function to see if pin 2 is low - don't print the next pattern unless it is

    // nextPat is true if pin 2 is low
    while(nextPat) {
        openFileSD(); // iterates through the scrambled array, constructs the file name and opens it
        sendGcode(); // sends the file to the controller
    }
}

//checks for low on pin 2
void contPat() {
    contRead = digitalRead(contPin); //read the toggle switch
    if (contRead == LOW) {
        nextPat = true;
    } else {
        nextPat = false;
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

    myFile = SD.open(homeFilename, FILE_READ);
    DebugSerial.print("-- ");
    DebugSerial.print("File : ");
    DebugSerial.print(homeFilename);
    DebugSerial.print(" opened!");
    DebugSerial.println(" --\n");
}

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

    nextPat = false; // reset the next pattern variable
    if (currentPattern >= fileCount) {
        // start over when we reach the end
        currentPattern = 0;
    }

    // Fetch the next pattern id
    int nextPatternNumber = patternOrder[currentPattern];
    String fileName = filenames[nextPatternNumber];

    // open the file for printing
    myFile = SD.open(fileName, FILE_READ);
    DebugSerial.print("-- ");
    DebugSerial.print("File : ");
    DebugSerial.print(fileName);
    DebugSerial.print(" opened!");
    DebugSerial.println(" --\n");
    currentPattern++; // Move to the next pattern
    delay(1000);
}


//All of the following code is unchanged (except for a little testing piece in sendGcode())

void emptySerialBuf() {
    while(GrblSerial.available()) {
        GrblSerial.read();
    }
}

// Wait for data on Serial
// Argument serialNum for Serial number
void waitSerial() {
    while(!GrblSerial.available()){
        delay(1);
    }
}

// Return String  from serial line reading
String getSerial() {

    waitSerial();

    String inLine = "";
    while(GrblSerial.available()) {
        inLine += (char)GrblSerial.read();
        delay(2);
    }
    return inLine;
}

void sendGcode() {
    // READING GCODE FILE AND SEND ON SERIAL PORT TO GRBL
    // START GCODE SENDING PROTOCOL ON SERIAL 1

    String line = "";

    GrblSerial.print("\r\n\r\n");          //Wake up grbl
    delay(2);
    emptySerialBuf();

    if(myFile) {
        // until the file's end
        while(myFile.available()) {
            line = readLine(myFile);    // read line in gcode file
            DebugSerial.print(line);         // send to serials
            GrblSerial.print(line);
            DebugSerial.print(getSerial()); // print grbl return on serial
        }
    }
    else {
        fileError();
    }

    myFile.close();
    DebugSerial.println("Finish!!\n");
    delay(2000);
}

/*
void sendGcode() {
    // Testing Code - comment out the sendGcode stuff above and uncomment this
    // to test without having to stream the whole gcode file

    DebugSerial.println("Printing the file.");
    myFile.close();
    DebugSerial.println("Finish!\n");
    nextPat = false;
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

