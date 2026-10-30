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
static uint8_t last_request[4];

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *h, const uint8_t *d, uint16_t n, uint32_t t) {
	(void)h; (void)d; (void)n; (void)t;
	return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *h, const uint8_t *d, uint16_t n) {
	(void)n;
	memcpy(last_request, d, 4);
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

/* NAV-PVT valid flags and time accuracy for make_pvt() */
static uint8_t pvt_valid = 0x07;
static uint32_t pvt_tacc = 25;

/* a 100-byte UBX-NAV-PVT reply */
static void make_pvt(uint8_t fix, uint8_t sv, int32_t lat, int32_t lon, int32_t hmsl) {
	uint8_t *p = reply + 6;
	memset(reply, 0, sizeof(reply));
	p[11] = pvt_valid;
	put32(p + 12, pvt_tacc);
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
	CHECK_EQ(GNSS_Handle.valid, 0x07);
	CHECK_EQ(GNSS_Handle.tAcc, 25);
}

/* valid flags and tAcc (#35), and the tick of the reply */
static void pvt_time_validity(void) {
	reset();
	fake_tick = 5000;
	pvt_valid = 0x03;
	pvt_tacc = 0x12345678;
	make_pvt(NoFix, 0, 0, 0, 0);
	GNSS_GetPVTData(&GNSS_Handle);
	CHECK_EQ(GNSS_Handle.valid, 0x03);
	CHECK_EQ(GNSS_Handle.tAcc, 0x12345678);
	CHECK_EQ(GNSS_Handle.pvtTick, 5000);

	/* a corrupt reply changes nothing, the tick included */
	fake_tick = 9000;
	make_pvt(Fix3D, 9, 1, 1, 1);
	reply[40] ^= 0x55;
	GNSS_GetPVTData(&GNSS_Handle);
	CHECK_EQ(GNSS_Handle.pvtTick, 5000);
	CHECK_EQ(GNSS_Handle.valid, 0x03);
	pvt_valid = 0x07;
	pvt_tacc = 25;
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

TEST_MAIN("gnss", TEST(pvt_parsed), TEST(pvt_time_validity), TEST(pvt_negative_altitude),
		TEST(corrupt_pvt_ignored), TEST(silent_gps_times_out), TEST(parse_buffer_stays_in_bounds))
