/*
 * test_gps.c - GPS power control, polling and UBX helpers (gps.c)
 */
#include "test.h"
#include "fake_hal.h"
#include "gps.h"
#include "GNSS.h"

/* ---- fakes ------------------------------------------------------------------ */

static UART_HandleTypeDef huart;
GNSS_StateHandle GNSS_Handle = { .huart = &huart };
volatile uint32_t GNSS_timeouts;

static uint8_t uart_log[256];
static size_t uart_len;
static int pvt_polls, config_calls, config_failures_left;
static uint32_t config_wake_order;	/* bytes on the UART before each config */

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *h, const uint8_t *d, uint16_t n, uint32_t t) {
	(void)h; (void)t;
	for (uint16_t i = 0; i < n && uart_len < sizeof(uart_log); i++)
		uart_log[uart_len++] = d[i];
	return HAL_OK;
}

void GNSS_GetPVTData(GNSS_StateHandle *g) { (void)g; pvt_polls++; }
void GNSS_GetUniqID(GNSS_StateHandle *g) { g->uniqueID[0] = 1; }
void GNSS_SetMode(GNSS_StateHandle *g, short mode) { g->selectedMode = mode; }
void GNSS_ParseBuffer(GNSS_StateHandle *g) { (void)g; }
void GNSS_LoadConfig(GNSS_StateHandle *g) {
	(void)g;
	config_calls++;
	config_wake_order = uart_len;
	if (config_failures_left > 0) {
		config_failures_left--;
		GNSS_timeouts++;
	}
}

static void reset(void) {
	uart_len = 0;
	pvt_polls = config_calls = config_failures_left = 0;
	GNSS_timeouts = 0;
	gpsWake();			/* leave gps.c awake */
	uart_len = 0;
	deassertGpsLock();
}

/* UBX checksum (8-bit Fletcher over class..payload), written independently */
static void ubx_ck(const uint8_t *msg, size_t len, uint8_t *a, uint8_t *b) {
	*a = *b = 0;
	for (size_t i = 2; i < len - 2; i++) {
		*a += msg[i];
		*b += *a;
	}
}

/* ---- tests -------------------------------------------------------------------- */

static void sleep_sends_pmreq_backup(void) {
	/* UBX-RXM-PMREQ, duration 0 (until woken), flags = backup */
	static const uint8_t want[] = {0xB5, 0x62, 0x02, 0x41, 0x08, 0x00,
			0, 0, 0, 0, 0x02, 0, 0, 0, 0x4D, 0x3B};
	uint8_t a, b;
	reset();

	gpsSleep();
	CHECK_EQ(uart_len, sizeof(want));
	CHECK_MEM(uart_log, want, sizeof(want));
	ubx_ck(want, sizeof(want), &a, &b);
	CHECK(a == 0x4D && b == 0x3B);
	gpsWake();
}

static void no_polling_while_asleep(void) {
	reset();
	assertGpsLock();
	gpsSleep();
	uart_len = 0;
	/* PPS may still be asserted for up to 2 s after the command */
	gpsUpdate();
	CHECK_EQ(pvt_polls, 0);
	CHECK_EQ(uart_len, 0);	/* any UART traffic would wake the GPS */

	gpsWake();
	gpsUpdate();
	CHECK_EQ(pvt_polls, 1);
}

static void no_polling_without_pps(void) {
	reset();
	gpsUpdate();
	CHECK_EQ(pvt_polls, 0);
	assertGpsLock();
	gpsUpdate();
	CHECK_EQ(pvt_polls, 1);
}

static void wake_clears_stale_fix(void) {
	reset();
	GNSS_Handle.fixType = Fix3D;
	GNSS_Handle.numSV = 9;
	gpsSleep();
	uart_len = 0;
	gpsWake();
	CHECK_EQ(GNSS_Handle.fixType, 0);
	CHECK_EQ(GNSS_Handle.numSV, 0);
	CHECK(uart_len >= 1);	/* activity on RX wakes the receiver */
	for (size_t i = 0; i < uart_len; i++)
		CHECK_EQ(uart_log[i], 0xFF);
}

static void configure_first_try(void) {
	reset();
	CHECK_EQ(gpsConfigure(), 1);
	CHECK_EQ(config_calls, 1);
	CHECK(config_wake_order > 0);	/* woken before the config */
}

static void configure_retries_until_answered(void) {
	reset();
	config_failures_left = 2;
	CHECK_EQ(gpsConfigure(), 1);
	CHECK_EQ(config_calls, 3);
}

static void configure_gives_up(void) {
	reset();
	config_failures_left = 100;
	CHECK_EQ(gpsConfigure(), 0);
	CHECK_EQ(config_calls, GPS_CONFIG_TRIES);
}

static void ubx_crc_checks(void) {
	/* the firmware's own NAV-PVT poll message */
	uint8_t msg[sizeof(getPVTData)];
	memcpy(msg, getPVTData, sizeof(msg));
	CHECK_EQ(checkUbxCrc(msg, sizeof(msg)), 1);
	msg[3] ^= 1;
	CHECK_EQ(checkUbxCrc(msg, sizeof(msg)), 0);
}

static void ubx_build_packet(void) {
	uint8_t payload[] = {0x01, 0x07, 0x00, 0x00};	/* NAV-PVT poll */
	uint8_t packet[sizeof(payload) + 4];
	CHECK_EQ(buildUbxPacket(packet, payload, sizeof(payload)), sizeof(packet));
	CHECK_MEM(packet, getPVTData, sizeof(packet));
}

static void static_config_messages_valid(void) {
	/* every canned UBX message in GNSS.h must have a correct checksum */
	CHECK_EQ(checkUbxCrc((uint8_t *)configUBX, sizeof(configUBX)), 1);
	CHECK_EQ(checkUbxCrc((uint8_t *)setNMEA410, sizeof(setNMEA410)), 1);
	CHECK_EQ(checkUbxCrc((uint8_t *)setGNSS, sizeof(setGNSS)), 1);
	CHECK_EQ(checkUbxCrc((uint8_t *)getDeviceID, sizeof(getDeviceID)), 1);
	CHECK_EQ(checkUbxCrc((uint8_t *)setAutomotiveMode, sizeof(setAutomotiveMode)), 1);
}

static void config_is_ubx_only_9600(void) {
	/* CFG-PRT for UART1: 9600 baud, UBX in and out (no NMEA output, which
	 * would break the NAV-PVT polls) */
	CHECK_EQ(configUBX[2], 0x06);
	CHECK_EQ(configUBX[3], 0x00);
	CHECK_EQ(configUBX[6], 1);		/* port 1 = UART1 */
	uint32_t baud = configUBX[14] | configUBX[15] << 8 | configUBX[16] << 16;
	CHECK_EQ(baud, 9600);
	CHECK_EQ(configUBX[18] | configUBX[19] << 8, 1);	/* inProtoMask: UBX */
	CHECK_EQ(configUBX[20] | configUBX[21] << 8, 1);	/* outProtoMask: UBX */
}

TEST_MAIN("gps",
	TEST(sleep_sends_pmreq_backup),
	TEST(no_polling_while_asleep),
	TEST(no_polling_without_pps),
	TEST(wake_clears_stale_fix),
	TEST(configure_first_try),
	TEST(configure_retries_until_answered),
	TEST(configure_gives_up),
	TEST(ubx_crc_checks),
	TEST(ubx_build_packet),
	TEST(static_config_messages_valid),
	TEST(config_is_ubx_only_9600))
