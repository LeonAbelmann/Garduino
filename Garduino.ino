/* Garduino Leon Abelmann */
// Version 1.0.4
// Sat Dec  5 12:36:47 2020
/* Program to control garden lights */
/* Version 1: Passive on PIR and Dark for lights along path. Relays
   controlled by Arduino for lights in garden. They can be controlled
   to switch on after dark after a PIR detection for a certain time
   and for a certain period.
   Version 1.0.3 Added Reed relay and extra relay bank, for "party mode".
   Version 1.0.4 Added season correction for duration lights on
*/

// Arduino Uno pinning:
int relayPin1 =  4;  // Control Relay Set 1 (under) #1: 
int relayPin2 =  5;  // Control Relay               #2: 
int relayPin3 =  6;  // Control Relay               #3: 
int relayPin4 =  7;  // Control Relay               #4:
int relayPin5 =  8;  // Control Relay Set 2 (above) #1:
// NOT CONNECTED:
int relayPin6 =  0;  // Control Relay Set 2 (above) #2:
int relayPin7 =  0;  // Control Relay Set 2 (above) #3:
int relayPin8 =  0;  // Control Relay Set 2 (above) #4: 

int darkPin   = 11;  // Input dark sensor. Goes low when dark.
int pirPin    = 12;  // Input PIR motion sensor. Goes high when PIR activated. 
int ledPin    = 13;  // Onboard LED Arduino Uno

/* Used for interupt routine (only pin 2 and 3 on Uno) */
int pushPin   =  2;  /* Input button on printed circuit board. Low
			when pressed. */
int reedPin   =  3;  /* Reed relay, can be activated from outside with
			magnet */
// Timing
/* TimeRelayi[0] : time (minutes) after dark when to switch on (nightmode)*/
/* TimeRelayi[1] : average duration after dark (minutes)
   (nightmode). Varies max +- 150 min or down depending on length of
   night. In spring and autumn, darkness starts at about 19:00 */
/* TimeRelayi[2] : Time to remain on after PIR has switched off
   (pirMode). Should be longer than the PIR remains on itself (1
   min), to avoid pulsing on the relay */
int timeRelay1[]  = {30,420,10};  // Relay nr.1: Single big sphere
int timeRelay2[]  = {45,400,5};   // Relay nr.2: Three small spheres
int timeRelay3[]  = {40,530,6};   // Relay nr.3: Tall light at path
int timeRelay4[]  = {35,415,7};   // Relay nr.4: Tall light at wall

int duskDuration  = 60;      // Time (minutes) of dusk (transition light-dark)
int nightDuration = 20;      /* Time in HOURS long to wait after dark
				before new night starts. Could in
				principle be as high as 23 h */
int testOn        =  1;      /* Time (seconds) that the relays are on
				when button is pressed. */

// Global variables
bool buttonPressed = false; // High when user presses button on board
bool reedPressed   = false; // High when reed relay is activated
bool nightMode     = false; /* Nightmode is when timers are running to
			       keep lights on after lightsensor
			       detects darkness */
bool PIRMode       = false; /* PIRmode is when timers are running to
			       keep lights on after PIR is detected */
bool reedMode      = false; /* ReedMode is on when reed relay is actived */

 int long msPerMin = 60000; /* There are 60000 ms in a minute.  Note
			     that we use this for calculations in
			     involving longs, where at least one
			     variable should be long. So this is
			     making sure we don't forget that by
			     accident */
 /* int long msPerMin = 1000; /* For testing 1 second in a minute. */

unsigned long startOfNightMode; /* Time when nightmode started (ms) */
unsigned long endOfNightMode;   /* Time when nightmode ended (ms) or
				   zero when it has not ended */
unsigned long startOfPIRMode;   /* Time when the PIR was activated */

// For season correction of length of lights on periods.
unsigned long measuredNight = 12*60*msPerMin; /* Length of last darkness
					   period (ms). Defaults to 12
					   hours 1000*3600*12. */
int seasonCorrection = 0;               /* Time add to or remove from
					   timeRelay?[1] */

// Relay states
/* This is somewhat complicated because we don't wan't conflicts between the PIR and nightmode, which leads to pulses on the relays */
#define relay1 0 // Enumerators for the relays. Relay 1 on relayPin1
#define relay2 1 // Relay 2 on relayPin2
#define relay3 2 // etc
#define relay4 3
#define relay5 4
#define relay6 5
#define relay7 6
#define relay8 7
const int relayPins[] = {relayPin1,relayPin2,relayPin3,relayPin4,
			   relayPin5,relayPin6,relayPin7,relayPin8};

/* 2D array of states that the different relays want for the relays (on/off). There are four relays, and three modes. So for instance relayState[1][0]=true indicates that modeNight '[0]' wants relay2 '[1]'to be on.*/
#define modeNight 0    // enumerators for the modes
#define modePIR   1    
#define modeReed  2
bool relaysState[8][3] = {false};

/* Status booleans */
bool PIR  = false; //PIR activated
bool dark = false; //Dark sensor activated
bool reed = false; //Reed relay activated

// Constants
#define sampleTime 99 /* Approximate timestep of main loop,
			  preferably multiple of 3 */
// Structs
struct filters {
  float IntValue;  // Integrated value (0-1)
  float tau;       // Low pass filter time constant (in timestep units)
  float lowThres;  // Value below which we consider input to be off (0-1)
  float highThres; // Value above which we consider input to be on (0-1)
};

/* Filter values. First value (IntValue) should be zero!
   See above for other parameters. The main loop is cycling at about 10 Hz. So tau=1 would be 0.1 s.*/
filters filterPIR  = {0,5,0.25,0.75};
filters filterDark = {0,50,0.25,0.75};
filters filterReed = {0,10,0.25,0.75};


/* ********************************* Routines ***************************** */

// Setup, runs once when program starts
void setup() {
  
  // Define the pins that operate the relays on the '4 Relay Module'
  pinMode(relayPin1, OUTPUT);
  pinMode(relayPin2, OUTPUT);
  pinMode(relayPin3, OUTPUT);
  pinMode(relayPin4, OUTPUT);
  pinMode(relayPin5, OUTPUT);

  // Switch off all relays
  digitalWrite(relayPin1, HIGH); //Pin high, relay off...
  digitalWrite(relayPin2, HIGH);
  digitalWrite(relayPin3, HIGH);
  digitalWrite(relayPin4, HIGH);
  digitalWrite(relayPin5, HIGH);

  switchOnSequence(testOn);//Switch relays on and off one by one
  
  // Input pins, attached to the two optocouplers on the printed circuit board
  pinMode(pirPin,  INPUT_PULLUP);
  pinMode(darkPin, INPUT_PULLUP);
  // Input pins, attached directly to manual switch and reed relay
  pinMode(pushPin, INPUT_PULLUP);
  pinMode(reedPin, INPUT_PULLUP);

  // Output pin for onboard LED
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  
  // We use the pushbutton to interupt the main loop and take action
  /* out of action */
  attachInterrupt(digitalPinToInterrupt(pushPin), buttonISR, FALLING);

  /* For communication with the serial port monitor (make sure your
     serial monitor also has 115200 */
  Serial.begin(115200);
  Serial.println("Garduino Version 1, Leon, 2020");
  Serial.println(measuredNight);
}

/* *******************************************************************/
/* buttonISR() */
/* Interupt Service Routine: runs when button is pressed Keep the code
   here to a minimum */
/* *******************************************************************/
void buttonISR()
 {
   buttonPressed = true;
 }  

/* *******************************************************************/
/* filter(filterID, pinStatus) */
/* Low pass filter the status of sensors, to avoid errors due to
   bouncing or cross-talk. Returns true if low pass value >
   filterID.highThres, false if < filterID.lowThres*/
/* status         : current status of sensor. Do nothing if IntValue is between low and high threshold */
/* filters  ID    : Filter ID (one of filterPIR, filterDark, filterReed) */
/* bool pinStatus : Pin high or low? */
/* The time constant of the filter is tau*deltaT, where deltaT is the timestep. In the main loop we set deltaT to about 100 ms. */ 
/* *******************************************************************/
bool filter(bool status, filters &filterID, bool pinStatus){
  float value = filterID.IntValue;
  float alpha = 1; // alpha should be smaller or equal 1
  if (filterID.tau > 0) {
    alpha = 1/(filterID.tau + 1);
  };
  // Integration, note that if pinstatus is 1(0) always, IntValue=1(0):
  filterID.IntValue = (1.0-alpha)*value + alpha*pinStatus;
  // Clip between 0 and 1
  if (filterID.IntValue >1) {filterID.IntValue=1;};
  if (filterID.IntValue <0) {filterID.IntValue=0;};
  // Use threshold to determine if pin has been (de-)activated
  if (filterID.IntValue > filterID.highThres) {
    return true;
  }
  else {
    if (filterID.IntValue < filterID.lowThres) {
	return false;
      }
    else { // do nothing
      return status;
    }
  }
}


/* *******************************************************************/
/* ledOn(on) */
/* Switch on or off onboard LED */
/* *******************************************************************/
void onBoardLED(bool on){
  digitalWrite(ledPin, on);
}

/* *******************************************************************/
/* switchRelay(mode,relay,on) */
/* mode:  modeNight, modePIR etc. */
/* relay: relay1, relay2 etc */
/* on: true, false */
/* All modes can switch the light on (OR). If all modes agree it
   should be off, than switch off (AND). So
   switchRelay(modePIR,relay1,true) in combination with
   switchRelay(modeNight,relay1,true) will switch relay1 on.  */
/* Todo: maybe we should have more options. Something like override,
   which allows a mode to force lights on or off, whatever the others
   decide. Or a truth table?*/
/* *******************************************************************/
void switchRelay(int mode, int relay, bool on){
  // Set the state of the relay for this mode:
  relaysState[relay][mode]=on;
  
  /* We switch on the relay when it is already on, and switch off when
     it is already off. Perhaps check first.*/
  
  // For debug:
  // Serial.print("Relay ");Serial.print(relay);
  // Serial.print(" Night: ");Serial.print(relaysState[relay][modeNight]);
  // Serial.print(" PIR: ");Serial.print(relaysState[relay][modePIR]);
  
  // If either of the modes want the light on, switch on:
  if (relaysState[relay][modeNight] ||
      relaysState[relay][modePIR]   ||
      relaysState[relay][modeReed]     ) {
    digitalWrite(relayPins[relay], LOW);//pin low is relay on...
    // Serial.println(" -> on");		
  }
  else {
    digitalWrite(relayPins[relay], HIGH);//pin high is relay off...
    // Serial.println(" -> off");
  };
}

/* *******************************************************************/
/* reedMode(on)*/
/* Switch (set of) relays on or off
/* *******************************************************************/
void reedModeAction(bool onoff){
  if (onoff) {
    /* relays are OR-ed, so on is simple: */
    switchRelay(modeReed  ,relay1, true); //Single big sphere
    switchRelay(modeReed  ,relay2, true); //Three small spheres
    switchRelay(modeReed  ,relay4, true); //Tall light at wall
    switchRelay(modeReed  ,relay5, true); //Path lights
    /* relays that should be off, need to be off in all modes : */
    switchRelay(modeNight  ,relay3, false); //Tall light at path
    switchRelay(modePIR    ,relay3, false);
    switchRelay(modeReed   ,relay3, false);
  }
  else {
    switchRelay(modeReed  ,relay1, false); 
    switchRelay(modeReed  ,relay2, false);
    switchRelay(modeReed  ,relay3, false); 
    switchRelay(modeReed  ,relay4, false); 
    switchRelay(modeReed  ,relay5, false); 
  };
}

/* *******************************************************************/
/* checkRelayTime(timePassed,relay,timeRelay[] */
/* Check if relays should be switched on */
/* timePassed :  timer that passed since nightMode started */
/* relay:        relay1,relay2, etc */
/* timeRelay[0]: when should light go on */
/* timeRelay[1]: average duration of light on */
/* correction  : (min) season correction for duration of light on
/* We correct timeRelay[1] depending on the measuredNight duration. */
/* Relay should be on between timeRelay[0] and
   timeRelay[0]+timeRelay[1]+correction */
/* *******************************************************************/
void checkRelayTime(unsigned long timePassed, int relay, int timeRelay[],
		    int correction){
  if ((timePassed >= timeRelay[0]) &&
      (timePassed <  timeRelay[0]+timeRelay[1]+correction)){
    switchRelay(modeNight,relay,true);
  }
  else {
    switchRelay(modeNight,relay,false);
  };
}

/* *******************************************************************/
/* goIntoPIRMode*/
/* Start PIRMode, init PIRStarted timer, switch off all relays */
/* *******************************************************************/
unsigned long goIntoPIRMode(){
  unsigned long PIRStarted = millis();
  switchRelay(modePIR  ,relay1,false);
  switchRelay(modeNight,relay1,false);
  switchRelay(modePIR  ,relay2,false);
  switchRelay(modeNight,relay2,false);
  switchRelay(modePIR  ,relay3,false);
  switchRelay(modeNight,relay3,false);
  switchRelay(modePIR  ,relay4,false);
  switchRelay(modeNight,relay4,false);
  PIRMode = true;
  return PIRStarted;
}

/* *******************************************************************/
/* checkPIRMode(startPIRTime, startNightTime, */
/*              timeRelay1, timeRelay2, timeRelay3, timeRelay4) */
/* Check if relays should be switched off after PIR event, but only
   switch on the light if it should already be on. In this way, if
   deep in the night the light has been switched off, the light will
   remain on a while after a PIR event. */
/* startPIRTime  : when did PIR mode start (ms)? */
/* startNightTime: when did nightMode start (ms) */
/* timeRelay1[0] : when to switch light on after nightTime started (minutes)*/
/* timeRelay1[2] : how long light on after PIR ended (minutes)*/
/* timeRelay2[0] : etc.
/* *******************************************************************/
void checkPIRMode(unsigned long startPIRTime, unsigned long startNightTime,
		    int timeRelay1[], int timeRelay2[],
		    int timeRelay3[], int timeRelay4[]) {
  unsigned long time; // Current time
  unsigned long durationPIR; // How long since startPIRTime
  unsigned long durationNight; // How long since startNightTime
  bool done1 = false; // Timer timeRelay1[2] has ended -> light off
  bool done2 = false; // etc
  bool done3 = false;
  bool done4 = false;
  bool on1   = false; // Timer timeRelay[1] has not ended yet-> no light
  bool on2   = false; // etc
  bool on3   = false;
  bool on4   = false;
  
  time     = millis();
  /* Check which lights are already on in nightMode*/
  durationNight = (time-startNightTime)/msPerMin; //Converted into minutes
  on1 = (durationNight >= timeRelay1[0]);
  on2 = (durationNight >= timeRelay2[0]);
  on3 = (durationNight >= timeRelay3[0]);
  on4 = (durationNight >= timeRelay4[0]);
	
  /* Check PIR timers */
  durationPIR = (time-startPIRTime)/msPerMin; //Converted into minutes
  done1 = (durationPIR >= timeRelay1[2]);
  done2 = (durationPIR >= timeRelay2[2]);
  done3 = (durationPIR >= timeRelay3[2]);
  done4 = (durationPIR >= timeRelay4[2]);

  // If not PIR mode not done yet and light should be on, switch on relays
  switchRelay(modePIR,relay1,(not done1 && on1));
  switchRelay(modePIR,relay2,(not done2 && on2));
  switchRelay(modePIR,relay3,(not done3 && on3));
  switchRelay(modePIR,relay4,(not done4 && on4));

  //PIRMode switches off if last timer has expired
  PIRMode = not (done1 && done2 && done3 && done4);
};

/* *******************************************************************/
/* goIntoNightMode() */
/* endNightTime   : time at which the last night mode has ended (ms)*/
/* duskDuration   : how long does dusk last (min)
/* When the light sensor detects darkness, set start time (ms) and
   return true. To avoid trouble in morning dusk transition, do not
   enter nightmode within duskDuration.*/
/* *******************************************************************/
bool goIntoNightMode(unsigned long endNightTime, unsigned long duskDuration){
  unsigned long currentTime=millis();
  bool night = false;
  if ((currentTime-endNightTime) > (duskDuration/msPerMin)) {
      night = true;
      Serial.println("Nightmode on");
    }
  return night;
};

/* *******************************************************************/
/* correctSeason(measuredNight) */
/* measuredNight : (ms) length of the last darkness period */
/* Correct the time the lights are on for the time of year by adding
   or substracting 150 minutes */
/* *******************************************************************/
int correctSeason(unsigned long measuredNight){
  double duration = measuredNight/(60*msPerMin); // length night in hours
  /* night duration is 12+-5 hours. Max shift is +- 150 minutes*/
  int correction = 150*(duration-12)/5; // Correction in minutes
  if (correction > 150) {
    correction = 150;
    Serial.println("correctSeason: Night is too long!");
  }
  if (correction < -150) {
    correction = -150;
    Serial.println("correctSeason: Night is too short!");
  }
  return correction;
}

/* *******************************************************************/
/* checkNightMode(startTime, dark, timeRelay1, ..., timeRelay4) */
/* 1. Check if it is still night
/* 2. Check if relays should be switched off after PIR event */
/* startTime  : when did night mode start (ms)? */
/* timeRelay1[0] : when to switch on (in minutes) */
/* timeRelay1[1] : how long the light should be on (in minutes) */
/* seasonCorr    : correction for season. Max +- 150 min.
/* Make sure nightmode is on for at least 20 hours. We don't want
   nightmode to start if we are still in the current night. In
   nightmode, we switch on the relays. NO!: If it is not dark, we switch
   them off. */
/* Returns false if night has ended */
/* *******************************************************************/
unsigned long checkNightMode(unsigned long startTime,
			     int timeRelay1[], int timeRelay2[],
			     int timeRelay3[], int timeRelay4[],
			     int seasonCorr) {
  unsigned long currentTime;
  unsigned long timePassed;
  bool night = true; // Will become false if nightmode has ended
  currentTime = millis();
  timePassed = (currentTime-startTime)/msPerMin; //converted to minutes
  // Serial.print("Time passed : ");
  // Serial.print(timePassed);
  // Serial.print(" timeRelay1[0] :");
  // Serial.print(timeRelay1[0]);
  // Serial.print(" timeRelay1[1] :");
  // Serial.print(timeRelay1[1]);
  // Serial.print(" timeRelay2[0] :");
  // Serial.print(timeRelay2[0]);
  // Serial.print(" timeRelay2[1] :");
  // Serial.print(timeRelay2[1]);
  // Serial.print(" nightDuration :");
  // Serial.println(nightDuration*60);

  /* A night is less than about 20 hours (nightDuration). If you
     make nightDuration too short, you may trigger a new nightmode in
     the transition to light. */
  if (timePassed >= nightDuration*60) {
    night = false;
    Serial.println("Nightmode off");
  };
  /* Check relays */
  checkRelayTime(timePassed,relay1, timeRelay1, seasonCorr);
  checkRelayTime(timePassed,relay2, timeRelay2, seasonCorr);
  checkRelayTime(timePassed,relay3, timeRelay3, seasonCorr);
  checkRelayTime(timePassed,relay4, timeRelay4, seasonCorr);
  return night;
}

/* *******************************************************************/
/* switchOnSequence(timeOn) */
/* Switch on relays one by one for timeOn seconds, both on and off */
/* timeOn  : how long should the lights be on/off
/* *******************************************************************/
void switchOnSequence(int timeOn){
  int time=timeOn*1000;//time in ms for delay function
  Serial.print("Testing relays : 1,");
  digitalWrite(relayPin1, LOW); delay(time); //Pin low, relay on...
  Serial.print(" 2,");
  digitalWrite(relayPin2, LOW); delay(time);
  Serial.print(" 3,");
  digitalWrite(relayPin3, LOW); delay(time);
  Serial.print(" 4");
  digitalWrite(relayPin4, LOW); delay(time);
  Serial.print(" 5");
  digitalWrite(relayPin5, LOW); delay(time);
  Serial.println(".");
  digitalWrite(relayPin1, HIGH);delay(time); // Pin high, relay off
  digitalWrite(relayPin2, HIGH);delay(time);
  digitalWrite(relayPin3, HIGH);delay(time);
  digitalWrite(relayPin4, HIGH);delay(time);
  digitalWrite(relayPin5, HIGH);
}

/* *******************************************************************/
/* Main loop, runs continuously */
/* - Read pins for dark/light sensor, PIR sensor */
/* - Check if user has pressed button (interupt)
/* - Act on night mode, PIR Mode, and user button pressed
/* *******************************************************************/
void loop() {
  /* Read pin values */
  bool PIRPinStatus  = digitalRead(pirPin);     //pirPin goes high when PIR
  bool darkPinStatus = not digitalRead(darkPin);//darkPin goes low when dark
  bool reedPinStatus = not digitalRead(reedPin);//reedPin goes low when pushed
  
  /* The PIR, Dark and reed are filtered with hysteresis to suppress bouncing
     or cross-talk. */
  PIR =  filter(PIR,  filterPIR,  PIRPinStatus);
  dark = filter(dark, filterDark, darkPinStatus);
  reed = filter(reed, filterReed, reedPinStatus);

  PIR = PIR && not reed;/* The reed relay also pulls the opto-coupler
			  down, we don't want to detect that as a PIR
			  event */
  
  /* the push button should react immediately, so it is caught by an
     interupt. No filtering for the moment */
  bool pushPinStatus = not digitalRead(pushPin);//pushpin goes low when pushed
  
  delay(trunc(sampleTime/3)); /* Determines the time constant for the
				 filters. Determines onboard led
				 flashing in PIR mode. */
  
  // reedMode: Is reed relay activated, go into party mode
  if (reed) {
    reedMode = true;
    reedModeAction(true);
    delay(sampleTime);
  }
  else { //Normal operation: nightMode and PIRMode (no party)
    reedMode = false;
    reedModeAction(false);
    // Nightmode: If it turns dark, than switch on lights for couple hours
    if (not nightMode) { // If we are not in nightmode, check if it is dark
      if (dark) {
	/* If it is dark and we are not in the morning dusk, switch on
	   and reset the timers. startOfNightMode is zero when nightmode
	   has not started */
	nightMode        = goIntoNightMode(endOfNightMode,duskDuration);
	startOfNightMode = millis();
      }
    }
    else { /* If we are in nightmode and PIR is not on, check the
	      timers and switch the lights that should be on. Switch
	      off nightMode after nightDuration hours. Record the time
	      when darkness ended.*/
      if (not PIR) {
	nightMode = checkNightMode(startOfNightMode,
				   timeRelay1,timeRelay2,
				   timeRelay3,timeRelay4,seasonCorrection);
	if (not nightMode) {
	  endOfNightMode = millis();
	  seasonCorrection = correctSeason(measuredNight);
	}
	/* measure the length of the night. If nightmode and dark,
	   counter is increased. If not dark, measuredNight will have
	   last value. Failsafe: If dark and not in nightmode anymore,
	   counter will be nightDuration. */
	if (dark) {
	  measuredNight = millis() - startOfNightMode;
	}
      }
      onBoardLED(true); // LED on in nightmode
    };

    delay(trunc(sampleTime/3));/*For LED flashing. Sum of all delays in
				 the main loop should be sampleTime, for
				 correct filtering */
    
    /* PirMode: If it is dark, switch off the relays when the PIR is
       active and reset the timer. Keep doing that while PIR is
       high. We do this to prevent that we use too much power. */
    if (PIR && dark) {
      startOfPIRMode = goIntoPIRMode();
    };
    
    /* If in PIR Mode, check if PIR is still high. If not, than switch
       on the relays and keep them on for timeRelayi[2] time if
       timeRelayi[1] has passed. If last relay has switched of, set
       PIRMode off*/
    if (PIRMode) {
      if (not PIR){
	checkPIRMode(startOfPIRMode,startOfNightMode,
		     timeRelay1,timeRelay2,timeRelay3,timeRelay4);
      };
      onBoardLED(false); /* Onboard LED off. It is switched on again
			    above, so it flashes */
    };
    delay(trunc(sampleTime/3));/* Sum of all delays should be sampleTime */
  }

  // For debug: Did user press button?
  if (buttonPressed) {
    Serial.println("Button pressed, testing lights");
    /* maybe this causes a problem, removed for the moment
       switchOnSequence(testOn); //switch on relays one by one for
       testOn seconds */
    buttonPressed = false; // Reset, and wait for next time
    }
  
  // For debug, output to serial port:
  Serial.print("PIR: ");Serial.print(PIRPinStatus);
  Serial.print(" (Int ");Serial.print(filterPIR.IntValue);
  Serial.print(" ");Serial.print(PIR);
  Serial.print("), ");
  Serial.print("Dark: ");Serial.print(darkPinStatus);
  Serial.print(" (Int ");Serial.print(filterDark.IntValue);
  Serial.print(" ");Serial.print(dark);
  Serial.print("), ");
  Serial.print("Reed: ");Serial.print(reedPinStatus);
  Serial.print(" (Int ");Serial.print(filterReed.IntValue);
  Serial.print(" ");Serial.print(reed);
  Serial.print("), ");
  Serial.print("Push: ");Serial.print(pushPinStatus);
  Serial.print(" Relay1 (NP): ");
  if (relaysState[0][0]) {Serial.print("x");} else {Serial.print("o");};
  if (relaysState[0][1]) {Serial.print("x");} else {Serial.print("o");};
  if (relaysState[0][2]) {Serial.print("x");} else {Serial.print("o");};
  Serial.print(" Relay2: ");
  if (relaysState[1][0]) {Serial.print("x");} else {Serial.print("o");};
  if (relaysState[1][1]) {Serial.print("x");} else {Serial.print("o");};
  if (relaysState[1][2]) {Serial.print("x");} else {Serial.print("o");};
  Serial.print(" Relay3: ");
  if (relaysState[2][0]) {Serial.print("x");} else {Serial.print("o");};
  if (relaysState[2][1]) {Serial.print("x");} else {Serial.print("o");};
  if (relaysState[2][2]) {Serial.print("x");} else {Serial.print("o");};
  Serial.print(" Relay4: ");
  if (relaysState[3][0]) {Serial.print("x");} else {Serial.print("o");};
  if (relaysState[3][1]) {Serial.print("x");} else {Serial.print("o");};
  if (relaysState[3][2]) {Serial.print("x");} else {Serial.print("o");};
  Serial.print(" Relay5: ");
  if (relaysState[4][0]) {Serial.print("x");} else {Serial.print("o");};
  if (relaysState[4][1]) {Serial.print("x");} else {Serial.print("o");};
  if (relaysState[4][2]) {Serial.print("x");} else {Serial.print("o");};
  Serial.print(" Dark: "); Serial.print(float(measuredNight/msPerMin));
  if (nightMode) Serial.print(" NightMode");
  if (PIRMode) Serial.print(" PIRMode");
  if (reedMode) Serial.print(" ReedMode");
  Serial.println();
}
