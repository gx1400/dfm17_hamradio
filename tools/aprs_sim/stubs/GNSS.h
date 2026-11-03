/* host stub: force-included by the Makefile; it claims the real GNSS.h
 * include guard because aprs.h pulls that header from its own directory */
#ifndef INC_GNSS_H_
#define INC_GNSS_H_
#include <inttypes.h>
enum GNSSFixType { NoFix = 0, DeadReckoningOnly = 1, Fix2D = 2, Fix3D = 3 };
#define GNSS_VALID_DATE 0x01
#define GNSS_VALID_TIME 0x02
/* only the fields aprs.c and hunt.c read, with the same types as Core/Inc/GNSS.h */
typedef struct {
	unsigned short year;
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t min;
	uint8_t sec;
	enum GNSSFixType fixType;
	uint8_t numSV;
	uint8_t valid;
	uint32_t pvtTick;
	signed long lon;
	signed long lat;
	signed long hMSL;
} GNSS_StateHandle;
#endif
