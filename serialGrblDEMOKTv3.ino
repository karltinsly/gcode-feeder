/*This is my arduino sketch to run a CNC device
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

String patNum = "";
String fileExt=".g";

int pats[100];
int patIdx = 0;
int fileCount = 0;
String numFiles;

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



void setup(){
  pinMode(2, INPUT_PULLUP); // initialize pin for next pattern selector - notice the pullup
	Serial.begin(115200); // regular serial monitor for troubleshooting
	Serial1.begin(115200); // serial1 streams gcode to the controller
  nextPat = false; // we won't print the next pattern until pin 2 goes low
  patIdx=0; // initialize the pattern array index - later incremented in openFileSD()
  checkSD(); // make sure SD is working
  delay(2000); // needed this or would sometimes miss the homing cycle
  root = SD.open("/"); // opening to count files
  countFiles(root,0); // counting files and saving as fileCount
  homeTable(); // opens the home.g file which homes and sets the zero points
  sendGcode(); // sends home.g
  randomPats(); // fills the pats[] array and scrambles the index
}


void loop(){
  contPat(); //function to see if pin 2 is low - don't print the next pattern unless it is
	while(nextPat){ // nextPat is true if pin 2 is low
   	openFileSD(); // iterates through the scrambled array, constructs the file name and opens it
		sendGcode(); // sends the file to the controller
	}
}
 

void contPat(){ //checks for low on pin 2
  contRead = digitalRead(contPin); //read the toggle switch
  if (contRead == LOW){
    nextPat = true;
    } else {
    nextPat = false;
    }
}

void checkSD(){

	// Check if SD card is OK

	// On the Ethernet Shield, CS is pin 4. It's set as an output by default.
	// Note that even if it's not used as the CS pin, the hardware SS pin 
	// (10 on most Arduino boards, 53 on the Mega) must be left as an output 
	// or the SD library functions will not work.
  // If possible, it's easiest to just use the SS pin for CS.
  // pinMode(10,OUTPUT); //if you use a different pin for CS, you might need these two lines
  // digitalWrite(10,HIGH); //change the pin number to 53 for a mega

	while(!SD.begin(10)){
		Serial.println("Please insert SD card...\n");
		delay(1000);
	}
	Serial.println("SD card OK...\n");
	delay(1000);
}


void countFiles(File dir, int numTabs) { // Counts files to be used in selecting from the array
  fileCount=0;
  while (true) {
    File entry =  dir.openNextFile();
    if (! entry) {
      // no more files
      break;
    }
    if (entry.isDirectory()) {
      // Do nothing with directories
    } else {
      // it's a file, add to the file count
      fileCount=fileCount+1;
    }
    entry.close();
  }
   fileCount = fileCount-1; // removing one from the count because home.g won't be called
   numFiles = fileCount; // numFiles is used to display the count
   Serial.println("File count is "+numFiles); //this shows up in the serial monitor if hooked up to a computer
}


void homeTable(){
    String homeFile = "home.gc"; 
    myFile = SD.open(homeFile, FILE_READ);    
    Serial.print("-- ");
    Serial.print("File : ");
    Serial.print(homeFile);
    Serial.print(" opened!");
    Serial.println(" --\n");
    }

void randomPats(){

  //first fill array with sequential numbers
  for (int i=0; i<100; ++i){
      pats[i]=i+1;
  }

  //randomize the array
  randomSeed(analogRead(0)); // this seeds the random number generator
   for (int j= 0; j< fileCount; j++) {
   int pos = random(fileCount);
   int t = pats[j];   
   pats[j] = pats[pos];
   pats[pos] = t;
 }
}


void openFileSD(){

	String fileName = "";
  nextPat = false; // reset the next pattern variable
   if (patIdx>fileCount) patIdx=0; //start over when we reach the end
   patNum=pats[patIdx]; // get a number from the scrambled array
   fileName = patNum+fileExt; // construct the file name
     // open the file for printing
     	myFile = SD.open(fileName, FILE_READ);	
     	Serial.print("-- ");
     	Serial.print("File : ");
		Serial.print(fileName);
		Serial.print(" opened!");
		Serial.println(" --\n");
      patIdx=patIdx+1; // set the index for the next pattern
      delay(1000);
    }


//All of the following code is unchanged (except for a little testing piece in sendGcode())

void emptySerialBuf(int serialNum){

	//Empty Serial buffer
	if(serialNum==0){
	 	while(Serial.available())                      
			Serial.read();
	}
	else if(serialNum==1){
		while(Serial1.available())                      
    	Serial1.read();
    }
}

void waitSerial(int serialNum){

	// Wait for data on Serial
	//Argument serialNum for Serial number

 	boolean serialAv = false;

 	if(serialNum==0){
 		while(!serialAv){ 
			if(Serial.available())
     		serialAv=true;
 		}
	}
	else if(serialNum==1){
		while(!serialAv){ 
			if(Serial1.available())
			serialAv=true;
		}
	}
}

String getSerial(int serialNum){

	//Return String  from serial line reading
	//Argument serialNum for Serial number

	String inLine = "";
	waitSerial(serialNum);

	if(serialNum==0){
		while(Serial.available()){              
			inLine += (char)Serial.read();
			delay(2);
		}
		return inLine;
	}
	else if(serialNum==1){
		while(Serial1.available()){               
    		inLine += (char)Serial1.read();
    		delay(2);
 		}
		return inLine;
	}
}

void sendGcode(){
	//READING GCODE FILE AND SEND ON SERIAL PORT TO GRBL
	//START GCODE SENDING PROTOCOL ON SERIAL 1

	String line = "";

    Serial1.print("\r\n\r\n");			//Wake up grbl
    delay(2);
    emptySerialBuf(1);
    if(myFile){                                                                      
	    while(myFile.available()){		//until the file's end
	    	line = readLine(myFile);	//read line in gcode file 
	      	Serial.print(line);		//send to serials
	      	Serial1.print(line);
	      	Serial.print(getSerial(1));	//print grbl return on serial
		}
	}
	else
		fileError();

	myFile.close();
	Serial.println("Finish!!\n");
	delay(2000);
}

/*
void sendGcode(){ 
// Testing Code - comment out the sendGcode stuff above and uncomment this
// to test without having to stream the whole gcode file

  Serial.println("Printing the file.");
  myFile.close();
  Serial.println("Finish!\n");
  nextPat = false;
  delay(2000);
}
// End Testing code
*/

void fileError(){

	// For file open or read error

	Serial.println("\n");
	Serial.println("File Error !");
}

String readLine(File f){
	
	//return line from file reading
  
	char inChar;
	String line = "";

	do{
		inChar =(char)f.read();
    	line += inChar;
    }while(inChar != '\n');

	return line;
}





