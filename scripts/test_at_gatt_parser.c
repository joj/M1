/* See COPYING.txt for license details. */
/*
 * test_at_gatt_parser.c
 *
 * Host-side test for the BLE GATT parser cases added to
 * Esp_spi_at/.../m1_at_response_parser.c. Uses tiny stubs in
 * scripts/host_test_stubs/ to compile the parser without STM32 headers.
 *
 * Build & run:
 *   gcc -std=c11 -Wall -Wextra \
 *     -I scripts/host_test_stubs \
 *     -I Esp_spi_at/examples/at_spi_master/spi/stm32/main \
 *     scripts/test_at_gatt_parser.c \
 *     Esp_spi_at/examples/at_spi_master/spi/stm32/main/m1_at_response_parser.c \
 *     -o tgp && ./tgp
 */

#include "ctrl_api.h"
#include "m1_at_response_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* m1_resp_string_strip is defined inside the parser .c file, exported. */

static int pass = 0, fail = 0;
static void EXPECT(int cond, const char *label)
{
    if (cond) { pass++; printf("PASS  %s\n", label); }
    else      { fail++; printf("FAIL  %s\n", label); }
}

int main(void)
{
    /* ---- +BLECONN parsing ---- */
    {
        ctrl_cmd_t app = {0};
        app.msg_id = CTRL_RESP_BLE_CONNECT;
        char r[] = "+BLECONN:0,0\r\n";
        m1_parse_spi_at_resp(r, "+BLECONN:", &app);
        EXPECT(app.u.ble_conn.connect_status == 0, "BLECONN success status=0");
    }
    {
        ctrl_cmd_t app = {0};
        app.msg_id = CTRL_RESP_BLE_CONNECT;
        char r[] = "+BLECONN:0,12\r\n";
        m1_parse_spi_at_resp(r, "+BLECONN:", &app);
        EXPECT(app.u.ble_conn.connect_status == 12, "BLECONN failure status=12");
    }

    /* ---- +BLEGATTCPRIMSRV parsing (multiple records) ---- */
    {
        ctrl_cmd_t app = {0};
        app.msg_id = CTRL_RESP_BLE_GATT_PRIMSRV;
        char r[] =
            "+BLEGATTCPRIMSRV:0,1,\"0x1800\",0\r\n"
            "+BLEGATTCPRIMSRV:0,2,\"0x180A\",0\r\n"
            "+BLEGATTCPRIMSRV:0,3,\"0000fe9f-0000-1000-8000-00805f9b34fb\",0\r\n";
        m1_parse_spi_at_resp(r, "+BLEGATTCPRIMSRV:", &app);
        EXPECT(app.u.ble_srv_list.count == 3, "PRIMSRV count=3");
        EXPECT(app.u.ble_srv_list.out_list[0].srv_idx == 1
            && strcmp(app.u.ble_srv_list.out_list[0].uuid, "0x1800") == 0,
            "PRIMSRV[0] = idx 1, 0x1800");
        EXPECT(strcmp(app.u.ble_srv_list.out_list[1].uuid, "0x180A") == 0,
            "PRIMSRV[1] = 0x180A");
        EXPECT(strncmp(app.u.ble_srv_list.out_list[2].uuid, "0000fe9f", 8) == 0,
            "PRIMSRV[2] = canonical 128-bit");
        free(app.u.ble_srv_list.out_list);
    }

    /* ---- +BLEGATTCCHAR parsing (mix of char + desc) ---- */
    {
        ctrl_cmd_t app = {0};
        app.msg_id = CTRL_RESP_BLE_GATT_CHAR;
        char r[] =
            "+BLEGATTCCHAR:0,\"char\",2,1,\"0x2A29\",2\r\n"
            "+BLEGATTCCHAR:0,\"desc\",2,1,1,\"0x2902\"\r\n"
            "+BLEGATTCCHAR:0,\"char\",2,2,\"0x2A24\",2\r\n"
            "+BLEGATTCCHAR:0,\"char\",2,3,\"0x2A26\",10\r\n";
        m1_parse_spi_at_resp(r, "+BLEGATTCCHAR:", &app);
        EXPECT(app.u.ble_char_list.count == 3, "CHAR count=3 (desc skipped)");
        EXPECT(strcmp(app.u.ble_char_list.out_list[0].uuid, "0x2A29") == 0
            && app.u.ble_char_list.out_list[0].props == 2,
            "CHAR[0] manufacturer, props=2 (Read)");
        EXPECT(app.u.ble_char_list.out_list[2].props == 10,
            "CHAR[2] firmware-rev, props=10 (Read|WriteNR)");
        free(app.u.ble_char_list.out_list);
    }

    /* ---- +BLEGATTCRD parsing ---- */
    {
        ctrl_cmd_t app = {0};
        app.msg_id = CTRL_RESP_BLE_GATT_READ;
        char r[] = "+BLEGATTCRD:0,5,48656c6c6f\r\n"; /* "Hello" */
        m1_parse_spi_at_resp(r, "+BLEGATTCRD:", &app);
        EXPECT(app.u.ble_read.value_len == 5
            && strcmp(app.u.ble_read.value_hex, "48656c6c6f") == 0,
            "BLEGATTCRD len=5, hex=\"Hello\"");
    }

    /* ---- +BLEGATTCRD parsing — empty value ---- */
    {
        ctrl_cmd_t app = {0};
        app.msg_id = CTRL_RESP_BLE_GATT_READ;
        char r[] = "+BLEGATTCRD:0,0,\r\n";
        m1_parse_spi_at_resp(r, "+BLEGATTCRD:", &app);
        EXPECT(app.u.ble_read.value_len == 0
            && app.u.ble_read.value_hex[0] == '\0',
            "BLEGATTCRD len=0, hex empty");
    }

    printf("\n%d passed, %d failed\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
