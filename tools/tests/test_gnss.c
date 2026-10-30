/*
 * test_gnss.c - u-blox UBX request/reply handling and NAV-PVT parsing
 * (GNSS.c, with the UART callbacks in gps.c)
 */
#include "test.h"
#include "fake_hal.h"
#include "gps.h"
#include "GNSS.h"

/* ---- fakes: a GPS on the other end of the UART ------------------------------ */

static UART_HandleTypeDef huart;
GNSS_StateHandle GNSS_Handle;
static uint8_t txDone, rxDone;

static uint8_t reply[128];
static size_t reply_len;		/* 0 = the GPS doesn't answer */
static uint8_t last_request[64];
/* class << 8 | id of every request since reset() */
static uint16_t requests[8];
static int nrequests;

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *h, const uint8_t *d, uint16_t n, uint32_t t) {
	(void)h; (void)d; (void)n; (void)t;
	return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *h, const uint8_t *d, uint16_t n) {
	memcpy(last_request, d, n < sizeof(last_request) ? n : sizeof(last_request));
	if (nrequests < 8)
		requests[nrequests++] = (uint16_t)(d[2] << 8 | d[3]);
	HAL_UART_TxCpltCallback(h);
	return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Receive_DMA(UART_HandleTypeDef *h, uint8_t *d, uint16_t n) {
	if (!reply_len)
		return HAL_OK;
	memcpy(d, reply, reply_len < n ? reply_len : n);
	HAL_UART_RxCpltCallback(h);
	return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Abort(UART_HandleTypeDef *h) { (void)h; return HAL_OK; }

/* ---- helpers ------------------------------------------------------------------ */

static void put32(uint8_t *p, uint32_t v) {
	p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

/* a 100-byte UBX-NAV-PVT reply */
static void make_pvt(uint8_t fix, uint8_t sv, int32_t lat, int32_t lon, int32_t hmsl) {
	uint8_t *p = reply + 6;
	memset(reply, 0, sizeof(reply));
	reply[0] = 0xB5; reply[1] = 0x62; reply[2] = 0x01; reply[3] = 0x07;
	reply[4] = 92; reply[5] = 0;
	p[4] = 2026 & 0xFF; p[5] = 2026 >> 8;
	p[6] = 10; p[7] = 29; p[8] = 12; p[9] = 52; p[10] = 13;
	p[20] = fix;
	p[23] = sv;
	put32(p + 24, (uint32_t)lon);
	put32(p + 28, (uint32_t)lat);
	put32(p + 32, (uint32_t)(hmsl + 30000));
	put32(p + 36, (uint32_t)hmsl);
	put32(p + 40, 4500);
	uint8_t a = 0, b = 0;
	for (int i = 2; i < 98; i++) {
		a += reply[i];
		b += a;
	}
	reply[98] = a;
	reply[99] = b;
	reply_len = 100;
}

static void reset(void) {
	GNSS_Init(&GNSS_Handle, &huart, &txDone, &rxDone);
	GNSS_timeouts = 0;
	reply_len = 0;
	nrequests = 0;
	fake_tick = 0;
	fake_tick_step = 0;
}

/* ---- tests -------------------------------------------------------------------- */

static void pvt_parsed(void) {
	reset();
	make_pvt(Fix3D, 9, 389987994, -946819494, 309000);
	GNSS_GetPVTData(&GNSS_Handle);

	CHECK_EQ(last_request[2], 0x01);
	CHECK_EQ(last_request[3], 0x07);
	CHECK_EQ(GNSS_timeouts, 0);
	CHECK_EQ(GNSS_Handle.fixType, Fix3D);
	CHECK_EQ(GNSS_Handle.numSV, 9);
	CHECK_EQ(GNSS_Handle.lat, 389987994);
	/* negative longitude survives the byte-wise union (32-bit, not long) */
	CHECK_EQ(GNSS_Handle.lon, -946819494);
	CHECK_EQ(GNSS_Handle.hMSL, 309000);
	CHECK_EQ(GNSS_Handle.hAcc, 4500);
	CHECK_EQ(GNSS_Handle.year, 2026);
	CHECK_EQ(GNSS_Handle.day, 29);
	CHECK_EQ(GNSS_Handle.hour, 12);
	CHECK_EQ(GNSS_Handle.min, 52);
}

static void pvt_negative_altitude(void) {
	reset();
	make_pvt(Fix3D, 5, -335000000, 1510000000, -12000);
	GNSS_GetPVTData(&GNSS_Handle);
	CHECK_EQ(GNSS_Handle.lat, -335000000);
	CHECK_EQ(GNSS_Handle.lon, 1510000000);
	CHECK_EQ(GNSS_Handle.hMSL, -12000);
}

static void corrupt_pvt_ignored(void) {
	reset();
	make_pvt(Fix3D, 9, 389987994, -946819494, 309000);
	GNSS_GetPVTData(&GNSS_Handle);
	make_pvt(NoFix, 0, 0, 0, 0);
	reply[40] ^= 0x55;		/* checksum no longer matches */
	GNSS_GetPVTData(&GNSS_Handle);
	CHECK_EQ(GNSS_Handle.fixType, Fix3D);
	CHECK_EQ(GNSS_Handle.lat, 389987994);
}

static void silent_gps_times_out(void) {
	reset();
	fake_tick_step = 1;		/* let the wait loop's clock run */
	GNSS_GetPVTData(&GNSS_Handle);
	CHECK_EQ(GNSS_timeouts, 1);
	CHECK_EQ(GNSS_timeoutMsg, 0x0107);
	CHECK(fake_tick >= GNSS_TIMEOUT_MS);
}

static void parse_buffer_stays_in_bounds(void) {
	/* a sync pattern in the last bytes of the buffer must not make the
	 * parser read past it (built with -fsanitize=bounds-strict) */
	reset();
	memset(GNSS_Handle.uartWorkingBuffer, 0, sizeof(GNSS_Handle.uartWorkingBuffer));
	GNSS_Handle.uartWorkingBuffer[99] = 0xB5;
	GNSS_Handle.uartWorkingBuffer[100] = 0x62;
	GNSS_ParseBuffer(&GNSS_Handle);
	CHECK_EQ(GNSS_Handle.uartWorkingBuffer[100], 0);	/* cleared after parsing */
}

/* a UBX ACK-ACK for a CFG request (10 bytes, what the config calls wait for) */
static void make_ack(void) {
	static const uint8_t ack[] = {0xB5, 0x62, 0x05, 0x01, 0x02, 0x00, 0x06, 0x00, 0x0E, 0x37};
	memset(reply, 0, sizeof(reply));
	memcpy(reply, ack, sizeof(ack));
	reply_len = sizeof(ack);
}

/* UBX-CFG-NAV5 with the dynamic platform model for each mode; mode 4 has no
 * u-blox model of its own and stays automotive */
static void set_mode_sends_dynamic_model(void) {
	static const struct {
		short mode;
		uint8_t model;
	} want[] = {{ModePortable, 0}, {ModeStationary, 2}, {ModePedestrian, 3}, {ModeAutomotive, 4},
			{4, 4}, {ModeAirbone1G, 6}, {ModeAirbone2G, 7}, {ModeAirbone4G, 8}, {ModeWrist, 9},
			{ModeBike, 10}};
	for (unsigned i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
		reset();
		make_ack();
		GNSS_SetMode(&GNSS_Handle, want[i].mode);
		CHECK_EQ(nrequests, 1);
		CHECK_EQ(requests[0], 0x0624);
		CHECK_EQ(last_request[6 + 2], want[i].model); /* payload byte 2: dynModel */
		CHECK_EQ(GNSS_Handle.selectedMode, want[i].mode);
		CHECK_EQ(GNSS_timeouts, 0);
	}
}

/* UBX only on the UART, NMEA 4.10, then the constellations */
static void load_config_sends_three_messages(void) {
	reset();
	make_ack();
	GNSS_LoadConfig(&GNSS_Handle);
	CHECK_EQ(nrequests, 3);
	CHECK_EQ(requests[0], 0x0600);
	CHECK_EQ(requests[1], 0x0617);
	CHECK_EQ(requests[2], 0x063E);
	CHECK_EQ(GNSS_timeouts, 0);
}

/* UBX-SEC-UNIQID: the 5-byte chip ID, only from a reply with a good checksum */
static void unique_id_parsed(void) {
	reset();
	memset(reply, 0, sizeof(reply));
	uint8_t id[] = {0xB5, 0x62, 0x27, 0x03, 9, 0, 1, 0, 0, 0, 0x12, 0x34, 0x56, 0x78, 0x9A, 0, 0};
	uint8_t a = 0, b = 0;
	for (int i = 2; i < 15; i++) {
		a += id[i];
		b += a;
	}
	id[15] = a;
	id[16] = b;
	memcpy(reply, id, sizeof(id));
	reply_len = sizeof(id);
	GNSS_GetUniqID(&GNSS_Handle);
	CHECK_EQ(requests[0], 0x2703);
	GNSS_ParseBuffer(&GNSS_Handle);
	CHECK_EQ(GNSS_Handle.uniqueID[0], 0x12);
	CHECK_EQ(GNSS_Handle.uniqueID[4], 0x9A);

	/* a corrupt reply leaves the ID alone */
	id[12] ^= 0xFF;
	memcpy(reply, id, sizeof(id));
	GNSS_GetUniqID(&GNSS_Handle);
	GNSS_ParseBuffer(&GNSS_Handle);
	CHECK_EQ(GNSS_Handle.uniqueID[2], 0x56);
}

TEST_MAIN("gnss", TEST(pvt_parsed), TEST(pvt_negative_altitude), TEST(corrupt_pvt_ignored),
		TEST(silent_gps_times_out), TEST(parse_buffer_stays_in_bounds),
		TEST(set_mode_sends_dynamic_model), TEST(load_config_sends_three_messages),
		TEST(unique_id_parsed))
