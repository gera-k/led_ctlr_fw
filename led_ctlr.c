#include "nrfx_spim.h"
#include "nrf_gpio.h"
#include "nrf_delay.h"
#include "nrf_error.h"
#include "nrf_log.h"

#include "app_error.h"
#include "app_util.h"
#include "app_timer.h"

#include "led_ctlr.h"
#include "led_show.h"

#define sizeofarr(a) (sizeof(a)/sizeof(a[0]))

//  stream info
typedef struct stream_frame     // frame info
{
    uint16_t duration;          // in refresh periods
    uint16_t repeat;            // repeat counter
    ls_frame_format_t format;   // step format
    uint32_t offset;            // offset to beginning of step data in showStream (after 'format' byte)
} stream_frame_t;


typedef struct stream_info
{
    const uint8_t* stream;

    uint8_t refreshPeriod;
    uint8_t frameCount;
    stream_frame_t frame[LS_MAX_FRAME_COUNT];

    // current frame info
    uint8_t frameNumber;    // current frame number
    uint8_t frameDuration;  // current frame duration
    uint8_t frameRepeat;    // current frame repeat counter

    // current frame info
    uint32_t * currFrame;       // points to current frame to show
    uint8_t currRow;
    uint8_t currRowCount;
    uint8_t currLedCount[LS_MAX_ROW_COUNT];
    uint32_t showFrame1[LS_MAX_ROW_COUNT * LS_MAX_LED_COUNT];
    uint32_t showFrame2[LS_MAX_ROW_COUNT * LS_MAX_LED_COUNT];

    uint8_t currRefresh;

} stream_info_t;

APP_TIMER_DEF(led_task_timer);
led_ctlr_hw_t* led_ctlr = NULL;
static stream_info_t curr_stream;

static int parseStream(const uint8_t* stream, size_t length, stream_info_t* info)
{
    int status;
    const uint8_t *p, *b; // current position in the stream
    uint32_t l;
    uint8_t refresh;
    uint8_t frame;
    uint8_t frameCount;

    info->stream = stream;

    b = p = stream;

    //      - total length in 4-byte words not including first 4 bytes - 2 bytes LE - max size of the stream is 256K
    //      - refresh period - 1 byte (in LS_REFRESH_UNIT) - refresh period length = refresh_period * LS_REFRESH_UNIT
    //      - frame count - 1 byte (from 1 to LS_MAX_FRAME_COUNT)

    if (length < 4)
    {
        NRF_LOG_ERROR("Stream (length %d) is too short", length);
        goto RetErr;
    }

    l = *p++;
    l += (uint32_t)(*p++) << 8;
    l *= 4;                 // convert to bytes
    if (l == 0)
        l = length;
    refresh = *p++;
    frameCount = *p++;

    if (l > length )
    {
        NRF_LOG_ERROR("Stream length %d > data length %d", l, length);
        goto RetErr;
    }

    if (frameCount > LS_MAX_FRAME_COUNT || frameCount == 0)
    {
        NRF_LOG_ERROR("Frame count %d is invalid", frameCount);
        goto RetErr;
    }

    NRF_LOG_DEBUG("Stream length: %d", length);
    NRF_LOG_DEBUG("Refresh period: %d", refresh);
    NRF_LOG_DEBUG("Frame count: %d", frameCount);

    l -= 4;

    curr_stream.refreshPeriod = refresh;
    curr_stream.frameCount = frameCount;

    uint32_t byteCount = 0;     // total data bytes in last FRAME
    for (frame = 0; frame < frameCount; frame++)
    {
        uint8_t format;
        uint16_t duration;
        uint16_t repeat;
        uint8_t row, rowCount;
        uint8_t ledCount;

        //          - frame header:
        //              - frame duration - 2 bytes (in refresh periods)
        //              - frame repeat count - 2 bytes
        //              - frame format - 1 byte (ls_frame_Invalid+1..ls_frame_FormatMax-1)

        if (l < 5)
        {
            NRF_LOG_ERROR("Stream is too short - incomplete header of frame %d", frame);
            goto RetErr;
        }

        duration = *p++;
        duration += (uint16_t)(*p++) << 8;
        l -= 2;

        repeat = *p++;
        repeat += (uint16_t)(*p++) << 8;
        l -= 2;

        format = *p++;
        l--;

        if (format > (ls_frame_FormatMax - 1) || format < (ls_frame_Invalid + 1))
        {
            NRF_LOG_ERROR("Invalid format %d of frame %d", format, frame);
            goto RetErr;
        }

        curr_stream.frame[frame].duration = duration;
        curr_stream.frame[frame].repeat = repeat;
        curr_stream.frame[frame].format = format;
        curr_stream.frame[frame].offset = p - b;

        NRF_LOG_DEBUG("Frame %d:", frame);
        NRF_LOG_DEBUG("  Duration: %d", duration);
        NRF_LOG_DEBUG("    Repeat: %d", repeat);
        NRF_LOG_DEBUG("    Format: %d", format);
        NRF_LOG_DEBUG("    Offset: %d", curr_stream.frame[frame].offset);

        switch (format)
        {
        case ls_frame_Base:
        {
            //                  - row count - 1 byte (1..LS_MAX_ROW_COUNT)
            //                  - row 0 data:
            //                      - led count - 1 byte (1..LS_MAX_LED_COUNT) 
            //                      - led 0 value - 3bytes RR GG BB
            //                      - led 1 value
            //                      - led ...
            //                  - row 1 data:
            //                      - led count
            //                      - ...

            byteCount = 0;

            if (l < 5)
            {
                NRF_LOG_ERROR("Stream is too short - no row count in frame %d", frame);
                goto RetErr;
            }

            rowCount = *p++;
            l--;

            if (rowCount > LS_MAX_ROW_COUNT || rowCount == 0)
            {
                NRF_LOG_ERROR("Row count %d in frame %d is invalid", rowCount, frame);
                goto RetErr;
            }

            NRF_LOG_DEBUG("  Format: FRAME");
            NRF_LOG_DEBUG("  Row count: %d", rowCount);

            for (row = 0; row < rowCount; row++)
            {
                if (l < 1)
                {
                    NRF_LOG_ERROR("Stream is too short - no led count in row %d on frame %d", row, frame);
                    goto RetErr;
                }

                ledCount = *p++;
                l--;

                if (ledCount > LS_MAX_LED_COUNT || ledCount == 0)
                {
                    NRF_LOG_ERROR("Led count %d in row %d of frame %d is invalid, allowed 1..%d", ledCount, row, frame, LS_MAX_LED_COUNT);
                    goto RetErr;
                }

                NRF_LOG_DEBUG("  Row %d  Led count: %d", row, ledCount);

                if (l < (size_t)(ledCount * 3))
                {
                    NRF_LOG_ERROR("Stream is too short - incomplete led data in row %d of frame %d", row, frame);
                    goto RetErr;
                }

                p += ledCount * 3;
                l -= ledCount * 3;

                byteCount += ledCount * 3;
            }

            break;
        }

        case ls_frame_Transition:
        {
            NRF_LOG_DEBUG("  Format: TRANSITION  byteCount %d", byteCount);

            if (l < byteCount)
            {
                NRF_LOG_ERROR("Stream is too short - %d bytes left, %d expected", l, byteCount);
                goto RetErr;
            }

            p += byteCount;
            l -= byteCount;

            break;
        }
        }

    }

    if (l > 3)
    {
        NRF_LOG_ERROR("Extra data (%d bytes) at the end of the stream", l);
        goto RetErr;
    }

    status = NRF_SUCCESS;

RetErr:
    return status;
}

static void streamNext(stream_info_t* s);

static int streamStart(stream_info_t* s)
{
    // reset current frame
    s->frameNumber = 0;
    s->frameDuration = 0;
    s->frameRepeat = 0;

    // calculate next frame
    streamNext(s);

    // TODO: start app timer
    //  WdfTimerStart(deviceContext->refreshTimer, WDF_REL_TIMEOUT_IN_MS(GNKSPL_REFRESH_UNIT));

    return 0;
}

static void streamNext(stream_info_t* s)
{
    NRF_LOG_DEBUG("frame %d  duration %d  repeat %d", s->frameNumber, s->frameDuration, s->frameRepeat);

    // select current step
    stream_frame_t * frame = &s->frame[s->frameNumber];

    // if duration counter not yet expired, do not recalculate 
    if (s->frameDuration > 0 && s->frameDuration++ < frame->duration)
        return;

    // reset duration counter
    s->frameDuration = 1;

    // if repeat counter expired, go to next step
    //  note that next step will be selected on next refresh
    if (s->frameRepeat >= frame->repeat)
    {
        s->frameRepeat = 0;

        s->frameNumber++;
        if (s->frameNumber >= s->frameCount)
            s->frameNumber = 0;
    }

    // update repeat counter
    s->frameRepeat++;

    // recalculate frame
    uint32_t* oldFrame;
    uint32_t* newFrame;

    oldFrame = s->currFrame;
    if (oldFrame == NULL || oldFrame == s->showFrame2)
        newFrame = s->showFrame2;
    else
        newFrame = s->showFrame1;

    // locate step data
    const uint8_t* p = s->stream + frame->offset;

    uint8_t rowCount = 0;
    uint8_t ledCount[LS_MAX_ROW_COUNT];

    switch (frame->format)
    {
    case ls_frame_Base:
    {
        // only calculate on very first repetition
//        if (deviceContext->stepRepeat != 1)
//            break;

        rowCount = *p++;

        NRF_LOG_DEBUG("Base  row count %d", rowCount);
        
        for (uint8_t row = 0; row < rowCount; row++)
        {
            uint32_t* r = newFrame + row * LS_MAX_LED_COUNT;
            ledCount[row] = *p++;

            for (uint8_t led = 0; led < ledCount[row]; led++)
            {
                uint8_t R = *p++;
                uint8_t G = *p++;
                uint8_t B = *p++;

                r[led] = ((uint32_t)G << 16) | ((uint32_t)R << 8) | ((uint32_t)B << 0);
            }
        }

        break;
    }

    case ls_frame_Transition:
    {
        if (oldFrame == NULL)
            break;

        rowCount = s->currRowCount;
        memcpy(ledCount, s->currLedCount, sizeof(ledCount));

        NRF_LOG_DEBUG("Transition  row count %d", rowCount);

        for (uint8_t row = 0; row < rowCount; row++)
        {
            uint32_t* lr = oldFrame + row * LS_MAX_LED_COUNT;
            uint32_t* nr = newFrame + row * LS_MAX_LED_COUNT;

            for (uint8_t led = 0; led < ledCount[row]; led++)
            {
                uint32_t l = lr[led];

                uint8_t R = (l >> 8) & 0xFF;
                uint8_t G = (l >> 16) & 0xFF;
                uint8_t B = (l >> 0) & 0xFF;

                R += (int8_t)(*p++);
                G += (int8_t)(*p++);
                B += (int8_t)(*p++);

                nr[led] = ((uint32_t)G << 16) | ((uint32_t)R << 8) | ((uint32_t)B << 0);
            }
        }

        break;
    }

    default:
        NRF_LOG_ERROR("Invalid frame format %d", frame->format);
        break;
    }

    // set frame to show 
    s->currFrame = newFrame;
    s->currRowCount = rowCount;
    if (s->currRow >= rowCount)
        s->currRow = 0;
    memcpy(s->currLedCount, ledCount, sizeof(ledCount));
}

#if 0
static void streamStop(stream_info_t* s)
{
    s->currFrame = NULL;
    s->currRow = 0;
}
#endif

static void streamRefresh(stream_info_t* s)
{
    uint32_t* frame;
    uint8_t ledCount;
    int ri;
 
    frame = s->currFrame;
    ri = s->currRow;
    ledCount = s->currLedCount[ri];

    if (frame == NULL)
        return;

    // output current row
    uint32_t* row = frame + ri * LS_MAX_LED_COUNT;

    led_ctlr->show(led_ctlr, ri, row, ledCount);

    if (++s->currRow >= s->currRowCount)
        s->currRow = 0;

    if (++s->currRefresh >= s->refreshPeriod)
    {
        s->currRefresh = 0;
        streamNext(s);
    }
}

void led_ctlr_task(void * p_context)
{
    streamRefresh(&curr_stream);
}

static const uint8_t stream[] = LS_TEST_STREAM_0;

int led_ctlr_init(led_ctlr_mode_t mode)
{
    APP_ERROR_CHECK(app_timer_create(&led_task_timer, APP_TIMER_MODE_REPEATED, led_ctlr_task));

    led_ctlr = led_ctlr_create(led_ctlr_NeoPixel);
    led_ctlr->init(led_ctlr);

    parseStream(stream, sizeof(stream), &curr_stream);
    streamStart(&curr_stream);

    return NRF_SUCCESS;
}

void led_ctlr_start()
{
    app_timer_start(led_task_timer, APP_TIMER_TICKS(10), NULL);
}
