// General settings
#define BUTTON_COUNT        3
#define DISPLAY_COUNT       4
#define DISPLAY_DELAY       5   // milliseconds ; delay before switching to next display
#define DISPLAY_BLINK       500 // milliseconds ; blink time when setting time or alarm
#define SEPARATOR_BLINK     500 // milliseconds ; time when the ':' turns on/off
#define ALARM_SET_BTN_DELAY 500 // milliseconds ; time after which user will be able to set an alarm
#define ALARM_BUZZ_TIME     350 // milliseconds ; buzzing time

// Index of button pin stored in buttons[] array
#define BTN_MODE 0
#define BTN_INCR 1
#define BTN_DECR 2

// Program states
#define TIME_DISPLAY 0
#define TIME_SET_HR  1
#define TIME_SET_M   2
#define ALARM_CHOOSE 3
#define ALARM_SET_HR 4
#define ALARM_SET_M  5

// 7-segment number mapping
const int8_t numbers[] = {
    0b00111111, // 0
    0b00000110, // 1
    0b01011011, // 2
    0b01001111, // 3
    0b01100110, // 4
    0b01101101, // 5
    0b01111101, // 6
    0b00000111, // 7
    0b01111111, // 8
    0b01101111, // 9

    0b01110111, // A
    0b00111000, // L
    0b01000000  // -
};
const int8_t segments[7] = {9, 10, 11, 12, 2, 3, 4};     // 7-segment pins (a, b, c, d, e, f, g)
const int8_t displays[DISPLAY_COUNT] = {6, 5, A1, 8};    // Common pins
const int8_t separator = 13;                             // Pin for the ':' between displays
const int8_t buttons[BUTTON_COUNT] = {A2, A3, A4};       // Button pins
const int8_t buzzer = 7;                                 // Buzzer pin
uint8_t alarms[99][2] = {}; // hours, minutes
uint8_t alarm_setting_index = 0;
bool    buzzing = false;
uint8_t program_state = 0;
uint8_t time[2] = {4, 20};  // hours, minutes
uint8_t buffer_time[2] = {0, 0};  // hours, minutes
uint8_t active_alarm_index = ~0;
bool    active_alarm_disabled = false;

char displayed_text[DISPLAY_COUNT] = {'0','0','0','0'};

uint64_t display_last_time = millis();
uint64_t buffer_time_last_update = ~0;
uint8_t current_display = 0;

// Helper function to turn on/off each segment
void set_number(int8_t number){
    digitalWrite(segments[0], (number>>0)&1);
    digitalWrite(segments[1], (number>>1)&1);
    digitalWrite(segments[2], (number>>2)&1);
    digitalWrite(segments[3], (number>>3)&1);
    digitalWrite(segments[4], (number>>4)&1);
    digitalWrite(segments[5], (number>>5)&1);
    digitalWrite(segments[6], (number>>6)&1);
}

// Helper function to parse array 'time' into 'displayed_text' array
void time_to_display(){
    displayed_text[0] = '0' + time[0]/10;
    displayed_text[1] = '0' + time[0]%10;
    displayed_text[2] = '0' + time[1]/10;
    displayed_text[3] = '0' + time[1]%10;
}

// Parses 'displayed_text' array onto the 7-segment displays
void update_display(){
    /* Since segment pins are shared between all displays, we can have only one display turned on at the same time.
     * To make it seem like all displays are simultaneously independent, we have to quickly switch between the displays.
     */

    uint64_t current_time = millis();

    // Update only if enough time has passed
    if(current_time - display_last_time > DISPLAY_DELAY){
        // Turn off currently turned on display
        digitalWrite(displays[current_display], HIGH);

        // Switch to next display
        current_display = (current_display+1)%DISPLAY_COUNT;

        // Blinking handler for settings
        switch(program_state){
            case TIME_SET_HR: case ALARM_SET_HR:{ // Leave first half of the digits turned off half the time when setting the hours
                if(current_display < (DISPLAY_COUNT/2) && (current_time%(DISPLAY_BLINK*2)) < DISPLAY_BLINK){
                    display_last_time = current_time;
                    return;
                }
                break;
            }
            case TIME_SET_M: case ALARM_SET_M:{ // Leave second half of the digits turned off half the time when setting the minutes
                if(current_display >= (DISPLAY_COUNT/2) && (current_time%(DISPLAY_BLINK*2)) < DISPLAY_BLINK){
                    display_last_time = current_time;
                    return;
                }
                break;
            }
        }

        // Set up the 7-segment display to show what is in 'displayed_text' array
        if(displayed_text[current_display] >= '0' && displayed_text[current_display] <= '9')
            set_number(numbers[displayed_text[current_display]-'0']);
        else if(displayed_text[current_display] == 'A') set_number(numbers[10]);
        else if(displayed_text[current_display] == 'L') set_number(numbers[11]);
        else                                            set_number(numbers[12]);

        // Turn on the display
        digitalWrite(displays[current_display], LOW);

        // Save current time as the last time displays were updated
        display_last_time = millis();
    }
}

// Catches button presses and manages program state 
void button_manager(){
    #define BOOLIFY(var, bit) ((var>>bit)&1)                                     // Pick one bit from a variable
    #define RISING_EDGE(state_var, bit, state)  ((!((state_var>>bit)&1))&&state) // Detect first instance of change from 0 to 1 
    #define FALLING_EDGE(state_var, bit, state) (((state_var>>bit)&1)&&(!state)) // Detect first instance of change from 1 to 0
    static uint8_t pressed = 0;
    static uint64_t mode_button_press_time = 0;
    static bool mode_ignore = false; // This is to protect program from continuous mode button holding
    uint64_t current_time = millis();

    // Read latest state of each button
    /* This function was designed with having a rising edge as pressing the button, but
     * hardware realization has falling edge as pressing the button. The easiest solution
     * was to reverse button inputs, so the program can deal with it properly.
     */
    bool mode_state = (digitalRead(buttons[BTN_MODE]))?(false):(true);
    bool incr_state = (digitalRead(buttons[BTN_INCR]))?(false):(true);
    bool decr_state = (digitalRead(buttons[BTN_DECR]))?(false):(true);

    // Ignore mode button press if it is being held continuously
    if(mode_ignore){
        if(mode_state) mode_state = false;
        else           mode_ignore = false;
    }

    // Button press handlers
    if(RISING_EDGE(pressed, BTN_INCR, incr_state)){
        pressed |= 1 << BTN_INCR; // Remember that the button was pressed
    }
    if(RISING_EDGE(pressed, BTN_DECR, decr_state)){
        pressed |= 1 << BTN_DECR; // Remember that the button was pressed
    }
    if(RISING_EDGE(pressed, BTN_MODE, mode_state)) {
        pressed |= 1 << BTN_MODE; // Remember that the button was pressed
        mode_button_press_time = current_time; // Remember when the button was pressed
    }

    // Force mode button falling edge after enough time
    if(program_state == TIME_DISPLAY &&
       BOOLIFY(pressed, BTN_MODE)    &&
       ((current_time-mode_button_press_time) > ALARM_SET_BTN_DELAY))
    {
        mode_ignore = true;
        mode_state = false;
    }

    // Button release handlers
    if(FALLING_EDGE(pressed, BTN_MODE, mode_state)){
        pressed &= ~(1 << BTN_MODE); // Remember that the button was released
        switch(program_state){
            case TIME_DISPLAY:{
                if(buzzing){ // Turn off alarm
                    buzzing = false;
                    active_alarm_index = ~0;
                    active_alarm_disabled = true;
                    break;
                }

                // Choose what settings will be modified
                if(current_time - mode_button_press_time > ALARM_SET_BTN_DELAY){
                    // Display alarm choice
                    displayed_text[0] = 'A';
                    displayed_text[1] = 'L';
                    displayed_text[2] = '0' + ((alarm_setting_index+1)/10);
                    displayed_text[3] = '0' + ((alarm_setting_index+1)%10);

                    // Move onto choosing an alarm to modify
                    program_state = ALARM_CHOOSE;
                }
                // Move onto modifying current time hours
                else program_state = TIME_SET_HR;
                break;
            }
            case ALARM_CHOOSE:{
                // Load chosen alarm and display it
                if(alarms[alarm_setting_index][0]&0x80){
                    time[0] = alarms[alarm_setting_index][0]&0x7f;
                    time[1] = alarms[alarm_setting_index][1];
                    time_to_display();
                }
                else{
                    time[0] = 24;
                    time[1] = 0;
                    memset(displayed_text, '-', DISPLAY_COUNT);
                }

                // Move onto modyfying chosen alarm hours
                program_state = ALARM_SET_HR;
                break;
            }
            case TIME_SET_HR:{
                // Move onto modifying current time minutes
                program_state = TIME_SET_M;
                break;
            }
            case ALARM_SET_HR:{
                if(displayed_text[0] == '-'){
                    // If alarm is not set, clear the 'alarm set' bit
                    alarms[alarm_setting_index][0] = 0;

                    // Get current time and display it
                    get_current_time();

                    // Check if alarm needs to be sounded
                    alarm();

                    // Move onto displaying current time
                    program_state = TIME_DISPLAY;
                }
                // Move onto modifying chosen alarm hours
                else program_state = ALARM_SET_M;
                break;
            }
            case TIME_SET_M:{
                // Set new current time
                set_current_time();

                // Check if alarm needs to be raied
                alarm();

                // Move onto displaying current time
                program_state = TIME_DISPLAY;
                break;
            }
            case ALARM_SET_M:{
                // Save current alarm settings
                alarms[alarm_setting_index][0] = time[0]|0x80; // 0x80 (7th bit) is a 'alarm set' bit indicating that the alarm was set
                alarms[alarm_setting_index][1] = time[1];

                // Get current time and display it
                get_current_time();

                // Check if an alarm needs to be raised
                alarm();

                // Move onto displaying current time
                program_state = TIME_DISPLAY;
                break;
            }
            default:{
                // Get current time and display it
                get_current_time();

                // Check if an alarm needs to be raised
                alarm();

                // Move onto displaying current time
                program_state = TIME_DISPLAY;
                break;
            }
        }
        // Forget when the button was pressed
        mode_button_press_time = 0;
    }
    if(FALLING_EDGE(pressed, BTN_INCR, incr_state)){
        pressed &= ~(1<<BTN_INCR); // Remember that the button was released
        switch(program_state){
            case TIME_SET_HR:{
                // Increase currently displayed hours by 1
                time[0] = (time[0]+1)%24;

                // Display this change
                time_to_display();
                break;
            }
            case ALARM_SET_HR:{
                // Increase currently displayed hours by 1
                time[0] = (time[0]+1)%25;

                // Hour 24 is a special state when the alarm is considered unset
                if(time[0] == 24) memset(displayed_text, '-', DISPLAY_COUNT);
                else time_to_display();
                break;
            }
            case TIME_SET_M:case ALARM_SET_M:{
                // Increase currently displayed minutes by 1
                time[1] = (time[1]+1)%60;

                // Display this change
                time_to_display();
                break;
            }
            case ALARM_CHOOSE:{
                // Increase chosen alarm index
                alarm_setting_index = (alarm_setting_index+1)%99;

                // Display this change
                displayed_text[2] = '0' + ((alarm_setting_index+1)/10);
                displayed_text[3] = '0' + ((alarm_setting_index+1)%10);
                break;
            }
        }
    }
    if(FALLING_EDGE(pressed, BTN_DECR, decr_state)){
        pressed &= ~(1<<BTN_DECR); // Remember that the button was released
        switch(program_state){
            case TIME_SET_HR:{
                // Decrease currently displayed hours by 1
                time[0] = (time[0] == 0)?(23):(time[0]-1);

                // Display this change
                time_to_display();
                break;
            }
            case ALARM_SET_HR:{
                // Decrease currently displayed hours by 1
                time[0] = (time[0] == 0)?(24):(time[0]-1);

                // Hour 24 is a special state when the alarm is considered unset
                if(time[0] == 24) memset(displayed_text, '-', DISPLAY_COUNT);
                else time_to_display();
                break;
            }
            case TIME_SET_M:case ALARM_SET_M:{
                // Decrease currently displayed minutes by 1
                time[1] = (time[1] == 0)?(59):(time[1]-1);

                // Display this change
                time_to_display();
                break;
            }
            case ALARM_CHOOSE:{
                // Decrease chosen alarm index
                alarm_setting_index = (alarm_setting_index==0)?(98):(alarm_setting_index-1);

                // Display this change
                displayed_text[2] = '0' + ((alarm_setting_index+1)/10);
                displayed_text[3] = '0' + ((alarm_setting_index+1)%10);
                break;
            }
        }
    }
}

// Updates current time
void set_current_time(){
    buffer_time[0] = time[0];
    buffer_time[1] = time[1];
    buffer_time_last_update = millis();
}

// Updates and loads current time into display
void get_current_time(){
    uint64_t current_time = millis();
    if(current_time - buffer_time_last_update >= 60000){
        if(buffer_time[1] == 59) buffer_time[0] = (buffer_time[0]+1)%24;
        buffer_time[1] = (buffer_time[1]+1)%60;

        buffer_time_last_update = current_time;
    }
    time[0] = buffer_time[0];
    time[1] = buffer_time[1];
    time_to_display();
}

// Checks all set up alarms
void alarm(){
    for(uint8_t i = 0; i < 99; i++){ // Loop through all alarms
        if(alarms[i][0]&0x80){ // Check if the alarm is set
            if((alarms[i][0]&0x7f) == time[0] && alarms[i][1] == time[1]){ // Check if the alarm needs to be raised
                if(i != active_alarm_index){
                    buzzing = true;
                    active_alarm_index = i;
                    active_alarm_disabled = false;
                    return;
                }
            }
        }
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("Serial initialized.");
    // Set pin definitions
    for(uint8_t i = 0; i < 7; i++){
        pinMode(segments[i], OUTPUT);
    }
    for(uint8_t i = 0; i < DISPLAY_COUNT; i++){
        pinMode(displays[i], OUTPUT);
        digitalWrite(displays[i], HIGH);
    }
    for(uint8_t i = 0; i < BUTTON_COUNT; i++){
        pinMode(buttons[i], INPUT);
        digitalWrite(buttons[i], HIGH);
    }
    pinMode(separator, OUTPUT);
    pinMode(buzzer, OUTPUT);

    // Get current time and display it
    get_current_time();
}

void loop() {
    uint64_t current_time = millis();
    static bool separator_state = false;
    static bool buzzing_state = false;
    static uint64_t last_update = ~0;

    // Separator blinking handler
    if(separator_state){
        // Check if separator needs to be turned on
        if(current_time%(SEPARATOR_BLINK*2) > SEPARATOR_BLINK){
            Serial.println("Separator turning on");
            separator_state = false;
            digitalWrite(separator, HIGH);
        }
    }
    else{
        // Check if separator needs to be turned off
        if(current_time%(SEPARATOR_BLINK*2) < SEPARATOR_BLINK){
            Serial.println("Separator turning off");
            separator_state = true;
            digitalWrite(separator, LOW);
        }
    }

    // Buzzer handler
    if(!active_alarm_disabled && buzzing){
        if(buzzing_state){
            // Check if buzzer needs to be turned off
            if(current_time%(ALARM_BUZZ_TIME*2) < ALARM_BUZZ_TIME){
                Serial.println("Buzzer turning off");
                digitalWrite(buzzer, LOW);
                buzzing_state = false;
            }
        }
        else{
            // Check if buzzer needs to be turned on
            if(current_time%(ALARM_BUZZ_TIME*2) > ALARM_BUZZ_TIME){
                Serial.println("Buzzer turning on");
                digitalWrite(buzzer, HIGH);
                buzzing_state = true;
            }
        }
    }

    // Check if time needs to be updated (every 5 seconds)
    if(program_state == TIME_DISPLAY && current_time - last_update >= 5000){
        get_current_time();
        alarm();
        last_update = current_time;
    }

    button_manager();
    update_display();
}
