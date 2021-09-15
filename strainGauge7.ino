/*
 * https://circuits4you.com
 * 2016 November 25
 * Load Cell HX711 Module Interface with Arduino to measure weight in Kgs
 Arduino 
 pin 
 8 -> HX711 CLK
 9 -> DOUT
 5V -> VCC
 GND -> GND
 
 Most any pin on the Arduino Uno will be compatible with DOUT/CLK.
 The HX711 board can be powered from 2.7V to 5V so the Arduino 5V power should be fine.
*/
 
#include "HX711.h"  //You must have this library in your arduino library folder

#include <SoftwareSerial.h>
#include <time.h>
 
#define DOUT  9
#define CLK  8
#define PWM_PIN 13
 
HX711 scale(DOUT, CLK);
SoftwareSerial btSerial(5, 4); // RX, TX

float calibration_factor = -9674;

//=============================================================================================
//                         Global Variables
//=============================================================================================
long scaleReading = 0;          // reading from scale (100 * calibration units)
bool usePullIntervals = false;  // start new interval when scaleReading goes above or below threshold 
bool startSounds = false;       // play start/stop and warning sounds

char * displayTime(unsigned long seconds); // converts ms to hh:mm:ss.## 
void reset();                              // reset timer
void setStartSounds(bool flag);
void newInterval();
//void output();

//=============================================================================================
//                         TIMER
//=============================================================================================
class Timer {
public:
    Timer() {}
    
    void startStop() {
        if (_paused) { // Resume Timer
            //_startTime += _now - _pauseTime;
            moveStartTime(_now - _pauseTime);
        } 
        else { // Pause Timer
            _pauseTime = _now;
        }
        _paused = !_paused;
    }
    
    void setNow() {_now = millis();}
    void update() {_timeSinceStart = _now - _startTime;}
    void moveStartTime (unsigned long millis) {_startTime += millis;}  
    
    unsigned long getTimeSinceStart() {return _timeSinceStart;}
    unsigned long getStartTime() {return _startTime;}
    bool isPaused() {return _paused;}
    
private: 
    unsigned long _now = 0;            // reference time since start up (ms)
    unsigned long _timeSinceStart = 0; // difference between now and startTime
    unsigned long _startTime = 0;      // reference time in ms for when t=0
    unsigned long _pauseTime = 0;      // reference time for when timer was last paused
    bool _paused = true;               // if paused, time does not increase
};
Timer timer;

//=============================================================================================
//                         COUNTDOWN
//=============================================================================================
class Countdown {
public:
    
    Countdown() {}
    
    char * displayProgress() {
        int length = 0;
        static char ret [32];
        
        char workRestChar;
        length += sprintf(ret + length, "o,%s", displayTime(_timeToEnd()));
        length += sprintf(ret + length, ",%d/%d", _repsCompleted, _reps);
        length += sprintf(ret + length, ",%d/%d", _setsCompleted, _sets);
        
        if (_workRest) {
            workRestChar = 'w';
        } else {
            workRestChar = 'r';
        }
        length += sprintf(ret + length, ",%c", workRestChar);
        return ret;
    }
    
    void setPrepareLength(unsigned long num) {
        if (timer.getTimeSinceStart() == 0) {
            _prepareLength = num;
            _countdownEnd = _prepareLength;
        }
    }
    
    void skip() {
        if (_countdownEnd > timer.getTimeSinceStart()) {
            _countdownEnd = timer.getTimeSinceStart();
        }
        if (_repsCompleted > 0) {
            _repsCompleted = _reps;
        }
    }
    
    void reset() {
        _countdownEnd = _prepareLength;
        _repsCompleted = 0;
        _setsCompleted = 0;
        _workRest = false;
    }
    
    void setRepLength(unsigned long num) {_repLength = num;}
    void setRestLength(unsigned long num) {_restLength = num;}
    void setSetRest(unsigned long num) {_setRest = num;}
    
    void setReps(unsigned int num) {_reps = num;}
    void setSets(unsigned int num) {_sets = num;}
    
    void setUseCountdown(bool flag) {_useCountdown = flag;}
    bool getUseCountdown() {return _useCountdown;}
    
    bool getWorkRest() {return _workRest;}
    
private:
    unsigned long _prepareLength = (unsigned long) 5*1000;
    unsigned long _countdownEnd = _prepareLength;
    
    unsigned long _repLength = (unsigned long) 7*1000;
    unsigned long _restLength = (unsigned long) 3*1000;
    unsigned long _setRest = (unsigned long) 2*60*1000;
    
    unsigned int _reps = 6;
    unsigned int _sets = 6;
    
    unsigned int _repsCompleted = 0;
    unsigned int _setsCompleted = 0;
    
    bool _useCountdown = false;
    bool _workRest = false; // true during rep, false during rest
    
    unsigned long _timeToEnd() { // if needed update _repsCompleted, _setsCompleted, and _countdownEnd
        unsigned long ret;       // returns time left before updated _countdownEnd is reached
        
        if (_setsCompleted < _sets) {
            if (timer.getTimeSinceStart() > _countdownEnd) {  // countdown interval has ended
                if (_repsCompleted < _reps) {             // rest between reps
                    if (_workRest) {
                        _countdownEnd += _restLength;
                        if (startSounds) {
                            Serial.println("e,r1");
                            btSerial.println("e,r1");
                        }
                    } else {                            // begin new rep
                        _countdownEnd += _repLength;
                        ++_repsCompleted;
                        if (startSounds) {
                            Serial.println("e,w1");
                            btSerial.println("e,w1");
                        }
                    }
                }
                else {                                  // rest betweeb sets
                    ++_setsCompleted;
                    _repsCompleted = 0;
                    if (startSounds) {
                        Serial.println("e,r2");
                        btSerial.println("e,r2");
                    }
                    if (_setsCompleted < _sets) {
                        _countdownEnd += _restLength + _setRest;
                    }
                }
                _workRest = !_workRest;
                if (!usePullIntervals) {
                    newInterval();
                }
            }
        } 
        
        if (timer.getTimeSinceStart() > _countdownEnd) {
            ret = timer.getTimeSinceStart() - _countdownEnd; // count up after workout 
        } else {
            ret = _countdownEnd - timer.getTimeSinceStart(); // count down during workout
        }
        return ret;
    }
};
Countdown countdown;

//=============================================================================================
//                         INTERVAL DATA
//=============================================================================================
class IntervalDat {
public:
    
    IntervalDat() {}
    
    void update() {
        _duration = timer.getTimeSinceStart() - _durationStart;
        _workRest = (scaleReading >= _threshold);
        if (usePullIntervals && _workRest != _prevWorkRest) {
            _workRest = !_workRest; // temporarily change type for interval summary
            newInterval();
            _workRest = !_workRest;
        } else if (!countdown.getUseCountdown()) {
            if (_workRest) {
                if (startSounds && _repWarning1 > 0 && _duration > _repWarning1 && !_repWarning1Sent) {
                    Serial.println("e,w1");
                    btSerial.println("e,w1");
                    _repWarning1Sent = true;
                } else if (startSounds && _repWarning2 > 0 && _duration > _repWarning2 && !_repWarning2Sent) {
                    Serial.println("e,w2");
                    btSerial.println("e,w2");
                    _repWarning2Sent = true;
                }
            } else {
                if (startSounds && _restWarning1 > 0 && _duration > _restWarning1 && !_restWarning1Sent) {
                    Serial.println("e,r1");
                    btSerial.println("e,r1");
                    _restWarning1Sent = true;
                } else if (startSounds && _restWarning2 > 0 && _duration > _restWarning2 && !_restWarning2Sent) {
                    Serial.println("e,r2");
                    btSerial.println("e,r2");
                    _restWarning2Sent = true;
                }
            }
        }
    }
    
    char * displayProgress() {
        int length = 0;
        static char ret [100];
        char workRestChar;
        
        // Duration
        if (timer.getTimeSinceStart() == 0) {
            _durationStart = 0;
            _duration = 0;
        }
        // Interval Start Time
        length += sprintf(ret+length, "%s", displayTime(_durationStart));
        // Duration
        length += sprintf(ret+length, ",%lu.%02lu", _duration/1000, _duration%1000/10);
        // Peak
        if (scaleReading > _peak && !timer.isPaused()) _peak = scaleReading;
        length += sprintf(ret+length, ",%lu.%02lu", _peak/100, _peak%100);
        // Impulse
        _impulse += (unsigned long) (scaleReading + _prevScaleReading)*(timer.getTimeSinceStart() - _prevTimeSinceStart)/2;
        length += sprintf(ret+length, ",%lu.%02lu", _impulse/1000/100, (_impulse/1000)%100);
        // Average
        if (timer.getTimeSinceStart() > _durationStart) {
            _avg = _impulse/_duration;
        }
        length += sprintf(ret+length, ",%lu.%02lu", _avg/100, _avg%100);
        // Work Rest
        if (countdown.getUseCountdown() & !usePullIntervals) {
            if (countdown.getWorkRest()) { // countdown work rest changed before this statement is called
                workRestChar = 'r';
            } 
            else {
                workRestChar = 'w';
            }
        }
        else if (_workRest && !timer.isPaused()) {
            workRestChar = 'w';
        }
        else {
            workRestChar = 'r';
        }
        length += sprintf(ret+length, ",%c", workRestChar);
        
        _prevScaleReading = scaleReading;
        _prevTimeSinceStart = timer.getTimeSinceStart();
        _prevWorkRest = _workRest;
        return ret;
    }
    
    void reset() {
        if (_durationStart > 0 && _durationStart < timer.getTimeSinceStart()) { // do not display very first interval
            char output [100];
            sprintf(output, "%s%s", "i,", displayProgress());
            Serial.println(output);
            btSerial.println(output);
         }
         
         if (startSounds && !countdown.getUseCountdown()) {
                if (_workRest) {
                    Serial.println("e,r1");
                    btSerial.println("e,r1");
                } else {
                    Serial.println("e,w1");
                    btSerial.println("e,w1");
                }
         }
        
        _durationStart = timer.getTimeSinceStart();
        _repWarning1Sent = false;
        _repWarning2Sent = false;
        _restWarning1Sent = false;
        _restWarning2Sent = false;
        _impulse = 0;
        _peak = 0;
    }
    
    unsigned long getThreshold() {return _threshold;}
    
    void setThreshold(unsigned long num) {_threshold = num;}
    
    void setWorkRest(bool val) {
        _workRest = val; 
        _prevWorkRest = val;
    }
    
    void setRepWarning1(unsigned long num) {_repWarning1 = num;}
    void setRepWarning2(unsigned long num) {_repWarning2 = num;}
    void setRestWarning1(unsigned long num) {_restWarning1 = num;}
    void setRestWarning2(unsigned long num) {_restWarning2 = num;}
    
private:
    unsigned long _threshold = 1*100;
    
    unsigned long _durationStart = 0;
    unsigned long _duration = 0;
    unsigned long _prevScaleReading = 0;
    unsigned long _prevTimeSinceStart = 0;
    long _peak = 0;
    unsigned long _impulse = 0;
    unsigned long _avg = 0;
    
    unsigned long _repWarning1  = 5*1000;
    unsigned long _repWarning2  = 10*1000;
    unsigned long _restWarning1 = 60*1000;
    unsigned long _restWarning2 = 120*1000;
    
    bool _repWarning1Sent  = false;
    bool _repWarning2Sent  = false;
    bool _restWarning1Sent = false;
    bool _restWarning2Sent = false;
    
    bool _workRest = false;
    bool _prevWorkRest = _workRest;
};
IntervalDat intervalDat;

//=============================================================================================
//                         READ SERIAL
//=============================================================================================
class ReadSerial {
public:
    void readBuffer (String command) { 
        _readCommand(command);
        Serial.println(command);
    }
    
private:
    
    void _readCommand (String command) {
        if (command.length() > 0) {
            if (command[0] == 's') {                                     // --- s for start/stop timer
                timer.startStop();
            } else if (command[0] == 't' || command[0] == 'a' || command[0] == 'c') {    // --- tare or calibrate
                if (command[0] == 't') {                                  // --- t for tare ---
                    scale.tare();  //Reset the scale to zero
                }
                else if (command[0] == 'a' && command.length() >= 2) {    // --- a# for set calibration factor to # ---
                    calibration_factor = atoi(command.c_str()+1);
                }
                else if (command[0] == 'c' && command.length() >= 2) {    // --- c# for calibrate scale weight to # ---
                    double calibration_weight = atof(command.c_str()+1);
                    calibration_factor = -scale.get_value(3) / calibration_weight;
                    btSerial.print("a,");
                    btSerial.println(calibration_factor);
                }
                scaleReading = 0.0;
                scale.set_scale(calibration_factor); //Adjust to this calibration factor
            } else if (command[0] == 'n' && !timer.isPaused()) {         // --- n for new interval
                intervalDat.reset();
            } else if (command[0] == 'f' && command.length() == 2) {      // --- fy/fn for yes/no interval start sounds (not including countdown)
                if (command[1] == 'y') {
                    setStartSounds(true); 
//                    Serial.println("--- SOUND ON ---");
                } else if (command[1] == 'n') {
                    setStartSounds(false);
//                    Serial.println("--- SOUND OFF ---");
                }
            } else if (command[0] == 'o' && command.length() == 2 && timer.isPaused()) {      // --- oy/on for yes/no use countdown
                if (command[1] == 'y') {
                    countdown.setUseCountdown(true);
//                    Serial.println("--- COUNTDOWN ON ---");
                } else if (command[1] == 'n') {
                    countdown.setUseCountdown(false);
//                    Serial.println("--- COUNTDOWN Off ---");
                }
            reset();
            } else if (command[0] == 'r') {                              // --- r,#,#,#,#,#,# for set repeater protocol to
                int prevSepIndex = 0;                                   // (reps,sets,prepareLength,repLength,restLength,setRestLength)
                int sepIndex = command.indexOf(',');
                int num = 0;
                int i = 0;
                while (prevSepIndex < sepIndex) {
                    prevSepIndex = sepIndex+1;
                    sepIndex = command.indexOf(',', prevSepIndex);
                    num = command.substring(prevSepIndex, sepIndex).toFloat();
                    if (i == 0)      countdown.setReps((unsigned int) num);
                    else if (i == 1) countdown.setSets((unsigned int) num);
                    else if (i == 2) countdown.setPrepareLength((unsigned long) 1000*num);
                    else if (i == 3) countdown.setRepLength((unsigned long) 1000*num); 
                    else if (i == 4) countdown.setRestLength((unsigned long) 1000*num);
                    else if (i == 5) countdown.setSetRest((unsigned long) 1000*num);
                    i++;
                }
            } else if (command[0] == 'm') {                              // --- m,#,#,#,# for set sound warning times 
                int prevSepIndex = 0;                                   // (repWarning1,restWarning1,repWarning2,restWarning2)
                int sepIndex = command.indexOf(',');
                int i = 0;
                while (prevSepIndex < sepIndex) {
                    prevSepIndex = sepIndex+1;
                    sepIndex = command.indexOf(',', prevSepIndex);
                    int num = command.substring(prevSepIndex, sepIndex).toInt();
                    if (i == 0)      intervalDat.setRepWarning1((unsigned long) 1000*num);
                    else if (i == 1) intervalDat.setRestWarning1((unsigned long) 1000*num);
                    else if (i == 2) intervalDat.setRepWarning2((unsigned long) 1000*num);
                    else if (i == 3) intervalDat.setRestWarning2((unsigned long) 1000*num);
                    i++;
                }
            } else if (command[0] == 'p' && command.length() == 2) {      // --- py/pn for yes/no usePullInterval
                if (command[1] == 'y') {
                    usePullIntervals = true; 
//                    Serial.println("--- PULL INTERVAL ON ---");
                } else if (command[1] == 'n') {
                    usePullIntervals = false;
//                    Serial.println("--- PULL INTERVAL OFF ---");
                }
            } else if (command[0] == 'l' && command.length() >= 2) {      // --- l# for set threshold limit for usePullInterval to #
                intervalDat.setThreshold((unsigned long) 100*atof(command.c_str()+1));
            } else if (command[0] == 'b') {                              // --- b for break/skip countdown section
                if (!timer.isPaused()) countdown.skip();
            } else if (command[0] == 'x' && timer.isPaused()) {          // --- x for reset timer
                reset();
            }
        }
    }
};
ReadSerial readSerial;
//=============================================================================================
//                         Global Methods
//=============================================================================================
char * displayTime(unsigned long millis) {
    static char ret [32];
    
    if (millis < 60000) { // seconds.##
      sprintf(ret, "%lu.%02lu", millis/1000, millis%1000/10);
    } else if (millis < 3600000) { // minutes:seconds.##
      sprintf(ret, "%lu:%02lu.%02lu", millis/60000, millis%60000/1000, millis%1000/10);
    } else { // hours:minutes:seconds.##
      sprintf(ret, "%lu:%02lu:%02lu.%02lu", millis/3600000, millis/60000%60, millis/1000%60, millis%1000/10);
    } 
    return ret;
}

void reset() {
    newInterval(); // includes sending message with final interval data
    
    timer = Timer();
    countdown.reset();
    intervalDat.setWorkRest(false);
}

void setStartSounds(bool flag) {startSounds = flag;}
void newInterval() {intervalDat.reset();}

//=============================================================================================
//                         SETUP
//=============================================================================================
void setup() {
    Serial.begin(9600); 
//    Serial.setTimeout(0);
    btSerial.begin(38400);
    btSerial.setTimeout(0);
    pinMode(PWM_PIN, OUTPUT);
  
    Serial.println("--- Straing Gauge Starting ---");
    
    for (int i = 0; i < 3; i++) {
        delay(250);
        analogWrite(PWM_PIN, 255);
        delay(250);
        analogWrite(PWM_PIN, 0);
    }
  
    scale.set_scale();
    scale.tare(); // Reset the scale to 0
    scale.set_scale(calibration_factor); //Adjust to this calibration factor
}

//=============================================================================================
//                         LOOP
//=============================================================================================
void loop() {
    //--------------------- Take Readings -----------------------------------------------------
    scaleReading = -100*scale.get_units(1);
    if(scaleReading < 0) {
        scaleReading = 0; //Do not allow negative readings
    } 
    timer.setNow();
    if (timer.isPaused()) {
        if (usePullIntervals && scaleReading > intervalDat.getThreshold()) {
            timer.startStop();
        }
    } else {
        timer.update();
        intervalDat.update();
    }
    //--------------------- Write Serial ------------------------------------------------------
    int length = 0;
    char output [100];
    
    if (countdown.getUseCountdown()) {
        length += sprintf(output+length, "%s", countdown.displayProgress());
        
        Serial.println(output);
        btSerial.println(output);
        length = 0;
        memset(output, 0, length);
    }
    
    // Current Time
    length += sprintf(output+length, "d");
    length += sprintf(output+length, ",%lu.%02lu", timer.getTimeSinceStart()/1000, timer.getTimeSinceStart()%1000/10);
    length += sprintf(output+length, ",%s", displayTime(timer.getTimeSinceStart()));
    // Reading
    length += sprintf(output+length, ",%lu.%02lu", scaleReading/100, scaleReading%100);
    // Interval Data
    length += sprintf(output+length, ",%s", intervalDat.displayProgress());
    
    Serial.println(output);
    btSerial.println(output);
    //--------------------- Read Serial -------------------------------------------------------
//     if(Serial.available()) {
//    String buffer = Serial.readStringUntil('\n');
  
    while(btSerial.available()) {
        String command = btSerial.readStringUntil('\n');
        readSerial.readBuffer(command);
    }
}
