#include "stm32f1xx_hal.h"
#include "defines.h"
#include "config.h"
#include "sensorcoms.h"
#include "protocol.h"



#ifdef INCLUDE_PROTOCOL

///////////////////////////////////////////////
// extern variables you want to read/write here
#ifdef CONTROL_SENSOR
extern SENSOR_DATA last_sensor_data[2];
#endif

#ifdef HALL_INTERRUPTS
extern volatile int HallPosn[2];
#endif

extern uint8_t enable; // global variable for motor enable
extern int speedL;
extern int speedR;
extern int debug_out;
extern int sensor_control;
int speedB = 0;
///////////////////////////////////////////////


//////////////////////////////////////////////
// variables and functions in support of parameters here

// e.g. to gather two separate speed variables togther,
typedef struct tag_SPEEDS{
    int speedl;
    int speedr;
} SPEEDS;
SPEEDS speeds = {0,0};

// before read we call this...
void getspeeds(void){
    speeds.speedl = speedL;
    speeds.speedr = speedR;
}

// after write we call this...
void setspeeds(void){
    speedL = speeds.speedl;
    speedR = speeds.speedr;
}


///////////////////////////////////////////////////
// structure used to gather variables we want to read/write.
#define PARAM_R     1
#define PARAM_RW    3

#pragma pack(push, 1)
typedef struct tag_PARAMSTAT {
    unsigned char code;     // code in protocol to refer to this
    void *ptr;              // pointer to value
    char len;               // length of value
    char rw;                // PARAM_R or PARAM_RW

    void (*preread)(void);                // function to call after write
    void (*postread)(void);                // function to call after write
    void (*prewrite)(void);                // function to call after write
    void (*postwrite)(void);                // function to call after write
} PARAMSTAT;
#pragma pack(pop)
///////////////////////////////////////////////////



int version = 1;

PARAMSTAT params[] = {
    { 0x00, &version,           4,                          PARAM_R,    NULL, NULL, NULL, NULL },
#ifdef CONTROL_SENSOR
    { 0x01, &last_sensor_data,   sizeof(last_sensor_data),   PARAM_R,    NULL, NULL, NULL, NULL },
#endif
#ifdef HALL_INTERRUPTS
    { 0x02, &HallPosn,           sizeof(HallPosn),           PARAM_R,    NULL, NULL, NULL, NULL },
#endif
    { 0x03, &speeds,             sizeof(speeds),           PARAM_RW,    getspeeds, NULL, NULL, setspeeds }

};


PROTOCOL_STAT s;



// local functions
void protocol_send_nack();
void protocol_send(PROTOCOL_MSG *msg);
void process_message(PROTOCOL_MSG *msg);
void ascii_process_msg(char *cmd, int len);
void ascii_byte( unsigned char byte );

///////////////////////////////////////////////////
// process incomming serial a byte at a time
// and only when a complete, valid message is received,
// process it.
// msgs with invalid CS will get NACK response.
void protocol_byte( unsigned char byte ){
    switch(s.state){
        case PROTOCOL_STATE_IDLE:
            if (byte == PROTOCOL_SOM){
                s.curr_msg.SOM = byte;
                s.state = PROTOCOL_STATE_WAIT_LEN;
                s.CS = 0;
            } else {
                // else process as an 'ASCII' message
                ascii_byte( byte );
                //s.nonsync++;
            }
            break;
        case PROTOCOL_STATE_WAIT_LEN:
            s.curr_msg.len = byte;
            s.count = 0;
            s.CS += byte;
            s.state = PROTOCOL_STATE_WAIT_END;
            break;
        case PROTOCOL_STATE_WAIT_END:
            s.curr_msg.bytes[s.count++] = byte;
            s.CS += byte;
            if (s.count == s.curr_msg.len){
                if (s.CS != 0){
                    protocol_send_nack();
                } else {
                    process_message(&s.curr_msg);
                }
                s.state = PROTOCOL_STATE_IDLE;
            }
            break;
    }
}

char ascii_cmd[20];
int ascii_posn = 0;

void ascii_byte( unsigned char byte ){
    if ((byte == '\r') || (byte == '\n')){
        softwareserial_Send((unsigned char *) &byte, 1);
        ascii_process_msg(ascii_cmd, ascii_posn);
        ascii_posn = 0;
        byte = '>';
        softwareserial_Send((unsigned char *) &byte, 1);
    } else {
        if (ascii_posn < 20){
            ascii_cmd[ascii_posn++] = byte;
            softwareserial_Send((unsigned char *) &byte, 1);
        } else {
            byte = '#';
            softwareserial_Send((unsigned char *) &byte, 1);
        }
    }
}


char ascii_out[200];

void ascii_process_msg(char *cmd, int len){
    if (len == 0){
        return;
    }
    ascii_out[0] = 0;

    switch(cmd[0]){
        case '?':
            sprintf(ascii_out, 
                "Hoverboard Mk1\r\n"\
                "Cmds:\r\n"\
                " E -Enabled Debug\r\n"\
                " D -Disable Debug\r\n"\
                " S -Enable Sensor control\r\n"\
                " T -Disable Sensor control\r\n"\
                " F/B/X -Faster/Slower/Stop\r\n"\
                " ? -show this\r\n"\
                );
            break;
        case 'D':
        case 'd':
            debug_out = 0;
            sprintf(ascii_out, "Debug now %d\r\n", debug_out);
            break;
        case 'E':
        case 'e':
            debug_out = 1;
            sprintf(ascii_out, "Debug now %d\r\n", debug_out);
            break;
        case 'S':
        case 's':
            sensor_control = 1;
            sprintf(ascii_out, "Sensor control now %d\r\n", sensor_control);
            break;
        case 'T':
        case 't':
            sensor_control = 0;
            sprintf(ascii_out, "Sensor control now %d\r\n", sensor_control);
            break;

        case 'F':
        case 'f':
            if (!enable) speedB = 0;
            speedB += 10;
            speedL = speedR = speedB;
            sensor_control = 0;
            enable = 1;
            sprintf(ascii_out, "Forward 10 set\r\n");
            break;

        case 'B':
        case 'b':
            if (!enable) speedB = 0;
            speedB -= 10;
            speedL = speedR = speedB;
            sensor_control = 0;
            enable = 1;
            sprintf(ascii_out, "Backward 10 set\r\n");
            break;

        case 'X':
        case 'x':
            speedB = 0;
            speedL = speedR = speedB;
            sensor_control = 0;
            enable = 1;
            sprintf(ascii_out, "Stop set\r\n");
            break;

        default:
            sprintf(ascii_out, "Unknown cmd %c\r\n", cmd[0]);
            break;
    }
    softwareserial_Send((unsigned char *) ascii_out, strlen(ascii_out));

}


void protocol_send_nack(){
    char tmp[] = { PROTOCOL_SOM, 2, PROTOCOL_CMD_NACK, 0 };
    protocol_send(tmp);
}

void protocol_send_ack(){
    char tmp[] = { PROTOCOL_SOM, 2, PROTOCOL_CMD_ACK, 0 };
    protocol_send(tmp);
}


void protocol_send(PROTOCOL_MSG *msg){
    unsigned char CS = 0;
    unsigned char *src = &msg->len;
    for (int i = 0; i < msg->len-1; i++){
        CS -= *(src++);
    }
    msg->bytes[msg->len-1] = CS;
    softwareserial_Send((unsigned char *) msg, msg->len+2);
}

void process_message(PROTOCOL_MSG *msg){
    PROTOCOL_BYTES *bytes = (PROTOCOL_BYTES *)msg->bytes; 
    switch (bytes->cmd){
        case PROTOCOL_CMD_READVAL:{

            PROTOCOL_BYTES_READVALS *readvals = (PROTOCOL_BYTES_READVALS *) msg->bytes;
            PROTOCOL_BYTES_WRITEVALS *writevals = (PROTOCOL_BYTES_WRITEVALS *) msg->bytes;
            int i;
            for (i = 0; i < sizeof(params)/sizeof(params[0]); i++){
                if (params[i].code == readvals->code){
                    if (params[i].preread) params[i].preread();
                    // NOTE: re-uses the msg object (part of stats)
                    unsigned char *src = params[i].ptr;
                    for (int j = 0; j < params[i].len; j++){
                        writevals->content[j] = *(src++);
                    }
                    msg->len = 1+1+1+params[i].len+1;
                    // send back with 'read' command plus data like write.
                    protocol_send(msg);
                    if (params[i].postread) params[i].postread();
                    break;
                }
            }
            // nothing read
            if (i == sizeof(params)/sizeof(params[0])){
                msg->len = 1+1+1+0+1;
                // send back with 'read' command plus data like write.
                protocol_send(msg);
            }
            break;
        }

        case PROTOCOL_CMD_WRITEVAL:{
            PROTOCOL_BYTES_WRITEVALS *writevals = (PROTOCOL_BYTES_WRITEVALS *) msg->bytes;
            int i;
            for (i = 0; i < sizeof(params)/sizeof(params[0]); i++){
                if (params[i].code == writevals->code){
                    if (params[i].prewrite) params[i].prewrite();
                    // NOTE: re-uses the msg object (part of stats)
                    unsigned char *dest = params[i].ptr;
                    for (int j = 0; j < params[i].len; j++){
                        *(dest++) = writevals->content[j];
                    }
                    msg->len = 1+1+0+1;
                    // send back with 'write' command with no data.
                    protocol_send(msg);
                    if (params[i].postwrite) params[i].postwrite();
                }
            }
            // nothing written
            if (i == sizeof(params)/sizeof(params[0])){
                msg->len = 1+1+1+0+1;
                // send back with 'write' command plus data like write.
                protocol_send(msg);
            }
            break;
        }

        case PROTOCOL_CMD_REBOOT:
            protocol_send_ack();
            HAL_Delay(500);
            HAL_NVIC_SystemReset();
            break;

        default:
            msg->bytes[0] = PROTOCOL_CMD_UNKNOWN;
            msg->len = 2;
            protocol_send(msg);
            break;
    }
}



#endif