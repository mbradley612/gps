#include <stdio.h>

#include "common/mbuf.h"
#include "common/platform.h"
#include "mgos_app.h"
#include "mgos_gpio.h"
#include "mgos_timers.h"
#include "mgos_uart.h"
#include "common/json_utils.h"

#include "mgos.h"
#include "gps.h"
#include "minmea.h"

static int gps_uart_no = 0;
static size_t gpsDataAvailable = 0;
static struct minmea_sentence_rmc lastFrame;

/*

In it's current form, this library only provides an API for longitude and
latitude and speed readings from the RMC sentence.

The library parses RMC, GGA and GSV sentences.

The adafruit GPS library for Arduino has the following recommendation:

"We suggest keeping the update rate at 1Hz and request that the GPS only
output RMC and GGA as the parser does not keep track of other data anyways."

The underlying source library is https://github.com/kosma/minmea. From the
example on the readme.MD, looks like we need a more sophisticated implementation
to capture all of the information we are interested, e.g. with a single
C struct that we can read an write from with long, lat, speed, number satellites
etc.

For now just adding speed and direction.

*/


char *mgos_get_location()
{

    struct mbuf fb;
    struct json_out out = JSON_OUT_MBUF(&fb);

    //printf("GPS Request direct \n");

    mbuf_init(&fb, 50);

    float lat = minmea_tocoord(&lastFrame.latitude);
    float lon = minmea_tocoord(&lastFrame.longitude);
    float speed = minmea_tofloat(&lastFrame.speed);

    if (lat == NAN)
    {
        lat = 0.0f;
    }

    if (lon == NAN)
    {
        lon = 0.0f;
    }

    if (speed == NAN)
    {
        speed = 0.0f;
    }

    json_printf(&out, "{lat: \"%f\", lon: \"%f\", sp: \"%f\"}", lat, lon, speed);

    //mbuf_free(&fb);

    return fb.buf;
}


static void parseGpsData(char *line)
{
    char lineNmea[MINMEA_MAX_LENGTH];
    strncpy(lineNmea, line, sizeof(lineNmea) - 1);
    strcat(lineNmea, "\n");
    lineNmea[sizeof(lineNmea) - 1] = '\0';

    enum minmea_sentence_id id = minmea_sentence_id(lineNmea, false);
    //printf("sentence id = %d from line %s\n", (int) id, lineNmea);
    switch (id)
    {
    case MINMEA_SENTENCE_RMC:
    {
        struct minmea_sentence_rmc frame;
        if (minmea_parse_rmc(&frame, lineNmea))
        {
            lastFrame = frame;
            /*
      printf("$RMC: raw coordinates and speed: (%d/%d,%d/%d) %d/%d\n",
             frame.latitude.value, frame.latitude.scale,
             frame.longitude.value, frame.longitude.scale,
             frame.speed.value, frame.speed.scale);
      printf("$RMC fixed-point coordinates and speed scaled to three decimal places: (%d,%d) %d\n",
             minmea_rescale(&frame.latitude, 1000),
             minmea_rescale(&frame.longitude, 1000),
             minmea_rescale(&frame.speed, 1000));
      printf("$RMC floating point degree coordinates and speed: (%f,%f) %f\n",
             minmea_tocoord(&frame.latitude),
             minmea_tocoord(&frame.longitude),
             minmea_tofloat(&frame.speed));
      */
        }
    }
    break;

    case MINMEA_SENTENCE_GGA:
    {
        struct minmea_sentence_gga frame;
        if (minmea_parse_gga(&frame, lineNmea))
        {
            printf("$GGA: fix quality: %d\n", frame.fix_quality);
        }
    }
    break;

    case MINMEA_SENTENCE_GSV:
    {
        struct minmea_sentence_gsv frame;
        if (minmea_parse_gsv(&frame, lineNmea))
        {
            //printf("$GSV: message %d of %d\n", frame.msg_nr, frame.total_msgs);
            printf("$GSV: sattelites in view: %d\n", frame.total_sats);
            /*for (int i = 0; i < 4; i++)
        printf("$GSV: sat nr %d, elevation: %d, azimuth: %d, snr: %d dbm\n",
               frame.sats[i].nr,
               frame.sats[i].elevation,
               frame.sats[i].azimuth,
               frame.sats[i].snr);
      */
        }
    }
    break;
    case MINMEA_INVALID:
    {
        break;
    }
    case MINMEA_UNKNOWN:
    {
        break;
    }
    case MINMEA_SENTENCE_GSA:
    {
        break;
    }
    case MINMEA_SENTENCE_GLL:
    {
        break;
    }
    case MINMEA_SENTENCE_GST:
    {
        break;
    }
    case MINMEA_SENTENCE_VTG:
    {
        break;
    }
    case MINMEA_SENTENCE_ZDA:
    {
        break;
    }
    }
}

static void gps_read_cb(void *arg)
{

    //printf("Hello, GPS!\r\n");
    if (gpsDataAvailable > 0)
    {
        struct mbuf rxb;
        mbuf_init(&rxb, 0);
        mgos_uart_read_mbuf(gps_uart_no, &rxb, gpsDataAvailable);
        if (rxb.len > 0)
        {
            char *pch;
            //printf("%.*s", (int) rxb.len, rxb.buf);
            pch = strtok(rxb.buf, "\n");
            while (pch != NULL)
            {
                //printf("GPS lineNmea: %s\n", pch);
                parseGpsData(pch);
                pch = strtok(NULL, "\n");
            }
        }
        mbuf_free(&rxb);

        gpsDataAvailable = 0;
    }

    (void)arg;
}

int esp32_uart_rx_fifo_len(int uart_no);

static void uart_dispatcher(int uart_no, void *arg)
{
    assert(uart_no == gps_uart_no);
    size_t rx_av = mgos_uart_read_avail(uart_no);
    if (rx_av > 0)
    {
        gpsDataAvailable = rx_av;
    }
    (void)arg;
}

bool mgos_gps_init(void)
{

    struct mgos_uart_config ucfg;
    gps_uart_no = mgos_sys_config_get_gps_uart_no();
    mgos_uart_config_set_defaults(gps_uart_no, &ucfg);

    ucfg.baud_rate = mgos_sys_config_get_gps_baud_rate();
    ucfg.num_data_bits = 8;
    //ucfg.parity = MGOS_UART_PARITY_NONE;
    //ucfg.stop_bits = MGOS_UART_STOP_BITS_1;
    if (!mgos_uart_configure(gps_uart_no, &ucfg))
    {
        return false;
    }

    mgos_set_timer(mgos_sys_config_get_gps_update_interval() /* ms */, true /* repeat */, gps_read_cb, NULL /* arg */);

    mgos_uart_set_dispatcher(gps_uart_no, uart_dispatcher, NULL /* arg */);
    mgos_uart_set_rx_enabled(gps_uart_no, true);

    return true;
}
