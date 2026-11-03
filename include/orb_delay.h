#ifndef ORB_DELAY_H
#define ORB_DELAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OrbDelay OrbDelay;

typedef struct OrbDelayParams {
    uint8_t steps;     /* 1..32 */
    uint8_t mode;      /* 0 = triplets, else sixteenths */
    uint8_t feedback;  /* 0..127 */
    uint8_t pan;       /* 0..127 */
    uint8_t mix;       /* 0..127 delay send, ReBirth T[] curve */
    uint8_t bypass;
} OrbDelayParams;

typedef struct OrbDelayPlayhead {
    double bpm;
} OrbDelayPlayhead;

OrbDelay *orb_delay_new(void);
void orb_delay_free(OrbDelay *delay);
void orb_delay_reset(OrbDelay *delay);
int32_t orb_delay_latency(void);
void orb_delay_process(OrbDelay *delay,
                       float *left,
                       float *right,
                       int32_t nframes,
                       const OrbDelayParams *params,
                       const OrbDelayPlayhead *playhead);

#ifdef __cplusplus
}
#endif

#endif
