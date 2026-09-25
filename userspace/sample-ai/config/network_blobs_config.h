#ifndef AI_NETWORK_BLOBS_CONFIG_H
#define AI_NETWORK_BLOBS_CONFIG_H

/* Keep the generated Neural-ART epoch-controller blobs in AXISRAM1. */
#define ECBLOB_CONST_SECTION __attribute__((section(".network_blobs")))

#endif
