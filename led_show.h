#ifndef LED_SHOW_H
#define LED_SHOW_H

#define LS_REFRESH_UNIT 10      // milliseconds
#define LS_MAX_FRAME_COUNT 64   // max frames in show stream
#define LS_MAX_ROW_COUNT 4      // max number of LED rows
#define LS_MAX_LED_COUNT 64     // max number of LEDs in a row

typedef enum ls_frame_format
{
    ls_frame_Invalid = 0,
    ls_frame_Base,              // base frame data
                                //  frame is calculated once then displayed for duration * repeat count * REFRESH_UNIT msec
    ls_frame_Transition,        // previous frame update
                                //  current frame displayed for 'duration' units then new frame is calculated
                                //  process is repeated 'repeat' times                          
    ls_frame_FormatMax
} ls_frame_format_t;

// Show stream:
//      - total length in 4-byte words not including first 4 bytes - 2 bytes LE - max size of the stream is 256K
//      - refresh period - 1 byte (in LS_REFRESH_UNIT) - refresh period length = refresh_period * LS_REFRESH_UNIT
//      - frame count - 1 byte (from 1 to LS_MAX_FRAME_COUNT)
//      - frame 0
//          - frame header:
//              - frame duration - 2 bytes (in refresh periods)
//              - frame repeat count - 2 bytes
//              - frame format - 1 byte (ls_frame_Invalid+1..ls_frame_FormatMax-1)
//          - frame data
//              format ls_frame_Base:
//                  - row count - 1 byte (1..LS_MAX_ROW_COUNT)
//                  - row 0 data:
//                      - led count - 1 byte (1..LS_MAX_LED_COUNT) 
//                      - led 0 value - 3bytes RR GG BB
//                      - led 1 value
//                      - led ...
//                  - row 1 data:
//                      - led count
//                      - ...
//                  Notes: 
//                      - frame_Base is static so total step duration is 'duration' * 'repeat count'
//
//              format ls_frame_Transition - contains LED update for all rows and leds defined by last Base
//                  - led update - 3 signed bytes for each led in the last Base
//                  - ...
//                  Notes:
//                      - transition is applied to current frame being displayed
//                      - transition is applied every 'duration' refresh periods
//                      - step is repeated 'repeat count' times so after its applied last time,
//                          the LED values differ from initial values by 'repeat count' * 'led update'
//                      - each LED is updated individually as signed byte addition, carry is ignored
//                      - total step duration is 'duration' * 'repeat count'
//
//              format X - TBD
//      - frame 1
//          - ...
//      - frame ...

typedef struct ls_stream_header
{
    uint8_t length[2];
    uint8_t refresh;
    uint8_t count;
} ls_stream_header_t;

typedef struct ls_frame_header
{
    uint8_t duration[2];
    uint8_t repeat[2];
    uint8_t format;
} ls_frame_header_t;

#define LS_TEST_STREAM_0 {\
    0,0,    /* ls_stream_header.length */   \
    1,      /* ls_stream_header.refresh */  \
    3,      /* ls_stream_header.count */    \
    /* frame 0 */                           \
    1,0,    /* ls_frame_header.duration */  \
    1,0,    /* ls_frame_header.repeat */    \
    ls_frame_Base,                          \
    4,      /* row count */                 \
    9,      /* row 0 led count */           \
    0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, \
    9,      /* row 1 led count */           \
    0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, \
    9,      /* row 2 led count */           \
    0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, \
    9,      /* row 3 led count */           \
    0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, \
    /* frame 1 */                           \
    1,0,    /* ls_frame_header.duration */  \
    255,0,    /* ls_frame_header.repeat */  \
    ls_frame_Transition,                    \
    0x00,0x01,0x00, 0x01,0x00,0x00, 0x00,0x00,0x01, 0x00,0x01,0x00, 0x01,0x00,0x00, 0x00,0x00,0x01, 0x00,0x01,0x00, 0x01,0x00,0x00, 0x00,0x00,0x01, \
    0x00,0x00,0x01, 0x00,0x01,0x00, 0x01,0x00,0x00, 0x00,0x00,0x01, 0x00,0x01,0x00, 0x01,0x00,0x00, 0x00,0x00,0x01, 0x00,0x01,0x00, 0x01,0x00,0x00, \
    0x01,0x00,0x00, 0x00,0x00,0x01, 0x00,0x01,0x00, 0x01,0x00,0x00, 0x00,0x00,0x01, 0x00,0x01,0x00, 0x01,0x00,0x00, 0x00,0x00,0x01, 0x00,0x01,0x00, \
    0x01,0x01,0x01, 0x01,0x01,0x01, 0x01,0x01,0x01, 0x01,0x01,0x01, 0x01,0x01,0x01, 0x01,0x01,0x01, 0x01,0x01,0x01, 0x01,0x01,0x01, 0x01,0x01,0x01, \
    /* frame 2 */                           \
    1,0,    /* ls_frame_header.duration */  \
    255,0,    /* ls_frame_header.repeat */  \
    ls_frame_Transition,                    \
    0x00,0xFF,0x00, 0xFF,0x00,0x00, 0x00,0x00,0xFF, 0x00,0xFF,0x00, 0xFF,0x00,0x00, 0x00,0x00,0xFF, 0x00,0xFF,0x00, 0xFF,0x00,0x00, 0x00,0x00,0xFF, \
    0x00,0x00,0xFF, 0x00,0xFF,0x00, 0xFF,0x00,0x00, 0x00,0x00,0xFF, 0x00,0xFF,0x00, 0xFF,0x00,0x00, 0x00,0x00,0xFF, 0x00,0xFF,0x00, 0xFF,0x00,0x00, \
    0xFF,0x00,0x00, 0x00,0x00,0xFF, 0x00,0xFF,0x00, 0xFF,0x00,0x00, 0x00,0x00,0xFF, 0x00,0xFF,0x00, 0xFF,0x00,0x00, 0x00,0x00,0xFF, 0x00,0xFF,0x00, \
    0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF, \
    0,0,0 \
}



#endif /*LED_SHOW_H*/