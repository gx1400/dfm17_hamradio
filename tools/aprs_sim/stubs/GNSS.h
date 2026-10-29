/* host stub: force-included by the Makefile; it claims the real GNSS.h
 * include guard because aprs.h pulls that header from its own directory */
#ifndef INC_GNSS_H_
#define INC_GNSS_H_
#include <inttypes.h>
enum GNSSFixType { NoFix = 0, DeadReckoningOnly = 1, Fix2D = 2, Fix3D = 3 };
/* only the fields aprs.c reads, with the same types as Core/Inc/GNSS.h */
typedef struct {
	uint8_t day;
	uint8_t hour;
	uint8_t min;
	enum GNSSFixType fixType;
	signed long lon;
	signed long lat;
	signed long hMSL;
} GNSS_StateHandle;
#endif
